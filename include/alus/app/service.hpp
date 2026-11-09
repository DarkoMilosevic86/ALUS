// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The app layer as one restartable unit.
//
// Rule 3 of the phase plan. For this layer it is not a formality: a plugin is
// a `.so` the user installs while ALUS is running, and the whole point of the
// two-phase uninstall is that the next START is what finishes it. SIGHUP is
// that start -- it unloads every plugin, deletes what was marked, and loads
// again, without the user losing their place in the document they were reading.
//
// WHERE THIS LAYER PLUGS IN, which is every seam the phases below deliberately
// left open:
//
//   a11y::OverlayRegistry  Stage::AppModule and Stage::Plugin  (mechanism 1)
//   a11y::Registry         setObjectInitHook                   (mechanism 2)
//   core::EventDispatcher  EventLevel::GlobalPlugin and ::AppModule
//                                                              (mechanism 3)
//   input::Service         a dispatch-context contributor: the app module is
//                          step 3 of the lookup, the plugins are step 2
//                                                              (mechanism 4)
//   a11y::Registry         setAppModuleProvider                (mechanism 5,
//                          which is how `Object::appModule` finally answers)
//   a11y::Registry         setFocusEventFilter, setContainerOverride
//                                                   (generalised hooks 1 and 3)
//   nav::TreeInterceptorRegistry::addVeto            (the Electron case)
//   input::GestureManager  appMap(), where a module's and a plugin's own
//                          bindings live
//
// NOTHING BELOW THIS LAYER KNOWS IT EXISTS. Every one of those seams is
// optional and every phase 4-7 test runs with none of them installed, which is
// what keeps a bug in an app module from being a bug in the object model.
//
// THREADING: main thread only (§13).
#pragma once

#include "alus/app/plugin.hpp"
#include "alus/app/registry.hpp"
#include "alus/config/config.hpp"
#include "alus/core/extension_points.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace alus::a11y {
class Registry;
}

namespace alus::input {
class Service;
}

namespace alus::nav {
class Service;
}

namespace alus::app {

class AppCommands;

struct ServiceOptions {
    // `--no-app`: no app modules and no plugins. The same idea as
    // `--no-speech`, and what the phase 4-7 tests get for free by not
    // creating this service at all.
    bool noApp = false;
    // `--no-plugins`: app modules yes, third-party code no. The setting a bug
    // report asks for first, and the one a user reaches for when a plugin
    // makes ALUS unusable -- which must not require editing a config file
    // with a broken screen reader.
    bool noPlugins = false;
    // Extra plugin directories, searched before the standard ones. Tests pass
    // their build directory here; the daemon passes what `--plugin-dir` says.
    std::vector<std::filesystem::path> pluginDirectories;
};

class Service final : public config::Subsystem {
public:
    using Options = ServiceOptions;
    // The object model and the text layer are restarted independently of this
    // layer, so they arrive as functions and not as pointers -- the same rule
    // as every service since phase 2.
    using RegistryProvider = std::function<a11y::Registry*()>;
    using NavProvider = std::function<nav::Service*()>;

    Service(config::Manager& config, RegistryProvider registry, NavProvider nav,
            input::Service* input, std::function<void(const std::string&)> message,
            Options options = {});
    ~Service() override;

    std::string_view name() const override { return "app"; }
    void initialize() override;
    void terminate() override;

    bool isRunning() const noexcept { return running_; }

    Registry* modules() noexcept { return registry_.get(); }
    PluginHost* plugins() noexcept { return plugins_.get(); }
    // The module of the focused application, or null.
    Module* currentModule() const;
    // One line for the start-up log and for `alus-cli app-info`.
    std::string describe() const;

    // Phase 7's handover, item 1: how a plugin's braille table directory
    // reaches liblouis. A function because the app layer does not link it.
    void setBrailleTableSourceHook(std::function<bool(const std::filesystem::path&)> hook);

    static void registerDefaults(config::Manager& config);

    // The standard plugin directories of §12.3, most specific first:
    // the scratchpad, the user's add-ons, the system plugins.
    static std::vector<std::filesystem::path> standardPluginDirectories();

private:
    void installOverlayChoosers();
    void installEventChain();
    void installFocusHooks();
    void installDispatchContext();
    void installInterceptorVeto();
    // Puts every script of a module or a plugin that declares its own gesture
    // into GestureManager::appMap(), and pushes the grab set.
    void publishGestures(const script::ScriptableObject& object);
    void republishGestures();
    // The app switch, from an object that has just taken the focus.
    void onFocusObject(const a11y::ObjectPtr& object);
    Module* moduleForObject(const a11y::ObjectPtr& object) const;

    config::Manager& config_;
    RegistryProvider registryProvider_;
    NavProvider navProvider_;
    input::Service* input_;
    std::function<void(const std::string&)> message_;
    Options options_;

    std::unique_ptr<Registry> registry_;
    std::unique_ptr<PluginHost> plugins_;
    std::unique_ptr<AppCommands> commands_;
    std::function<bool(const std::filesystem::path&)> brailleTableSource_;

    // Every registration this service made, so that terminate() takes back
    // exactly its own.
    std::vector<core::HandlerHandle> overlayHandles_;
    core::HandlerHandle moduleCreatedHandle_;
    std::uint64_t contextContributor_ = 0;
    bool running_ = false;
};

}  // namespace alus::app
