// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Input help: every key says what it does, and does nothing.
//
// Port of NVDA's `inputCore.manager._inputHelpCaptor` and `_handleInputHelp`
// (architecture.md §5.2). It is how a user learns the command set, so the
// details are the feature:
//
//   * the key's display name is spoken with FULL punctuation symbol level, so
//     that `nvda+shift+'` is read as a key and not swallowed;
//   * the character the key would type is spoken too, but only when it adds
//     something the key name did not already say;
//   * the bound script's description follows;
//   * `nvda+1` — the toggle itself — IS EXECUTED while input help is on, or
//     there would be no way out except killing the screen reader. NVDA keys
//     this off the script NAME, and so does ALUS.
//   * a gesture that declares `bypassInputHelp` (numLock) runs normally.
#pragma once

#include "alus/script/script.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace alus::input {

class Gesture;

class InputHelp {
public:
    struct Hooks {
        // Speaks a key name or a description. `symbolLevelAll` asks the
        // speech layer for the "all punctuation" level NVDA uses here.
        std::function<void(const std::string& text, bool symbolLevelAll)> speakText;
        std::function<void(const std::string& text)> speakSpelling;
        // Braille output, when phase 7 exists.
        std::function<void(const std::string& text)> brailleMessage;
    };

    explicit InputHelp(Hooks hooks = {});

    bool active() const noexcept { return active_; }
    void setActive(bool active);

    // NVDA: `_inputHelpCaptor`. Returns true when the gesture should be
    // handled AS IF input help were off.
    bool shouldBypass(const Gesture& gesture, const std::optional<script::Script>& script) const;

    // NVDA: `_handleInputHelp`. Speaks the gesture and its script. Returns the
    // script that must still be run (the input help toggle), if any.
    std::optional<script::Script> report(Gesture& gesture,
                                         const std::optional<script::Script>& script,
                                         bool onlyLog);

    // The name of the script that may always run: without it, input help is a
    // trap. NVDA hardcodes the same name.
    static constexpr std::string_view kToggleScriptName = "toggleInputHelp";

private:
    Hooks hooks_;
    bool active_ = false;
};

// Should input help spell this character rather than speak it? NVDA spells
// whitespace and single upper-case letters, because "A" spoken as a word is
// indistinguishable from "a".
bool shouldSpellInputHelpCharacter(const std::string& text);

}  // namespace alus::input
