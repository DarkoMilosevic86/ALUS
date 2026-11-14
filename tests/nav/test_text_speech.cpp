// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// How text is said out loud: the spelling path (which closes docs/history/phase-4.md
// O11), the format field reporting behind `alus+f`, the indentation report,
// and the selection change announcement.
#include "alus/a11y/text_speech.hpp"

#include "alus/a11y/text_range.hpp"
#include "alus/text/char_descriptions.hpp"

#include <filesystem>
#include "nav/support.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::testing::DocumentTree;
using alus::testing::NavFixture;
using alus::text::Position;
using alus::text::TextUnit;

namespace {

std::string textOf(const alus::speech::SpeechSequence& sequence) {
    std::string result;
    for (const alus::speech::SpeechItem& item : sequence) {
        if (!item.isText()) {
            continue;
        }
        if (!result.empty()) {
            result += "|";
        }
        result += item.text();
    }
    return result;
}

bool hasCommand(const alus::speech::SpeechSequence& sequence,
                alus::speech::CommandType type) {
    for (const alus::speech::SpeechItem& item : sequence) {
        if (item.is(type)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("the spelling path names punctuation and spells one character at a time",
          "[nav][speech]") {
    NavFixture fixture;
    const alus::speech::SpeechSequence sequence = fixture.presenter().getSpellingSpeech("a.b");

    // Each character is its own utterance, which is what makes spelling
    // interruptible letter by letter.
    CHECK(hasCommand(sequence, alus::speech::CommandType::EndUtterance));
    // Character mode, so "b" is a letter and not the word.
    CHECK(hasCommand(sequence, alus::speech::CommandType::CharacterMode));
    // And normalisation is suppressed around the whole sequence, because this
    // function already normalised what it meant to.
    CHECK(hasCommand(sequence, alus::speech::CommandType::SuppressUnicodeNormalization));
    // The full stop is NAMED rather than being silence.
    CHECK(textOf(sequence).find("dot") != std::string::npos);
}

TEST_CASE("a capital letter raises the pitch and puts it back", "[nav][speech]") {
    NavFixture fixture;
    alus::speech::Presenter::Settings settings = fixture.presenter().settings();
    settings.capPitchChange = 30;
    settings.sayCapForCapitals = false;
    fixture.presenter().setSettings(settings);

    const alus::speech::SpeechSequence sequence = fixture.presenter().getSpellingSpeech("A");
    int pitchCommands = 0;
    for (const alus::speech::SpeechItem& item : sequence) {
        if (item.is(alus::speech::CommandType::Pitch)) {
            ++pitchCommands;
        }
    }
    // Twice: the offset and the reset. Without the reset, every character
    // after the first capital stays high.
    CHECK(pitchCommands == 2);

    // A lower-case letter changes nothing.
    const alus::speech::SpeechSequence lower = fixture.presenter().getSpellingSpeech("a");
    CHECK_FALSE(hasCommand(lower, alus::speech::CommandType::Pitch));
}

TEST_CASE("say cap for capitals puts the word before the letter", "[nav][speech]") {
    NavFixture fixture;
    alus::speech::Presenter::Settings settings = fixture.presenter().settings();
    settings.sayCapForCapitals = true;
    fixture.presenter().setSettings(settings);
    CHECK(textOf(fixture.presenter().getSpellingSpeech("A")).find("cap") !=
          std::string::npos);
}

TEST_CASE("the third press describes each character", "[nav][speech]") {
    NavFixture fixture;
    // English: "b" is "Bravo" in NVDA's own data, which ALUS uses unchanged,
    // capital letter and all.
    const alus::speech::SpeechSequence described =
        fixture.presenter().getSpellingSpeech("b", "en", /*useCharacterDescriptions=*/true);
    CHECK(textOf(described).find("Bravo") != std::string::npos);

    // Serbian: the same letter is "Beograd" -- a user who learned the Serbian
    // table in NVDA must hear the same word here.
    const alus::speech::SpeechSequence serbian =
        fixture.presenter().getSpellingSpeech("b", "sr", /*useCharacterDescriptions=*/true);
    CHECK(textOf(serbian).find("Beograd") != std::string::npos);
}

TEST_CASE("a decomposed character spells as one character", "[nav][speech]") {
    NavFixture fixture;
    alus::speech::Presenter::Settings settings = fixture.presenter().settings();
    settings.unicodeNormalization = true;
    settings.reportNormalizedForCharacterNavigation = true;
    fixture.presenter().setSettings(settings);

    // "e" + combining acute: two codepoints, ONE character (T5.4).
    const alus::speech::SpeechSequence sequence =
        fixture.presenter().getSpellingSpeech("e\xCC\x81");
    const std::string spoken = textOf(sequence);
    // The composed form is spoken, and the user is told it was normalised.
    CHECK(spoken.find("\xC3\xA9") != std::string::npos);
    CHECK(spoken.find("normalized") != std::string::npos);
}

TEST_CASE("the delayed character description is a pause and then the word",
          "[nav][speech]") {
    NavFixture fixture;
    const alus::speech::SpeechSequence sequence =
        fixture.presenter().getSingleCharacterDescription("b", "en");
    REQUIRE_FALSE(sequence.empty());
    CHECK(sequence.front().is(alus::speech::CommandType::Break));
    CHECK(textOf(sequence).find("Bravo") != std::string::npos);

    // A character with no description says nothing at all, rather than
    // repeating the character the user just heard.
    CHECK(fixture.presenter().getSingleCharacterDescription("\xC2\xA7", "en").empty());
    // And it is only ever for ONE character.
    CHECK(fixture.presenter().getSingleCharacterDescription("ab", "en").empty());
}

TEST_CASE("format fields are reported once and then only when they change",
          "[nav][speech]") {
    alus::text::FormatReportSettings settings;
    settings.reportFontName = true;
    settings.reportFontSize = true;
    settings.reportFontAttributes = true;

    alus::text::FormatField first;
    first.set("font-name", std::string("Cantarell"));
    first.set("font-size", std::string("11 pt"));
    first.set("bold", true);

    alus::text::Field cache;
    alus::speech::SpeechSequence sequence = alus::a11y::getFormatFieldSpeech(
        first, &cache, settings, alus::a11y::OutputReason::Query, TextUnit::Line, false,
        true);
    const std::string spoken = textOf(sequence);
    CHECK(spoken.find("Cantarell") != std::string::npos);
    CHECK(spoken.find("11 pt") != std::string::npos);
    CHECK(spoken.find("bold") != std::string::npos);

    // The same field again: nothing is said, because nothing changed. This is
    // the diff NVDA's format cache exists for -- without it every line of a
    // document would re-announce the font.
    sequence = alus::a11y::getFormatFieldSpeech(first, &cache, settings,
                                                alus::a11y::OutputReason::Query,
                                                TextUnit::Line, false, false);
    CHECK(textOf(sequence).empty());

    // Bold going AWAY is news too, and it is reported as its own phrase.
    alus::text::FormatField plain;
    plain.set("font-name", std::string("Cantarell"));
    plain.set("font-size", std::string("11 pt"));
    plain.set("bold", false);
    sequence = alus::a11y::getFormatFieldSpeech(plain, &cache, settings,
                                                alus::a11y::OutputReason::Query,
                                                TextUnit::Line, false, false);
    CHECK(textOf(sequence) == "no bold");
}

TEST_CASE("a spelling error is reported, and leaving one only in extra detail",
          "[nav][speech]") {
    alus::text::FormatReportSettings settings;
    settings.reportSpellingErrors = true;

    alus::text::FormatField bad;
    bad.set("invalid-spelling", true);
    alus::text::Field cache;
    CHECK(textOf(alus::a11y::getFormatFieldSpeech(bad, &cache, settings,
                                                  alus::a11y::OutputReason::Caret,
                                                  TextUnit::Word, true, true)) ==
          "spelling error");

    // Moving out of it: said when the user is inspecting a character or a
    // word, silent when reading a line -- NVDA's `extraDetail`.
    alus::text::FormatField good;
    good.set("invalid-spelling", false);
    alus::text::Field wordCache = cache;
    CHECK(textOf(alus::a11y::getFormatFieldSpeech(good, &wordCache, settings,
                                                  alus::a11y::OutputReason::Caret,
                                                  TextUnit::Word, true, false)) ==
          "out of spelling error");
    alus::text::Field lineCache = cache;
    CHECK(textOf(alus::a11y::getFormatFieldSpeech(good, &lineCache, settings,
                                                  alus::a11y::OutputReason::Caret,
                                                  TextUnit::Line, false, false))
              .empty());
}

TEST_CASE("the indentation report is a tone, a phrase, or both", "[nav][speech]") {
    using LineIndentation = alus::text::FormatReportSettings::LineIndentation;
    alus::text::FormatReportSettings settings;

    // A real symbol registry, because the indentation is SPOKEN through it:
    // the word for a space comes from the locale's symbol dictionary.
    alus::text::SymbolRegistry symbols{std::filesystem::path{ALUS_TEST_DATA_DIR} /
                                       "symbols"};

    settings.reportLineIndentation = LineIndentation::Speech;
    CHECK(textOf(alus::a11y::getIndentationSpeech("", settings, "en", &symbols)) ==
          "no indent");
    // Runs of the SAME character are counted and NAMED: "4 space", not four
    // words and not four silences.
    CHECK(textOf(alus::a11y::getIndentationSpeech("    ", settings, "en", &symbols)) ==
          "4 space");
    CHECK(textOf(alus::a11y::getIndentationSpeech("\t\t", settings, "en", &symbols)) ==
          "2 tab");

    settings.reportLineIndentation = LineIndentation::Tones;
    const alus::speech::SpeechSequence tones =
        alus::a11y::getIndentationSpeech("\t", settings, "en", &symbols);
    CHECK(hasCommand(tones, alus::speech::CommandType::Beep));
    CHECK(textOf(tones).empty());

    // More than 72 spaces of indentation is SPOKEN rather than played: a tone
    // that high hurts.
    const alus::speech::SpeechSequence tooDeep =
        alus::a11y::getIndentationSpeech(std::string(80, ' '), settings, "en", &symbols);
    CHECK_FALSE(textOf(tooDeep).empty());
}

TEST_CASE("a selection change says what was selected, and what was not",
          "[nav][speech]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "hello world");
    const auto document = fixture.registry.objectFor(tree.document);

    alus::text::TextRangePtr empty = document->makeTextRange(0, 0);
    alus::text::TextRangePtr word = document->makeTextRange(0, 5);
    REQUIRE(empty != nullptr);
    REQUIRE(word != nullptr);

    fixture.clearSpeech();
    alus::a11y::speakSelectionChange(fixture.presenter(), *empty, *word);
    fixture.drainSpeech();
    CHECK(fixture.spokenJoined().find("hello") != std::string::npos);
    CHECK(fixture.spokenJoined().find("selected") != std::string::npos);

    // And back again: the text is reported as UNSELECTED, not as selected.
    fixture.clearSpeech();
    alus::a11y::speakSelectionChange(fixture.presenter(), *word, *empty);
    fixture.drainSpeech();
    CHECK(fixture.spokenJoined().find("unselected") != std::string::npos);
}

TEST_CASE("a selection too large to read is reported by its size",
          "[nav][speech]") {
    const alus::speech::SpeechSequence sequence =
        alus::a11y::getSelectionMessageSpeech("%s selected", std::string(600, 'x'));
    const std::string spoken = textOf(sequence);
    CHECK(spoken.find("600 characters") != std::string::npos);
    CHECK(spoken.find("xxxx") == std::string::npos);
}

TEST_CASE("text already selected when a control gets the focus is announced first",
          "[nav][speech]") {
    // The order matters: "selected hello world" warns the user that typing
    // would overwrite it, where "hello world selected" would read as though
    // they had just selected it themselves.
    const alus::speech::SpeechSequence sequence =
        alus::a11y::getPreselectedTextSpeech("hello world");
    CHECK(textOf(sequence) == "selected hello world");
}

TEST_CASE("an object with navigable text reads its text, not its value",
          "[nav][speech]") {
    // docs/history/phase-4.md O10, closed: until phase 5 there was no text layer, so
    // getObjectSpeech spoke the value.
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "the first line\nthe second line");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(document != nullptr);
    CHECK(document->hasNavigableText());
    CHECK(alus::a11y::shouldReportTextContent(*document, alus::a11y::OutputReason::Focus));

    const std::string spoken = alus::testing::joined(
        alus::a11y::getObjectSpeech(*document, alus::a11y::OutputReason::Focus,
                                    &fixture.presenter()));
    // The LINE at the caret, which is what a user hears on focusing an editor.
    CHECK(spoken.find("the first line") != std::string::npos);
    CHECK(spoken.find("the second line") == std::string::npos);

    // An ancestor of the focus never reads text content.
    CHECK_FALSE(
        alus::a11y::shouldReportTextContent(*document, alus::a11y::OutputReason::FocusEntered));
}
