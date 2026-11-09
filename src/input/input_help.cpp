// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/input_help.hpp"

#include "alus/core/log.hpp"
#include "alus/input/gesture.hpp"

#include <glib.h>

#include <utility>
#include <vector>

namespace alus::input {

bool shouldSpellInputHelpCharacter(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    const gunichar codepoint = g_utf8_get_char(text.c_str());
    if (g_unichar_isspace(codepoint) != 0) {
        return true;
    }
    const bool single = g_utf8_strlen(text.c_str(), -1) == 1;
    return single && g_unichar_isalpha(codepoint) != 0 && g_unichar_isupper(codepoint) != 0;
}

InputHelp::InputHelp(Hooks hooks) : hooks_(std::move(hooks)) {}

void InputHelp::setActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    ALUS_INFO("input help {}", active_ ? "on" : "off");
}

bool InputHelp::shouldBypass(const Gesture& gesture,
                             const std::optional<script::Script>& script) const {
    if (gesture.bypassInputHelp()) {
        return true;
    }
    return script.has_value() && script->info != nullptr && script->info->bypassInputHelp;
}

std::optional<script::Script> InputHelp::report(Gesture& gesture,
                                                const std::optional<script::Script>& script,
                                                bool onlyLog) {
    const std::string displayName = gesture.displayName();
    const std::optional<std::string> helpCharacter = gesture.inputHelpCharacter();
    std::string description;
    std::optional<script::Script> toRun;

    std::string logLine = "input help: gesture " + displayName;
    if (script.has_value() && script->info != nullptr) {
        logLine += ", bound to script " + script->info->name;
        if (!script->info->location.empty()) {
            logLine += " on " + script->info->location;
        }
        if (script->info->name == kToggleScriptName) {
            // The one script that runs while input help is on. Without this,
            // the only way out of input help would be to kill the process.
            toRun = script;
        } else {
            description = script->info->description;
        }
    }
    ALUS_INFO("{}", logLine);
    if (onlyLog) {
        return toRun;
    }

    bool reportCharacter = false;
    bool spellCharacter = false;
    if (helpCharacter.has_value()) {
        spellCharacter = shouldSpellInputHelpCharacter(*helpCharacter);
        // NVDA compares the normalized speech of the two so that a key whose
        // name already IS the character is not said twice. ALUS compares the
        // strings case-insensitively, which is the same decision for every
        // case that occurs and does not need the speech layer to answer a
        // question about a string.
        gchar* left = g_utf8_casefold(displayName.c_str(), -1);
        gchar* right = g_utf8_casefold(helpCharacter->c_str(), -1);
        reportCharacter = g_strcmp0(left, right) != 0;
        g_free(left);
        g_free(right);
    }

    if (hooks_.brailleMessage) {
        std::string brailleText = displayName;
        if (reportCharacter) {
            brailleText += "\t\t" + *helpCharacter;
        }
        if (!description.empty()) {
            brailleText += "\t\t" + description;
        }
        hooks_.brailleMessage(brailleText);
    }

    if (hooks_.speakText) {
        // Punctuation must be spoken in full here, or a punctuation key says
        // nothing at all.
        hooks_.speakText(displayName, true);
    }
    if (reportCharacter) {
        if (spellCharacter && hooks_.speakSpelling) {
            hooks_.speakSpelling(*helpCharacter);
        } else if (hooks_.speakText) {
            hooks_.speakText(*helpCharacter, true);
        }
    }
    if (!description.empty() && hooks_.speakText) {
        hooks_.speakText(description, false);
    }
    return toRun;
}

}  // namespace alus::input
