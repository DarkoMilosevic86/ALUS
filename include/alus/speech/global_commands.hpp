// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The part of NVDA's `globalCommands` that changes SPEECH:
//
//     alus+s                                speech mode (off, beeps, talk, on demand)
//     alus+p                                punctuation and symbol level
//     alus+x                                say the last thing again
//     alus+control+rightArrow / leftArrow   next / previous setting in the ring
//     alus+control+upArrow    / downArrow   that setting up / down
//     alus+control+pageUp     / pageDown    up / down a large step
//
// WHY THESE ARE ONE FILE IN A LIBRARY OF THEIR OWN. The speech library
// (phase 2) must not depend on the script layer, and the input library (phase
// 3) must not depend on speech -- src/meson.build says why for both, and the
// second one is what lets the whole gesture pipeline be tested with no
// synthesizer. A script that CHANGES A SPEECH SETTING needs both halves, so it
// lives above both, exactly as braille's commands do (phase 7).
//
// Until these existed, ALUS could be configured only by typing a voice name
// into a text entry or by editing alus.ini by hand -- so a user whose
// synthesizer was too fast to follow had no way to slow it down, which is the
// first thing a screen reader has to be able to do.
#pragma once

#include "alus/script/scriptable.hpp"

#include <functional>
#include <string>

namespace alus::config {
class Manager;
}

namespace alus::script {
class Dispatcher;
}

namespace alus::speech {

class Presenter;
class SettingsRing;

// At namespace scope, for the usual reason (a nested class's default member
// initializers cannot be used in a default argument of the enclosing class).
struct GlobalCommandsContext {
    // Both are asked for on every key press rather than held, because the
    // speech service rebuilds them when the synthesizer changes (rule 3 of
    // phase-plan.md: every subsystem is restartable in process). A getter that
    // returns null is the normal state in alus-cli and in the command-coverage
    // test, and every script below says so out loud instead of doing nothing.
    std::function<Presenter*()> presenter;
    std::function<SettingsRing*()> ring;
    // Settings are written back, so a change survives `alus+control+c`.
    config::Manager* config = nullptr;
    // ui::message by default; injected so the tests can read what was said.
    std::function<void(const std::string&)> message;
    // Repeat counting: the second press of `alus+x` spells instead of
    // repeating (see the comment on the script).
    // Asked for on every press, not held: the input service creates its
    // dispatcher when it starts -- AFTER the daemon builds this context -- and
    // again on every restart. A held pointer was null for the whole life of
    // the daemon, so every second press counted as a first (phase-10.md N122).
    std::function<const script::Dispatcher*()> dispatcher;
};

class GlobalCommands final : public script::ScriptableObject {
public:
    using Context = GlobalCommandsContext;

    GlobalCommands(Context context, const script::Catalog* catalog);

private:
    void speechMode(input::Gesture& gesture);
    void cycleSpeechSymbolLevel(input::Gesture& gesture);
    void repeatLastSpokenInformation(input::Gesture& gesture);
    void nextSynthSetting(input::Gesture& gesture);
    void previousSynthSetting(input::Gesture& gesture);
    void increaseSynthSetting(input::Gesture& gesture);
    void decreaseSynthSetting(input::Gesture& gesture);
    void increaseLargeSynthSetting(input::Gesture& gesture);
    void decreaseLargeSynthSetting(input::Gesture& gesture);

    void say(const std::string& text) const;
    // NVDA: NO_SETTINGS_MSG. Said when the driver has nothing to step.
    void sayNoSettings() const;
    // The four ring adjustments differ only in which call they make, and the
    // announcement ("Rate 55") is the same for all of them.
    void adjust(const std::function<bool(SettingsRing&, std::string*)>& change);

    Context context_;
};

}  // namespace alus::speech
