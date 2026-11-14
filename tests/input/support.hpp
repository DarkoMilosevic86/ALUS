// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Shared helpers for the phase 3 tests: key events, a scriptable test object
// and a recording of what the input layer asked speech to do.
#pragma once

#include "alus/input/gesture.hpp"
#include "alus/input/manager.hpp"
#include "alus/script/scriptable.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace alus::testing {

inline alus::input::KeyEvent keyDown(std::uint32_t keysym, std::uint32_t state = 0,
                                     std::uint32_t keycode = 100, std::uint32_t unichar = 0) {
    alus::input::KeyEvent event;
    event.pressed = true;
    event.keysym = keysym;
    event.state = state;
    event.keycode = keycode;
    event.unichar = unichar;
    return event;
}

inline alus::input::KeyEvent keyUp(std::uint32_t keysym, std::uint32_t state = 0,
                                   std::uint32_t keycode = 100) {
    alus::input::KeyEvent event = keyDown(keysym, state, keycode);
    event.pressed = false;
    return event;
}

// A scriptable object whose scripts only record that they ran, with a
// settable pass-through state so that step 6 of the lookup can be tested
// without a tree interceptor existing yet.
class TestScriptable final : public alus::script::ScriptableObject {
public:
    explicit TestScriptable(std::vector<std::string> classChain,
                            const alus::script::Catalog* catalog = nullptr)
        : ScriptableObject(std::move(classChain), catalog) {}

    void add(const std::string& name, alus::script::ScriptSpec spec = {}) {
        bindScript(name, [this, name](alus::input::Gesture&) { ran.push_back(name); },
                   std::move(spec));
    }

    bool passThrough() const override { return passThrough_; }
    void setPassThrough(bool value) { passThrough_ = value; }

    std::vector<std::string> ran;

private:
    bool passThrough_ = false;
};

// What the input layer told speech to do, in order.
struct SpeechRecorder {
    std::vector<std::string> spoken;
    std::vector<std::string> spelled;
    int cancels = 0;
    int pauses = 0;
    int resumes = 0;
    // Everything above, in the order it reached the speech layer: "cancel",
    // "speak:<text>", "spell:<text>". The ORDER is the point for N130.
    std::vector<std::string> order;

    alus::input::GestureManager::Hooks hooks() {
        alus::input::GestureManager::Hooks hooks;
        hooks.cancelSpeech = [this]() {
            ++cancels;
            order.push_back("cancel");
        };
        hooks.pauseSpeech = [this](bool pause) { pause ? ++pauses : ++resumes; };
        hooks.speakText = [this](const std::string& text, bool) {
            spoken.push_back(text);
            order.push_back("speak:" + text);
        };
        hooks.speakSpelling = [this](const std::string& text) {
            spelled.push_back(text);
            order.push_back("spell:" + text);
        };
        return hooks;
    }
};

}  // namespace alus::testing
