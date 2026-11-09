// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/app/service.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/app/commands.hpp"
#include "alus/app/modules.hpp"
#include "alus/core/log.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/service.hpp"
#include "alus/nav/tree_interceptor.hpp"
#include "alus/script/dispatch.hpp"

#include <cstdlib>
#include <format>
#include <utility>

namespace alus::app {
namespace {

constexpr const char* kSection = "app";

std::filesystem::path homeDirectory() {
    if (const char* fromEnvironment = std::getenv("HOME");
        fromEnvironment != nullptr && *fromEnvironment != '\0') {
        return fromEnvironment;
    }
    return {};
}

std::filesystem::path dataHome() {
    if (const char* fromEnvironment = std::getenv("XDG_DATA_HOME");
        fromEnvironment != nullptr && *fromEnvironment != '\0') {
        return fromEnvironment;
    }
    const std::filesystem::path home = homeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".local" / "share";
}

}  // namespace

Service::Service(config::Manager& config, RegistryProvider registry, NavProvider nav,
                 input::Service* input, std::function<void(const std::string&)> message,
                 Options options)
    : config_(config),
      registryProvider_(std::move(registry)),
      navProvider_(std::move(nav)),
      input_(input),
      message_(std::move(message)),
      options_(std::move(options)) {}

Service::~Service() { terminate(); }

void Service::registerDefaults(config::Manager& config) {
    config.setDefault(kSection, "enabled", "true");
    config.setDefault(kSection, "loadPlugins", "true");
    // The terminal modules' one knob; see src/app/modules/terminal.cpp.
    config.setDefault(kSection, "terminalStabilizeDelay", "30");
}

std::vector<std::filesystem::path> Service::standardPluginDirectories() {
    std::vector<std::filesystem::path> directories;
    const std::filesystem::path data = dataHome();
    if (!data.empty()) {
        // §12.3's order: the scratchpad wins over the user's add-ons, which win
        // over the system ones. That is NVDA's "path injection" model, and the
        // reason is development: a plugin being worked on must shadow the
        // installed copy of itself without uninstalling it.
        directories.push_back(data / "alus" / "scratchpad");
        directories.push_back(data / "alus" / "addons");
    }
#ifdef ALUS_DATA_DIR
    directories.emplace_back(std::filesystem::path{ALUS_DATA_DIR} / "plugins");
#endif
    return directories;
}

void Service::setBrailleTableSourceHook(
    std::function<bool(const std::filesystem::path&)> hook) {
    brailleTableSource_ = std::move(hook);
}

Module* Service::currentModule() const {
    return registry_ != nullptr ? registry_->current() : nullptr;
}

// ------------------------------------------------------------- lifecycle ----

void Service::initialize() {
    if (running_) {
        terminate();
    }
    if (options_.noApp || !config_.getBool(kSection, "enabled", true)) {
        ALUS_INFO("app modules and plugins are disabled");
        return;
    }
    a11y::Registry* a11y = registryProvider_ ? registryProvider_() : nullptr;
    if (a11y == nullptr) {
        // --no-a11y: there are no objects, so there is no application to have
        // a module for. Not an error.
        ALUS_INFO("the app layer is idle: there is no object model");
        return;
    }

    Services services;
    services.a11y = a11y;
    services.nav = navProvider_ ? navProvider_() : nullptr;
    services.input = input_;
    services.config = &config_;
    services.message = message_;
    services.addBrailleTableSource = brailleTableSource_;

    registry_ = std::make_unique<Registry>(services, &config_);
    const std::size_t rules = registerBuiltinModules(*registry_);

    plugins_ = std::make_unique<PluginHost>(
        services, input_ != nullptr ? &input_->catalog() : nullptr);
    if (options_.noPlugins || !config_.getBool(kSection, "loadPlugins", true)) {
        ALUS_INFO("plugins are disabled; {} app module rules active", rules);
    } else {
        for (const std::filesystem::path& directory : options_.pluginDirectories) {
            plugins_->addDirectory(directory);
        }
        for (const std::filesystem::path& directory : standardPluginDirectories()) {
            plugins_->addDirectory(directory);
        }
        // THE SECOND PHASE OF THE UNINSTALL, BEFORE ANY LOADING. This is the
        // moment the whole two-phase mechanism exists for: a plugin marked
        // during the last session is deleted now, while nothing has it open
        // (T8.7).
        const std::size_t removed = plugins_->processPendingRemovals();
        if (removed > 0) {
            ALUS_INFO("{} plugin(s) uninstalled at start-up", removed);
        }
        plugins_->loadAll();
    }

    if (input_ != nullptr) {
        commands_ = std::make_unique<AppCommands>(
            *registry_,
            [this](const std::string& text) {
                if (message_) {
                    message_(text);
                }
            },
            [this](const std::string& text) {
                a11y::Registry* registry = registryProvider_ ? registryProvider_() : nullptr;
                if (registry != nullptr && registry->presentation().spell) {
                    registry->presentation().spell(text);
                } else if (message_) {
                    message_(text);
                }
            },
            &input_->catalog(), input_->dispatcher());
    }

    installOverlayChoosers();
    installEventChain();
    installFocusHooks();
    installDispatchContext();
    installInterceptorVeto();

    // A module's own keys exist from the moment the module does.
    moduleCreatedHandle_ = registry_->moduleCreated().add(
        [this](Module& module) { publishGestures(module); }, "app-gestures");
    republishGestures();

    // THE FOCUS IS ALREADY SET BY THE TIME THIS LAYER STARTS, and it has to be
    // picked up here rather than waited for.
    //
    // The subsystems come up in order and this one is last, so the object
    // model has already run `Registry::start()` -- which finds the initial
    // focus and dispatches its `foreground` event through a chain this layer
    // had not yet joined. Without this line the application the user is
    // ALREADY in never gets its app switch: its profile trigger does not fire,
    // `appGainFocus` is never called, and `alus+end` answers "no focused
    // application" until the user alt-tabs away and back.
    //
    // It is also what makes SIGHUP correct: a reconfigured app layer must
    // arrive at the focus that exists, not at the next one.
    if (a11y::Registry* registry = registryProvider_ ? registryProvider_() : nullptr) {
        onFocusObject(registry->api().focus());
    }

    running_ = true;
    ALUS_INFO("{}", describe());
}

void Service::terminate() {
    if (input_ != nullptr && contextContributor_ != 0) {
        input_->removeContextContributor(contextContributor_);
        contextContributor_ = 0;
    }
    // The overlay choosers go before the modules they call into.
    overlayHandles_.clear();

    a11y::Registry* a11y = registryProvider_ ? registryProvider_() : nullptr;
    if (a11y != nullptr) {
        a11y->setAppModuleProvider(nullptr);
        a11y->setObjectInitHook(nullptr);
        a11y->setFocusEventFilter(nullptr);
        a11y->setContainerOverride(nullptr);
        a11y->dispatcher().setChainProvider(core::EventLevel::GlobalPlugin, nullptr);
        a11y->dispatcher().setChainProvider(core::EventLevel::AppModule, nullptr);
    }
    // The app gesture layer is this service's; a restarted one refills it.
    if (input_ != nullptr && input_->manager() != nullptr) {
        input_->manager()->appMap().clear();
        input_->manager()->refreshMaps();
    }

    commands_.reset();
    // The plugins go before the modules: a plugin may have registered a rule.
    plugins_.reset();
    registry_.reset();
    running_ = false;
}

std::string Service::describe() const {
    if (!running_ && registry_ == nullptr) {
        return "app layer: disabled";
    }
    std::string text = std::format("app layer: {} module rules, {} modules running",
                                   registry_ != nullptr ? registry_->ruleCount() : 0,
                                   registry_ != nullptr ? registry_->moduleCount() : 0);
    if (plugins_ != nullptr) {
        text += std::format(", {} plugins", plugins_->pluginCount());
    } else {
        text += ", plugins disabled";
    }
    if (const Module* module = currentModule()) {
        text += std::format(", focus in {}", module->moduleName());
    }
    return text;
}

// ------------------------------------------------------------- the seams ----

Module* Service::moduleForObject(const a11y::ObjectPtr& object) const {
    if (registry_ == nullptr || object == nullptr) {
        return nullptr;
    }
    return registry_->moduleFor(object->appInfo());
}

void Service::installOverlayChoosers() {
    a11y::Registry* a11y = registryProvider_();
    // Mechanism 1, stage 2: the app module. After the platform layer, which
    // is what lets a module remove what the platform proposed (T8.2).
    overlayHandles_.push_back(a11y->overlays().add(
        a11y::OverlayRegistry::Stage::AppModule,
        [this](a11y::Object& object, a11y::BehaviorList& list) {
            if (registry_ == nullptr) {
                return;
            }
            if (Module* module = registry_->moduleFor(object.appInfo())) {
                module->chooseOverlayClasses(object, list);
            }
        },
        "app-module"));
    // Mechanism 1, stage 3: the plugins. Last, so a plugin can undo both.
    overlayHandles_.push_back(a11y->overlays().add(
        a11y::OverlayRegistry::Stage::Plugin,
        [this](a11y::Object& object, a11y::BehaviorList& list) {
            if (plugins_ != nullptr) {
                plugins_->chooseOverlayClasses(object, list);
            }
        },
        "plugins"));

    // Mechanism 2: object init, for the app module. The plugin ABI has no
    // equivalent on purpose -- a C function called for every object of every
    // application, able to change what the object is, is a performance and a
    // correctness risk out of proportion to what it buys.
    a11y->setObjectInitHook([this](a11y::Object& object) {
        if (registry_ == nullptr) {
            return;
        }
        if (Module* module = registry_->moduleFor(object.appInfo())) {
            module->objectInit(object);
        }
    });

    // Mechanism 5: this is what finally makes `Object::appModule()` answer,
    // and with it step 3 of the ten-step script lookup.
    a11y->setAppModuleProvider(
        [this](const a11y::AppInfo& info) -> script::ScriptableObject* {
            return registry_ != nullptr ? registry_->moduleFor(info) : nullptr;
        });
}

void Service::installEventChain() {
    a11y::Registry* a11y = registryProvider_();

    // Level 1: every global plugin.
    a11y->dispatcher().setChainProvider(
        core::EventLevel::GlobalPlugin,
        [this](const core::Event&) -> std::vector<core::EventDispatcher::ChainHandler> {
            std::vector<core::EventDispatcher::ChainHandler> handlers;
            if (plugins_ == nullptr || plugins_->pluginCount() == 0) {
                return handlers;
            }
            handlers.push_back([this](const core::Event& event,
                                      const core::EventDispatcher::NextHandler& next) {
                a11y::Registry* registry = registryProvider_ ? registryProvider_() : nullptr;
                const a11y::ObjectPtr object =
                    registry != nullptr && event.object != 0
                        ? registry->objectFor(static_cast<a11y::Ref>(event.object))
                        : nullptr;
                if (plugins_->handleEvent(event.name, object) ==
                    Module::Disposition::Consume) {
                    // Not calling next() IS the mechanism (§3.3).
                    return;
                }
                next();
            });
            return handlers;
        });

    // Level 2: the object's app module.
    a11y->dispatcher().setChainProvider(
        core::EventLevel::AppModule,
        [this](const core::Event&) -> std::vector<core::EventDispatcher::ChainHandler> {
            std::vector<core::EventDispatcher::ChainHandler> handlers;
            if (registry_ == nullptr) {
                return handlers;
            }
            handlers.push_back([this](const core::Event& event,
                                      const core::EventDispatcher::NextHandler& next) {
                a11y::Registry* registry = registryProvider_ ? registryProvider_() : nullptr;
                const a11y::ObjectPtr object =
                    registry != nullptr && event.object != 0
                        ? registry->objectFor(static_cast<a11y::Ref>(event.object))
                        : nullptr;
                Module* module = moduleForObject(object);

                // THE APP SWITCH HAPPENS HERE, before level 4 announces
                // anything. NVDA does it inside `api.setFocusObject`; ALUS
                // does it in the chain, because the chain is the one place
                // that runs after the focus is settled and before it is
                // spoken -- so a module's profile is active for the
                // announcement of the very focus change that activated it.
                if (event.name == a11y::events::kGainFocus ||
                    event.name == a11y::events::kForeground) {
                    onFocusObject(object);
                }

                if (module == nullptr) {
                    next();
                    return;
                }
                // Sleep mode: ALUS says nothing about this application. The
                // focus bookkeeping has already happened (the registry does it
                // before the chain), so the user can still navigate; nothing
                // is announced, which is what a self-voicing application
                // needs.
                if (module->sleepMode()) {
                    return;
                }
                if (module->handleEvent(event.name, object) ==
                    Module::Disposition::Consume) {
                    return;
                }
                next();
            });
            return handlers;
        });
}

void Service::installFocusHooks() {
    a11y::Registry* a11y = registryProvider_();

    // Generalised hook 1.
    a11y->setFocusEventFilter([this](const a11y::ObjectPtr& object) {
        Module* module = moduleForObject(object);
        return module == nullptr || module->shouldAllowFocusEvent(object);
    });

    // Generalised hook 3.
    a11y->setContainerOverride([this](const a11y::Object& object) -> a11y::ObjectPtr {
        if (registry_ == nullptr) {
            return nullptr;
        }
        Module* module = registry_->moduleFor(object.appInfo());
        return module != nullptr ? module->container(object) : nullptr;
    });
}

void Service::installDispatchContext() {
    if (input_ == nullptr) {
        return;
    }
    contextContributor_ = input_->addContextContributor([this](script::DispatchContext& context) {
        // Step 2 of the lookup: every global plugin.
        if (plugins_ != nullptr) {
            for (script::ScriptableObject* object : plugins_->scriptObjects()) {
                context.globalPlugins.push_back(object);
            }
        }
        // Step 3 (the app module) is filled in by the object model, from
        // `focus->appModule()`, which reaches the provider installed above --
        // so it is already right and is not set twice here.
        if (commands_ != nullptr) {
            context.globalCommands.push_back(commands_.get());
        }
        // NVDA: `focus.sleepMode`. App sleep lets through only the scripts
        // that asked for it, which is how the user wakes ALUS up again.
        if (const Module* module = currentModule(); module != nullptr && module->sleepMode()) {
            context.sleepMode = script::DispatchContext::Sleep::App;
        }
    });
}

void Service::installInterceptorVeto() {
    nav::Service* nav = navProvider_ ? navProvider_() : nullptr;
    nav::TreeInterceptorRegistry* interceptors =
        nav != nullptr ? nav->interceptors() : nullptr;
    if (interceptors == nullptr) {
        return;
    }
    // ONE veto for the whole app layer; it asks the module that owns the
    // object. See Module::blocksTreeInterceptor for why it is not one veto per
    // module.
    interceptors->addVeto([this](const a11y::ObjectPtr& object) {
        if (object == nullptr || registry_ == nullptr) {
            return false;
        }
        Module* module = registry_->moduleFor(object->appInfo());
        return module != nullptr && module->blocksTreeInterceptor(*object);
    });
}

// --------------------------------------------------------------- gestures ----

void Service::publishGestures(const script::ScriptableObject& object) {
    if (input_ == nullptr || input_->manager() == nullptr) {
        return;
    }
    input::GestureMap& map = input_->manager()->appMap();
    const std::size_t before = map.size();
    for (const script::ScriptInfo* info : object.scripts()) {
        if (info == nullptr) {
            continue;
        }
        for (const std::string& gesture : info->gestures) {
            map.add(gesture, info->location, info->name);
        }
    }
    if (map.size() == before) {
        return;
    }
    // The grab set changes with the map: a module's key that is not grabbed is
    // a key the application gets instead (the Wayland model, §5.2).
    input_->manager()->refreshMaps();
}

void Service::republishGestures() {
    if (plugins_ == nullptr || input_ == nullptr || input_->manager() == nullptr) {
        return;
    }
    input::GestureMap& map = input_->manager()->appMap();
    for (const auto& [gesture, entry] : plugins_->gestureMap().allEntries()) {
        map.add(gesture, entry.location, entry.script);
    }
    input_->manager()->refreshMaps();
}

// -------------------------------------------------------------- app switch ----

void Service::onFocusObject(const a11y::ObjectPtr& object) {
    if (registry_ == nullptr) {
        return;
    }
    if (object == nullptr || !object->isAlive()) {
        return;
    }
    registry_->setFocusedApplication(keyFor(object->appInfo()));
}

}  // namespace alus::app
