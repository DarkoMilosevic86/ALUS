// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The plugin host: `dlopen`, the ABI check, and the two-phase uninstall.
//
// 01-ARHITEKTURA.md §12.3. The C side of the contract is `alus/alus-plugin.h`,
// which is the only header an add-on includes; this is the side that enforces
// it.
//
// THE THREE THINGS THAT MUST NOT BE GOT WRONG
//
//  1. THE VERSION CHECK HAPPENS BEFORE ANYTHING IS CALLED. A plugin built
//     against a different ABI is refused after exactly two reads
//     (`abi_version`, `min_alus_version`) and one log line. Calling `init`
//     first and asking afterwards is how a screen reader crashes on a
//     function pointer that moved (T8.5).
//
//  2. A LOADED `.so` CANNOT BE DELETED. NVDA's add-on manager solves this with
//     a two-phase uninstall and so does ALUS: `markForRemoval` writes a marker
//     beside the plugin and returns; `processPendingRemovals`, which runs
//     BEFORE any loading at the next start, deletes what is marked. Trying to
//     unlink a mapped library instead gives either a running plugin that the
//     user has uninstalled or a half-deleted add-on directory (T8.7).
//
//  3. WHAT A PLUGIN REGISTERED IS OWNED HERE, not by the plugin. A plugin's
//     scripts, event handlers and overlay choosers are held by this host and
//     removed when it unloads the library -- in that order, because a handler
//     whose code has been unmapped is the one crash with no stack trace.
//     dlclose() is therefore the LAST thing that happens to a plugin, after
//     its own `terminate` and after every registration is gone.
//
// WHAT A PLUGIN CANNOT DO, and why that is deliberate: it cannot supply a C++
// `a11y::Behavior`. It can add a NAMED behaviour to an object's overlay list,
// which puts its name in the object's script class chain and so lets its
// scripts and its gesture map sections reach that object -- which is what NVDA
// global plugins actually use overlay classes for. Property overrides stay in
// C++, in this process, where a wrong answer is a bug and not an ABI break.
//
// THREADING: main thread only (§13), including every call into a plugin.
#pragma once

#include "alus/a11y/behavior.hpp"
#include "alus/a11y/object.hpp"
#include "alus/app/module.hpp"
#include "alus/alus-plugin.h"
#include "alus/input/gesture_map.hpp"
#include "alus/script/script.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace alus::script {
class Catalog;
}

namespace alus::app {

// A plugin's scripts live on a ScriptableObject of their own, which is what
// makes them step 2 of the ten-step lookup ("every global plugin") and lets
// `gestures.ini` rebind them.
class PluginScripts final : public script::ScriptableObject {
public:
    explicit PluginScripts(std::string pluginName, const script::Catalog* catalog = nullptr);
    ~PluginScripts() override;

    // The gesture map section a user writes to rebind this plugin's keys:
    // `[plugins.<name>]`.
    static std::string classNameFor(std::string_view pluginName);

    void addScript(std::string name, script::ScriptFunction function, script::ScriptSpec spec);
};

class PluginHost {
public:
    // One event handler a plugin registered.
    struct EventRegistration {
        std::string event;
        AlusEventFunc handler = nullptr;
        void* userData = nullptr;
    };

    struct Loaded {
        // THE `AlusHost*` A PLUGIN IS GIVEN IS A POINTER TO THIS. That is why
        // it carries a back-pointer: a plugin calling `register_script` has to
        // be identified, and identifying it by its library handle would mean a
        // lookup on every call from the one place that must stay cheap.
        PluginHost* owner = nullptr;
        std::string name;
        std::filesystem::path path;
        std::string minVersion;
        std::uint32_t abiVersion = 0;
        // Where its scripts live, which is what makes them step 2 of the
        // lookup. Created with the plugin, even when it registers none.
        std::unique_ptr<PluginScripts> scripts;
        std::vector<EventRegistration> events;
        // Owned by the library, valid until it is unloaded.
        const AlusPluginV1* plugin = nullptr;
        void* handle = nullptr;
    };

    // The marker file that says "delete me at the next start". A dot file, so
    // that it cannot collide with a plugin's own files, and with the ALUS name
    // in it, so that it is obvious what wrote it.
    static constexpr std::string_view kRemovalMarker = ".alus-pending-remove";

    PluginHost(Services services, const script::Catalog* catalog = nullptr);
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // Searched in the order given; §12.3's order is scratchpad, user add-ons,
    // system plugins. A directory that does not exist is skipped silently --
    // a user with no add-ons is the normal case.
    void addDirectory(std::filesystem::path directory);
    const std::vector<std::filesystem::path>& directories() const noexcept {
        return directories_;
    }

    // MUST be called before loadAll(). Deletes every plugin directory (and
    // every bare `.so`) marked for removal; returns how many went.
    std::size_t processPendingRemovals();

    // Loads every plugin found in the directories. Returns how many loaded;
    // the ones refused are in the log, with the reason.
    std::size_t loadAll();
    // One plugin, by path. False with `error` filled when it is refused --
    // which covers a missing entry point, a wrong ABI version, a too-new
    // `min_alus_version` and an `init` that returned non-zero.
    bool load(const std::filesystem::path& path, std::string* error);

    // The second phase of the uninstall. True when the plugin was found and
    // the marker was written; the plugin keeps running until the next start.
    bool markForRemoval(std::string_view name, std::string* error);
    bool isMarkedForRemoval(std::string_view name) const;

    // Runs `terminate`, removes every registration, then dlclose()s. In that
    // order; see the file comment.
    void unloadAll();

    const Services& services() const noexcept { return services_; }
    const std::vector<std::unique_ptr<Loaded>>& plugins() const noexcept { return plugins_; }
    std::size_t pluginCount() const noexcept { return plugins_.size(); }
    const Loaded* find(std::string_view name) const;

    // ---- what the app service plugs into the rest of ALUS ----
    // Mechanism 1 for every loaded plugin, in load order. Installed as the
    // Plugin stage of a11y::OverlayRegistry.
    void chooseOverlayClasses(a11y::Object& object, a11y::BehaviorList& list);
    // Level 1 of the event chain. Returns Consume when any plugin consumed
    // the event.
    Module::Disposition handleEvent(std::string_view name, const a11y::ObjectPtr& object);
    // Step 2 of the ten-step script lookup: every plugin's script object, in
    // load order.
    std::vector<script::ScriptableObject*> scriptObjects() const;
    // The gestures every loaded plugin asked for, as a gesture map the
    // dispatcher can be given.
    const input::GestureMap& gestureMap() const noexcept { return gestures_; }

    // The host API vtable, exposed for the tests: a test plugin can be
    // exercised without a `.so` at all.
    static const AlusHostApi& hostApi();

    // What the C API's `AlusHost*` really is, and the call every host
    // function makes first. Public so that the api functions, which are free
    // functions in the .cpp, can use it; it is not part of the plugin ABI.
    static Loaded* fromHandle(AlusHost* host);

private:
    void unload(Loaded& loaded);
    std::vector<std::filesystem::path> discover() const;
    // Fills `error` and returns false when this plugin may not run here.
    bool checkVersions(const AlusPluginV1& plugin, std::string* error) const;

    Services services_;
    const script::Catalog* catalog_;
    std::vector<std::filesystem::path> directories_;
    std::vector<std::unique_ptr<Loaded>> plugins_;
    input::GestureMap gestures_{"plugins"};
};

// A behaviour that is nothing but a name.
//
// It exists for the plugin ABI (a plugin cannot define a C++ class) and it is
// not a placeholder: a name in the overlay list IS a name in the object's
// script class chain, which is the whole of what an NVDA global plugin's
// overlay class usually does -- make a gesture map section and a set of
// scripts apply to these objects and no others.
class NamedBehavior final : public a11y::Behavior {
public:
    explicit NamedBehavior(std::string className);
    ~NamedBehavior() override;

    std::string_view className() const override { return className_; }

private:
    std::string className_;
};

}  // namespace alus::app
