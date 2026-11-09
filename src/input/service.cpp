// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/service.hpp"

#include <algorithm>

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/core/paths.hpp"
#include "alus/script/command_status.hpp"
#include "alus/core/watchdog.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

namespace alus::input {
namespace {

std::string currentLanguage() {
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(variable);
        if (value == nullptr || *value == '\0') {
            continue;
        }
        std::string language(value);
        // "sr_RS.UTF-8@latin" -> "sr_RS"
        const std::size_t dot = language.find_first_of(".@");
        if (dot != std::string::npos) {
            language.resize(dot);
        }
        if (language == "C" || language == "POSIX") {
            return "en";
        }
        return language;
    }
    return "en";
}

}  // namespace

Service::Service(core::Loop& loop, config::Manager& config, GestureManager::Hooks hooks,
                 Options options)
    : loop_(loop), config_(config), hooks_(std::move(hooks)), options_(std::move(options)) {}

Service::~Service() { terminate(); }

void Service::registerDefaults(config::Manager& config) {
    // NVDA's own defaults and NVDA's own key names, so that a user arriving
    // from NVDA finds the same screen reader (Z1).
    config.setDefault("keyboard", "keyboardLayout", "desktop");
    config.setDefault("keyboard", "alusKeyInsert", "true");
    config.setDefault("keyboard", "alusKeyNumpadInsert", "true");
    config.setDefault("keyboard", "alusKeyCapsLock", "false");
    config.setDefault("keyboard", "multiPressTimeout", "500");
    config.setDefault("keyboard", "speakCommandKeys", "false");
    config.setDefault("keyboard", "speakTypedCharacters", "editControls");
    config.setDefault("keyboard", "speakTypedWords", "off");
    config.setDefault("keyboard", "speechInterruptForCharacters", "true");
    config.setDefault("keyboard", "speechInterruptForEnter", "true");
    config.setDefault("keyboard", "allowSkimReadingInSayAll", "false");
    // The D-Bus name the compositor's access control keys on (R1). A default
    // that is not Orca's name means no keyboard at all on GNOME today, so the
    // default is the one that works and the reason is written down.
    config.setDefault("input", "appId", "org.gnome.Orca");
    config.setDefault("input", "backend", "auto");
    config.setDefault("input", "callTimeout", "2000");
}

KeyboardConfig Service::keyboardConfigFrom(const config::Manager& config) {
    KeyboardConfig keyboard;
    keyboard.layout =
        layoutFromName(config.getString("keyboard", "keyboardLayout", "desktop"))
            .value_or(Layout::Desktop);
    keyboard.alusKeyInsert = config.getBool("keyboard", "alusKeyInsert", true);
    keyboard.alusKeyNumpadInsert = config.getBool("keyboard", "alusKeyNumpadInsert", true);
    keyboard.alusKeyCapsLock = config.getBool("keyboard", "alusKeyCapsLock", false);
    keyboard.multiPressTimeout =
        std::chrono::milliseconds(config.getInt("keyboard", "multiPressTimeout", 500));
    keyboard.speakCommandKeys = config.getBool("keyboard", "speakCommandKeys", false);
    keyboard.speakTypedCharacters =
        typingEchoFromName(config.getString("keyboard", "speakTypedCharacters", "editControls"));
    keyboard.speakTypedWords =
        typingEchoFromName(config.getString("keyboard", "speakTypedWords", "off"));
    keyboard.speechInterruptForCharacters =
        config.getBool("keyboard", "speechInterruptForCharacters", true);
    keyboard.speechInterruptForEnter =
        config.getBool("keyboard", "speechInterruptForEnter", true);
    keyboard.allowSkimReadingInSayAll =
        config.getBool("keyboard", "allowSkimReadingInSayAll", false);
    return keyboard;
}

void Service::initialize() {
    if (running_) {
        return;
    }
    // `scripts.csv` is the marker: without the generated catalogue the input
    // layer has no commands, which is a different failure from speech's.
    dataDir_ = core::findDataDirectory(options_.dataDir, "scripts.csv");
    if (dataDir_.empty()) {
        ALUS_WARNING(
            "no ALUS data directory with scripts.csv; run tools/extract-commands.py — "
            "no keys will be bound");
    } else {
        catalog_.loadFromDataDirectory(dataDir_);
    }

    std::string keymapError;
    if (!keymap_.load({}, &keymapError)) {
        // Best effort by design: the keysym the compositor sends is then used
        // as the key's identity, which is right for every key except shifted
        // punctuation (see KeyMap).
        ALUS_WARNING("no keyboard layout: {}; shifted punctuation bindings may not match",
                     keymapError);
    }

    dispatcher_ = std::make_unique<script::Dispatcher>(&loop_, &catalog_);
    manager_ = std::make_unique<GestureManager>(loop_, *dispatcher_, hooks_);
    manager_->setKeyMap(&keymap_);
    manager_->setBuiltinMap(&catalog_.builtinGestures());

    InputCommands::Context commandContext;
    commandContext.manager = manager_.get();
    commandContext.config = &config_;
    commandContext.message = [this](const std::string& text) {
        if (hooks_.speakText) {
            hooks_.speakText(text, false);
        }
    };
    commandContext.quit = hooks_.quit;
    commands_ = std::make_unique<InputCommands>(std::move(commandContext), &catalog_);

    // Every v1.0 command in data/commands-status.ini's [announce] list that
    // the global-commands section assigns a key to. Its own object, and not a
    // few more bindings on `commands_`, for one reason: `commands_` is FIRST
    // in step 10's list, so an announcement bound there would answer before
    // the object model, the text layer, braille and the GUI ever got the
    // chance -- a real command would be replaced by "not in this version".
    missing_ = std::make_unique<script::ScriptableObject>(
        std::vector<std::string>{"globalCommands.GlobalCommands"}, &catalog_);
    const std::size_t announced = script::bindAnnouncements(
        *missing_, catalog_, [this](const std::string& text) {
            if (hooks_.speakText) {
                hooks_.speakText(text, false);
            }
        });
    if (announced > 0) {
        ALUS_INFO("{} command(s) are not in this version and say so when pressed",
                  announced);
    }

    // Step 10 of the lookup: the global commands, last resort. Phases 4 and 6
    // fill the earlier steps in through this same provider.
    dispatcher_->setContextProvider([this](script::DispatchContext& context) {
        context.globalCommands.push_back(commands_.get());
        for (const ContextContributor& contributor : contextContributors_) {
            if (contributor.function) {
                contributor.function(context);
            }
        }
        // LAST, always: an announcement must never answer a key that a real
        // command in one of the subsystems above would have answered.
        if (missing_ != nullptr) {
            context.globalCommands.push_back(missing_.get());
        }
    });
    dispatcher_->setKeyEmulator([this](std::string_view keyName, Gesture& /*gesture*/) {
        if (backend_ == nullptr || !backend_->canSendKeys()) {
            return false;
        }
        const std::optional<std::uint32_t> keysymValue = KeyNames::shared().keysym(keyName);
        if (!keysymValue.has_value()) {
            return false;
        }
        KeyDefinition key{*keysymValue, 0, "emulation"};
        std::string error;
        return backend_->sendKey(key, &error);
    });

    applyConfiguration();
    loadMaps();

    if (options_.noInput) {
        backend_ = std::make_unique<NullInputBackend>();
    } else {
        A11yManagerBackend::Options backendOptions;
        backendOptions.appId = options_.appId.empty()
                                   ? config_.getString("input", "appId", "org.gnome.Orca")
                                   : options_.appId;
        backendOptions.callTimeout =
            std::chrono::milliseconds(config_.getInt("input", "callTimeout", 2000));
        backend_ = std::make_unique<A11yManagerBackend>(std::move(backendOptions));
    }
    std::string backendError;
    if (!backend_->start(&backendError)) {
        // SAID, not swallowed. The whole point of N7 and N8 is that this
        // failure is invisible otherwise: ALUS would run, speak on demand and
        // never react to a key.
        ALUS_ERROR("input is NOT available: {}", backendError);
    }
    manager_->setBackend(backend_.get());

    running_ = true;
    ALUS_INFO("{}", describe());
}

void Service::applyConfiguration() {
    if (manager_ == nullptr) {
        return;
    }
    manager_->setConfig(keyboardConfigFrom(config_));
}

void Service::loadMaps() {
    if (manager_ == nullptr) {
        return;
    }
    // Layer 1: the user's own map. The same file NVDA writes, so an NVDA user
    // can copy it across unchanged (Z3).
    const std::filesystem::path userPath = options_.userGestureMap.empty()
                                               ? config_.paths().configDir / "gestures.ini"
                                               : options_.userGestureMap;
    manager_->userMap().clear();
    std::string error;
    if (std::filesystem::exists(userPath)) {
        if (!manager_->userMap().load(userPath, &error)) {
            ALUS_WARNING("user gesture map: {}", error);
        }
    } else {
        ALUS_DEBUG("no user gesture map at {}", userPath.string());
    }

    // Layer 2: the locale map, NVDA's own, copied by the extractor.
    manager_->localeMap().clear();
    if (!dataDir_.empty()) {
        const std::string language = currentLanguage();
        const std::filesystem::path candidates[] = {
            dataDir_ / "locale" / language / "gestures.ini",
            dataDir_ / "locale" / language.substr(0, language.find('_')) / "gestures.ini",
        };
        for (const std::filesystem::path& candidate : candidates) {
            if (!std::filesystem::exists(candidate)) {
                continue;
            }
            error.clear();
            if (manager_->localeMap().load(candidate, &error)) {
                ALUS_INFO("locale gesture map: {}", candidate.string());
            } else {
                ALUS_WARNING("locale gesture map {}: {}", candidate.string(), error);
            }
            break;
        }
    }

    // Layer 3 (the braille driver's map) arrives with phase 7; the built-in
    // layer is already set from the catalogue.
    manager_->refreshMaps();
}

Service::ContextContributorId Service::addContextContributor(
    std::function<void(script::DispatchContext&)> contributor) {
    const ContextContributorId id = ++nextContributorId_;
    contextContributors_.push_back(ContextContributor{id, std::move(contributor)});
    return id;
}

bool Service::removeContextContributor(ContextContributorId id) {
    const auto it = std::find_if(contextContributors_.begin(), contextContributors_.end(),
                                 [id](const ContextContributor& entry) {
                                     return entry.id == id;
                                 });
    if (it == contextContributors_.end()) {
        return false;
    }
    contextContributors_.erase(it);
    return true;
}

void Service::setContextContributor(std::function<void(script::DispatchContext&)> contributor) {
    contextContributors_.clear();
    if (contributor) {
        addContextContributor(std::move(contributor));
    }
}

void Service::attachWatchdog(core::Watchdog* watchdog) {
    watchdog_ = watchdog;
    if (watchdog_ == nullptr) {
        return;
    }
    // N19 measure 2: the grabs go BEFORE any recovery attempt. This handler
    // runs on the watchdog thread, so it may only touch what is safe there —
    // `emergencyRelease()` is written for exactly that.
    watchdog_->freezeDetected()
        .add([this]() {
            if (backend_ != nullptr) {
                backend_->emergencyRelease();
            }
        })
        .release();
}

BackendHealth Service::health() const {
    return backend_ == nullptr ? BackendHealth::Unknown : backend_->health();
}

std::string Service::describe() {
    std::string text = "input: ";
    if (backend_ == nullptr) {
        return text + "not initialized";
    }
    text += std::string(backend_->name()) + " backend, " +
            std::string(backendHealthName(backend_->health()));
    if (manager_ != nullptr) {
        text += ", layout " + std::string(layoutName(manager_->config().layout));
        text += ", " + std::to_string(manager_->computeGrabs().size()) + " grabs";
        text += ", ALUS modifier mask 0x" +
                [](std::uint32_t mask) {
                    char buffer[16];
                    std::snprintf(buffer, sizeof buffer, "%x", mask);
                    return std::string(buffer);
                }(manager_->alusModifierMask());
    }
    text += ", " + std::to_string(catalog_.scriptCount()) + " catalogued scripts";
    if (keymap_.loaded()) {
        text += ", keymap " + keymap_.layout();
    }
    return text;
}

void Service::terminate() {
    if (!running_) {
        return;
    }
    if (manager_ != nullptr) {
        manager_->setBackend(nullptr);
    }
    if (backend_ != nullptr) {
        backend_->stop();
    }
    backend_.reset();
    commands_.reset();
    manager_.reset();
    dispatcher_.reset();
    running_ = false;
    ALUS_INFO("input subsystem stopped");
}

}  // namespace alus::input
