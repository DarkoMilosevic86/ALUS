// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Spelling: what the SECOND and THIRD press of a review command say.
//
// Port of NVDA's `getSpellingSpeech`, `_getSpellingSpeechWithoutCharMode`,
// `_getSpellingSpeechAddCharMode`, `_getSpellingCharAddCapNotification` and
// `getSingleCharDescription` (`speech/speech.py:350-660`). These are
// `speech::Presenter` methods, because that is where NVDA keeps them and
// because they need the symbol processor, the character descriptions and the
// synthesizer's capabilities -- all three of which the Presenter already has.
//
// This file closes docs/history/phase-4.md O11: phase 4 spelled by sending the text
// with `CharacterModeCommand` and nothing else, so the third press of
// `alus+tab` spelled exactly like the second. Now the third press describes
// the characters, and a capital letter changes pitch.
//
// FOUR RULES THAT ARE AUDIBLE WHEN BROKEN:
//
//  1. CHARACTER MODE WRAPS ONLY SINGLE CHARACTERS. A description ("Avala") is
//     a word and must not be spelled; the character before it must. So the
//     mode is switched on and off AROUND each single-character item, which is
//     what `_getSpellingSpeechAddCharMode` does.
//  2. ONE UTTERANCE PER CHARACTER. `EndUtteranceCommand` after each one is
//     what makes spelling interruptible letter by letter, and what stops the
//     synthesizer from running the letters together into a word.
//  3. THE PITCH CHANGE IS AN OFFSET AND IT IS UNDONE. `PitchCommand::byOffset`
//     then `PitchCommand::toDefault()`, or every character after the first
//     capital stays high.
//  4. NORMALISATION IS SUPPRESSED AROUND THE WHOLE SEQUENCE. This function
//     normalises where it means to; `speak()` would otherwise normalise again
//     and a decomposed character would be spelled as its composed form
//     without the "normalized" report the user asked for.

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/speech/synth.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>
#include <set>

namespace alus::speech {
namespace {

// NVDA joins several descriptions of one character with the ideographic comma
// when it speaks them all at once.
constexpr std::string_view kIdeographicComma = "\xE3\x80\x81";

std::string baseLanguage(const std::string& locale) {
    const std::size_t underscore = locale.find('_');
    return underscore == std::string::npos ? locale : locale.substr(0, underscore);
}

std::string stripTrailingWhitespace(const std::string& text) {
    std::size_t end = text.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(0, end);
}

bool isAllWhitespace(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

}  // namespace

bool Presenter::synthSupports(CommandType type) const {
    const ISynth* synth = manager_.synth();
    if (synth == nullptr) {
        // No synthesizer: assume the richest form, so that a test asserting on
        // the sequence sees what a capable synthesizer would be given.
        return true;
    }
    const std::set<CommandType> supported = synth->supportedCommands();
    return supported.find(type) != supported.end();
}

std::string Presenter::resolveSpellingLocale(const std::string& locale) const {
    // NVDA: an empty locale, or one whose LANGUAGE matches the current one
    // while dialect switching is off, resolves to the current language. That
    // is what stops "en_GB" from being announced as a language change in an
    // en_US session.
    if (locale.empty()) {
        return settings_.language;
    }
    if (!settings_.autoDialectSwitching &&
        baseLanguage(locale) == baseLanguage(settings_.language)) {
        return settings_.language;
    }
    return locale;
}

void Presenter::appendCapitalNotification(SpeechSequence& out, const std::string& character,
                                          bool sayCapForCapitals, int capPitchChange,
                                          bool beepForCapitals, bool reportNormalized) {
    // NVDA splits its translatable "cap %s" so that a language which puts the
    // word AFTER the letter still works. ALUS keeps the same two halves, which
    // is what phase 9's catalogue will translate.
    const std::string capBefore = sayCapForCapitals ? "cap " : "";
    const std::string capAfter;
    const std::string normalizedBefore;
    const std::string normalizedAfter = reportNormalized ? " normalized" : "";

    if (capPitchChange != 0) {
        out.emplace_back(PitchCommand::byOffset(capPitchChange));
    }
    if (!normalizedBefore.empty()) {
        out.emplace_back(normalizedBefore);
    }
    if (beepForCapitals) {
        // NVDA: 2000 Hz for 50 ms. The numbers are the user's expectation.
        out.emplace_back(beep(2000.0, 50));
    }
    if (!capBefore.empty()) {
        out.emplace_back(capBefore);
    }
    out.emplace_back(character);
    if (!capAfter.empty()) {
        out.emplace_back(capAfter);
    }
    if (!normalizedAfter.empty()) {
        out.emplace_back(normalizedAfter);
    }
    if (capPitchChange != 0) {
        out.emplace_back(PitchCommand::toDefault());
    }
}

void Presenter::appendSpellingWithoutCharacterMode(
    SpeechSequence& out, const std::string& rawText, std::string locale,
    bool useCharacterDescriptions, bool sayCapForCapitals, int capPitchChange,
    bool beepForCapitals, bool fallbackToCharacterIfNoDescription, bool unicodeNormalization,
    bool reportNormalized, bool endsUtterance) const {
    locale = resolveSpellingLocale(locale);

    if (rawText.empty()) {
        // Translators: spoken when the review cursor reaches an empty line.
        out.emplace_back(_("blank"));
        return;
    }
    // Trailing whitespace is dropped, EXCEPT when the text is nothing but
    // whitespace -- then it is the thing being spelled.
    std::string text = isAllWhitespace(rawText) ? rawText : stripTrailingWhitespace(rawText);
    if (text.empty()) {
        // Translators: spoken when the review cursor reaches an empty line.
        out.emplace_back(_("blank"));
        return;
    }

    const std::size_t textLength = text::codepointCount(text);
    bool textIsNormalized = false;
    if (unicodeNormalization && textLength > 1) {
        const std::string normalized = text::normalizeNfc(text);
        if (text::codepointCount(normalized) == 1) {
            // A decomposed character composed into one: this is T5.4, and it
            // is what makes "press twice to spell" work on an accented letter.
            text = normalized;
            textIsNormalized = true;
        }
    }

    const bool conjuncts = text::languageHasConjuncts(locale);
    // What is spelled, one entry at a time: a character and, when it has one,
    // its description.
    std::vector<text::DescribedCharacter> items;
    if (conjuncts && descriptions_ != nullptr) {
        // The conjunct languages need the LONGEST describable piece, not one
        // codepoint (§11.1's note, NVDA's `getCharDescListFromText`).
        items = text::describeText(*descriptions_, locale, text);
    } else {
        const text::OffsetConverter converter(text);
        for (std::size_t index = 0; index < converter.codepointLength(); ++index) {
            const auto [start, end] = converter.codepointRangeAt(converter.toByte(index));
            text::DescribedCharacter item;
            item.character = text.substr(start, end - start);
            items.push_back(std::move(item));
        }
    }

    for (text::DescribedCharacter& item : items) {
        const std::string& character = item.character;
        std::vector<std::string> charList;
        bool itemIsNormalized = textIsNormalized;

        std::vector<std::string> description = item.descriptions;
        if (useCharacterDescriptions && description.empty() && descriptions_ != nullptr &&
            !conjuncts) {
            if (const std::vector<std::string>* found =
                    descriptions_->findRelaxed(locale, character)) {
                description = *found;
            }
        }

        if (useCharacterDescriptions && !description.empty()) {
            // Spelling a WORD uses only the first description per letter;
            // describing ONE character says all of them.
            if (textLength > 1) {
                charList.push_back(description.front());
            } else {
                std::string joined;
                for (std::size_t index = 0; index < description.size(); ++index) {
                    if (index > 0) {
                        joined += kIdeographicComma;
                    }
                    joined += description[index];
                }
                charList.push_back(std::move(joined));
            }
        } else if (useCharacterDescriptions && description.empty() &&
                   !fallbackToCharacterIfNoDescription) {
            // The delayed description path: no description means say nothing
            // at all, rather than repeating the character the user just heard.
            return;
        } else {
            const std::string symbol = symbols_.processSymbol(locale, character);
            if (symbol != character) {
                charList.push_back(symbol);
            } else if (!textIsNormalized && unicodeNormalization) {
                const std::string normalized = text::normalizeNfc(character);
                if (normalized != character) {
                    // The character has a composed form: speak the pieces of
                    // it, each through the symbol processor, and report that
                    // it was normalised.
                    std::string joined;
                    const text::OffsetConverter converter(normalized);
                    for (std::size_t index = 0; index < converter.codepointLength();
                         ++index) {
                        const auto [start, end] =
                            converter.codepointRangeAt(converter.toByte(index));
                        if (index > 0) {
                            joined += " ";
                        }
                        joined += symbols_.processSymbol(
                            locale, normalized.substr(start, end - start));
                    }
                    charList.push_back(std::move(joined));
                    itemIsNormalized = true;
                } else {
                    charList.push_back(character);
                }
            } else {
                charList.push_back(character);
            }
        }

        const bool uppercase = text::isUppercase(character);
        if (settings_.autoLanguageSwitching) {
            out.emplace_back(langChange(locale));
        }
        for (const std::string& toSpeak : charList) {
            appendCapitalNotification(out, toSpeak, uppercase && sayCapForCapitals,
                                      uppercase ? capPitchChange : 0,
                                      uppercase && beepForCapitals,
                                      itemIsNormalized && reportNormalized);
            if (endsUtterance) {
                out.emplace_back(endUtterance());
            }
        }
    }
}

SpeechSequence Presenter::getSpellingSpeech(const std::string& text, const std::string& locale,
                                            bool useCharacterDescriptions,
                                            bool endsUtterance,
                                            bool useCharacterMode) const {
    const int capPitchChange =
        synthSupports(CommandType::Pitch) ? settings_.capPitchChange : 0;
    // NVDA: normalisation is applied here only when descriptions are NOT being
    // used, because a description is a word and normalising it says nothing.
    const bool unicodeNormalization =
        !useCharacterDescriptions && settings_.unicodeNormalization;

    SpeechSequence raw;
    appendSpellingWithoutCharacterMode(
        raw, text, locale, useCharacterDescriptions, settings_.sayCapForCapitals,
        capPitchChange, settings_.beepForCapitals,
        /*fallbackToCharacterIfNoDescription=*/true, unicodeNormalization,
        settings_.reportNormalizedForCharacterNavigation, endsUtterance);

    SpeechSequence result;
    // Rule 4: this function already normalised what it meant to.
    result.emplace_back(suppressUnicodeNormalization(true));

    if (useCharacterMode && settings_.useSpellingFunctionality &&
        synthSupports(CommandType::CharacterMode)) {
        // Rule 1: character mode goes on for a single character and off for
        // anything longer, so a description is read as a word.
        bool inCharacterMode = false;
        for (SpeechItem& item : raw) {
            if (item.isText()) {
                const bool single = text::codepointCount(item.text()) == 1;
                if (single && !inCharacterMode) {
                    result.emplace_back(characterMode(true));
                    inCharacterMode = true;
                } else if (!single && inCharacterMode) {
                    result.emplace_back(characterMode(false));
                    inCharacterMode = false;
                }
            }
            result.push_back(std::move(item));
        }
        if (inCharacterMode) {
            result.emplace_back(characterMode(false));
        }
    } else {
        for (SpeechItem& item : raw) {
            result.push_back(std::move(item));
        }
    }

    result.emplace_back(suppressUnicodeNormalization(false));
    return result;
}

void Presenter::speakSpelling(const std::string& text, const std::string& locale,
                              bool useCharacterDescriptions, Priority priority) {
    SpeechSequence sequence = getSpellingSpeech(text, locale, useCharacterDescriptions);
    if (sequence.empty()) {
        return;
    }
    speak(std::move(sequence), std::nullopt, priority);
}

SpeechSequence Presenter::getSingleCharacterDescription(const std::string& text,
                                                        const std::string& locale) const {
    SpeechSequence result;
    // NVDA: only for a single character, and silent otherwise.
    if (text::codepointCount(text) != 1) {
        return result;
    }
    const int capPitchChange =
        synthSupports(CommandType::Pitch) && text::isUppercase(text) ? settings_.capPitchChange
                                                                     : 0;
    result.emplace_back(breakFor(settings_.singleCharacterDescriptionDelayMilliseconds));
    appendSpellingWithoutCharacterMode(
        result, text, locale, /*useCharacterDescriptions=*/true,
        // #14239: the character announcement already beeped or said "cap";
        // saying it again with the description is noise. The pitch change
        // stays, because a continued pitch is easier to follow than a jump.
        /*sayCapForCapitals=*/false, capPitchChange, /*beepForCapitals=*/false,
        /*fallbackToCharacterIfNoDescription=*/false,
        /*unicodeNormalization=*/false, /*reportNormalized=*/false,
        /*endsUtterance=*/true);
    if (result.size() == 1) {
        // Nothing but the pause: there was no description, so say nothing.
        return {};
    }
    return result;
}

}  // namespace alus::speech
