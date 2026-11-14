// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/presenter.hpp"

#include "alus/audio/tones.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/speech/manager.hpp"

#include <glib.h>

#include <algorithm>
#include <utility>

namespace alus::speech {

namespace {

std::string languageRoot(const std::string& language) {
    const std::size_t separator = language.find_first_of("_-");
    return separator == std::string::npos ? language : language.substr(0, separator);
}

std::string trim(std::string text) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto begin = std::find_if(text.begin(), text.end(), notSpace);
    const auto end = std::find_if(text.rbegin(), text.rend(), notSpace).base();
    if (begin >= end) {
        return {};
    }
    return std::string{begin, end};
}

std::string trimRight(std::string text) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto end = std::find_if(text.rbegin(), text.rend(), notSpace).base();
    text.erase(end, text.end());
    return text;
}

}  // namespace

const char* speechModeName(SpeechMode mode) {
    switch (mode) {
        case SpeechMode::Off:
            return "off";
        case SpeechMode::Beeps:
            return "beeps";
        case SpeechMode::Talk:
            return "talk";
        case SpeechMode::OnDemand:
            return "onDemand";
    }
    return "?";
}

std::optional<SpeechMode> parseSpeechMode(std::string_view name) {
    if (name == "off") {
        return SpeechMode::Off;
    }
    if (name == "beeps") {
        return SpeechMode::Beeps;
    }
    if (name == "talk") {
        return SpeechMode::Talk;
    }
    if (name == "onDemand" || name == "ondemand") {
        return SpeechMode::OnDemand;
    }
    return std::nullopt;
}

bool isBlank(std::string_view text) {
    if (text.empty()) {
        return true;
    }
    // NVDA: BLANK_CHUNK_CHARS. The no-break space is U+00A0 (two bytes in
    // UTF-8), so the test is on the decoded string.
    for (const char* p = text.data(); p < text.data() + text.size();) {
        const gunichar character = g_utf8_get_char(p);
        const bool blank = character == ' ' || character == '\n' || character == '\r' ||
                           character == 0 || character == 0x00A0;
        if (!blank) {
            return false;
        }
        p = g_utf8_next_char(p);
    }
    return true;
}

Presenter::Presenter(SpeechManager& manager, text::SymbolRegistry& symbols,
                     Dictionaries& dictionaries)
    : manager_(manager), symbols_(symbols), dictionaries_(dictionaries) {}

void Presenter::applyConfiguration(const config::Manager& manager) {
    Settings settings;
    const std::string level = manager.getString("speech", "symbolLevel", "some");
    if (const auto parsed = text::parseSymbolLevel(level)) {
        settings.symbolLevel = *parsed;
    } else {
        ALUS_WARNING("unknown symbol level in the configuration: {}", level);
    }
    settings.autoLanguageSwitching = manager.getBool("speech", "autoLanguageSwitching", true);
    settings.autoDialectSwitching = manager.getBool("speech", "autoDialectSwitching", false);
    settings.unicodeNormalization = manager.getBool("speech", "unicodeNormalization", false);
    settings.language = manager.getString("general", "language", settings_.language);
    settings.beepSpeechModePitch = manager.getDouble("speech", "beepSpeechModePitch", 10000.0);
    settings_ = std::move(settings);

    const std::string mode = manager.getString("speech", "speechMode", "talk");
    if (const auto parsed = parseSpeechMode(mode)) {
        mode_ = *parsed;
    } else {
        ALUS_WARNING("unknown speech mode in the configuration: {}", mode);
    }
}

std::string Presenter::unicodeNormalize(const std::string& text) {
    gchar* normalized = g_utf8_normalize(text.c_str(), static_cast<gssize>(text.size()),
                                         G_NORMALIZE_NFC);
    if (normalized == nullptr) {
        return text;
    }
    std::string out{normalized};
    g_free(normalized);
    return out;
}

std::string Presenter::processText(const std::string& locale, const std::string& text,
                                   text::SymbolLevel level, bool normalize) const {
    std::string out = dictionaries_.processText(text);
    out = symbols_.processText(locale, out, level);
    // NVDA: RE_CONVERT_WHITESPACE. A newline inside a chunk would end the
    // utterance at the synthesizer.
    for (char& character : out) {
        if (character == '\0' || character == '\r' || character == '\n') {
            character = ' ';
        }
    }
    if (normalize) {
        out = unicodeNormalize(out);
        // The leading space is kept on purpose: it separates the normalisation
        // announcement from the text.
        return trimRight(std::move(out));
    }
    return trim(std::move(out));
}

SpeechSequence Presenter::languagePass(const SpeechSequence& sequence) const {
    const std::string& defaultLanguage = settings_.language;
    const std::string defaultRoot = languageRoot(defaultLanguage);
    std::string currentLanguage = defaultLanguage;
    std::optional<std::string> previousLanguage;

    SpeechSequence out;
    out.reserve(sequence.size());
    for (const SpeechItem& item : sequence) {
        if (const auto langChangeCommand = as<LangChangeCommand>(item)) {
            if (!settings_.autoLanguageSwitching) {
                continue;
            }
            currentLanguage = langChangeCommand->language();
            if (currentLanguage.empty() ||
                (!settings_.autoDialectSwitching &&
                 languageRoot(currentLanguage) == defaultRoot)) {
                currentLanguage = defaultLanguage;
            }
            continue;
        }
        if (item.is(CommandType::SuppressUnicodeNormalization)) {
            if (!settings_.unicodeNormalization) {
                continue;  // nothing to suppress
            }
            out.push_back(item);
            continue;
        }
        if (item.isText()) {
            if (item.text().empty()) {
                continue;
            }
            if (settings_.autoLanguageSwitching &&
                (!previousLanguage.has_value() || *previousLanguage != currentLanguage)) {
                out.emplace_back(langChange(currentLanguage));
                previousLanguage = currentLanguage;
            }
            out.push_back(item);
            continue;
        }
        out.push_back(item);
    }
    return out;
}

void Presenter::textPass(SpeechSequence& sequence, text::SymbolLevel level) const {
    std::string currentLanguage = settings_.language;
    bool inCharacterMode = false;
    bool normalize = settings_.unicodeNormalization;
    const bool normalizeInitially = normalize;

    for (SpeechItem& item : sequence) {
        if (const auto characterModeCommand = as<CharacterModeCommand>(item)) {
            inCharacterMode = characterModeCommand->state();
        }
        if (settings_.autoLanguageSwitching) {
            if (const auto langChangeCommand = as<LangChangeCommand>(item)) {
                currentLanguage = langChangeCommand->language();
            }
        }
        if (const auto suppress = as<SuppressUnicodeNormalizationCommand>(item)) {
            normalize = normalizeInitially && !suppress->state();
        }
        if (!item.isText()) {
            continue;
        }
        item.text() = processText(currentLanguage, item.text(), level, normalize);
        if (!inCharacterMode) {
            // See the header: two spaces, not one.
            item.text() += kChunkSeparator;
        }
    }
}

void Presenter::speak(SpeechSequence sequence, std::optional<text::SymbolLevel> symbolLevel,
                      Priority priority) {
    // 1. the filter chain may rewrite or drop anything
    sequence = filter_.apply(std::move(sequence));
    // 2. type validation is the compiler's job here
    // 3. an unset priority is Normal
    // 4. nothing to say
    if (sequence.empty()) {
        return;
    }
    // 5/6. the speech viewer and anything else that wants to see every
    // utterance before the settings are applied
    preSpeech_.notify(sequence, priority);

    // Remembered before the mode gate, so that `alus+x` can repeat what was
    // SAID and also what was suppressed -- a user who turns speech back on
    // after missing something is exactly the case the key is for.
    lastSpeech_ = LastSpeech{sequence, symbolLevel};

    // 7. the speech mode
    switch (mode_) {
        case SpeechMode::Off:
            return;
        case SpeechMode::Beeps:
            audio::beep(settings_.beepSpeechModePitch, settings_.beepSpeechModeMilliseconds);
            return;
        case SpeechMode::OnDemand:
            if (onDemand_ == nullptr || !onDemand_()) {
                return;
            }
            break;
        case SpeechMode::Talk:
            break;
    }

    // 8. speaking while paused resumes by cancelling: the paused utterance is
    // stale by definition.
    if (paused_) {
        cancelSpeech();
    }
    beenCanceled_ = false;

    // 9. the language pass
    SpeechSequence processed = languagePass(sequence);
    if (processed.empty()) {
        // Everything was dropped; there is nothing to say.
        return;
    }

    // 10. the text pass
    text::SymbolLevel level = symbolLevel.value_or(text::SymbolLevel::Unchanged);
    if (level == text::SymbolLevel::Unchanged) {
        level = settings_.symbolLevel;
    }
    textPass(processed, level);

    if (core::Log::instance().isEnabledFor(core::LogLevel::Io)) {
        ALUS_IO("speaking {}", repr(processed));
    }
    manager_.speak(std::move(processed), priority);
}

void Presenter::speakMessage(const std::string& text, Priority priority) {
    if (isBlank(text)) {
        // Translators: spoken when a line or a field is empty.
        speak(SpeechSequence{std::string{_("blank")}}, std::nullopt, priority);
        return;
    }
    speak(SpeechSequence{text}, std::nullopt, priority);
}

void Presenter::speakText(const std::string& text, std::optional<text::SymbolLevel> symbolLevel,
                          Priority priority) {
    if (text.empty()) {
        return;
    }
    speak(SpeechSequence{text}, symbolLevel, priority);
}

void Presenter::cancelSpeech() {
    preSpeechCanceled_.notify();
    if (beenCanceled_ || mode_ == SpeechMode::Off || mode_ == SpeechMode::Beeps) {
        return;
    }
    // A cancel ends a pause (NVDA: `isPaused = False`), and the SYNTHESIZER
    // has to be told so. speech-dispatcher keeps a connection paused across
    // spd_cancel: Shift (which pauses) and then Tab (which cancels) left
    // every later utterance queued behind the pause, and ALUS silent until
    // the connection was rebuilt (FAZA10.md N123).
    //
    // The resume goes FIRST. Measured against speech-dispatcher 0.12 with
    // rhvoice: pause, cancel, resume while an utterance was playing leaves
    // the connection silent all the same; pause, resume, cancel does not.
    if (paused_) {
        if (ISynth* synth = manager_.synth()) {
            synth->pause(false);
        }
    }
    manager_.cancel();
    speechCanceled_.notify();
    beenCanceled_ = true;
    paused_ = false;
}

void Presenter::pauseSpeech(bool switchOn) {
    if (ISynth* synth = manager_.synth()) {
        synth->pause(switchOn);
    }
    postSpeechPaused_.notify(switchOn);
    paused_ = switchOn;
    beenCanceled_ = false;
}

}  // namespace alus::speech
