// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/commands.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/manager.hpp"

#include <utility>

namespace alus::input {
namespace {

// NVDA cycles OFF -> EDIT_CONTROLS -> ALWAYS (config/configFlags.TypingEcho,
// values 0, 1, 2), and announces the mode it landed on.
TypingEcho nextEcho(TypingEcho echo) {
    switch (echo) {
        case TypingEcho::Off:
            return TypingEcho::EditControls;
        case TypingEcho::EditControls:
            return TypingEcho::Always;
        case TypingEcho::Always:
            break;
    }
    return TypingEcho::Off;
}

// NVDA's TypingEcho display strings.
// The SPOKEN setting, not the configuration value: parseEcho/echoName handle
// the configuration file and are deliberately not translated.
const char* echoLabel(TypingEcho echo) {
    switch (echo) {
        // Translators: one of the typing-echo settings; nothing is echoed.
        case TypingEcho::Off:
            return _("Off");
        // Translators: one of the typing-echo settings; echoed only while the
        // focus is in a field the user can type into.
        case TypingEcho::EditControls:
            return _("Only in edit controls");
        // Translators: one of the typing-echo settings; echoed everywhere.
        case TypingEcho::Always:
            return _("Always");
    }
    return _("Off");
}

}  // namespace

InputCommands::InputCommands(Context context, const script::Catalog* catalog)
    // One class name, NVDA's: it is what the generated gesture map addresses.
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      context_(std::move(context)) {
    // The descriptions and the key assignments come from the catalogue; what
    // is written here is only what changes behaviour. `speakOnDemand` on the
    // input help toggle is NVDA's own flag and is in the catalogue too, so it
    // is not repeated.
    ALUS_SCRIPT(toggleInputHelp);
    ALUS_SCRIPT(passNextKeyThrough);
    ALUS_SCRIPT(toggleSpeakTypedCharacters);
    ALUS_SCRIPT(toggleSpeakTypedWords);
    ALUS_SCRIPT(toggleSpeakCommandKeys);
    ALUS_SCRIPT(quit);
}

void InputCommands::say(const std::string& text) const {
    if (context_.message) {
        context_.message(text);
    }
}

void InputCommands::toggleInputHelp(Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    InputHelp& help = context_.manager->inputHelp();
    const bool enable = !help.active();
    help.setActive(enable);
    // In input help, every key is ALUS's: the compositor has to hand over the
    // whole keyboard, or the keys being explained would also reach the
    // application.
    if (IInputBackend* backend = context_.manager->backend(); backend != nullptr) {
        std::string error;
        if (!backend->grabKeyboard(enable, &error)) {
            ALUS_DEBUG_WARNING("input help: could not {} the keyboard grab: {}",
                               enable ? "take" : "release", error);
        }
    }
    say(enable
            // Translators: reported when input help is turned on.
            ? _("input help on. Press the same keys again to turn it off.")
            // Translators: reported when input help is turned off.
            : _("input help off"));
}

void InputCommands::passNextKeyThrough(Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    context_.manager->passNextKeyThrough();
    // Translators: spoken to indicate that the next key press goes straight to
    // the application, as though ALUS were not running.
    say(_("Pass next key through"));
    if (IInputBackend* backend = context_.manager->backend();
        backend != nullptr && backend->health() == BackendHealth::Working) {
        // Honesty about the platform: a key ALUS has grabbed has already been
        // consumed by the compositor when ALUS hears about it, and Wayland
        // offers no way to inject it afterwards (N18). So this command can
        // only stop ALUS from ACTING on the next key; a key the compositor
        // grabbed for us still will not reach the application.
        ALUS_DEBUG("pass next key through: ALUS will ignore the next key; a grabbed key still "
                   "does not reach the application on Wayland (N18)");
    }
}

void InputCommands::toggleSpeakTypedCharacters(Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    KeyboardConfig config = context_.manager->config();
    config.speakTypedCharacters = nextEcho(config.speakTypedCharacters);
    context_.manager->setConfig(config);
    if (context_.config != nullptr) {
        context_.config->setString("keyboard", "speakTypedCharacters",
                                   typingEchoName(config.speakTypedCharacters));
    }
    // Translators: reported when cycling the speaking of typed characters.
    // Translators: reported when the typed-character echo is cycled; %s is
    // the new setting ("off", "always", "edit controls only").
    say(i18n::substitute(_("Speak typed characters %s"),
                         echoLabel(config.speakTypedCharacters)));
}

void InputCommands::toggleSpeakTypedWords(Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    KeyboardConfig config = context_.manager->config();
    config.speakTypedWords = nextEcho(config.speakTypedWords);
    context_.manager->setConfig(config);
    if (context_.config != nullptr) {
        context_.config->setString("keyboard", "speakTypedWords",
                                   typingEchoName(config.speakTypedWords));
    }
    // Translators: reported when cycling the speaking of typed words.
    // Translators: reported when the typed-word echo is cycled; %s is the
    // new setting.
    say(i18n::substitute(_("Speak typed words %s"), echoLabel(config.speakTypedWords)));
}

void InputCommands::toggleSpeakCommandKeys(Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        return;
    }
    KeyboardConfig config = context_.manager->config();
    config.speakCommandKeys = !config.speakCommandKeys;
    context_.manager->setConfig(config);
    if (context_.config != nullptr) {
        context_.config->setBool("keyboard", "speakCommandKeys", config.speakCommandKeys);
    }
    say(config.speakCommandKeys
            // Translators: reported when the speaking of command keys is toggled.
            ? _("speak command keys on")
            // Translators: reported when the speaking of command keys is toggled
            // off. (It was an untranslated string literal, the only one left
            // after FAZA10.md's sweep.)
            : _("speak command keys off"));
}

void InputCommands::quit(Gesture& /*gesture*/) {
    if (!context_.quit) {
        // Not silent. The one command whose absence a user cannot work
        // around without finding a terminal must say that it is absent
        // (claude/FAZA9.md N99, and §3.8's rule for an expected failure).
        // Translators: spoken when ALUS cannot stop itself, e.g. because the
        // command was run from a diagnostic tool rather than the screen
        // reader.
        say(_("cannot quit from here"));
        return;
    }
    ALUS_INFO("quit requested from the keyboard");
    // Translators: spoken as ALUS shuts down, on the user's own command.
    say(_("exiting"));
    context_.quit();
}

}  // namespace alus::input
