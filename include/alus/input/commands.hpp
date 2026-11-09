// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The input commands: the part of NVDA's `globalCommands` that phase 3 owns.
//
// These five are the ones a user needs in order to USE the input layer, and
// they are the ones the phase 3 manual test exercises (02-FAZE.md):
//
//     nvda+1    input help on and off
//     nvda+2    cycle speaking of typed characters
//     nvda+3    cycle speaking of typed words
//     nvda+4    toggle speaking of command keys
//     nvda+f2   pass the next key straight to the application
//
// The class answers to `globalCommands.GlobalCommands`, which is the section
// name the generated built-in gesture map uses, so these scripts pick up
// NVDA's own key assignments and NVDA's own descriptions without a line of
// transcription (see script::Catalog). The rest of `globalCommands` arrives
// with the object model in phase 4.
#pragma once

#include "alus/script/scriptable.hpp"

#include <functional>
#include <string>

namespace alus::config {
class Manager;
}

namespace alus::input {

class GestureManager;

// At namespace scope for the usual reason (default member initializers in a
// default argument).
struct InputCommandsContext {
    GestureManager* manager = nullptr;
    // Settings are written back, so that a toggle survives a restart exactly
    // as it does in NVDA.
    config::Manager* config = nullptr;
    // ui::message, injected so the input library does not depend on speech.
    std::function<void(const std::string&)> message;
    // Stops ALUS. Supplied by the daemon; null in a test and in alus-cli,
    // where `quit` then says it cannot (never silently does nothing).
    std::function<void()> quit;
};

class InputCommands final : public script::ScriptableObject {
public:
    using Context = InputCommandsContext;

    InputCommands(Context context, const script::Catalog* catalog);

private:
    void toggleInputHelp(Gesture& gesture);
    void passNextKeyThrough(Gesture& gesture);
    void toggleSpeakTypedCharacters(Gesture& gesture);
    void toggleSpeakTypedWords(Gesture& gesture);
    void toggleSpeakCommandKeys(Gesture& gesture);
    // NVDA: `script_quit`, `alus+q`. See the context's `quit` above.
    void quit(Gesture& gesture);

    void say(const std::string& text) const;

    Context context_;
};

}  // namespace alus::input
