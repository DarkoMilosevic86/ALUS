// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/app/commands.hpp"

#include "alus/app/module.hpp"
#include "alus/app/registry.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/core/clipboard.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/script/dispatch.hpp"

#include <utility>

namespace alus::app {

AppCommands::AppCommands(Registry& modules, std::function<void(const std::string&)> message,
                         std::function<void(const std::string&)> spell,
                         const script::Catalog* catalog, script::Dispatcher* dispatcher)
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      modules_(modules),
      message_(std::move(message)),
      spell_(std::move(spell)),
      dispatcher_(dispatcher) {
    // No gestures here: `alus+end` and `alus+shift+end` come from the
    // generated catalogue, which has NVDA's own assignments for this script.
    ALUS_SCRIPT(reportStatusLine);
    ALUS_SCRIPT(reportAppModuleInfo);
    ALUS_SCRIPT(toggleCurrentAppSleepMode);
    ALUS_SCRIPT(readStatusLine);
    ALUS_SCRIPT(spellStatusLine);
    ALUS_SCRIPT(copyStatusLine);
}

AppCommands::~AppCommands() = default;

void AppCommands::reportStatusLine(input::Gesture& /*gesture*/) {
    Module* module = modules_.current();
    if (module == nullptr) {
        // No focused application at all, which happens before the first focus
        // event and after the last window closes.
        if (message_) {
            // Translators: reported when no application has the focus.
            message_(_("No focused application"));
        }
        return;
    }
    const std::string text = module->statusBarText();
    if (text.empty()) {
        if (message_) {
            // Translators: reported when the focused application has no status
            // line, or when its status line is empty.
            message_(_("No status line found"));
        }
        return;
    }
    const int repeats = dispatcher_ != nullptr ? dispatcher_->lastScriptRepeatCount() : 0;
    if (repeats == 0) {
        if (message_) {
            message_(text);
        }
        return;
    }
    // Twice: spell it. Three times: copy it, as NVDA does (N156: the
    // clipboard works now, through alus-clipboard).
    if (repeats >= 2) {
        copy(text);
        return;
    }
    if (spell_) {
        spell_(text);
    } else if (message_) {
        message_(text);
    }
}


std::optional<std::string> AppCommands::statusText() const {
    Module* module = modules_.current();
    if (module == nullptr) {
        if (message_) {
            message_(_("No focused application"));
        }
        return std::nullopt;
    }
    std::string text = module->statusBarText();
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (message_) {
            message_(_("No status line found"));
        }
        return std::nullopt;
    }
    return text;
}

void AppCommands::copy(const std::string& text) const {
    const std::weak_ptr<int> alive = lifetime_;
    const auto report = [this, alive, text](bool ok) {
        if (alive.expired() || !message_) {
            return;
        }
        message_(ok
                     // Translators: announced when a text has been copied to
                     // the clipboard. %s is the copied text.
                     ? i18n::substitute(_("Copied to clipboard: %s"), text)
                     // Translators: Presented when unable to copy to the
                     // clipboard because of an error.
                     : std::string(_("Unable to copy")));
    };
    if (!core::writeClipboard(text, report)) {
        report(false);
    }
}

void AppCommands::readStatusLine(input::Gesture& /*gesture*/) {
    if (const std::optional<std::string> text = statusText(); text && message_) {
        message_(*text);
    }
}

void AppCommands::spellStatusLine(input::Gesture& /*gesture*/) {
    if (const std::optional<std::string> text = statusText()) {
        if (spell_) {
            spell_(*text);
        } else if (message_) {
            message_(*text);
        }
    }
}

void AppCommands::copyStatusLine(input::Gesture& /*gesture*/) {
    if (const std::optional<std::string> text = statusText()) {
        copy(*text);
    }
}

void AppCommands::reportAppModuleInfo(input::Gesture& /*gesture*/) {
    Module* module = modules_.current();
    if (module == nullptr) {
        if (message_) {
            // Translators: reported when no application has the focus.
            message_(_("No focused application"));
        }
        return;
    }
    // Upstream says two sentences, and only the first one when a module other
    // than the generic one is loaded: " %s module is loaded. " and
    // " %s is currently running." The msgids are upstream's, leading space and
    // all, because they are the key into its Serbian catalogue
    // (tools/import-nvda-po.py).
    std::string text;
    if (!module->isGeneric()) {
        // Translators: the app module loaded for the focused application.
        text += i18n::substitute(_(" %s module is loaded. "), module->moduleName());
    }
    // Translators: the program that is running, by its executable name.
    text += i18n::substitute(_(" %s is currently running."), module->key().describe());
    if (message_) {
        message_(text);
    }
}

void AppCommands::toggleCurrentAppSleepMode(input::Gesture& /*gesture*/) {
    Module* module = modules_.current();
    if (module == nullptr) {
        if (message_) {
            // Translators: reported when no application has the focus.
            message_(_("No focused application"));
        }
        return;
    }
    const bool sleeping = !module->sleepMode();
    module->setSleepMode(sleeping);
    ALUS_INFO("sleep mode {} for {}", sleeping ? "on" : "off", module->moduleName());
    if (message_) {
        // The announcement is made EITHER WAY and before the mode takes hold
        // for the "on" case, because app sleep silences this application --
        // including, without this, the message saying that it had.
        message_(sleeping
                     // Translators: reported when ALUS is told to say nothing
                     // about the focused application, which speaks for itself.
                     ? _("Sleep mode on")
                     // Translators: reported when ALUS goes back to announcing
                     // the focused application.
                     : _("Sleep mode off"));
    }
    // NOT re-announcing the focus on waking, which upstream does by firing
    // gainFocus again. The app layer reaches speech through its own hooks and
    // has no way to ask the object model to present the focus a second time;
    // what a user loses is one repetition they can get with alus+tab, and
    // inventing a second event path for it would be the larger harm
    // (app/service.hpp lists the seams this layer has).
}

}  // namespace alus::app
