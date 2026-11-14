// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Which module an application gets, and what happens when the user switches.
//
// Port of NVDA's `appModuleHandler` (architecture.md §12.1): the running
// table, the lookup, and `handleAppSwitch`'s choreography.
//
// THE LOOKUP IS RULES, NOT FILENAMES. NVDA imports `appModules/<name>.py` and
// so its lookup is "is there a file with this name". ALUS is compiled, so a
// rule registers itself and says what it matches:
//
//     executable + toolkit    most specific  (this editor, on this toolkit)
//     executable              by program     (T8.1, first half)
//     toolkit                 by family      (T8.1, second half -- every
//                                             Electron application at once)
//     neither                 the generic module
//
// A rule is chosen once per process and the module is kept until the process
// exits, which is NVDA's `runningTable` keyed by pid. It is NOT keyed by
// executable: two LibreOffice processes get two modules, because a module
// holds per-application state (the sleep mode, the last announced cell) and
// sharing that between two windows of the same program is a bug the user hears.
//
// WHY THERE IS ALWAYS A MODULE. NVDA creates a plain `AppModule` for every
// application that has no file of its own, and the whole configuration-profile
// mechanism hangs off it: `app:firefox` is a profile trigger whether or not
// anyone wrote a Firefox module. A null module would mean no trigger, no sleep
// mode and no `statusBar` for 95% of applications, so a lookup that matches no
// rule produces a generic module rather than nothing (Module::isGeneric).
//
// THREADING: main thread only (§13).
#pragma once

#include "alus/app/module.hpp"
#include "alus/config/config.hpp"
#include "alus/core/extension_points.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace alus::app {

class Registry {
public:
    using Factory = std::function<ModulePtr(const AppKey&, const Services&)>;

    struct Rule {
        // The module's name: its gesture map section, its profile trigger and
        // what the log prints. Required.
        std::string moduleName;
        // Lower-cased executable basename; empty matches any.
        std::string executable;
        // Lower-cased toolkit name; empty matches any.
        std::string toolkit;
        Factory factory;
    };

    // `config` may be null, and then the profile triggers of §12.1 do not
    // fire -- which is what every test that only looks at the lookup does.
    Registry(Services services, config::Manager* config = nullptr);
    ~Registry();

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    const Services& services() const noexcept { return services_; }

    // ---- rules ----
    // A rule with no factory is rejected with a log line rather than silently
    // ignored: a registration that does nothing is always a mistake.
    bool add(Rule rule);
    // NVDA's `EXECUTABLE_NAMES_TO_APP_MODS`, at run time. `from` and `to` are
    // executable basenames; the alias is applied before the rules are
    // consulted, so one rule serves every spelling of one program.
    void addAlias(std::string from, std::string to);
    // The executable a lookup will actually use: the built-in alias table
    // (app/key.hpp) and then the registered aliases, which win because they
    // are newer.
    std::string resolveExecutable(std::string_view executable) const;
    // Which rule would be chosen, without creating anything. Null when only
    // the generic module would be made.
    const Rule* ruleFor(const AppKey& key) const;
    std::size_t ruleCount() const noexcept { return rules_.size(); }

    // ---- modules ----
    // The module for this application, created on first sight and then
    // reused. Never null for a live process; null for pid 0 (which is what an
    // application that has already exited reports).
    Module* moduleFor(const AppKey& key);
    Module* moduleFor(const a11y::AppInfo& info);
    // Without creating one.
    Module* forProcess(int processId) const;
    std::size_t moduleCount() const noexcept { return modules_.size(); }

    // ---- the app switch (§12.1) ----
    // The module of the application that has the focus. Null before the first
    // focus event.
    Module* current() const noexcept { return current_; }
    // NVDA: `handleAppSwitch`. Runs the whole choreography, in this order and
    // ALL INSIDE ONE `config::Manager::AtomicSwitch`:
    //
    //   1. `appLoseFocus` on the module losing the focus,
    //   2. its profile trigger (`app:<name>`) exits,
    //   3. the new module's profile trigger enters,
    //   4. `appGainFocus` on it.
    //
    // Returns true when the current module changed. Switching to the module
    // that is already current does nothing at all -- not even one profile
    // rebuild, which matters because a focus event inside one application
    // arrives dozens of times a minute (T8.4).
    bool setFocusedModule(Module* module);
    // The same, from an application's key: the usual caller, since what the
    // focus change supplies is an object.
    bool setFocusedApplication(const AppKey& key);
    // How many times the choreography has actually run. The measurement T8.4
    // asks for.
    std::uint64_t switchCount() const noexcept { return switches_; }

    // Notified right after a module is created and initialised. The app
    // service listens so that a module's own gestures reach the gesture map
    // the moment the module exists -- which is the first focus event in that
    // application, not the start of the session.
    core::Action<Module&>& moduleCreated() noexcept { return moduleCreated_; }

    // NVDA: `cleanup`. Drops the modules whose processes have exited; returns
    // how many went. Called on every app switch, which is often enough: an
    // application that exits while in the background costs one stale module
    // until the user moves.
    std::size_t cleanup();
    // Terminates and drops everything, current module included.
    void clear();

private:
    Module* install(const AppKey& key, ModulePtr module);

    Services services_;
    config::Manager* config_;
    // Most recently added first, so that a rule registered later (a plugin's)
    // wins over an equally specific built-in one.
    std::vector<Rule> rules_;
    std::map<std::string, std::string, std::less<>> aliases_;
    std::map<int, ModulePtr> modules_;
    Module* current_ = nullptr;
    std::uint64_t switches_ = 0;
    core::Action<Module&> moduleCreated_;
};

}  // namespace alus::app
