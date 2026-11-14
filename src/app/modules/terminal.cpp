// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The terminal app modules: GNOME Terminal, Konsole, Xfce Terminal.
//
// WHAT WAS MEASURED, because it decides how little this module does. On the
// target system (Debian 13, GNOME 48.7):
//
//     org.gnome.Terminal   toolkit gtk 3.24.49   pid -> gnome-terminal-server
//     .../page tab/panel/filler/terminal   [Accessible,Action,Component,Text]
//
// The widget's AT-SPI role IS `terminal`, so phase 5's role-based chooser has
// already attached `NVDAObjects.behaviors.Terminal` to it before this module
// is asked anything (nav/service.cpp). GNOME Terminal therefore needs NO app
// module to be readable, and this one does not pretend otherwise: it checks
// whether the behaviour is there and adds nothing when it is.
//
// WHAT IT IS FOR, then:
//
//  1. THE TERMINALS WHOSE WIDGET DOES NOT SAY `terminal`. Konsole and Xfce
//     Terminal are not installed on the measuring system, so what their text
//     widget reports is UNKNOWN here (docs/history/phase-8.md records this as a
//     deviation rather than guessing). The rule is written so that it is right
//     either way: in a process that IS a terminal emulator, a focused object
//     that exposes Text and reports a generic role is treated as a terminal.
//     Getting this wrong in the safe direction costs a terminal behaviour on
//     an object that did not need one; getting it wrong in the other direction
//     costs a terminal that announces nothing it prints, which is the failure
//     a user cannot work around.
//
//  2. THE STABILISATION DELAY AS A SETTING. `behaviors::Terminal` hard-codes
//     30 ms, which is right for a shell prompt and wrong at both ends: a
//     remote session over a slow link needs longer, and a user reading
//     compiler output wants it shorter. It becomes `[app] terminalStabilizeDelay`,
//     per terminal emulator, which is the kind of thing an app module exists
//     to carry.
#include "alus/a11y/registry.hpp"
#include "alus/app/modules.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/nav/service.hpp"
#include "alus/nav/text_behaviors.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace alus::app {
namespace {

constexpr std::string_view kTerminalBehavior = "NVDAObjects.behaviors.Terminal";

class TerminalModule final : public Module {
public:
    TerminalModule(std::string name, AppKey key, Services services)
        : Module(std::move(name), std::move(key), std::move(services),
                 {"appModules.terminal.AppModule"}) {}

    void chooseOverlayClasses(a11y::Object& object, a11y::BehaviorList& list) override {
        nav::Service* nav = services().nav;
        if (nav == nullptr) {
            return;
        }
        if (a11y::hasBehavior(list, kTerminalBehavior)) {
            // GNOME Terminal: the platform role already said so. Only the
            // delay is this module's business, and it is applied where the
            // behaviour is built rather than here -- see objectInit.
            return;
        }
        if (!isTerminalText(object)) {
            return;
        }
        // The front, where it wins: a terminal's role override has to beat
        // whatever generic role the toolkit reported, or the user is told they
        // are in a "panel".
        ALUS_DEBUG("terminal module: attaching Terminal to {} (role {})", object.path(),
                   a11y::roleName(object.platformRole() == a11y::Role::Unknown
                                      ? a11y::Role::Unknown
                                      : object.platformRole()));
        a11y::prependBehavior(
            list, a11y::BehaviorEntry{std::string{kTerminalBehavior}, [nav] {
                                          return std::make_unique<a11y::behaviors::Terminal>(
                                              *nav);
                                      }});
    }

    void initialize() override {
        // Read once, here, and not per object: objectInit runs for every
        // object a busy terminal creates.
        if (config::Manager* config = services().config) {
            delay_ = std::chrono::milliseconds{
                config->getInt("app", "terminalStabilizeDelay", delay_.count())};
        }
    }

    void objectInit(a11y::Object& object) override {
        // The delay belongs to the behaviour instance, and the instance only
        // exists once the behaviours have been built -- which is exactly when
        // this is called (§12.2, mechanism 2).
        for (const std::unique_ptr<a11y::Behavior>& behavior : object.behaviors()) {
            if (behavior == nullptr || behavior->className() != kTerminalBehavior) {
                continue;
            }
            auto* terminal = static_cast<a11y::behaviors::Terminal*>(behavior.get());
            terminal->stabilizeDelay = delay_;
        }
    }

private:
    static bool isTerminalText(const a11y::Object& object) {
        if (!object.hasInterface(a11y::Interface::Text)) {
            return false;
        }
        switch (object.platformRole()) {
            case a11y::Role::Terminal:
                return true;
            // What a terminal widget that does not know it is one reports.
            // Narrow on purpose: a label and a menu item also expose Text in
            // GTK, and treating those as terminals would make every menu
            // announce itself as a terminal.
            case a11y::Role::EditableText:
            case a11y::Role::Document:
            case a11y::Role::RichEdit:
                return object.states().has(a11y::State::MultiLine);
            default:
                return false;
        }
    }

    // The behaviour's own default, which is right for a local shell prompt
    // (nav/text_behaviors.cpp says why 30 ms).
    std::chrono::milliseconds delay_{30};
};

ModulePtr makeTerminal(std::string name, const AppKey& key, const Services& services) {
    return std::make_unique<TerminalModule>(std::move(name), key, services);
}

}  // namespace

void registerTerminalModules(Registry& registry) {
    struct Known {
        const char* module;
        const char* executable;
    };
    // The three the phase plan names. `gnome-terminal` and `mate-terminal` are
    // aliased onto `gnome-terminal-server` by the built-in table (app/key.hpp),
    // because that is what the process is actually called.
    constexpr Known kTerminals[] = {
        {"gnome-terminal", "gnome-terminal-server"},
        {"konsole", "konsole"},
        {"xfce4-terminal", "xfce4-terminal"},
    };
    for (const Known& terminal : kTerminals) {
        const std::string moduleName = terminal.module;
        registry.add(Registry::Rule{
            moduleName, terminal.executable, "",
            [moduleName](const AppKey& key, const Services& services) -> ModulePtr {
                return makeTerminal(moduleName, key, services);
            }});
    }
}

}  // namespace alus::app
