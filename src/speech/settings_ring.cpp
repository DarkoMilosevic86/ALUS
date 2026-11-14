// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/settings_ring.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace alus::speech {
namespace {

// The ring in order. Which of them actually appear is asked of the driver:
// SynthFeature is ISynth's own answer to `availableInSettingsRing`.
constexpr std::array<SettingsRing::Setting, 7> kRingOrder{
    SettingsRing::Setting::Module,     SettingsRing::Setting::Voice,
    SettingsRing::Setting::Variant,    SettingsRing::Setting::Rate,
    SettingsRing::Setting::Pitch,      SettingsRing::Setting::Inflection,
    SettingsRing::Setting::Volume};

// The primary language subtag: "sr-RS" -> "sr".
std::string primaryOf(const std::string& language) {
    return language.substr(0, language.find_first_of("-_.@"));
}

SynthFeature featureFor(SettingsRing::Setting setting) {
    switch (setting) {
        case SettingsRing::Setting::Module:
        case SettingsRing::Setting::Voice:
        case SettingsRing::Setting::Variant:
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
        // (alus+control+arrows): which speech-dispatcher module speaks
        // (espeak-ng, rhvoice, ...).
        case Setting::Module:
            return C_("synth setting", "Output module");
        // Translators: a setting in the synthesizer settings ring
        // (alus+control+arrows): which voice speaks.
        case Setting::Voice:
            return C_("synth setting", "Voice");
        // Translators: a setting in the synthesizer settings ring: the
        // variant of the voice (eSpeak's Adam, Alex, ...).
        case Setting::Variant:
            return C_("synth setting", "Variant");
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
        case Setting::Module:
            return "outputModule";
        case Setting::Voice:
            return "voice";
        case Setting::Variant:
            return "variant";
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
    modules_.clear();
    catalog_ = VoiceCatalog{};
    index_ = 0;
    if (synth_ == nullptr) {
        return;
    }
    if (synth_->supports(SynthFeature::Voice)) {
        readVoices();
    }
    for (const Setting setting : kRingOrder) {
        switch (setting) {
            case Setting::Module:
                modules_ = synth_->availableModules();
                if (modules_.size() < 2) {
                    continue;
                }
                break;
            case Setting::Voice:
                // A driver that offers fewer than two voices is not a ring
                // entry: there is nothing to step to, and an entry that cannot
                // change is worse than no entry -- the user presses the arrow,
                // hears the same voice name back, and has no way to tell that
                // from a key that did not arrive.
                if (!synth_->supports(SynthFeature::Voice) || catalog_.voices().size() < 2) {
                    ALUS_DEBUG("settings ring: {} voice(s) to choose from, so Voice is not in "
                               "the ring",
                               catalog_.voices().size());
                    continue;
                }
                break;
            case Setting::Variant:
                if (!synth_->supports(SynthFeature::Voice) || !catalog_.hasVariants()) {
                    continue;
                }
                break;
            default:
                if (!synth_->supports(featureFor(setting))) {
                    continue;
                }
                break;
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

bool SettingsRing::isList(Setting setting) {
    return setting == Setting::Module || setting == Setting::Voice || setting == Setting::Variant;
}

void SettingsRing::readVoices() {
    catalog_ = VoiceCatalog::build(synth_->availableVoices(), i18n::currentLanguage());
}

std::string SettingsRing::currentBase() const {
    return synth_ != nullptr ? catalog_.split(synth_->voice()).first : std::string{};
}

std::string SettingsRing::currentVariant() const {
    return synth_ != nullptr ? catalog_.split(synth_->voice()).second : std::string{};
}

void SettingsRing::chooseVoiceForNewModule(const std::string& previousLanguage) {
    const std::vector<VoiceInfo>& voices = catalog_.voices();
    if (voices.empty()) {
        return;
    }
    const std::string wanted = primaryOf(previousLanguage);
    auto chosen = voices.begin();
    if (!wanted.empty()) {
        const auto same = std::find_if(voices.begin(), voices.end(), [&wanted](const VoiceInfo& v) {
            return primaryOf(v.language) == wanted;
        });
        if (same != voices.end()) {
            chosen = same;
        }
    }
    if (!synth_->setVoice(chosen->id)) {
        ALUS_WARNING("settings ring: the new module refused the voice '{}'", chosen->id);
    }
}

int SettingsRing::valueOf(Setting setting) const {
    if (synth_ == nullptr) {
        return 0;
    }
    switch (setting) {
        case Setting::Module: {
            const auto it = std::find(modules_.begin(), modules_.end(), synth_->outputModule());
            return it == modules_.end() ? 0 : static_cast<int>(it - modules_.begin());
        }
        case Setting::Voice: {
            // A voice the list does not have (the configuration names one the
            // driver dropped) reads as the first one, so the arrows still work
            // instead of refusing to move.
            const int at = catalog_.indexOfVoice(currentBase());
            return at < 0 ? 0 : at;
        }
        case Setting::Variant: {
            // 0 is "no variant"; the variants follow it.
            const std::string variant = currentVariant();
            return variant.empty() ? 0 : catalog_.indexOfVariant(variant) + 1;
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
    return isList(setting) ? kVoiceStep : kNormalStep;
}

int SettingsRing::maximumOf(Setting setting) const {
    switch (setting) {
        case Setting::Module:
            return modules_.empty() ? 0 : static_cast<int>(modules_.size()) - 1;
        case Setting::Voice:
            return catalog_.voices().empty() ? 0 : static_cast<int>(catalog_.voices().size()) - 1;
        case Setting::Variant:
            return static_cast<int>(catalog_.variants().size());
        default:
            return kMaximum;
    }
}

void SettingsRing::applyValue(Setting setting, int value) {
    if (synth_ == nullptr) {
        return;
    }
    const std::size_t at = static_cast<std::size_t>(std::max(0, value));
    switch (setting) {
        case Setting::Module: {
            if (at >= modules_.size() || modules_[at] == synth_->outputModule()) {
                return;
            }
            // The language of the voice being left, so that going from
            // mbrola_ng's Croatian `cr1` to espeak-ng lands on Croatian and not
            // on Afrikaans.
            std::string language;
            if (const int voice = catalog_.indexOfVoice(currentBase()); voice >= 0) {
                language = catalog_.voices()[static_cast<std::size_t>(voice)].language;
            }
            if (!synth_->setOutputModule(modules_[at])) {
                ALUS_WARNING("settings ring: the driver refused the module '{}'", modules_[at]);
                return;
            }
            readVoices();
            chooseVoiceForNewModule(language);
            remember(Setting::Module);
            remember(Setting::Voice);
            // The new module may have variants where the old had none, or
            // the other way round; the ring stays on Module.
            rebuild(Setting::Module);
            return;
        }
        case Setting::Voice: {
            if (at >= catalog_.voices().size()) {
                return;
            }
            const std::string& base = catalog_.voices()[at].id;
            const std::string variant = currentVariant();
            if (!synth_->setVoice(catalog_.join(base, variant)) &&
                (variant.empty() || !synth_->setVoice(base))) {
                ALUS_WARNING("settings ring: the driver refused the voice '{}'", base);
                return;
            }
            break;
        }
        case Setting::Variant: {
            const std::string variant = at == 0 || at > catalog_.variants().size()
                                            ? std::string{}
                                            : catalog_.variants()[at - 1];
            std::string base = currentBase();
            if (catalog_.indexOfVoice(base) < 0 && !catalog_.voices().empty()) {
                base = catalog_.voices().front().id;
            }
            if (!synth_->setVoice(catalog_.join(base, variant))) {
                ALUS_WARNING("settings ring: the driver refused the variant '{}'", variant);
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
    if (setting == Setting::Module) {
        config_->setString("speech", "outputModule", synth_->outputModule());
        return;
    }
    if (isList(setting)) {
        // Voice and variant are one key: the full id, as the driver takes it.
        config_->setString("speech", "voice", synth_->voice());
        return;
    }
    config_->setInt("speech", settingKey(setting), valueOf(setting));
}

std::string SettingsRing::reportValue(Setting setting) const {
    const std::size_t at = static_cast<std::size_t>(std::max(0, valueOf(setting)));
    switch (setting) {
        case Setting::Module:
            return at < modules_.size() ? modules_[at] : std::string{};
        case Setting::Voice: {
            if (at >= catalog_.voices().size()) {
                return {};
            }
            const VoiceInfo& voice = catalog_.voices()[at];
            // The name, not the id: the id is what the driver takes and can be
            // anything (`rhvoice+Marija`, a file name); the name is what the
            // user was told the voice is called.
            return voice.name.empty() ? voice.id : voice.name;
        }
        case Setting::Variant:
            if (at == 0 || at > catalog_.variants().size()) {
                // Translators: the value of the Variant setting in the
                // synthesizer settings ring when the voice has none.
                return C_("synth setting", "no variant");
            }
            return catalog_.variants()[at - 1];
        default:
            break;
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
