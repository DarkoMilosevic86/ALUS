// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/text_behaviors.hpp.

#include "alus/nav/text_behaviors.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>
#include <set>

namespace alus::nav {
namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    for (std::string& line : lines) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
    }
    return lines;
}

bool isBlankLine(const std::string& line) {
    return line.empty() || std::all_of(line.begin(), line.end(), [](unsigned char c) {
               return std::isspace(c) != 0;
           });
}

}  // namespace

// Shared with the Terminal behaviour below, which lives in another namespace.
std::string trimLine(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1);
}

namespace {

// Beyond this many lines in the changed middle, the LCS is abandoned for a
// set comparison: an O(n*m) diff over a scrollback buffer is the kind of cost
// that makes a screen reader stutter, and the answer it gives for terminal
// output is the same.
constexpr std::size_t kMaxDiffLines = 400;

}  // namespace

std::vector<std::string> diffNewLines(const std::string& oldText,
                                      const std::string& newText) {
    std::vector<std::string> result;
    if (newText.empty() || newText == oldText) {
        return result;
    }
    const std::vector<std::string> oldLines = splitLines(oldText);
    const std::vector<std::string> newLines = splitLines(newText);

    // The common prefix and suffix. For appended output -- which is what a
    // terminal does -- the prefix alone answers the question, in one pass over
    // the lines and no allocation.
    std::size_t prefix = 0;
    while (prefix < oldLines.size() && prefix < newLines.size() &&
           oldLines[prefix] == newLines[prefix]) {
        ++prefix;
    }
    std::size_t suffix = 0;
    while (suffix + prefix < oldLines.size() && suffix + prefix < newLines.size() &&
           oldLines[oldLines.size() - 1 - suffix] == newLines[newLines.size() - 1 - suffix]) {
        ++suffix;
    }

    const std::size_t oldMiddle = oldLines.size() - prefix - suffix;
    const std::size_t newMiddle = newLines.size() - prefix - suffix;

    const auto take = [&result](const std::string& line) {
        // NVDA never reports a blank line as new text: a terminal scrolling
        // produces a great many of them.
        if (!isBlankLine(line)) {
            result.push_back(line);
        }
    };

    if (oldMiddle == 0) {
        // Pure insertion: every middle line is new.
        for (std::size_t index = 0; index < newMiddle; ++index) {
            take(newLines[prefix + index]);
        }
        return result;
    }

    if (oldMiddle > kMaxDiffLines || newMiddle > kMaxDiffLines) {
        // Too much changed for a real diff. A line that was not there before
        // is new; a line that was is not. That is what a scrolled terminal
        // needs and it costs one hash set.
        const std::set<std::string> before(oldLines.begin() + static_cast<std::ptrdiff_t>(prefix),
                                           oldLines.end() - static_cast<std::ptrdiff_t>(suffix));
        for (std::size_t index = 0; index < newMiddle; ++index) {
            const std::string& line = newLines[prefix + index];
            if (before.find(line) == before.end()) {
                take(line);
            }
        }
        return result;
    }

    // A longest-common-subsequence diff over the changed middle. The lines not
    // in the subsequence are the new ones.
    std::vector<std::vector<std::uint16_t>> table(
        oldMiddle + 1, std::vector<std::uint16_t>(newMiddle + 1, 0));
    for (std::size_t i = oldMiddle; i-- > 0;) {
        for (std::size_t j = newMiddle; j-- > 0;) {
            if (oldLines[prefix + i] == newLines[prefix + j]) {
                table[i][j] = static_cast<std::uint16_t>(table[i + 1][j + 1] + 1);
            } else {
                table[i][j] = std::max(table[i + 1][j], table[i][j + 1]);
            }
        }
    }
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < oldMiddle && j < newMiddle) {
        if (oldLines[prefix + i] == newLines[prefix + j]) {
            ++i;
            ++j;
        } else if (table[i + 1][j] >= table[i][j + 1]) {
            ++i;
        } else {
            take(newLines[prefix + j]);
            ++j;
        }
    }
    for (; j < newMiddle; ++j) {
        take(newLines[prefix + j]);
    }
    return result;
}

}  // namespace alus::nav

namespace alus::a11y::behaviors {

// ---------------------------------------------------------------------------
// LiveText
// ---------------------------------------------------------------------------

LiveText::LiveText(nav::Service& service) : service_(&service) {}

LiveText::~LiveText() {
    stopMonitoring();
}

nav::Service& LiveText::service() {
    return *service_;
}

std::optional<bool> LiveText::isPresentableFocusAncestor(const Object& /*object*/) const {
    return true;
}

std::string LiveText::textOf(Object& object) {
    const int count = object.characterCount();
    if (count <= 0) {
        return {};
    }
    return object.textRange(0, count);
}

void LiveText::reportNewText(const std::string& line) {
    if (speech::Presenter* presenter = service_->presenter()) {
        presenter->speakText(line);
    }
}

void LiveText::reportNewLines(Object& /*object*/, std::vector<std::string> lines) {
    for (const std::string& line : lines) {
        reportNewText(line);
    }
}

void LiveText::startMonitoring(Object& object) {
    if (monitoring_) {
        return;
    }
    monitoring_ = true;
    // The baseline: whatever is there now is not new.
    lastText_ = textOf(object);
    ALUS_DEBUG("live text monitoring started ({} bytes of text)", lastText_.size());
}

void LiveText::stopMonitoring() {
    if (!monitoring_) {
        return;
    }
    monitoring_ = false;
    if (timer_ != 0 && service_ != nullptr) {
        service_->loop().cancelLater(timer_);
        timer_ = 0;
    }
    lastText_.clear();
    pending_.reset();
}

void LiveText::scheduleDiff(Object& object) {
    if (!monitoring_) {
        return;
    }
    pending_ = object.weak_from_this();
    if (stabilizeDelay.count() <= 0) {
        runDiff(object);
        return;
    }
    if (timer_ != 0) {
        // A burst of change events costs ONE diff: the timer is re-armed
        // rather than queued. This is what NVDA's single monitor thread with
        // an event flag achieves, without a thread.
        service_->loop().cancelLater(timer_);
    }
    timer_ = service_->loop().callLater(
        stabilizeDelay,
        [this] {
            timer_ = 0;
            const ObjectPtr target = pending_.lock();
            if (target != nullptr && monitoring_) {
                runDiff(*target);
            }
        },
        "live-text-diff");
}

void LiveText::runDiff(Object& object) {
    if (!monitoring_) {
        return;
    }
    const std::string current = textOf(object);
    if (current == lastText_) {
        return;
    }
    if (!service_->settings().reportDynamicContentChanges) {
        lastText_ = current;
        return;
    }
    std::vector<std::string> lines = nav::diffNewLines(lastText_, current);
    lastText_ = current;
    if (lines.size() == 1 &&
        alus::text::codepointCount(alus::nav::trimLine(lines.front())) == 1) {
        // NVDA: a single new character is almost certainly the echo of a key
        // the user just pressed, and the key press was already announced.
        return;
    }
    if (lines.empty()) {
        return;
    }
    reportNewLines(object, std::move(lines));
}

bool LiveText::handleEvent(Object& object, const core::Event& event) {
    if (event.name == events::kTextChange) {
        scheduleDiff(object);
        return false;
    }
    if (event.name == events::kShow) {
        startMonitoring(object);
        return false;
    }
    if (event.name == events::kHide) {
        stopMonitoring();
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------

Terminal::Terminal(nav::Service& service) : LiveText(service) {
    // A terminal updates in bursts: a shell prompt redraw is several change
    // events a few milliseconds apart. NVDA's own consoles use a stabilisation
    // delay for exactly this, and without it `ls` of a large directory is
    // diffed once per line.
    stabilizeDelay = std::chrono::milliseconds{30};
}

Terminal::~Terminal() = default;

std::optional<Role> Terminal::role(const Object& /*object*/) const {
    return Role::Terminal;
}

void Terminal::queueTypedCharacter(const std::string& character) {
    if (character == "\t") {
        // After a tab, a completion may be very short, so the short-line
        // filter is suspended for one update.
        hasTab_ = true;
    } else {
        hasTab_ = false;
    }
    queued_.push_back(character);
}

void Terminal::flushQueuedCharacters() {
    queued_.clear();
}

void Terminal::reportNewLines(Object& object, std::vector<std::string> lines) {
    // NVDA's typed-character filter. In a terminal the SHELL echoes what the
    // user types, so the echo arrives here as new text. Announcing both the
    // key press and the echo says everything twice; announcing the key press
    // in a program that echoes nothing (`sudo`) reads the password aloud. So:
    //
    //   * a single new line shorter than what the user has typed is the echo,
    //     and is dropped;
    //   * anything longer is real output, and the typed-character queue is
    //     discarded with the word buffer -- what the terminal did with those
    //     characters is now on the screen.
    if (lines.size() == 1 && !hasTab_) {
        const std::size_t typed = queued_.size();
        const std::size_t length =
            alus::text::codepointCount(alus::nav::trimLine(lines.front()));
        if (length < std::max(typed + 1, static_cast<std::size_t>(3))) {
            return;
        }
    }
    if (input::Service* input = service().inputService()) {
        if (input::GestureManager* manager = input->manager()) {
            manager->typedChars().clearWordBuffer();
        }
    }
    flushQueuedCharacters();
    LiveText::reportNewLines(object, std::move(lines));
}

bool Terminal::handleEvent(Object& object, const core::Event& event) {
    if (event.name == events::kGainFocus) {
        // Monitoring follows the focus: a terminal in another workspace must
        // not be read, and it must not cost a text fetch per update either.
        startMonitoring(object);
        return false;
    }
    if (event.name == events::kLoseFocus) {
        stopMonitoring();
        flushQueuedCharacters();
        return false;
    }
    return LiveText::handleEvent(object, event);
}

}  // namespace alus::a11y::behaviors
