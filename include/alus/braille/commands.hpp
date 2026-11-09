// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The braille command group.
//
// The scripts of NVDA's `globalCommands.GlobalCommands` that belong to its
// "Braille" and "Emulated system keyboard keys" categories. Like phase 4's
// `a11y::ObjectCommands` and phase 5's `nav::TextCommands`, this is another
// step-10 command group that ANSWERS TO that NVDA class name -- so the
// generated command table (data/scripts.csv) gives every script here NVDA's
// own description, category and keys, and nothing is retyped.
//
// The table already has all 43 of them, with their gestures: `bk:dot7`,
// `bk:dot8`, `bk:dot7+dot8`, `bk:dots`, `kb:alus+control+t`,
// `kb:alus+alt+t`, `kb:alus+alt+j/k/l`. Phase 3 generated them; this file is
// what finally makes them do something.
//
// WHAT THE USER GETS:
//
//     alus+control+t     tether: focus / review / automatic
//     alus+alt+t         braille mode: follow the cursors / show speech
//     alus+alt+k         automatic scroll on and off
//     alus+alt+j / l     slower / faster automatic scroll
//     bk:dot7            erase the last cell or character
//     bk:dot8            translate and press enter
//     bk:dot7+dot8       translate
//     bk:dots            type braille
//     br(brlapi):fwinlt  scroll back      (from the display's own keys)
//     br(brlapi):fwinrt  scroll forward
//     br(brlapi):route   route the cursor to this cell
//
// The display's own keys reach these scripts through the DRIVER gesture map,
// which phase 3 already built as the third of the three global layers
// (`GestureManager::driverMap`). Phase 7 fills it in; no new mechanism.
#pragma once

#include "alus/script/scriptable.hpp"

#include <string>
#include <vector>

namespace alus::config {
class Manager;
}

namespace alus::input {
class Gesture;
}

namespace alus::script {
class Catalog;
}

namespace alus::braille {

class Service;

// The NVDA class the generated command table addresses these scripts by.
inline constexpr const char* kGlobalCommandsClass = "globalCommands.GlobalCommands";

class Commands final : public script::ScriptableObject {
public:
    Commands(Service& service, const script::Catalog* catalog = nullptr);
    ~Commands() override;

private:
    // ---- the display window ----
    void braille_scrollBack(input::Gesture& gesture);
    void braille_scrollForward(input::Gesture& gesture);
    void braille_previousLine(input::Gesture& gesture);
    void braille_nextLine(input::Gesture& gesture);
    void braille_toFocus(input::Gesture& gesture);

    // ---- the routing keys ----
    void braille_routeTo(input::Gesture& gesture);
    void braille_reportFormatting(input::Gesture& gesture);
    void braille_selectRange(input::Gesture& gesture);

    // ---- braille input ----
    void braille_dots(input::Gesture& gesture);
    void braille_enter(input::Gesture& gesture);
    void braille_eraseLastCell(input::Gesture& gesture);
    void braille_translate(input::Gesture& gesture);

    // ---- the emulated modifiers ----
    void braille_toggleShift(input::Gesture& gesture);
    void braille_toggleControl(input::Gesture& gesture);
    void braille_toggleAlt(input::Gesture& gesture);
    void braille_toggleWindows(input::Gesture& gesture);
    void braille_toggleAlusKey(input::Gesture& gesture);
    void braille_toggleControlShift(input::Gesture& gesture);
    void braille_toggleAltShift(input::Gesture& gesture);
    void braille_toggleWindowsShift(input::Gesture& gesture);
    void braille_toggleAlusKeyShift(input::Gesture& gesture);
    void braille_toggleControlAlt(input::Gesture& gesture);
    void braille_toggleControlAltShift(input::Gesture& gesture);

    // ---- the settings the user cycles with a key ----
    void braille_toggleTether(input::Gesture& gesture);
    void toggleBrailleMode(input::Gesture& gesture);
    void braille_toggleShowCursor(input::Gesture& gesture);
    void braille_cycleCursorShape(input::Gesture& gesture);
    void braille_cycleShowSelection(input::Gesture& gesture);
    void braille_cycleShowMessages(input::Gesture& gesture);
    void braille_toggleFocusContextPresentation(input::Gesture& gesture);
    void braille_cycleReviewRoutingMovesSystemCaret(input::Gesture& gesture);
    void braille_cycleUnicodeNormalization(input::Gesture& gesture);
    void braille_toggleSpeakOnRouting(input::Gesture& gesture);
    void toggleSpeakingOnNavigatingByUnit(input::Gesture& gesture);
    void toggleBrailleAutoScroll(input::Gesture& gesture);
    void increaseBrailleAutoScrollRate(input::Gesture& gesture);
    void decreaseBrailleAutoScrollRate(input::Gesture& gesture);

    // The helpers every command above shares.
    void toggleModifiers(const std::vector<std::string>& modifiers);
    // The cells a cell-addressed gesture names, empty for any other gesture.
    static std::vector<int> cellsOf(input::Gesture& gesture);
    void say(const std::string& text) const;

    Service& service_;
};

}  // namespace alus::braille
