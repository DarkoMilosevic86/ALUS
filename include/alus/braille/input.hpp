// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Braille INPUT: the display's own keyboard.
//
// Port of NVDA's `braille/input/` -- `BrailleInputHandler`,
// `BrailleInputGesture` -- plus `braille/display/gesture.py` for the display's
// command keys.
//
// TWO KINDS OF KEY COME OFF A BRAILLE DISPLAY and they are not the same thing:
//
//   `br(brlapi):fwinrt`   a COMMAND key -- a thumb key, a routing key, a
//                         rocker. It is a gesture like any other and the
//                         ten-step script lookup handles it.
//   `bk:dot1+dot2`        the braille KEYBOARD. The dots either spell text or,
//                         in the three reserved combinations, issue a command:
//                         dot 7 erases, dot 8 is enter, dot 7+8 translates.
//
// WHY CONTRACTED BRAILLE IS NOT SIMPLY "TRANSLATE EACH CELL". In grade 2,
// "tg" is the two letters and "tgr" is the word "together": a later cell
// changes what the earlier ones mean. So contracted input is buffered until
// the word ends, and the cells that have not yet produced text are SHOWN on
// the display between two indicator cells, so the user can see what they have
// typed. Uncontracted input translates per cell and sends immediately.
//
// WHAT THIS CANNOT DO ON WAYLAND, stated plainly: nothing here can inject a
// keystroke into an application. That is finding N18 of phase 0 and it has not
// changed -- `atspi_generate_keyboard_event` is a no-op and there is no
// portal for it. So:
//
//   * with `handleBrailleInput` false (the default), brltty types for the user
//     through its own uinput device and ALUS never sees the dots;
//   * with it true, ALUS translates, shows the cells, and hands the text to
//     the `typeText` hook. The daemon's hook reports honestly that it cannot
//     type rather than swallowing the text, because a braille keyboard that
//     silently does nothing is exactly the failure phase 0 was written to
//     prevent (N1, N7, N15, N18).
//
// The command side -- dot 7, dot 8, dot 7+8, the modifier toggles and any
// `bk:` binding a user writes -- works in both modes, because a script needs
// no keystroke injection.
#pragma once

#include "alus/braille/handler.hpp"
#include "alus/braille/louis.hpp"
#include "alus/braille/settings.hpp"
#include "alus/input/gesture.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace alus::braille {

// NVDA: `braille.input.constants`. Dot 7 is bit 6, dot 8 is bit 7.
inline constexpr std::uint8_t kDot7 = 1U << 6;
inline constexpr std::uint8_t kDot8 = 1U << 7;

// NVDA: `BrailleInputGesture`. Dots and/or the space bar.
class InputGesture final : public input::Gesture {
public:
    InputGesture(std::uint8_t dots, bool space);

    std::uint8_t dots() const noexcept { return dots_; }
    bool space() const noexcept { return space_; }

    std::vector<std::string> identifiers() const override;
    std::string displayName() const override;
    bool shouldReportAsCommand() const override { return false; }

    // NVDA: `GENERIC_ID_DOTS` / `GENERIC_ID_SPACE_DOTS`. The identifiers a
    // user binds once to cover every dot combination.
    static const std::string& genericDotsIdentifier();
    static const std::string& genericSpaceDotsIdentifier();

    // What input help says for an identifier the user did not just press:
    // "braille keyboard", "dot 1 2 5".
    static std::pair<std::string, std::string> describeIdentifier(
        std::string_view identifier);

private:
    std::uint8_t dots_;
    bool space_;
};

// NVDA: `BrailleDisplayGesture`. A command key on the display.
class DisplayGesture final : public input::Gesture {
public:
    DisplayGesture(std::string source, std::string model, std::string id,
                   std::vector<int> cellIndexes = {});

    const std::string& id() const noexcept { return id_; }
    const std::vector<int>& cellIndexes() const noexcept { return cellIndexes_; }

    std::vector<std::string> identifiers() const override;
    std::string displayName() const override;

    // NVDA: `idForCellCount`. One routing key is "routing"; two at once are
    // "multiRouting", which is what selects a range.
    static std::string idForCellCount(std::size_t count, const std::string& baseName);

private:
    std::string source_;
    std::string model_;
    std::string id_;
    std::vector<int> cellIndexes_;
};

// NVDA: `BrailleInputHandler`.
class BrailleInput final : public IBrailleInput {
public:
    struct Hooks {
        // Send text to the focused application. False when the platform
        // cannot (Wayland, N18), and the caller then says so.
        std::function<bool(const std::string& text)> typeText;
        // Emulate one keyboard gesture ("backspace", "control+c").
        std::function<bool(const std::string& gesture)> emulateKey;
        // NVDA: `speech.speakMessage`.
        std::function<void(const std::string& text)> message;
        // NVDA: `speakDots`. "dot 1 2 5" as the cell is typed.
        std::function<void(const std::string& text)> speakDots;
        // NVDA: `api.isTypingProtected`. The cells are masked in a password
        // field, exactly as the characters are.
        std::function<bool()> isTypingProtected;
        // NVDA: `currentFocusIsTextObj`. Whether the focus is somewhere text
        // can be typed at all; false means a dot combination is a COMMAND.
        std::function<bool()> isFocusTextObject;
        // Tell the display that the untranslated cells changed.
        std::function<void()> updateDisplay;
    };

    BrailleInput(Louis& louis, Hooks hooks = {});
    ~BrailleInput() override;

    void applySettings(Settings settings);
    const Settings& settings() const noexcept { return settings_; }

    // ---- IBrailleInput ----
    std::string untranslatedBraille() const override { return untranslated_; }
    int untranslatedCursorPos() const override { return untranslatedCursorPos_; }
    void setUntranslatedCursorPos(int position) override;

    // NVDA: `input`. One cell.
    void input(std::uint8_t dots);
    // NVDA: `eraseLastCell` (dot 7).
    void eraseLastCell();
    // NVDA: `enter` (dot 8).
    void enter();
    // NVDA: `translate` (dot 7 + dot 8).
    void translate();
    // NVDA: `toggleModifiers`. The `braille_toggle<Modifier>` family.
    void toggleModifiers(const std::vector<std::string>& modifiers);
    const std::set<std::string>& currentModifiers() const noexcept { return modifiers_; }

    // NVDA: `flushBuffer`, and the two events that call it.
    void flushBuffer();
    void handleGainFocus();
    void handleCaretMove();

    // The input table list, resolved from the settings.
    std::vector<std::string> inputTables() const;
    // NVDA: `_table.contracted`.
    bool isContracted() const;
    // NVDA: `useContractedForCurrentFocus`.
    bool useContractedForCurrentFocus() const;

    // What has been typed and translated so far, for the tests.
    const std::string& bufferText() const noexcept { return bufferText_; }
    const std::vector<std::uint8_t>& bufferCells() const noexcept { return bufferCells_; }

private:
    // NVDA: `_translate`.
    bool translateBuffer(bool endOfWord);
    // NVDA: `_translateForReportContractedCell`.
    std::string translateForReport(std::size_t position);
    // NVDA: `_reportContractedCell`.
    bool reportContractedCell(std::size_t position);
    // NVDA: `_reportUntranslated`.
    void reportUntranslated(std::size_t position);
    // NVDA: `_updateUntranslated`.
    void updateUntranslated();
    // NVDA: `emulateKey`.
    void emulateKey(const std::string& key, bool withModifiers = true);
    void sendChars(const std::string& chars);

    Louis* louis_;
    Hooks hooks_;
    Settings settings_;

    std::vector<std::uint8_t> bufferCells_;
    std::string bufferText_;
    // NVDA: `cellsWithText`. Which cells produced text, so that erasing knows
    // when to send a real backspace.
    std::set<std::size_t> cellsWithText_;
    std::string untranslated_;
    std::size_t untranslatedStart_ = 0;
    int untranslatedCursorPos_ = 0;
    std::set<std::string> modifiers_;
    // NVDA: `_uncontSentTime`. Our own text entry moves the caret; a caret
    // move just after we typed must not be read as the user moving away.
    std::chrono::steady_clock::time_point lastSent_{};
    bool hasSent_ = false;
    // Said once per session, not once per cell: the platform cannot type.
    bool reportedNoTyping_ = false;
};

}  // namespace alus::braille
