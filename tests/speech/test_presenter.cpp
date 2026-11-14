// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T2.1 — ui::message("zdravo") is spoken.
//
// And the rest of the pipeline from architecture.md §4.2, whose ORDER is the
// contract: the filter chain, the speech modes, the language pass, the text
// pass, and the two-space chunk separator that exists because one space would
// merge adjacent numbers in French and German.
#include "alus/speech/presenter.hpp"

#include "../support/loop_fixture.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/synth.hpp"
#include "alus/text/symbols.hpp"
#include "alus/ui/message.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using alus::speech::as;
using alus::speech::CommandType;
using alus::speech::Dictionaries;
using alus::speech::DictEntry;
using alus::speech::DictionaryType;
using alus::speech::EntryType;
using alus::speech::LangChangeCommand;
using alus::speech::NullSynth;
using alus::speech::Presenter;
using alus::speech::Priority;
using alus::speech::SpeechManager;
using alus::speech::SpeechMode;
using alus::speech::SpeechSequence;
using alus::testing::LoopFixture;
using alus::text::SymbolLevel;
using alus::text::SymbolRegistry;
using namespace std::chrono_literals;

namespace {

class PresenterFixture : public LoopFixture {
public:
    PresenterFixture()
        : symbols_(std::filesystem::path{ALUS_TEST_DATA_DIR} / "symbols"),
          manager_(loop()),
          presenter_(manager_, symbols_, dictionaries_) {
        manager_.setSynth(&synth_);
        manager_.setLostIndexGrace(0ms);
        manager_.setStallTimeout(0ms);
        Presenter::Settings settings;
        settings.language = "en";
        settings.symbolLevel = SymbolLevel::Some;
        presenter_.setSettings(settings);
    }

    NullSynth& synth() noexcept { return synth_; }
    Presenter& presenter() noexcept { return presenter_; }
    SpeechManager& manager() noexcept { return manager_; }
    Dictionaries& dictionaries() noexcept { return dictionaries_; }

    // What the synthesizer was given, with the commands left out.
    std::vector<std::string> spoken() {
        drain();
        return synth_.spokenText();
    }
    SpeechSequence lastUtterance() {
        drain();
        REQUIRE_FALSE(synth_.utterances().empty());
        return synth_.utterances().back();
    }

private:
    SymbolRegistry symbols_;
    Dictionaries dictionaries_;
    NullSynth synth_{true};
    SpeechManager manager_;
    Presenter presenter_;
};

}  // namespace

TEST_CASE("T2.1 a ui message is spoken", "[speech][presenter][ui]") {
    PresenterFixture fixture;
    alus::ui::initialize(&fixture.presenter());

    int notifications = 0;
    const auto handle = alus::ui::messageShown().add(
        [&](const std::string& text, Priority) {
            ++notifications;
            CHECK(text == "zdravo");
        },
        "test");

    alus::ui::message("zdravo");
    const std::vector<std::string> spoken = fixture.spoken();
    REQUIRE(spoken.size() == 1);
    CHECK(spoken.front().starts_with("zdravo"));
    // Braille (phase 7) and the speech viewer (phase 9) hang off this.
    CHECK(notifications == 1);
    alus::ui::terminate();
}

TEST_CASE("an empty message is announced as blank", "[speech][presenter]") {
    PresenterFixture fixture;
    // Silence is indistinguishable from a broken screen reader, so an empty
    // field says so.
    fixture.presenter().speakMessage("");
    CHECK(fixture.spoken().front().starts_with("blank"));
    fixture.synth().clear();
    fixture.presenter().speakMessage("   \n  ");
    CHECK(fixture.spoken().front().starts_with("blank"));
}

TEST_CASE("the chunk separator is two spaces", "[speech][presenter]") {
    PresenterFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"1"});
    sequence.emplace_back(std::string{"200"});
    fixture.presenter().speak(std::move(sequence));
    // One space would let French and German read "1 200" as one number
    // (NVDA issue #555).
    CHECK(fixture.spoken().front() == "1  200  ");
}

TEST_CASE("character mode gets no chunk separator", "[speech][presenter]") {
    PresenterFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(alus::speech::characterMode(true));
    sequence.emplace_back(std::string{"a"});
    sequence.emplace_back(std::string{"b"});
    fixture.presenter().speak(std::move(sequence));
    // Spelling must not insert pauses between the letters.
    CHECK(fixture.spoken().front() == "ab");
}

TEST_CASE("the symbol level is applied, and can be overridden per call",
          "[speech][presenter]") {
    PresenterFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"50%"});
    fixture.presenter().speak(sequence, SymbolLevel::None);
    CHECK(fixture.spoken().front().find("percent") == std::string::npos);

    fixture.synth().clear();
    fixture.presenter().speak(sequence, SymbolLevel::All);
    CHECK(fixture.spoken().front().find("percent") != std::string::npos);

    // Unchanged means "whatever the user configured".
    fixture.synth().clear();
    fixture.presenter().speak(sequence, SymbolLevel::Unchanged);
    CHECK(fixture.spoken().front().find("percent") != std::string::npos);
}

TEST_CASE("the dictionaries run before the symbols", "[speech][presenter]") {
    PresenterFixture fixture;
    // The entry introduces a symbol, which the symbol pass then speaks: the
    // order matters and is user visible.
    fixture.dictionaries()
        .dictionary(DictionaryType::Default)
        .add(DictEntry{"percent", "%", {}, true, EntryType::Word});
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"fifty percent"});
    fixture.presenter().speak(std::move(sequence), SymbolLevel::Some);
    CHECK(fixture.spoken().front().find("percent") != std::string::npos);
}

TEST_CASE("the language pass regenerates the language changes", "[speech][presenter]") {
    PresenterFixture fixture;
    Presenter::Settings settings = fixture.presenter().settings();
    settings.language = "en";
    settings.autoLanguageSwitching = true;
    fixture.presenter().setSettings(settings);

    SpeechSequence sequence;
    sequence.emplace_back(alus::speech::langChange("sr"));
    sequence.emplace_back(std::string{"здраво"});
    sequence.emplace_back(alus::speech::langChange("sr"));  // redundant
    sequence.emplace_back(std::string{"свете"});
    sequence.emplace_back(alus::speech::langChange("en"));
    sequence.emplace_back(std::string{"hello"});
    fixture.presenter().speak(std::move(sequence));

    const SpeechSequence utterance = fixture.lastUtterance();
    std::vector<std::string> languages;
    for (const auto& item : utterance) {
        if (const auto command = as<LangChangeCommand>(item)) {
            languages.push_back(command->language());
        }
    }
    // One command per actual change, not one per command the caller wrote.
    CHECK(languages == std::vector<std::string>{"sr", "en"});
}

TEST_CASE("language switching off drops the language changes", "[speech][presenter]") {
    PresenterFixture fixture;
    Presenter::Settings settings = fixture.presenter().settings();
    settings.autoLanguageSwitching = false;
    fixture.presenter().setSettings(settings);

    SpeechSequence sequence;
    sequence.emplace_back(alus::speech::langChange("sr"));
    sequence.emplace_back(std::string{"text"});
    fixture.presenter().speak(std::move(sequence));

    for (const auto& item : fixture.lastUtterance()) {
        CHECK_FALSE(item.is(CommandType::LangChange));
    }
}

TEST_CASE("a dialect is collapsed unless dialect switching is on", "[speech][presenter]") {
    PresenterFixture fixture;
    Presenter::Settings settings = fixture.presenter().settings();
    settings.language = "en";
    settings.autoDialectSwitching = false;
    fixture.presenter().setSettings(settings);

    SpeechSequence sequence;
    sequence.emplace_back(alus::speech::langChange("en_GB"));
    sequence.emplace_back(std::string{"colour"});
    fixture.presenter().speak(sequence);
    {
        const SpeechSequence utterance = fixture.lastUtterance();
        const auto command = as<LangChangeCommand>(utterance.front());
        REQUIRE(command != nullptr);
        // Same language, different dialect: the configured voice is kept.
        CHECK(command->language() == "en");
    }

    settings.autoDialectSwitching = true;
    fixture.presenter().setSettings(settings);
    fixture.synth().clear();
    fixture.presenter().speak(sequence);
    {
        const SpeechSequence utterance = fixture.lastUtterance();
        const auto command = as<LangChangeCommand>(utterance.front());
        REQUIRE(command != nullptr);
        CHECK(command->language() == "en_GB");
    }
}

TEST_CASE("the speech modes behave as documented", "[speech][presenter]") {
    PresenterFixture fixture;

    fixture.presenter().setMode(SpeechMode::Off);
    fixture.presenter().speakMessage("nothing");
    CHECK(fixture.spoken().empty());

    // Beeps: a diagnostic mode. Nothing reaches the synthesizer.
    fixture.presenter().setMode(SpeechMode::Beeps);
    fixture.presenter().speakMessage("still nothing");
    CHECK(fixture.spoken().empty());

    // On demand: silent unless the caller is one of the things the user asked
    // for (phase 3 supplies the predicate).
    fixture.presenter().setMode(SpeechMode::OnDemand);
    fixture.presenter().speakMessage("not on demand");
    CHECK(fixture.spoken().empty());

    bool allowed = true;
    fixture.presenter().setOnDemandPredicate([&] { return allowed; });
    fixture.presenter().speakMessage("on demand");
    CHECK(fixture.spoken().size() == 1);
    allowed = false;
    fixture.presenter().speakMessage("suppressed");
    CHECK(fixture.spoken().size() == 1);

    fixture.presenter().setMode(SpeechMode::Talk);
    fixture.presenter().speakMessage("talking");
    CHECK(fixture.spoken().size() == 2);
}

TEST_CASE("the filter chain can rewrite or drop a sequence", "[speech][presenter]") {
    PresenterFixture fixture;
    const auto handle = fixture.presenter().filterSpeechSequence().add(
        [](SpeechSequence sequence) {
            for (auto& item : sequence) {
                if (item.isText() && item.text() == "secret") {
                    item.text() = "redacted";
                }
            }
            return sequence;
        },
        "test-filter");

    fixture.presenter().speakMessage("secret");
    CHECK(fixture.spoken().front().starts_with("redacted"));

    const auto dropper = fixture.presenter().filterSpeechSequence().add(
        [](SpeechSequence) { return SpeechSequence{}; }, "dropper");
    fixture.synth().clear();
    fixture.presenter().speakMessage("anything");
    CHECK(fixture.spoken().empty());
}

TEST_CASE("pre-speech sees the sequence before the settings are applied",
          "[speech][presenter]") {
    PresenterFixture fixture;
    int notifications = 0;
    const auto handle = fixture.presenter().preSpeech().add(
        [&](const SpeechSequence& sequence, Priority priority) {
            ++notifications;
            CHECK(priority == Priority::Now);
            CHECK(sequence.front().text() == "raw text");
        },
        "test");
    fixture.presenter().speakMessage("raw text", Priority::Now);
    CHECK(notifications == 1);
}

TEST_CASE("cancelling speech is reported once", "[speech][presenter]") {
    PresenterFixture fixture;
    int cancels = 0;
    const auto handle = fixture.presenter().speechCanceled().add([&] { ++cancels; }, "test");

    fixture.presenter().speakMessage("something");
    fixture.drain();
    fixture.presenter().cancelSpeech();
    CHECK(cancels == 1);
    // A second cancel with nothing in flight does not fire again: NVDA's
    // beenCanceled flag, which keeps cancel from being a storm of events.
    fixture.presenter().cancelSpeech();
    CHECK(cancels == 1);

    fixture.presenter().speakMessage("again");
    fixture.drain();
    fixture.presenter().cancelSpeech();
    CHECK(cancels == 2);
}

TEST_CASE("speaking while paused resumes by cancelling", "[speech][presenter]") {
    PresenterFixture fixture;
    fixture.presenter().speakMessage("first");
    fixture.drain();
    fixture.presenter().pauseSpeech(true);
    CHECK(fixture.presenter().isPaused());
    CHECK(fixture.synth().isPaused());

    fixture.presenter().speakMessage("second");
    fixture.drain();
    // The paused utterance was stale by definition; the new one is spoken.
    CHECK_FALSE(fixture.presenter().isPaused());
    // ... and the SYNTHESIZER is no longer paused either, or it would hold
    // the new utterance behind the pause (N123: speech-dispatcher keeps a
    // pause across spd_cancel, and so does NullSynth).
    CHECK_FALSE(fixture.synth().isPaused());
    CHECK(fixture.spoken().back().starts_with("second"));
}

TEST_CASE("Unicode normalisation is NFC", "[speech][presenter]") {
    // Decomposed "é" is one character after normalisation, which is what the
    // review layer will need for spelling (phase 5, T5.4).
    const std::string decomposed = "é";
    const std::string composed = "é";
    CHECK(Presenter::unicodeNormalize(decomposed) == composed);
    CHECK(Presenter::unicodeNormalize(composed) == composed);
}

TEST_CASE("whitespace inside a chunk is flattened", "[speech][presenter]") {
    PresenterFixture fixture;
    SpeechSequence sequence;
    // A newline would end the utterance at the synthesizer, which would undo
    // everything the sentence buffer does.
    sequence.emplace_back(std::string{"one\ntwo\rthree"});
    fixture.presenter().speak(std::move(sequence), SymbolLevel::None);
    const std::string spoken = fixture.spoken().front();
    CHECK(spoken.find('\n') == std::string::npos);
    CHECK(spoken.find('\r') == std::string::npos);
    CHECK(spoken.find("one two three") != std::string::npos);
}

TEST_CASE("shift then tab: the cancel ends the synthesizer's pause", "[speech][presenter]") {
    // phase-10.md N123, measured in the settings window: Shift pauses, Tab
    // cancels, and everything after it was silent.
    PresenterFixture fixture;
    fixture.presenter().speakMessage("Rate 81");
    fixture.drain();
    fixture.presenter().pauseSpeech(true);
    REQUIRE(fixture.synth().isPaused());
    fixture.presenter().cancelSpeech();
    CHECK_FALSE(fixture.synth().isPaused());
    fixture.presenter().speakMessage("Voice");
    fixture.drain();
    CHECK(fixture.spoken().back().starts_with("Voice"));
}
