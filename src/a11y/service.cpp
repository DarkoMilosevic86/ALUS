// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/service.hpp"

#include "alus/a11y/object_speech.hpp"
#include "alus/core/log.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "alus/script/dispatch.hpp"

#include <utility>

namespace alus::a11y {
namespace {

constexpr const char* kSection = "a11y";

}  // namespace

const char* a11yHealthName(A11yHealth health) {
    switch (health) {
        case A11yHealth::Working:
            return "working";
        case A11yHealth::Disabled:
            return "disabled";
        case A11yHealth::NoBus:
            return "no accessibility bus";
        case A11yHealth::NoEvents:
            return "no events";
    }
    return "unknown";
}

Service::Service(core::Loop& loop, config::Manager& config, input::Service* input,
                 PresenterProvider presenter, Options options)
    : loop_(loop),
      config_(config),
      input_(input),
      presenter_(std::move(presenter)),
      options_(std::move(options)) {}

Service::~Service() { terminate(); }

void Service::registerDefaults(config::Manager& config) {
    // What the user wants announced (NVDA's own keys and defaults).
    registerSettingDefaults(config);
    // The object model's own knobs.
    config.setDefault(kSection, "callTimeout", "500");
    config.setDefault(kSection, "maxEventsPerApplication", "10");
    config.setDefault(kSection, "maxFocusEvents", "4");
    config.setDefault(kSection, "trackMouse", "false");
}

void Service::applyConfiguration() {
    if (registry_ == nullptr) {
        return;
    }
    registry_->settings() = settingsFrom(config_);
}

void Service::initialize() {
    if (running_) {
        terminate();
    }
    if (options_.noA11y) {
        health_ = A11yHealth::Disabled;
        ALUS_INFO("the object model is disabled on the command line");
        return;
    }

    Backend* backend = options_.backend;
    if (backend == nullptr) {
        AtspiBackend::Options backendOptions;
        backendOptions.callTimeout =
            std::chrono::milliseconds{config_.getInt(kSection, "callTimeout", 500)};
        // The loop's context, so events arrive inside a pump cycle (§3.1).
        backendOptions.mainContext = loop_.context();
        // The screen reader makes sure applications expose themselves (N151).
        backendOptions.ensureAccessibilityEnabled = true;
        ownedBackend_ = std::make_unique<AtspiBackend>(backendOptions);
        std::string error;
        if (!ownedBackend_->initialize(&error)) {
            health_ = A11yHealth::NoBus;
            // Logged as an error because it is one: on this system ALUS can
            // speak but can read nothing. The daemon says it out loud.
            ALUS_ERROR("the object model could not start: {}", error);
            ownedBackend_.reset();
            return;
        }
        backend = ownedBackend_.get();
    }

    Registry::Options registryOptions;
    registryOptions.maxEventsPerApplication =
        static_cast<std::size_t>(config_.getInt(kSection, "maxEventsPerApplication", 10));
    registryOptions.maxFocusEvents =
        static_cast<std::size_t>(config_.getInt(kSection, "maxFocusEvents", 4));
    registryOptions.trackMouse = config_.getBool(kSection, "trackMouse", false);
    registry_ = std::make_unique<Registry>(loop_, *backend, registryOptions);
    applyConfiguration();

    speech::Presenter* presenter = presenter_ ? presenter_() : nullptr;
    if (presenter != nullptr) {
        installSpeechPresentation(*registry_, *presenter);
    } else {
        ALUS_INFO("the object model is running without speech");
    }

    // The commands this phase owns, with NVDA's descriptions and key
    // assignments from the catalogue.
    commands_ = std::make_unique<ObjectCommands>(
        *registry_, input_ != nullptr ? &input_->catalog() : nullptr,
        input_ != nullptr ? input_->dispatcher() : nullptr, &config_);

    if (input_ != nullptr) {
        // Phase 3's handover, item 2: the parts of the dispatch context the
        // object model owns. The lookup was written and tested for all ten
        // steps in phase 3; this is what finally fills steps 3, 7, 8 and the
        // object half of step 10.
        contextContributor_ = input_->addContextContributor(
            [this](script::DispatchContext& context) {
            if (registry_ == nullptr) {
                return;
            }
            Api& api = registry_->api();
            if (const ObjectPtr& focus = api.focus()) {
                context.focus = focus.get();
                context.appModule = focus->appModule();
                context.treeInterceptor = focus->treeInterceptor();
                context.treeInterceptorReady = focus->treeInterceptorReady();
            }
            context.focusAncestors.clear();
            for (const ObjectPtr& ancestor : api.focusAncestors()) {
                if (ancestor != nullptr) {
                    context.focusAncestors.push_back(ancestor.get());
                }
            }
            if (commands_ != nullptr) {
                context.globalCommands.push_back(commands_.get());
            }
                // Sleep mode is an app module property (NVDA:
                // appModule.sleepMode); phase 8 brings app modules, so until
                // then ALUS is always awake.
            });

        // Phase 3's handover, item 1: the grab set depends on which scripts
        // could run here, so it is rebuilt when the focus moves (O6).
        registry_->setFocusChangedHook([this] {
            if (input_ != nullptr && input_->manager() != nullptr) {
                input_->manager()->refreshGrabs();
            }
        });
    }

    // Stage 1 of every pump cycle, by the order in §3.1: platform events are
    // converted and queued before anything else runs.
    pumpHandler_ = loop_.addStageHandler(core::PumpStage::PlatformEvents, "a11y", [this] {
        if (registry_ == nullptr) {
            return;
        }
        // First: let go of the previous cycle's objects. Not at the end of
        // the cycle, because that is where the property caches are
        // invalidated and nothing may be destroyed there (§3.1; see
        // Registry::releaseCycleObjects).
        registry_->releaseCycleObjects();
        registry_->pumpEvents();
        registry_->sweepObjectCache();
    });

    std::string error;
    if (!registry_->start(&error)) {
        health_ = A11yHealth::NoEvents;
        ALUS_ERROR("the object model has no events: {}", error);
        running_ = true;  // The tree can still be read; only events are lost.
        return;
    }

    health_ = A11yHealth::Working;
    running_ = true;
    ALUS_INFO("{}", describe());
}

void Service::terminate() {
    if (pumpHandler_ != 0) {
        loop_.removeStageHandler(pumpHandler_);
        pumpHandler_ = 0;
    }
    if (input_ != nullptr) {
        if (contextContributor_ != 0) {
            input_->removeContextContributor(contextContributor_);
            contextContributor_ = 0;
        }
    }
    // Order matters: the commands hold a Registry reference, and the registry
    // holds objects that hold backend references.
    commands_.reset();
    if (registry_ != nullptr) {
        registry_->stop();
    }
    registry_.reset();
    ownedBackend_.reset();
    running_ = false;
    health_ = A11yHealth::Disabled;
}

std::string Service::describe() const {
    std::string text = "object model: ";
    text += a11yHealthName(health_);
    if (registry_ == nullptr) {
        return text;
    }
    text += ", backend ";
    text += registry_->backend().backendName();
    if (ownedBackend_ != nullptr) {
        text += " (AT-SPI " + AtspiBackend::platformVersion() + ")";
    }
    const Api& api = registry_->api();
    if (const ObjectPtr& focus = api.focus()) {
        text += ", focus ";
        text += roleName(focus->role());
    } else {
        text += ", no focus";
    }
    return text;
}

}  // namespace alus::a11y
