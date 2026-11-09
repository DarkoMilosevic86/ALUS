// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/brlapi_display.hpp.

#include "alus/braille/brlapi_display.hpp"

#include "alus/core/log.hpp"

#include <brlapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace alus::braille {
namespace {

std::string brlapiError() {
    const char* message = brlapi_strerror(&brlapi_error);
    return message != nullptr ? std::string{message} : std::string{"BrlAPI error"};
}

std::string lowered(const char* text) {
    std::string out = text != nullptr ? text : "";
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

BrlapiDisplay::BrlapiDisplay(std::string host) : host_(std::move(host)) {}

BrlapiDisplay::~BrlapiDisplay() { terminate(); }

bool BrlapiDisplay::initialize(std::string* error) {
    const std::lock_guard<std::mutex> lock{mutex_};
    if (connected_) {
        return true;
    }
    brlapi_connectionSettings_t wanted{};
    brlapi_connectionSettings_t actual{};
    wanted.host = host_.empty() ? nullptr : host_.data();
    if (brlapi_openConnection(&wanted, &actual) < 0) {
        if (error != nullptr) {
            // Not an ALUS fault: brltty simply is not running, which is the
            // normal case on a machine with no braille display (§3.8).
            *error = "cannot reach brltty: " + brlapiError();
        }
        return false;
    }
    connected_ = true;

    // An empty driver name asks for brltty COMMANDS rather than raw key
    // codes, which is what makes the gesture names driver independent.
    if (brlapi_enterTtyMode(BRLAPI_TTY_DEFAULT, "") < 0) {
        if (error != nullptr) {
            *error = "brltty refused tty mode: " + brlapiError();
        }
        brlapi_closeConnection();
        connected_ = false;
        return false;
    }
    ttyMode_ = true;

    unsigned int columns = 0;
    unsigned int rows = 0;
    if (brlapi_getDisplaySize(&columns, &rows) < 0) {
        if (error != nullptr) {
            *error = "brltty would not report the display size: " + brlapiError();
        }
        brlapi_leaveTtyMode();
        ttyMode_ = false;
        brlapi_closeConnection();
        connected_ = false;
        return false;
    }
    dimensions_ = DisplayDimensions{static_cast<int>(rows), static_cast<int>(columns)};

    char driver[64] = {};
    if (brlapi_getDriverName(driver, sizeof(driver)) >= 0) {
        driverName_ = lowered(driver);
    }
    // The ignore/accept sets are reset by entering tty mode, so the braille
    // keyboard decision has to be re-stated here.
    {
        const brlapi_keyCode_t dots = BRLAPI_KEY_CMD_PASSDOTS;
        if (brailleInput_) {
            brlapi_acceptKeys(brlapi_rangeType_command, &dots, 1);
        } else {
            brlapi_ignoreKeys(brlapi_rangeType_command, &dots, 1);
        }
    }
    ALUS_INFO("braille display: brltty driver {}, {} rows x {} cols",
              driverName_.empty() ? "unknown" : driverName_, dimensions_.numRows,
              dimensions_.numCols);
    return true;
}

void BrlapiDisplay::terminate() {
    const std::lock_guard<std::mutex> lock{mutex_};
    if (ttyMode_) {
        brlapi_leaveTtyMode();
        ttyMode_ = false;
    }
    if (connected_) {
        brlapi_closeConnection();
        connected_ = false;
    }
    dimensions_ = DisplayDimensions{};
}

DisplayDimensions BrlapiDisplay::dimensions() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return dimensions_;
}

bool BrlapiDisplay::display(const std::vector<std::uint8_t>& cells) {
    const std::lock_guard<std::mutex> lock{mutex_};
    if (!connected_ || dimensions_.size() == 0) {
        return false;
    }
    // brltty expects exactly the display's cell count; a short frame is padded
    // with blanks rather than leaving the previous content on the end cells.
    std::vector<unsigned char> frame(static_cast<std::size_t>(dimensions_.size()), 0);
    const std::size_t count = std::min(frame.size(), cells.size());
    for (std::size_t index = 0; index < count; ++index) {
        frame[index] = static_cast<unsigned char>(cells[index]);
    }
    // NVDA's workaround, and it is still needed: brltty can freeze if data is
    // written while key presses are waiting to be read. Consuming them first
    // is cheap, and the keys are not lost -- they are queued for the pump.
    std::vector<DisplayKey> pending;
    readKeysLocked(pending);
    if (!pending.empty()) {
        pendingKeys_.insert(pendingKeys_.end(), pending.begin(), pending.end());
    }
    if (brlapi_writeDots(frame.data()) < 0) {
        ALUS_WARNING("braille: brltty refused a write: {}", brlapiError());
        return false;
    }
    countWrite();
    return true;
}

void BrlapiDisplay::setBrailleInputEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock{mutex_};
    brailleInput_ = enabled;
    if (!connected_) {
        return;
    }
    // NVDA's brltty driver does exactly this and says why: "BRLTTY simulates
    // key presses for braille typing keys, so let BRLTTY handle them."
    // Ignoring the dots here means brltty types them through its own uinput
    // device -- the only path that works on Wayland (N18).
    const brlapi_keyCode_t dots = BRLAPI_KEY_CMD_PASSDOTS;
    if (enabled) {
        brlapi_acceptKeys(brlapi_rangeType_command, &dots, 1);
    } else {
        brlapi_ignoreKeys(brlapi_rangeType_command, &dots, 1);
    }
}

std::vector<DisplayKey> BrlapiDisplay::readKeys() {
    const std::lock_guard<std::mutex> lock{mutex_};
    std::vector<DisplayKey> keys = std::move(pendingKeys_);
    pendingKeys_.clear();
    if (connected_) {
        readKeysLocked(keys);
    }
    return keys;
}

bool BrlapiDisplay::readKeysLocked(std::vector<DisplayKey>& into) {
    if (!connected_) {
        return false;
    }
    bool any = false;
    for (int guard = 0; guard < 64; ++guard) {
        brlapi_keyCode_t code = 0;
        const int result = brlapi_readKey(0, &code);
        if (result < 0) {
            ALUS_DEBUG_WARNING("braille: could not read a key: {}", brlapiError());
            return any;
        }
        if (result == 0) {
            return any;
        }
        any = true;

        brlapi_expandedKeyCode_t expanded{};
        if (brlapi_expandKeyCode(code, &expanded) < 0) {
            continue;
        }
        if (expanded.type != BRLAPI_KEY_TYPE_CMD) {
            // A keysym from the display's own keyboard. brltty is already
            // typing it into the focused application, so ALUS must not also
            // act on it.
            continue;
        }
        const brlapi_keyCode_t block = code & BRLAPI_KEY_CMD_BLK_MASK;
        DisplayKey key;
        if (block == (BRLAPI_KEY_CMD_PASSDOTS & BRLAPI_KEY_CMD_BLK_MASK)) {
            // Braille keyboard input: the dots are the argument. Dot 1 is
            // bit 0, the same convention ALUS uses everywhere else.
            key.isDots = true;
            key.dots = static_cast<std::uint8_t>(expanded.argument & 0xFF);
            key.space = key.dots == 0;
            key.command = "dots";
            into.push_back(std::move(key));
            continue;
        }

        brlapi_describedKeyCode_t described{};
        if (brlapi_describeKeyCode(code, &described) < 0 || described.command == nullptr) {
            continue;
        }
        key.command = lowered(described.command);
        if (block == BRLAPI_KEY_CMD_ROUTE) {
            key.cellIndexes.push_back(static_cast<int>(expanded.argument));
        }
        into.push_back(std::move(key));
    }
    return any;
}

}  // namespace alus::braille
