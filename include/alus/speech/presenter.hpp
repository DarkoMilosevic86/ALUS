// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The presentation layer: speak().
//
// Port of the platform-neutral half of NVDA's `speech/speech.py` (3198 lines).
// See 01-ARHITEKTURA.md §4.2. What matters here is not the code but THE ORDER
// OF OPERATIONS, which is a contract (`speech.py:1126-1233`):
//
//   filter_speechSequence → type validation → priority → empty? → speech viewer
//   → pre_speech → speech mode (off/beeps/talk/onDemand) → if paused: cancel
//   → language pass (insert LangChangeCommand where the language changes)
//   → text pass (dictionaries → symbols → whitespace → normalisation)
//   → CHUNK_SEPARATOR between chunks, except in character mode
//   → SpeechManager::speak()
//
// Two details that look like noise and are not:
//
//  * CHUNK_SEPARATOR IS TWO SPACES. One space would let the last number of one
//    chunk and the first number of the next be read as a single number in
//    French and German, where space is the thousands separator. This is NVDA
//    issue #555, and it is a one-character bug with an audible consequence.
//  * LanguageChangeCommands FROM THE CALLER ARE DROPPED AND REGENERATED.
//    The caller's commands say where the language changes; the presenter
//    decides what that means, after applying the user's settings for automatic
//    language and dialect switching.
#pragma once

#include "alus/core/extension_points.hpp"
#include "alus/speech/commands.hpp"
#include "alus/speech/dictionaries.hpp"
#include "alus/speech/priority.hpp"
#include "alus/text/char_descriptions.hpp"
#include "alus/text/symbols.hpp"

#include <functional>
#include <optional>
#include <string>

namespace alus::config {
class Manager;
}

namespace alus::speech {

class SpeechManager;

// NVDA: SpeechMode.
enum class SpeechMode {
    // Say nothing at all.
    Off,
    // Beep instead of speaking. A diagnostic mode, and the one that makes it
    // obvious that speech is alive when a synthesizer is misbehaving.
    Beeps,
    Talk,
    // Speak only what was explicitly asked for (a script marked as
    // speak-on-demand, input help, say-all). Everything else is silent.
    OnDemand,
};

const char* speechModeName(SpeechMode mode);
std::optional<SpeechMode> parseSpeechMode(std::string_view name);

// The string between adjacent chunks spoken without a pause. TWO SPACES; see
// the file comment.
inline constexpr std::string_view kChunkSeparator = "  ";

// NVDA: isBlank(). Text made only of these is reported as "blank".
bool isBlank(std::string_view text);

class Presenter {
public:
    struct Settings {
        // The user's punctuation verbosity (nvda+p cycles it).
        text::SymbolLevel symbolLevel = text::SymbolLevel::Some;
        // Honour LangChangeCommands at all.
        bool autoLanguageSwitching = true;
        // Distinguish en_GB from en_US, sr_RS from sr. Off by default, as in
        // NVDA: most users want one voice per language, not per dialect.
        bool autoDialectSwitching = false;
        bool unicodeNormalization = false;
        // The ALUS locale: the language of anything with no language of its own.
        std::string language = "en";

        // ---- spelling (phase 5) ----
        // NVDA keeps these per synthesizer (`config.conf["speech"][synth]`),
        // because what a synthesizer can do about a capital letter differs.
        // ALUS keeps one set and asks the synthesizer whether it supports the
        // command, which is the same decision made in one place.
        //
        // "Say cap for capitals": the word before the letter. Off by default,
        // as in NVDA, because the pitch change is less intrusive.
        bool sayCapForCapitals = false;
        // The pitch OFFSET while a capital is spelled, on top of whatever the
        // user set (§4.3: offset XOR multiplier, relative to the user's own
        // value). NVDA's default is 30.
        int capPitchChange = 30;
        bool beepForCapitals = false;
        // NVDA: "use spelling functionality" -- wrap single characters in
        // CharacterModeCommand so the synthesizer spells rather than reads.
        bool useSpellingFunctionality = true;
        // After a character is spoken by navigation, speak its description a
        // second later ("a... Avala").
        bool delayedCharacterDescriptions = false;
        int singleCharacterDescriptionDelayMilliseconds = 1000;
        // Say "normalized" when a character had to be normalised to be spoken.
        bool reportNormalizedForCharacterNavigation = false;
        // The pitch of the beep that stands in for speech in Beeps mode.
        double beepSpeechModePitch = 10000.0;
        int beepSpeechModeMilliseconds = 15;
    };

    Presenter(SpeechManager& manager, text::SymbolRegistry& symbols,
              Dictionaries& dictionaries);

    const Settings& settings() const noexcept { return settings_; }
    void setSettings(Settings settings) { settings_ = std::move(settings); }
    // Reads everything in the [speech] section. Called at startup and from
    // resetConfiguration().
    void applyConfiguration(const config::Manager& manager);

    SpeechMode mode() const noexcept { return mode_; }
    void setMode(SpeechMode mode) { mode_ = mode; }

    // NVDA: `speech.speech._lastSpeech`. The last sequence that reached the
    // pipeline, with the symbol level it was spoken at, so that `alus+x` can
    // say it again.
    //
    // It is the sequence AS GIVEN, before the language and text passes: a
    // repeat has to go through the pipeline again, because the user may have
    // changed the symbol level or the voice in between -- which is one of the
    // reasons to press the key at all.
    struct LastSpeech {
        SpeechSequence sequence;
        std::optional<text::SymbolLevel> symbolLevel;
    };
    const std::optional<LastSpeech>& lastSpeech() const noexcept { return lastSpeech_; }

    // The whole pipeline. `symbolLevel` unset (or Unchanged) means the user's
    // configured level.
    void speak(SpeechSequence sequence,
               std::optional<text::SymbolLevel> symbolLevel = std::nullopt,
               Priority priority = Priority::Normal);

    // NVDA: speakMessage(). Blank text becomes the word "blank", because
    // silence is indistinguishable from a broken screen reader.
    void speakMessage(const std::string& text, Priority priority = Priority::Normal);
    void speakText(const std::string& text,
                   std::optional<text::SymbolLevel> symbolLevel = std::nullopt,
                   Priority priority = Priority::Normal);

    // NVDA: cancelSpeech(). Also clears the paused state.
    void cancelSpeech();
    // NVDA: pauseSpeech().
    void pauseSpeech(bool switchOn);
    bool isPaused() const noexcept { return paused_; }

    // ---- spelling (phase 5; NVDA: speech.getSpellingSpeech family) ----
    //
    // This is what phase 4's `PresentationHooks::spell` was standing in for
    // (claude/FAZA4.md O11): the real path, with character descriptions, the
    // pitch change for capitals and the character mode the synthesizer needs
    // in order to spell "a" rather than say the word "a".
    //
    // The character descriptions are OPTIONAL and set by the service: a
    // Presenter without them spells characters and never describes them,
    // which is what a unit test wants.
    void setCharacterDescriptions(text::CharacterDescriptionRegistry* descriptions) {
        descriptions_ = descriptions;
    }
    text::CharacterDescriptionRegistry* characterDescriptions() const noexcept {
        return descriptions_;
    }

    // NVDA: `getSpellingSpeech`. `locale` empty means the ALUS language.
    SpeechSequence getSpellingSpeech(const std::string& text, const std::string& locale = {},
                                     bool useCharacterDescriptions = false,
                                     bool endsUtterance = true,
                                     bool useCharacterMode = true) const;
    // NVDA: `speakSpelling`.
    void speakSpelling(const std::string& text, const std::string& locale = {},
                       bool useCharacterDescriptions = false,
                       Priority priority = Priority::Normal);
    // NVDA: `getSingleCharDescription`. A pause, then the description -- the
    // delayed second half of a character announcement.
    SpeechSequence getSingleCharacterDescription(const std::string& text,
                                                 const std::string& locale = {}) const;

    // Whether the synthesizer in use supports a command, which decides whether
    // the capital pitch change and the character mode are emitted at all.
    bool synthSupports(CommandType type) const;

    // dictionaries → symbols → whitespace → Unicode normalisation.
    std::string processText(const std::string& locale, const std::string& text,
                            text::SymbolLevel level, bool normalize) const;
    // NFC. Public because the review layer (phase 5) needs the same answer for
    // "is this one character" as the speech layer gives.
    static std::string unicodeNormalize(const std::string& text);

    // In OnDemand mode, what counts as "on demand". Set by phase 3 (the script
    // layer): a script marked speakOnDemand, input help, or say-all.
    void setOnDemandPredicate(std::function<bool()> predicate) {
        onDemand_ = std::move(predicate);
    }

    // ---- extension points (NVDA: speech/extensions.py) ----
    core::Filter<SpeechSequence>& filterSpeechSequence() noexcept { return filter_; }
    core::Action<const SpeechSequence&, Priority>& preSpeech() noexcept { return preSpeech_; }
    core::Action<>& preSpeechCanceled() noexcept { return preSpeechCanceled_; }
    core::Action<>& speechCanceled() noexcept { return speechCanceled_; }
    core::Action<bool>& postSpeechPaused() noexcept { return postSpeechPaused_; }

    SpeechManager& manager() noexcept { return manager_; }

private:
    SpeechSequence languagePass(const SpeechSequence& sequence) const;
    void textPass(SpeechSequence& sequence, text::SymbolLevel level) const;
    // NVDA: `_getSpellingSpeechWithoutCharMode` and
    // `_getSpellingCharAddCapNotification`.
    void appendSpellingWithoutCharacterMode(SpeechSequence& out, const std::string& text,
                                            std::string locale,
                                            bool useCharacterDescriptions,
                                            bool sayCapForCapitals, int capPitchChange,
                                            bool beepForCapitals,
                                            bool fallbackToCharacterIfNoDescription,
                                            bool unicodeNormalization,
                                            bool reportNormalized, bool endsUtterance) const;
    static void appendCapitalNotification(SpeechSequence& out, const std::string& character,
                                          bool sayCapForCapitals, int capPitchChange,
                                          bool beepForCapitals, bool reportNormalized);
    // NVDA's resolution of an empty or same-language locale to the current one.
    std::string resolveSpellingLocale(const std::string& locale) const;

    SpeechManager& manager_;
    text::SymbolRegistry& symbols_;
    Dictionaries& dictionaries_;
    Settings settings_;
    SpeechMode mode_ = SpeechMode::Talk;
    std::optional<LastSpeech> lastSpeech_;
    bool paused_ = false;
    bool beenCanceled_ = true;
    std::function<bool()> onDemand_;

    text::CharacterDescriptionRegistry* descriptions_ = nullptr;

    core::Filter<SpeechSequence> filter_;
    core::Action<const SpeechSequence&, Priority> preSpeech_;
    core::Action<> preSpeechCanceled_;
    core::Action<> speechCanceled_;
    core::Action<bool> postSpeechPaused_;
};

}  // namespace alus::speech
