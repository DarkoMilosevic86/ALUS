// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The commands the object model owns: object navigation, and "what is this".
//
// These are the part of NVDA's `globalCommands.GlobalCommands` that needs an
// object model, so they arrive with it. Like phase 3's InputCommands, the
// class answers to `globalCommands.GlobalCommands`, which is the section name
// of the generated built-in gesture map -- so each script picks up NVDA's own
// key assignment and NVDA's own description without a line of transcription
// (data/scripts.csv, data/gestures-builtin.ini).
//
//     alus+tab            what has the focus
//     alus+numpad5        what is the navigator object
//     alus+numpad8        up to the containing object
//     alus+numpad2        down into the first object inside
//     alus+numpad4/6      the previous / next object
//     alus+numpad9/3      the previous / next object in a flattened view
//     alus+numpadMinus    the navigator follows the focus
//     alus+shift+numpadMinus   the focus follows the navigator
//     alus+numpadEnter    activate the navigator object
//     alus+t              the title of the active window
//     alus+shift+numpadDelete  where the navigator object is on the screen
//     alus+6              the caret moves the review cursor, on or off
//     alus+7              the focus moves the navigator object, on or off
//     alus+5              report dynamic content changes, on or off
//     alus+u              progress bar updates: off, speak, beep, both
//     shift+numpad2       the focus object's keyboard shortcut
//     alus+d              the summary of the details the focus points to
//     alus+f1             the navigator object's developer info, to the log
//     alus+k              where the link under the cursor points
//
// THE FOUR TOGGLES ARE HERE AND NOT IN THE SPEECH LAYER, although two of them
// are in NVDA's "Speech" category: what they change is a field of
// a11y::Settings, which this layer owns and which the object model and the
// text layer are the only readers of. A toggle that wrote the configuration
// without reaching the live settings would take effect at the next restart,
// which for a screen reader is the same as not working.
//
// THE REVIEW CURSOR IS NOT HERE. `review_*` is phase 5: it needs a TextRange.
// `review_activate` is, because activating an object needs no text layer and
// the phase 4 manual test presses it.
#pragma once

#include "alus/core/event_dispatcher.hpp"
#include "alus/script/scriptable.hpp"

#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <memory>
#include <string>

namespace alus::config {
class Manager;
}

namespace alus::script {
class Dispatcher;
}

namespace alus::a11y {

class Registry;
class Object;

class ObjectCommands final : public script::ScriptableObject {
public:
    // `dispatcher` may be null in a test that calls the scripts directly; the
    // repeat count (press twice to spell) is then always zero.
    // `config` may be null in a test; a toggle then changes the live settings
    // and says so, and only the writing-back is lost.
    ObjectCommands(Registry& registry, const script::Catalog* catalog,
                   script::Dispatcher* dispatcher = nullptr,
                   config::Manager* config = nullptr);

private:
    // ---- the scripts (the method names are NVDA's script names) ----
    void reportCurrentFocus(input::Gesture& gesture);
    void navigatorObject_current(input::Gesture& gesture);
    // N145: the mouse buttons, on the navigator object. On Wayland ALUS can
    // neither move the pointer nor send a click (N18); the navigator object's
    // own actions are what a click would have done there.
    void leftMouseClick(input::Gesture& gesture);
    void rightMouseClick(input::Gesture& gesture);
    // The index of the first action whose name is one of `names`, or -1.
    static int actionNamed(const Object& object, std::initializer_list<std::string_view> names);
    void navigatorObject_parent(input::Gesture& gesture);
    void navigatorObject_firstChild(input::Gesture& gesture);
    void navigatorObject_next(input::Gesture& gesture);
    void navigatorObject_previous(input::Gesture& gesture);
    void navigatorObject_nextInFlow(input::Gesture& gesture);
    void navigatorObject_previousInFlow(input::Gesture& gesture);
    void navigatorObject_toFocus(input::Gesture& gesture);
    void navigatorObject_moveFocus(input::Gesture& gesture);
    void navigatorObject_currentDimensions(input::Gesture& gesture);
    void review_activate(input::Gesture& gesture);
    void title(input::Gesture& gesture);
    void toggleCaretMovesReviewCursor(input::Gesture& gesture);
    void toggleFocusMovesNavigatorObject(input::Gesture& gesture);
    void toggleReportDynamicContentChanges(input::Gesture& gesture);
    void toggleProgressBarOutput(input::Gesture& gesture);
    void reportFocusObjectAccelerator(input::Gesture& gesture);
    void reportDetailsSummary(input::Gesture& gesture);
    void navigatorObject_devInfo(input::Gesture& gesture);
    void reportLinkDestination(input::Gesture& gesture);

    // ---- helpers ----
    void say(const std::string& text) const;
    void spell(const std::string& text) const;
    // The third press: spell with character descriptions. Phase 5 made this
    // real (phase-4.md O11); before it, the third press spelled like the second.
    void describeCharacters(const std::string& text) const;
    // NVDA's "press once to say, twice to spell, three times to describe"
    // pattern, which every object query command uses.
    void sayOrSpell(const std::string& text) const;
    // The navigator object, or a spoken "No navigator object" and null.
    std::shared_ptr<Object> navigatorOrComplain() const;
    // Moves the navigator object and announces it, or says `edgeMessage`.
    void moveTo(const std::shared_ptr<Object>& target, const char* edgeMessage);
    // How many times this script was pressed in a row (0 for the first press).
    int repeatCount() const;

    Registry& registry_;
    script::Dispatcher* dispatcher_;
    config::Manager* config_;
    // Which of the details targets `alus+d` reported last, so that pressing
    // it again moves to the next one -- upstream's `_annotationNav`. Held
    // with the object it was reported FOR, because "the next one" means
    // nothing once the focus has moved.
    core::ObjectId detailsOrigin_ = 0;
    std::size_t detailsIndex_ = 0;
};

}  // namespace alus::a11y
