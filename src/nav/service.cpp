// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/service.hpp.

#include "alus/nav/service.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/browse_mode.hpp"
#include "alus/nav/paragraph_nav.hpp"
#include "alus/nav/say_all.hpp"
#include "alus/nav/screen_explorer.hpp"
#include "alus/nav/text_behaviors.hpp"
#include "alus/nav/text_commands.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/ui/message.hpp"

namespace alus::nav {
namespace {

constexpr const char* kNavSection = "nav";

}  // namespace

Service::Service(core::Loop& loop, config::Manager& config, RegistryProvider registry,
                 input::Service* input, PresenterProvider presenter, Options options)
    : loop_(loop),
      config_(config),
      registryProvider_(std::move(registry)),
      input_(input),
      presenter_(std::move(presenter)),
      options_(std::move(options)) {}

Service::~Service() {
    terminate();
}

void Service::registerDefaults(config::Manager& config) {
    // Nothing of its own yet: every setting phase 5 reads belongs to a section
    // the object model already registers ([documentFormatting],
    // [reviewCursor], [documentNavigation], [editableText]). The section is
    // created so that the file has a place for phase 9's additions.
    config.setDefault(kNavSection, "enabled", "true");
}

core::TaskRegistry& Service::tasks() {
    return loop_.tasks();
}

speech::Presenter* Service::presenter() {
    return presenter_ ? presenter_() : nullptr;
}

a11y::Registry* Service::registry() {
    return registry_;
}

input::Service* Service::inputService() {
    return input_;
}

const script::Catalog* Service::catalog() const {
    return input_ != nullptr ? &input_->catalog() : nullptr;
}

const a11y::Settings& Service::settings() const {
    return settings_;
}

const text::FormatReportSettings& Service::formatSettings() const {
    return settings_.format;
}

void Service::initialize() {
    if (options_.noNav) {
        ALUS_INFO("text and review layer disabled (--no-nav)");
        return;
    }
    registry_ = registryProvider_ ? registryProvider_() : nullptr;
    if (registry_ == nullptr) {
        // Without an object model there is nothing to review. Not an error:
        // --no-a11y is a supported way to run.
        ALUS_INFO("text and review layer idle: no object model");
        return;
    }
    applyConfiguration();

    review_ = std::make_unique<Review>(*registry_, settings_);
    sayAll_ = std::make_unique<SayAll>(*this);
    explorer_ = std::make_unique<ScreenExplorer>(*this);
    review_->setMessageHook([this](const std::string& text) { message(text); });

    script::Dispatcher* dispatcher = nullptr;
    const script::Catalog* catalog = nullptr;
    if (input_ != nullptr) {
        dispatcher = input_->dispatcher();
        catalog = &input_->catalog();
    }
    commands_ = std::make_unique<TextCommands>(*this, catalog, dispatcher);
    if (input_ != nullptr) {
        // Step 10 of the lookup, as phase 4's O4 made it: a LIST of command
        // groups, each owning the commands of its own subsystem. Phase 5 adds
        // the third group, which is why the contributor became a list.
        contextContributor_ = input_->addContextContributor(
            [this](script::DispatchContext& context) {
                if (commands_ != nullptr) {
                    context.globalCommands.push_back(commands_.get());
                }
            });

        // Stopping say-all on the first key press, which is T5.8. It has to
        // happen BEFORE the script lookup, or a review command would resume
        // the say-all it was meant to interrupt; `keyEvent` is the extension
        // point phase 3 left for exactly this.
        if (input::GestureManager* manager = input_->manager()) {
            keyHandler_ = manager->keyEvent().add(
                [this](const input::KeyEvent& event) {
                    if (!event.pressed || sayAll_ == nullptr || !sayAll_->isRunning()) {
                        return;
                    }
                    sayAll_->stop();
                },
                "nav-stop-say-all");
        }
    }

    // Phase 6: the documents. The registry owns the interceptors and the
    // objects hold raw pointers to them (tree_interceptor.hpp says why), so it
    // must outlive every object it has attached one to -- which it does,
    // because terminate() clears it before the registry pointer is dropped.
    interceptors_ = std::make_unique<TreeInterceptorRegistry>(*this);
    interceptors_->addChooser(&BrowseMode::chooseForDocument);

    // Say-all's own focus changes are not announced (N128).
    registry_->presentation().isFocusAnnouncementSuppressed = [this](const a11y::Object& object) {
        return sayAll_ != nullptr && sayAll_->ownsFocusChangeTo(object);
    };

    installBehaviors();
    installEventHandlers();
    installInterceptorHandlers();
    running_ = true;
    ALUS_INFO("text and review layer started: {}", describe());
}

void Service::terminate() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (input_ != nullptr && contextContributor_ != 0) {
        input_->removeContextContributor(contextContributor_);
        contextContributor_ = 0;
    }
    keyHandler_ = {};
    if (registry_ != nullptr) {
        registry_->presentation().isFocusAnnouncementSuppressed = nullptr;
    }
    if (sayAll_ != nullptr) {
        sayAll_->stop();
    }
    commands_.reset();
    // Before the interceptors go, every object that holds a pointer to one
    // must forget it; `clear()` is what does that, and doing it here rather
    // than in the destructor is what keeps a SIGHUP from leaving an object
    // pointing at a freed interceptor.
    if (interceptors_ != nullptr) {
        interceptors_->clear();
        interceptors_.reset();
    }
    explorer_.reset();
    sayAll_.reset();
    review_.reset();
    speechStates_.clear();
    if (registry_ != nullptr) {
        registry_->setNavigatorChangedHook(nullptr);
    }
    overlayChooser_ = {};
    registry_ = nullptr;
}

std::string Service::describe() const {
    std::string result = "review mode: ";
    result += review_ != nullptr ? std::string(reviewModeName(review_->mode())) : "none";
    result += ", paragraphs: ";
    result += text::paragraphStyleName(settings_.paragraphStyle);
    result += ", say all unit: ";
    result += text::readingUnitName(settings_.sayAllReadingUnit);
    return result;
}

void Service::applyConfiguration() {
    settings_ = a11y::settingsFrom(config_);
    caretSettings_.timeout =
        std::chrono::milliseconds{settings_.caretMoveTimeoutMilliseconds};
    if (review_ != nullptr) {
        // A reconfigured ALUS must not keep reviewing by the old rules.
        review_->clear();
    }
    speechStates_.clear();
}

void Service::installBehaviors() {
    // Phase 4 built the overlay chooser; this is its first user from outside
    // the object model (§6.2's "composition").
    overlayChooser_ = registry_->overlays().add(
        a11y::OverlayRegistry::Stage::Api,
        [this](a11y::Object& object, a11y::BehaviorList& list) {
            // The platform role, not the behaviour-filtered one: the chooser
            // must not ask the behaviours it is choosing (§6.2).
            const a11y::Role role = object.platformRole();
            const a11y::StateSet& states = object.states();
            const bool editable = states.has(a11y::State::Editable) &&
                                  !states.has(a11y::State::ReadOnly);

            if (role == a11y::Role::Terminal) {
                // A terminal is a live text AND an editable text: NVDA's
                // `Terminal` inherits from both.
                list.push_back(a11y::BehaviorEntry{
                    "NVDAObjects.behaviors.Terminal",
                    [this] { return std::make_unique<a11y::behaviors::Terminal>(*this); }});
                list.push_back(a11y::BehaviorEntry{
                    "editableText.EditableText",
                    [this] { return std::make_unique<EditableTextBehavior>(*this); }});
                return;
            }
            if (role == a11y::Role::EditableText || role == a11y::Role::Document ||
                editable) {
                list.push_back(a11y::BehaviorEntry{
                    "editableText.EditableText",
                    [this] { return std::make_unique<EditableTextBehavior>(*this); }});
            }
            // An ARIA live region, or anything the author marked as live.
            const a11y::Attributes& attributes = object.attributes();
            const auto live = attributes.find("live");
            const auto container = attributes.find("container-live");
            const bool isLive = (live != attributes.end() && live->second != "off") ||
                                (container != attributes.end() && container->second != "off");
            if (isLive) {
                list.push_back(a11y::BehaviorEntry{
                    "NVDAObjects.behaviors.LiveText",
                    [this] { return std::make_unique<a11y::behaviors::LiveText>(*this); }});
            }
        },
        "nav");
}

void Service::installEventHandlers() {
    // The review position is rebuilt from the navigator object, so the review
    // cursor has to know when that object changes (phase 4's handover point 3).
    registry_->setNavigatorChangedHook(
        [this](const a11y::ObjectPtr& object, bool isFocus) {
            if (review_ != nullptr) {
                review_->onNavigatorObjectChanged(object, isFocus);
            }
        });
}

void Service::installInterceptorHandlers() {
    if (registry_ == nullptr || interceptors_ == nullptr) {
        return;
    }
    // Level 3 of the chain. NVDA runs a tree interceptor's handlers here, and
    // the handler MUST call `next()`: an interceptor that swallowed gainFocus
    // would stop the object layer from announcing anything at all, which is
    // the worst failure this project has (see the daemon's own comment).
    registry_->dispatcher().setChainProvider(
        core::EventLevel::TreeInterceptor,
        [this](const core::Event& event) -> std::vector<core::EventDispatcher::ChainHandler> {
            std::vector<core::EventDispatcher::ChainHandler> handlers;
            if (interceptors_ == nullptr || registry_ == nullptr) {
                return handlers;
            }
            handlers.push_back([this](const core::Event& one,
                                      const core::EventDispatcher::NextHandler& next) {
                handleInterceptorEvent(one);
                next();
            });
            (void)event;
            return handlers;
        });
}

void Service::handleInterceptorEvent(const core::Event& event) {
    if (interceptors_ == nullptr || registry_ == nullptr) {
        return;
    }
    // Objects die constantly in a browser; sweeping here rather than on a
    // timer means an interceptor for a closed tab is collected the next time
    // anything happens, with no timer and no leak.
    interceptors_->sweep();

    const a11y::ObjectPtr object =
        event.object != 0 ? registry_->objectFor(static_cast<a11y::Ref>(event.object))
                          : nullptr;
    if (object == nullptr) {
        return;
    }
    if (event.name == "gainFocus") {
        TreeInterceptor* before = interceptors_->current();
        // The focus's ancestry is already known (Api reuses it across keys);
        // walking it costs nothing, where asking each ancestor for its parent
        // again was a round trip per ancestor per key (N129).
        const a11y::ObjectPtr& focus = registry_->api().focus();
        const bool isFocus = focus != nullptr && focus->sameAs(*object);
        TreeInterceptor* after =
            interceptors_->update(object, isFocus ? &registry_->api().focusAncestors() : nullptr);
        if (after == nullptr) {
            // The focus left every document: nothing intercepts the keys now,
            // so the letters go back to the application.
            if (before != nullptr) {
                interceptors_->setCurrent(nullptr);
                refreshGrabs();
                return;
            }
            interceptors_->setCurrent(nullptr);
            return;
        }
        if (after != before) {
            // The grab set was computed before this event (see
            // Service::refreshGrabs): the document's scripts did not exist
            // yet. They do now.
            refreshGrabs();
            after->onGainFocus(object);
        } else {
            after->onInnerFocus(object);
        }
        return;
    }
    if (event.name == "caret") {
        if (TreeInterceptor* current = interceptors_->current()) {
            current->onCaretMove(object);
        }
        return;
    }
    if (event.name == "stateChange" || event.name == "documentLoadComplete") {
        // A document that has finished loading becomes ready, and the object
        // has to be told so that `shouldReportTextContent` and the review
        // cursor start believing in it.
        if (TreeInterceptor* current = interceptors_->current()) {
            const bool ready = current->isReady();
            object->setTreeInterceptor(current, ready);
            // N147: the FOCUS is what the key lookup asks, and the grab set
            // was computed while the document was still busy -- without
            // browse mode. On YouTube the focus was the player, the page went
            // on loading, and for twenty seconds every arrow key reached the
            // player (volume down 5% a press) while browse mode also read the
            // line. When the focus becomes ready, the grabs follow it.
            const a11y::ObjectPtr& focus = registry_->api().focus();
            if (focus != nullptr && focus->treeInterceptor() == current &&
                focus->treeInterceptorReady() != ready) {
                focus->setTreeInterceptor(current, ready);
                refreshGrabs();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// What the commands and behaviours use
// ---------------------------------------------------------------------------

a11y::TextRangeSpeechState& Service::speechStateFor(const a11y::Object& object) {
    return speechStates_[object.ref()];
}

void Service::forgetSpeechState(const a11y::Object& object) {
    speechStates_.erase(object.ref());
}

bool Service::speakRange(const text::TextRange& range, std::optional<text::TextUnit> unit,
                         a11y::OutputReason reason, const a11y::ObjectPtr& object,
                         bool suppressBlanks) {
    speech::Presenter* presenter = this->presenter();
    if (presenter == nullptr) {
        return false;
    }
    a11y::TextRangeSpeechOptions options;
    options.unit = unit;
    options.reason = reason;
    options.suppressBlanks = suppressBlanks;
    a11y::TextRangeSpeechState* state =
        object != nullptr ? &speechStateFor(*object) : nullptr;
    return a11y::speakTextRange(*presenter, range, state, formatSettings(), options);
}

void Service::message(const std::string& text) {
    ui::message(text);
}

void Service::refreshGrabs() {
    if (input_ == nullptr) {
        return;
    }
    if (input::GestureManager* manager = input_->manager()) {
        // `pushGrabs` compares the computed set against the installed one and
        // says nothing to the compositor when they match, so calling this on
        // every document event costs nothing when nothing moved.
        manager->refreshGrabs();
    }
}

void Service::sayOrSpell(const std::string& text, int repeatCount,
                         const std::string& locale) {
    speech::Presenter* presenter = this->presenter();
    if (presenter == nullptr) {
        message(text);
        return;
    }
    if (repeatCount == 0) {
        presenter->speakMessage(text);
        return;
    }
    presenter->speakSpelling(text, locale, /*useCharacterDescriptions=*/repeatCount > 1);
}

void Service::cycleParagraphStyle() {
    const text::ParagraphStyle next = nextParagraphStyle(settings_.paragraphStyle);
    settings_.paragraphStyle = next;
    config_.setString("documentNavigation", "paragraphStyle",
                      std::string(text::paragraphStyleName(next)));
    message(std::string(paragraphStyleLabel(next)));
}

bool Service::toggleScreenLayout() {
    const bool on = !settings_.format.useScreenLayout;
    settings_.format.useScreenLayout = on;
    config_.setString("virtualBuffers", "useScreenLayout", on ? "true" : "false");
    std::string error;
    if (!config_.save(&error)) {
        ALUS_WARNING("screen layout: could not save the configuration: {}", error);
    }
    return on;
}

void Service::cycleLineIndentationReporting() {
    using LineIndentation = text::FormatReportSettings::LineIndentation;
    LineIndentation next = LineIndentation::Off;
    const char* label = "Report line indentation off";
    switch (settings_.format.reportLineIndentation) {
        case LineIndentation::Off:
            next = LineIndentation::Speech;
            label = "Report line indentation with speech";
            break;
        case LineIndentation::Speech:
            next = LineIndentation::Tones;
            label = "Report line indentation with tones";
            break;
        case LineIndentation::Tones:
            next = LineIndentation::SpeechAndTones;
            label = "Report line indentation with speech and tones";
            break;
        case LineIndentation::SpeechAndTones:
            next = LineIndentation::Off;
            label = "Report line indentation off";
            break;
    }
    settings_.format.reportLineIndentation = next;
    const char* value = next == LineIndentation::Off           ? "off"
                        : next == LineIndentation::Speech      ? "speech"
                        : next == LineIndentation::Tones       ? "tones"
                                                               : "speechAndTones";
    config_.setString("documentFormatting", "reportLineIndentation", value);
    message(label);
}

}  // namespace alus::nav
