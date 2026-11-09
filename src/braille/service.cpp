// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/service.hpp.

#include "alus/braille/service.hpp"

#include "alus/a11y/api.hpp"
#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_speech.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/service.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/ui/message.hpp"

#include <utility>

namespace alus::braille {
namespace {

// What phase 3's third global layer -- the braille DRIVER map -- is filled
// with. brltty's own command names on the left, ALUS's scripts on the right.
// It is DATA for the same reason phase 3's command table is: a user who wants
// another key writes it in their own `gestures.ini`, and this is the default
// they are overriding.
struct DriverBinding {
    const char* gesture;
    const char* script;
};

constexpr DriverBinding kBrlapiBindings[] = {
    {"br(brlapi):fwinlt", "braille_scrollBack"},
    {"br(brlapi):fwinrt", "braille_scrollForward"},
    {"br(brlapi):lnup", "braille_previousLine"},
    {"br(brlapi):lndn", "braille_nextLine"},
    {"br(brlapi):route", "braille_routeTo"},
    {"br(brlapi):multiRoute", "braille_selectRange"},
    {"br(brlapi):describe", "braille_reportFormatting"},
    {"br(brlapi):home", "braille_toFocus"},
    {"br(brlapi):learn", "toggleInputHelp"},
    {"br(brlapi):prefmenu", "activateBrailleSettingsDialog"},
    {"br(brlapi):prefload", "revertConfiguration"},
    {"br(brlapi):prefsave", "saveConfiguration"},
    {"br(brlapi):time", "dateTime"},
    {"br(brlapi):say_line", "review_currentLine"},
    {"br(brlapi):say_below", "review_sayAll"},
    {"br(brlapi):top", "review_top"},
    {"br(brlapi):bot", "review_bottom"},
    {"br(brlapi):chrlt", "review_previousCharacter"},
    {"br(brlapi):chrrt", "review_nextCharacter"},
    {"br(brlapi):freeze", "braille_toggleTether"},
};

constexpr const char* kGlobalCommands = "globalCommands.GlobalCommands";

}  // namespace

Service::Service(core::Loop& loop, config::Manager& config, RegistryProvider registry,
                 NavProvider nav, input::Service* input, PresenterProvider presenter,
                 Options options)
    : loop_(loop),
      config_(config),
      registryProvider_(std::move(registry)),
      navProvider_(std::move(nav)),
      inputService_(input),
      presenterProvider_(std::move(presenter)),
      options_(options) {}

Service::~Service() { terminate(); }

void Service::registerDefaults(config::Manager& config) { registerSettingDefaults(config); }

a11y::Registry* Service::registry() const {
    return registryProvider_ ? registryProvider_() : nullptr;
}

nav::Service* Service::nav() const { return navProvider_ ? navProvider_() : nullptr; }

speech::Presenter* Service::presenter() const {
    return presenterProvider_ ? presenterProvider_() : nullptr;
}

void Service::applyConfiguration() {
    settings_ = settingsFrom(config_);
}

void Service::initialize() {
    if (options_.noBraille) {
        ALUS_INFO("braille disabled (--no-braille)");
        return;
    }
    applyConfiguration();

    louis_ = &Louis::shared();
    louis_->initialize();

    HandlerHooks hooks;
    hooks.registry = [this] { return registry(); };
    hooks.nav = [this] { return nav(); };
    hooks.message = [](const std::string& text) { ui::message(text); };
    hooks.spellRange = [this](const text::TextRange& range) {
        speech::Presenter* presenter = this->presenter();
        nav::Service* navService = this->nav();
        if (presenter == nullptr || navService == nullptr) {
            return;
        }
        a11y::spellTextRange(*presenter, range, navService->formatSettings());
    };
    hooks.speakRange = [this](const text::TextRange& range, text::TextUnit unit) {
        nav::Service* navService = this->nav();
        if (navService == nullptr) {
            return;
        }
        navService->speakRange(range, unit, a11y::OutputReason::Caret);
    };
    hooks.cancelSpeech = [this] {
        if (speech::Presenter* presenter = this->presenter()) {
            presenter->cancelSpeech();
        }
    };

    handler_ = std::make_unique<Handler>(loop_, *louis_, std::move(hooks));

    BrailleInput::Hooks inputHooks;
    inputHooks.message = [](const std::string& text) { ui::message(text); };
    inputHooks.speakDots = [](const std::string& text) { ui::message(text); };
    inputHooks.isTypingProtected = [this] {
        a11y::Registry* reg = registry();
        return reg != nullptr && reg->api().isTypingProtected();
    };
    inputHooks.isFocusTextObject = [this] {
        a11y::Registry* reg = registry();
        return reg != nullptr && reg->api().isFocusEditable();
    };
    inputHooks.updateDisplay = [this] {
        if (handler_ != nullptr) {
            handler_->updateForUntranslatedInput();
        }
    };
    // N18 again: there is no keystroke injection on Wayland, so both of these
    // report failure and `BrailleInput` says so out loud once. They are wired
    // anyway, because `Gesture::send()` is where the v1.1 uinput helper will
    // land and nothing above here will have to change.
    inputHooks.typeText = [](const std::string&) { return false; };
    inputHooks.emulateKey = [](const std::string& /*name*/) {
        // NVDA emulates the key through `inputCore.manager.emulateGesture`,
        // which ends in a real keystroke. ALUS has no such ending on Wayland:
        // executing the gesture would only run ALUS's OWN scripts, so a
        // braille chord meant for the application would be silently
        // swallowed. Answering false makes `BrailleInput` say that it cannot
        // type, which is the one thing phase 0 insists on (N1, N7, N15, N18).
        return false;
    };
    input_ = std::make_unique<BrailleInput>(*louis_, std::move(inputHooks));
    input_->applySettings(settings_);
    handler_->setInput(input_.get());

    std::string displayError;
    DisplayPtr display = makeDisplay(settings_.display, settings_.brlapiHost, &displayError);
    if (!displayError.empty()) {
        // §3.8 and the lesson of phase 0: an expected failure is REPORTED, not
        // swallowed. A braille user whose display did not come up must be told
        // in the log, and the rest of ALUS must carry on.
        ALUS_INFO("no braille display: {}", displayError);
    }
    display->setBrailleInputEnabled(settings_.handleBrailleInput);
    handler_->start(std::move(display), settings_);

    const script::Catalog* catalog = nullptr;
    if (inputService_ != nullptr) {
        catalog = &inputService_->catalog();
    }
    commands_ = std::make_unique<Commands>(*this, catalog);
    if (inputService_ != nullptr) {
        // Step 10 of the lookup, the FOURTH command group: the global
        // commands, the object commands, the text commands and now braille.
        contextContributor_ = inputService_->addContextContributor(
            [this](script::DispatchContext& context) {
                if (commands_ != nullptr) {
                    context.globalCommands.push_back(commands_.get());
                }
            });
        installDisplayGestures();
    }

    installEventHandlers();

    // The display's own keys are drained in the MOUSE stage, which runs before
    // the braille stage: a routing key read here is acted on, and the frame
    // the braille stage then writes already shows the result.
    keyPumpHandler_ = loop_.addStageHandler(core::PumpStage::Mouse, "braille-keys",
                                           [this] { pumpDisplayKeys(); });

    running_ = true;
    ALUS_INFO("braille: {}", describe());
    handler_->initialDisplay();
}

void Service::terminate() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (keyPumpHandler_ != 0) {
        loop_.removeStageHandler(keyPumpHandler_);
        keyPumpHandler_ = 0;
    }
    if (inputService_ != nullptr && contextContributor_ != 0) {
        inputService_->removeContextContributor(contextContributor_);
        contextContributor_ = 0;
        if (input::GestureManager* manager = inputService_->manager()) {
            manager->driverMap().clear();
            manager->refreshMaps();
        }
    }
    eventHandle_ = {};
    messageHandle_ = {};
    if (nav::Service* navService = nav(); navService != nullptr) {
        if (nav::Review* review = navService->review()) {
            review->setPositionChangedHook(nullptr);
        }
    }
    commands_.reset();
    if (handler_ != nullptr) {
        handler_->setInput(nullptr);
        handler_->stop();
        handler_.reset();
    }
    input_.reset();
    if (louis_ != nullptr) {
        louis_->terminate();
        louis_ = nullptr;
    }
}

void Service::updateSettings(const Settings& settings) {
    settings_ = settings;
    if (input_ != nullptr) {
        input_->applySettings(settings_);
    }
    if (handler_ != nullptr) {
        handler_->applySettings(settings_);
        if (IDisplay* display = handler_->display()) {
            display->setBrailleInputEnabled(settings_.handleBrailleInput);
        }
    }
}

void Service::message(const std::string& text) {
    if (handler_ != nullptr) {
        handler_->message(text);
    }
}

void Service::installDisplayGestures() {
    input::GestureManager* manager = inputService_->manager();
    if (manager == nullptr) {
        return;
    }
    input::GestureMap& map = manager->driverMap();
    map.clear();
    for (const DriverBinding& binding : kBrlapiBindings) {
        map.add(binding.gesture, kGlobalCommands, binding.script);
    }
    // The braille keyboard's own three reserved chords and the generic dots
    // binding come from the GENERATED table (data/gestures-builtin.ini has
    // them already), so they are not repeated here.
    manager->refreshMaps();
}

void Service::installEventHandlers() {
    a11y::Registry* reg = registry();
    if (reg != nullptr) {
        eventHandle_ = reg->eventDispatched().add(
            [this](const core::Event& event, const a11y::ObjectPtr& object) {
                onEvent(event, object);
            },
            "braille");
    }
    if (nav::Service* navService = nav(); navService != nullptr) {
        if (nav::Review* review = navService->review()) {
            review->setPositionChangedHook([this] {
                if (handler_ != nullptr) {
                    handler_->handleReviewMove();
                }
            });
        }
    }
    // NVDA: `ui.message` goes to speech AND braille. This is the subscription
    // ui/message.hpp said phase 7 would make.
    messageHandle_ = ui::messageShown().add(
        [this](const std::string& text, speech::Priority) {
            if (handler_ != nullptr) {
                if (settings_.mode == Mode::SpeechOutput) {
                    handler_->showSpeech(text);
                } else {
                    handler_->message(text);
                }
            }
        },
        "braille-message");
}

void Service::onEvent(const core::Event& event, const a11y::ObjectPtr& object) {
    if (handler_ == nullptr || object == nullptr) {
        return;
    }
    if (event.name == a11y::events::kGainFocus) {
        if (input_ != nullptr) {
            input_->handleGainFocus();
        }
        handler_->handleGainFocus(object);
        return;
    }
    if (event.name == a11y::events::kCaret) {
        if (input_ != nullptr) {
            input_->handleCaretMove();
        }
        handler_->handleCaretMove(object);
        return;
    }
    if (event.name == a11y::events::kNameChange || event.name == a11y::events::kValueChange ||
        event.name == a11y::events::kDescriptionChange ||
        event.name == a11y::events::kStateChange ||
        event.name == a11y::events::kSelection) {
        handler_->handleUpdate(object);
        return;
    }
    if (event.name == a11y::events::kTextChange) {
        handler_->handleUpdate(object);
    }
}

void Service::pumpDisplayKeys() {
    if (handler_ == nullptr || handler_->display() == nullptr || inputService_ == nullptr) {
        return;
    }
    input::GestureManager* manager = inputService_->manager();
    if (manager == nullptr) {
        return;
    }
    const std::string source{handler_->display()->name()};
    const std::string model = handler_->display()->model();
    for (const DisplayKey& key : handler_->display()->readKeys()) {
        if (key.isDots) {
            if (!settings_.handleBrailleInput) {
                // brltty is typing for the user; acting on the cells here as
                // well would double every character.
                continue;
            }
            manager->executeGesture(std::make_shared<InputGesture>(key.dots, key.space));
            continue;
        }
        const std::string id =
            DisplayGesture::idForCellCount(key.cellIndexes.size(), key.command);
        manager->executeGesture(
            std::make_shared<DisplayGesture>(source, model, id, key.cellIndexes));
    }
}

std::string Service::describe() const {
    if (handler_ == nullptr) {
        return "not running";
    }
    std::string out = handler_->describe();
    out += ", input table ";
    const std::vector<std::string> tables =
        input_ != nullptr ? input_->inputTables() : std::vector<std::string>{};
    out += tables.empty() ? "none" : tables.front();
    out += settings_.handleBrailleInput ? " (ALUS types)" : " (brltty types)";
    return out;
}

}  // namespace alus::braille
