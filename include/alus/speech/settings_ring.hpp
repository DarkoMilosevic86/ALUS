// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The synthesizer settings ring: changing voice, rate, pitch, volume and
// inflection from the keyboard, with no window and no sighted help.
//
// Port of NVDA's `synthSettingsRing.SynthSettingsRing`
// (reference/nvda/source/synthSettingsRing.py). Six keys drive it:
//
//     alus+control+rightArrow / leftArrow   the next / previous setting
//     alus+control+upArrow    / downArrow   that setting up / down one step
//     alus+control+pageUp     / pageDown    up / down a large step
//
// WHY IT IS NOT A CONVENIENCE. Until this existed, ALUS could be configured
// in exactly two ways: by typing a voice name into a text entry in the
// settings window, or by editing ~/.config/alus/alus.ini by hand. Both of
// them require knowing the name of a voice the synthesizer has -- which is
// the one thing a user who cannot hear the screen reader properly cannot
// find out. The ring needs no names: it steps through what the driver
// reports and SAYS each value as it lands on it.
//
// THE NUMBERS ARE UPSTREAM'S, not invented here. NVDA's NumericDriverSetting
// defaults are minVal 0, maxVal 100, normalStep 5, largeStep 10, and a large
// change moves by largeStep * 2 -- so one press of the up arrow is 5 and one
// press of pageUp is 20, on the same 0-100 scale ALUS's ISynth already uses.
//
// The ring holds NO state of its own about the values: it reads them from the
// driver on every call, because the driver is also changed by the settings
// window, by a profile switch and by alus-cli. What it does hold is which
// setting is selected, and that survives a driver change if the new driver
// has the same setting (updateSupportedSettings() in upstream).
#pragma once

#include "alus/speech/synth.hpp"
#include "alus/speech/voice_catalog.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace alus::config {
class Manager;
}

namespace alus::speech {

class SettingsRing {
public:
    // Which setting the ring is on. The order of the enumerators is the order
    // of the ring, and it is upstream's driver order: voice first, then the
    // numbers, because the voice is what a user reaches for first.
    //
    // Module and Variant are ALUS's (phase-10.md N126): speech-dispatcher fronts
    // several engines, and a user on a module with one voice (mbrola_ng) had
    // nothing in the ring to change it with; eSpeak's 14805 "voices" are 140
    // languages times 105 variants, which NVDA's eSpeak driver also offers as
    // Voice and Variant. Each is in the ring only when it has two values.
    enum class Setting {
        Module,
        Voice,
        Variant,
        Rate,
        Pitch,
        Inflection,
        Volume,
    };

    // NVDA: the SPOKEN name of a setting (pgettext "synth setting").
    // Translated when called, which is why it is not a `const char*` constant:
    // the language can change while ALUS runs (i18n::Service).
    static std::string settingName(Setting setting);
    // The name in the configuration file and in a log line. NEVER translated.
    static const char* settingKey(Setting setting);

    // Both may be null, and then the ring is empty: `--no-speech`, a test with
    // no driver, and alus-cli all take that path, and an empty ring SAYS it is
    // empty rather than doing nothing (NVDA: NO_SETTINGS_MSG).
    SettingsRing(ISynth* synth = nullptr, config::Manager* config = nullptr);

    // Rebuilds the ring for a new driver, keeping the selected setting if the
    // new driver has it. Called by speech::Service when the synthesizer is
    // replaced -- otherwise the ring would be pointing into the old driver.
    void setSynth(ISynth* synth);
    ISynth* synth() const noexcept { return synth_; }

    bool empty() const noexcept { return settings_.empty(); }
    std::size_t size() const noexcept { return settings_.size(); }

    // Which setting is selected, and its value as it would be spoken. All
    // three answer nothing when the ring is empty.
    std::optional<Setting> current() const noexcept;
    std::string currentName() const;
    std::string currentValue() const;

    // Move around the ring. Returns the new setting's spoken name, or nothing
    // when the ring is empty.
    std::optional<std::string> next();
    std::optional<std::string> previous();

    // Change the selected setting. Returns its new value as it would be
    // spoken, or nothing when the ring is empty.
    //
    // `increaseLarge`/`decreaseLarge` move by `kLargeStep * 2`, which is
    // upstream's arithmetic and not a typo (see the file comment).
    std::optional<std::string> increase();
    std::optional<std::string> decrease();
    std::optional<std::string> increaseLarge();
    std::optional<std::string> decreaseLarge();
    std::optional<std::string> first();
    std::optional<std::string> last();

    // NVDA's NumericDriverSetting defaults, on ISynth's own 0-100 scale.
    static constexpr int kMinimum = 0;
    static constexpr int kMaximum = 100;
    static constexpr int kNormalStep = 5;
    static constexpr int kLargeStep = 10;
    // The VOICE steps by one, because its "value" is a position in the
    // driver's voice list and a step of five would skip four voices. Upstream
    // makes the same distinction (`SynthSetting.step` is `normalStep` only for
    // a NumericDriverSetting, and 1 for everything else) -- and it is the
    // difference between a key that walks through the voices and one that
    // jumps past the one the user wanted.
    static constexpr int kVoiceStep = 1;

private:
    // Module, voice and variant are chosen by their position in a list, so
    // the ring steps through them with the same arithmetic as a number.
    static bool isList(Setting setting);
    // Re-reads the module's voices into catalog_.
    void readVoices();
    // After a module change: a voice of the same language as before if the
    // new module has one, else the first (the interface language's come
    // first in the catalog).
    void chooseVoiceForNewModule(const std::string& previousLanguage);
    std::string currentBase() const;
    std::string currentVariant() const;
    int valueOf(Setting setting) const;
    int maximumOf(Setting setting) const;
    int stepOf(Setting setting) const;
    void applyValue(Setting setting, int value);
    std::string reportValue(Setting setting) const;
    // `steps` is +1 or -1; `large` picks the large step.
    std::optional<std::string> moveBy(int steps, bool large);
    void rebuild(std::optional<Setting> keep);
    void remember(Setting setting);

    ISynth* synth_ = nullptr;
    config::Manager* config_ = nullptr;
    std::vector<Setting> settings_;
    std::size_t index_ = 0;
    // The driver's modules and voices, read when the ring is built: stepping
    // through them must not re-ask the driver on every key press (speech-
    // dispatcher answers over a socket). Re-read when the ring changes the
    // module, which is the one thing that changes the voice list.
    std::vector<std::string> modules_;
    VoiceCatalog catalog_;
};

}  // namespace alus::speech
