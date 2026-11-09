// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/app/registry.hpp"

#include "alus/core/log.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace alus::app {
namespace {

// The profile trigger spec for an application, which is the string a user
// associates with a profile: `app:firefox`. config.hpp names this format, so
// it is not invented here.
std::string triggerSpecFor(const Module& module) {
    return std::format("app:{}", module.moduleName());
}

// How specific a rule is. Higher wins; two rules of the same specificity are
// resolved by registration order (the newest first), which is what lets a
// plugin override a built-in rule.
int specificityOf(const Registry::Rule& rule) {
    int score = 0;
    if (!rule.executable.empty()) {
        score += 2;
    }
    if (!rule.toolkit.empty()) {
        score += 1;
    }
    return score;
}

}  // namespace

Registry::Registry(Services services, config::Manager* config)
    : services_(std::move(services)), config_(config) {}

Registry::~Registry() { clear(); }

// ------------------------------------------------------------------ rules ----

bool Registry::add(Rule rule) {
    if (rule.moduleName.empty()) {
        ALUS_WARNING("app module rule with no name; refused");
        return false;
    }
    if (rule.factory == nullptr) {
        // Refused rather than ignored: a rule with no factory matches an
        // application and then produces nothing, which looks exactly like a
        // module that silently does nothing.
        ALUS_WARNING("app module rule {} has no factory; refused", rule.moduleName);
        return false;
    }
    if (rule.executable.empty() && rule.toolkit.empty()) {
        ALUS_WARNING("app module rule {} matches everything; refused", rule.moduleName);
        return false;
    }
    ALUS_DEBUG("app module rule: {} (executable '{}', toolkit '{}')", rule.moduleName,
               rule.executable, rule.toolkit);
    rules_.insert(rules_.begin(), std::move(rule));
    return true;
}

void Registry::addAlias(std::string from, std::string to) {
    if (from.empty() || to.empty() || from == to) {
        return;
    }
    aliases_[std::move(from)] = std::move(to);
}

std::string Registry::resolveExecutable(std::string_view executable) const {
    if (executable.empty()) {
        return {};
    }
    // The registered aliases first: they are newer than the built-in table and
    // a user (or a plugin) correcting the table must win over it.
    const auto it = aliases_.find(executable);
    if (it != aliases_.end()) {
        return it->second;
    }
    return std::string{canonicalExecutable(executable)};
}

const Registry::Rule* Registry::ruleFor(const AppKey& key) const {
    const std::string executable = resolveExecutable(key.executable);
    const Rule* best = nullptr;
    int bestScore = -1;
    for (const Rule& rule : rules_) {
        if (!rule.executable.empty() && rule.executable != executable) {
            continue;
        }
        if (!rule.toolkit.empty() && rule.toolkit != key.toolkit) {
            continue;
        }
        const int score = specificityOf(rule);
        // Strictly greater: rules_ is newest-first, so a tie keeps the newer
        // one, which is the rule a plugin just added.
        if (score > bestScore) {
            best = &rule;
            bestScore = score;
        }
    }
    return best;
}

// ---------------------------------------------------------------- modules ----

Module* Registry::forProcess(int processId) const {
    const auto it = modules_.find(processId);
    return it != modules_.end() ? it->second.get() : nullptr;
}

Module* Registry::moduleFor(const a11y::AppInfo& info) { return moduleFor(keyFor(info)); }

Module* Registry::moduleFor(const AppKey& key) {
    if (key.processId <= 0) {
        // An application that has already exited, or an object with no
        // application: there is nothing to make a module for, and making one
        // keyed on pid 0 would hand every such object the same module.
        return nullptr;
    }
    if (Module* existing = forProcess(key.processId)) {
        return existing;
    }

    const Rule* rule = ruleFor(key);
    if (rule != nullptr) {
        ModulePtr module = rule->factory(key, services_);
        if (module != nullptr) {
            ALUS_INFO("app module {} for {}", rule->moduleName, key.describe());
            return install(key, std::move(module));
        }
        // A factory may decline -- the Electron rule does, for a Chromium
        // application that is a browser rather than an Electron app. The
        // generic module then takes over, which is what NVDA does when
        // `AppModule` is not subclassed.
        ALUS_DEBUG("app module rule {} declined {}", rule->moduleName, key.describe());
    }

    // The generic module. Not null: see the file comment on registry.hpp --
    // the profile trigger, the sleep mode and `statusBar` all hang off a
    // module existing, and most applications have no rule.
    std::string name = resolveExecutable(key.executable);
    if (name.empty()) {
        name = key.name.empty() ? "unknown" : key.name;
    }
    auto generic = std::make_unique<Module>(name, key, services_);
    generic->setGeneric(true);
    ALUS_DEBUG("generic app module for {}", key.describe());
    return install(key, std::move(generic));
}

Module* Registry::install(const AppKey& key, ModulePtr module) {
    Module* raw = module.get();
    modules_.emplace(key.processId, std::move(module));
    // After it is in the map, so that a module whose initialize() asks the
    // registry for itself finds it.
    raw->initialize();
    moduleCreated_.notify(*raw);
    return raw;
}

// ------------------------------------------------------------- app switch ----

bool Registry::setFocusedApplication(const AppKey& key) {
    return setFocusedModule(moduleFor(key));
}

bool Registry::setFocusedModule(Module* module) {
    if (module == current_) {
        // The common case by a wide margin: every focus event inside one
        // application comes through here. Nothing happens -- not one profile
        // rebuild (T8.4).
        return false;
    }

    // §12.1's choreography, and the reason it is one transaction: without the
    // atomic switch this function notifies postProfileSwitch twice (the old
    // trigger leaving, the new one arriving) and every subsystem listening
    // rebuilds itself twice per alt-tab.
    config::Manager::AtomicSwitch atomic;
    if (config_ != nullptr) {
        atomic = config_->atomicProfileSwitch();
    }

    Module* previous = current_;
    if (previous != nullptr) {
        // 1. The module losing the focus hears about it first, while its
        //    profile is still the active one -- which is what lets it put
        //    back a setting it changed on the way in.
        previous->appLoseFocus();
        if (config_ != nullptr) {
            config_->triggerExit(triggerSpecFor(*previous));
        }
    }

    current_ = module;
    ++switches_;

    if (module != nullptr) {
        // 2. The new profile is active BEFORE the module is told it has the
        //    focus, so that a module reading the configuration in
        //    appGainFocus() reads its own profile.
        if (config_ != nullptr) {
            config_->triggerEnter(triggerSpecFor(*module));
        }
        module->appGainFocus();
    }

    ALUS_DEBUG("app switch: {} -> {}", previous != nullptr ? previous->moduleName() : "(none)",
               module != nullptr ? module->moduleName() : "(none)");

    // The dead modules go now rather than on a timer: an application the user
    // just left may be the one that exited, and this is the moment at which
    // nothing holds its module.
    cleanup();
    return true;
}

std::size_t Registry::cleanup() {
    std::size_t removed = 0;
    for (auto it = modules_.begin(); it != modules_.end();) {
        Module* module = it->second.get();
        if (module == current_ || module->isAlive()) {
            ++it;
            continue;
        }
        ALUS_DEBUG("app module {} removed: process {} is gone", module->moduleName(),
                   it->first);
        module->terminate();
        it = modules_.erase(it);
        ++removed;
    }
    return removed;
}

void Registry::clear() {
    // The current module is cleared FIRST, so that the trigger of the
    // application that was focused leaves while its module still exists.
    setFocusedModule(nullptr);
    for (auto& [pid, module] : modules_) {
        if (module != nullptr) {
            module->terminate();
        }
    }
    modules_.clear();
}

}  // namespace alus::app
