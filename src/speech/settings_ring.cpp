// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/settings_ring.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace alus::speech {
namespace {

// The ring in order. Which of them actually appear is asked of the driver:
// SynthFeature is ISynth's own answer to `availableInSettingsRing`.
constexpr std::array<SettingsRing::Setting, 5> kRingOrder{
    SettingsRing::Setting::Voice, SettingsRing::Setting::Rate,
    SettingsRing::Setting::Pitch, SettingsRing::Setting::Inflection,
    SettingsRing::Setting::Volume};

SynthFeature featureFor(SettingsRing::Setting setting) {
    switch (setting) {
        case SettingsRing::Setting::Voice:
            return SynthFeature::Voice;
        case SettingsRing::Setting::Rate:
            return SynthFeature::Rate;
        case SettingsRing::Setting::Pitch:
            return SynthFeature::Pitch;
        case SettingsRing::Setting::Inflection:
            return SynthFeature::Inflection;
        case SettingsRing::Setting::Volume:
            return SynthFeature::Volume;
    }
    return SynthFeature::Rate;
}

int clampTo(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(maximum, value));
}

}  // namespace

std::string SettingsRing::settingName(Setting setting) {
    switch (setting) {
        // Translators: a setting in the synthesizer settings ring
        // (alus+control+arrows): which voice speaks.
        case Setting::Voice:
            return C_("synth setting", "Voice");
        // Translators: a setting in the synthesizer settings ring: how fast
        // the synthesizer speaks.
        case Setting::Rate:
            return C_("synth setting", "Rate");
        // Translators: a setting in the synthesizer settings ring: how high
        // the voice is.
        case Setting::Pitch:
            return C_("synth setting", "Pitch");
        // Translators: a setting in the synthesizer settings ring: how much
        // the voice varies in pitch.
        case Setting::Inflection:
            return C_("synth setting", "Inflection");
        // Translators: a setting in the synthesizer settings ring: how loud
        // the synthesizer is.
        case Setting::Volume:
            return C_("synth setting", "Volume");
    }
    return {};
}

const char* SettingsRing::settingKey(Setting setting) {
    switch (setting) {
        case Setting::Voice:
            return "voice";
        case Setting::Rate:
            return "rate";
        case Setting::Pitch:
            return "pitch";
        case Setting::Inflection:
            return "inflection";
        case Setting::Volume:
            return "volume";
    }
    return "";
}

SettingsRing::SettingsRing(ISynth* synth, config::Manager* config) : config_(config) {
    setSynth(synth);
}

void SettingsRing::setSynth(ISynth* synth) {
    const std::optional<Setting> keep = current();
    synth_ = synth;
    rebuild(keep);
}

void SettingsRing::rebuild(std::optional<Setting> keep) {
    settings_.clear();
    voices_.clear();
    index_ = 0;
    if (synth_ == nullptr) {
        return;
    }
    for (const Setting setting : kRingOrder) {
        if (!synth_->supports(featureFor(setting))) {
            continue;
        }
        // A driver that reports a voice setting and offers fewer than two
        // voices is not a ring entry: there is nothing to step to, and an
        // entry that cannot change is worse than no entry -- the user presses
        // the arrow, hears the same voice name back, and has no way to tell
        // that from a key that did not arrive.
        if (setting == Setting::Voice) {
            voices_ = synth_->availableVoices();
            if (voices_.size() < 2) {
                ALUS_DEBUG(
                    "settings ring: the driver offers {} voice(s), so Voice is not in the ring",
                    voices_.size());
                continue;
            }
        }
        settings_.push_back(setting);
    }
    if (settings_.empty()) {
        ALUS_INFO("settings ring: empty; the driver reports no settings that can be stepped");
        return;
    }
    // Upstream's `initialSettingsRingSetting`: keep what was selected if the
    // new driver has it, else Rate, else the first entry.
    const auto select = [this](Setting setting) {
        const auto it = std::find(settings_.begin(), settings_.end(), setting);
        if (it == settings_.end()) {
            return false;
        }
        index_ = static_cast<std::size_t>(it - settings_.begin());
        return true;
    };
    if (!(keep.has_value() && select(*keep)) && !select(Setting::Rate)) {
        index_ = 0;
    }
}

std::optional<SettingsRing::Setting> SettingsRing::current() const noexcept {
    if (settings_.empty()) {
        return std::nullopt;
    }
    return settings_[index_];
}

bool SettingsRing::isVoice(Setting setting) const { return setting == Setting::Voice; }

int SettingsRing::valueOf(Setting setting) const {
    if (synth_ == nullptr) {
        return 0;
    }
    switch (setting) {
        case Setting::Voice: {
            const std::string id = synth_->voice();
            const auto it = std::find_if(voices_.begin(), voices_.end(),
                                         [&id](const VoiceInfo& v) { return v.id == id; });
            // A voice the list does not have (the configuration names one the
            // driver dropped) reads as the first one, so the arrows still work
            // instead of refusing to move.
            return it == voices_.end() ? 0 : static_cast<int>(it - voices_.begin());
        }
        case Setting::Rate:
            return synth_->rate();
        case Setting::Pitch:
            return synth_->pitch();
        case Setting::Inflection:
            return synth_->inflection();
        case Setting::Volume:
            return synth_->volume();
    }
    return 0;
}

int SettingsRing::stepOf(Setting setting) const {
    return isVoice(setting) ? kVoiceStep : kNormalStep;
}

int SettingsRing::maximumOf(Setting setting) const {
    if (isVoice(setting)) {
        return voices_.empty() ? 0 : static_cast<int>(voices_.size()) - 1;
    }
    return kMaximum;
}

void SettingsRing::applyValue(Setting setting, int value) {
    if (synth_ == nullptr) {
        return;
    }
    switch (setting) {
        case Setting::Voice: {
            const std::size_t at = static_cast<std::size_t>(value);
            if (at >= voices_.size()) {
                return;
            }
            if (!synth_->setVoice(voices_[at].id)) {
                ALUS_WARNING("settings ring: the driver refused the voice '{}'",
                             voices_[at].id);
                return;
            }
            break;
        }
        case Setting::Rate:
            synth_->setRate(value);
            break;
        case Setting::Pitch:
            synth_->setPitch(value);
            break;
        case Setting::Inflection:
            synth_->setInflection(value);
            break;
        case Setting::Volume:
            synth_->setVolume(value);
            break;
    }
    remember(setting);
}

void SettingsRing::remember(Setting setting) {
    if (config_ == nullptr || synth_ == nullptr) {
        return;
    }
    // Into the configuration, NOT to disk. The ring is used while listening to
    // the result, often several presses in a row, and a user who was only
    // trying out a voice must be able to get the old one back with
    // `alus+control+r` -- which is what makes `alus+control+c` (save) a
    // separate command, upstream and here.
    if (isVoice(setting)) {
        config_->setString("speech", "voice", synth_->voice());
        return;
    }
    config_->setInt("speech", settingKey(setting), valueOf(setting));
}

std::string SettingsRing::reportValue(Setting setting) const {
    if (isVoice(setting)) {
        const std::size_t at = static_cast<std::size_t>(valueOf(setting));
        if (at >= voices_.size()) {
            return {};
        }
        const VoiceInfo& voice = voices_[at];
        // The name, not the id: the id is what the driver takes and can be
        // anything (`rhvoice+Marija`, a file name); the name is what the user
        // was told the voice is called.
        return voice.name.empty() ? voice.id : voice.name;
    }
    return std::format("{}", valueOf(setting));
}

std::string SettingsRing::currentName() const {
    const std::optional<Setting> setting = current();
    return setting.has_value() ? settingName(*setting) : std::string{};
}

std::string SettingsRing::currentValue() const {
    const std::optional<Setting> setting = current();
    return setting.has_value() ? reportValue(*setting) : std::string{};
}

std::optional<std::string> SettingsRing::next() {
    if (settings_.empty()) {
        return std::nullopt;
    }
    index_ = (index_ + 1) % settings_.size();
    return currentName();
}

std::optional<std::string> SettingsRing::previous() {
    if (settings_.empty()) {
        return std::nullopt;
    }
    index_ = (index_ + settings_.size() - 1) % settings_.size();
    return currentName();
}

std::optional<std::string> SettingsRing::moveBy(int steps, bool large) {
    const std::optional<Setting> setting = current();
    if (!setting.has_value()) {
        return std::nullopt;
    }
    // Upstream's arithmetic: a large change is largeStep * 2 -- 20 on the
    // 0-100 scale, or 20 voices. Not a typo; see synthSettingsRing.py's
    // `increaseLarge`, whose docstring says "(20 items, 20%)".
    const int delta = steps * (large ? kLargeStep * 2 : stepOf(*setting));
    const int value = clampTo(valueOf(*setting) + delta, kMinimum, maximumOf(*setting));
    applyValue(*setting, value);
    return reportValue(*setting);
}

std::optional<std::string> SettingsRing::increase() { return moveBy(1, /*large=*/false); }

std::optional<std::string> SettingsRing::decrease() { return moveBy(-1, /*large=*/false); }

std::optional<std::string> SettingsRing::increaseLarge() { return moveBy(1, /*large=*/true); }

std::optional<std::string> SettingsRing::decreaseLarge() { return moveBy(-1, /*large=*/true); }

std::optional<std::string> SettingsRing::first() {
    const std::optional<Setting> setting = current();
    if (!setting.has_value()) {
        return std::nullopt;
    }
    applyValue(*setting, kMinimum);
    return reportValue(*setting);
}

std::optional<std::string> SettingsRing::last() {
    const std::optional<Setting> setting = current();
    if (!setting.has_value()) {
        return std::nullopt;
    }
    applyValue(*setting, maximumOf(*setting));
    return reportValue(*setting);
}

}  // namespace alus::speech
