// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 5 test fixture: the phase 4 one, plus a real speech pipeline and a
// real text-and-review layer.
//
// WHY A REAL PRESENTER AND NOT A RECORDING HOOK. Phase 4's tests could assert
// on what WOULD be said, because the object model only ever hands a sequence
// to the presentation layer. Phase 5 cannot: spelling, say-all and the caret
// all depend on what the SPEECH PIPELINE does with the sequence -- the index
// marks that drive say-all, the character mode that makes a letter a letter,
// the sentence buffer that makes say-all continuous. So the pipeline is real,
// with a NullSynth at the bottom that records and reports its index marks.
#pragma once

#include "alus/a11y/registry.hpp"
#include "alus/a11y/service.hpp"
#include "alus/config/config.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/say_all.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/speech/synth.hpp"
#include "alus/text/char_descriptions.hpp"
#include "alus/text/symbols.hpp"
#include "alus/core/queue.hpp"
#include "alus/ui/message.hpp"
#include "a11y/mock_backend.hpp"
#include "a11y/support.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace alus::testing {

class NavFixture : public A11yFixture {
public:
    NavFixture()
        : symbols_(std::filesystem::path{ALUS_TEST_DATA_DIR} / "symbols"),
          descriptions_(std::filesystem::path{ALUS_TEST_DATA_DIR}),
          speechManager_(loop()),
          presenter_(speechManager_, symbols_, dictionaries_),
          nav_(loop(), config_, [this] { return &registry; }, nullptr,
               [this] { return &presenter_; }) {
        speechManager_.setSynth(&synth_);
        speechManager_.setLostIndexGrace(std::chrono::milliseconds{0});
        speechManager_.setStallTimeout(std::chrono::milliseconds{0});
        speech::Presenter::Settings settings;
        settings.language = "en";
        settings.symbolLevel = text::SymbolLevel::Some;
        presenter_.setSettings(settings);
        presenter_.setCharacterDescriptions(&descriptions_);
        ui::initialize(&presenter_);

        a11y::Service::registerDefaults(config_);
        nav::Service::registerDefaults(config_);
        // The real presentation hooks, so an object announced during a review
        // command is announced the way the daemon would do it -- and still
        // recorded by the phase 4 fixture.
        a11y::installSpeechPresentation(registry, presenter_);
        recordMessages();
        nav_.initialize();
    }

    ~NavFixture() {
        nav_.terminate();
        ui::initialize(nullptr);
    }

    nav::Service& nav() noexcept { return nav_; }
    nav::Review& review() { return *nav_.review(); }
    nav::SayAll& sayAll() { return *nav_.sayAll(); }
    speech::Presenter& presenter() noexcept { return presenter_; }
    speech::SpeechManager& speechManager() noexcept { return speechManager_; }
    speech::NullSynth& synth() noexcept { return synth_; }
    config::Manager& config() noexcept { return config_; }

    // Runs the loop until the speech queue is empty, so that the index marks
    // say-all depends on have all been delivered. With autoComplete on, the
    // NullSynth reports each index as soon as it is given the utterance, so
    // this is just "run until nothing is left".
    void drainSpeech(int maxCycles = 2000) {
        for (int cycle = 0; cycle < maxCycles; ++cycle) {
            loop().iterateUntilIdle();
            if (!speechManager_.isSpeaking() && !speechManager_.hasPendingSpeech() &&
                loop().queue().empty()) {
                return;
            }
        }
    }

    // What the synthesizer was actually given, with the commands dropped.
    std::vector<std::string> spokenText() { return synth_.spokenText(); }
    std::string spokenJoined() {
        std::string result;
        for (const std::string& piece : synth_.spokenText()) {
            if (!result.empty()) {
                result += " | ";
            }
            result += piece;
        }
        return result;
    }

    void clearSpeech() { synth_.clear(); }

private:
    void recordMessages() {
        // ui::message goes through the presenter now, so the phase 4 fixture's
        // `messages` list is fed from the extension point instead of a hook.
        messageHandle_ = ui::messageShown().add(
            [this](const std::string& text, speech::Priority) { messages.push_back(text); },
            "nav-test");
    }

    // The log goes nowhere: a test that prints the daemon's start-up lines
    // buries the assertion that failed.
    QuietLog quiet_;
    config::Manager config_;
    text::SymbolRegistry symbols_;
    text::CharacterDescriptionRegistry descriptions_;
    speech::Dictionaries dictionaries_;
    speech::NullSynth synth_{true};
    speech::SpeechManager speechManager_;
    speech::Presenter presenter_;
    nav::Service nav_;
    core::HandlerHandle messageHandle_;
};

// A window with one multi-line text document in it: what the caret and review
// tests need, and the shape of a GNOME Text Editor window.
struct DocumentTree {
    DocumentTree(MockBackend& backend, std::string text, const char* toolkit = "gtk") {
        application = backend.addApplication("Text Editor", toolkit);
        window = backend.add(application, PlatformRole::Frame, "Untitled Document");
        backend.addState(window, PlatformState::Active);
        document = backend.add(window, PlatformRole::Text, {});
        MockNode& node = backend.node(document);
        node.text = std::move(text);
        node.caret = 0;
        node.interfaces |= a11y::Interface::Text;
        node.interfaces |= a11y::Interface::EditableText;
        backend.addState(document, PlatformState::Editable);
        backend.addState(document, PlatformState::MultiLine);
        backend.addState(document, PlatformState::Focusable);
    }

    a11y::Ref application = 0;
    a11y::Ref window = 0;
    a11y::Ref document = 0;
};

}  // namespace alus::testing
