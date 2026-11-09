// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/service.hpp"

#include "alus/audio/wave_player.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/core/paths.hpp"
#include "alus/speech/ssip_synth.hpp"
#include "alus/ui/message.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <utility>

namespace alus::speech {

namespace {

#ifndef ALUS_DATA_DIR
#define ALUS_DATA_DIR ""
#endif

}  // namespace

std::filesystem::path Service::resolveDataDirectory(const std::filesystem::path& preferred) {
    // The search rule itself lives in core::findDataDirectory, because phase 3
    // needs the same directory for the generated command catalogue and two
    // copies of this rule would be two things to get wrong. The marker stays
    // "symbols" here: that is what the speech subsystem cannot do without.
    const std::filesystem::path found = core::findDataDirectory(preferred, "symbols");
    if (found.empty()) {
        // The compiled-in English dictionary still works (symbols_data.cpp), so
        // this is a degradation, not a failure.
        ALUS_WARNING("no ALUS data directory found; falling back to the built-in symbol data");
    }
    return found;
}

void Service::registerDefaults(config::Manager& config) {
    config.setDefault("speech", "synth", "ssip");
    config.setDefault("speech", "voice", "");
    config.setDefault("speech", "outputModule", "");
    config.setDefault("speech", "rate", "50");
    config.setDefault("speech", "pitch", "50");
    config.setDefault("speech", "volume", "100");
    config.setDefault("speech", "inflection", "50");
    config.setDefault("speech", "symbolLevel", "some");
    config.setDefault("speech", "speechMode", "talk");
    config.setDefault("speech", "autoLanguageSwitching", "true");
    config.setDefault("speech", "autoDialectSwitching", "false");
    config.setDefault("speech", "unicodeNormalization", "false");
    config.setDefault("speech", "beepSpeechModePitch", "10000");
    // Finding N13: index marks precede the audio, and by how much depends on
    // the audio device. Measuring it needs an audio tap (spikes/s2-speech.c),
    // which does not belong in a driver, so the measured value is the default
    // and the user's own measurement can override it.
    config.setDefault("speech", "indexMarkOffsetMs", "-40");
    // Risk R14: how long to wait for an index mark that the synthesizer has
    // already stopped speaking past.
    config.setDefault("speech", "lostIndexGraceMs", "300");
    config.setDefault("speech", "stallTimeoutMs", "10000");
}

Service::Service(core::Loop& loop, config::Manager& config, ServiceOptions options)
    : loop_(loop), config_(config), options_(std::move(options)) {}

Service::~Service() {
    if (running_) {
        terminate();
    }
}

std::unique_ptr<ISynth> Service::createSynth(const std::string& name) {
    if (options_.noSpeech || name == "null" || name == "none") {
        ALUS_INFO("speech driver: null (nothing will be heard)");
        return std::make_unique<NullSynth>();
    }
    if (name == "ssip") {
        auto synth = std::make_unique<SsipSynth>();
        synth->setPreferredOutputModule(config_.getString("speech", "outputModule", ""));
        std::string error;
        if (synth->initialize(&error)) {
            return synth;
        }
        // A screen reader that will not start because speech is broken is
        // worse than a silent one: the user can still reach the settings.
        ALUS_ERROR("could not start the speech-dispatcher driver ({}); continuing without speech",
                   error);
        return std::make_unique<NullSynth>();
    }
    ALUS_WARNING("unknown speech driver {}; using none", name);
    return std::make_unique<NullSynth>();
}

void Service::applyConfiguration() {
    if (!running_ || synth_ == nullptr) {
        return;
    }
    synth_->setRate(static_cast<int>(config_.getInt("speech", "rate", 50)));
    synth_->setPitch(static_cast<int>(config_.getInt("speech", "pitch", 50)));
    synth_->setVolume(static_cast<int>(config_.getInt("speech", "volume", 100)));
    synth_->setInflection(static_cast<int>(config_.getInt("speech", "inflection", 50)));
    if (applyVoiceFromConfiguration()) {
        // The voice dictionary belongs to the driver and the voice, so it has
        // to be reloaded when the voice changes.
        dictionaries_.loadVoiceDict(std::string{synth_->name()}, synth_->voice());
    }
    if (presenter_) {
        presenter_->applyConfiguration(config_);
    }

    const std::string wantedSynth = config_.getString("speech", "synth", "ssip");
    const std::string wantedModule = config_.getString("speech", "outputModule", "");
    if (wantedSynth != synth_->name()) {
        ALUS_INFO("speech: synth is now '{}' in the configuration but '{}' is running; "
                  "it takes effect on the next resetConfiguration",
                  wantedSynth, synth_->name());
    } else if (!wantedModule.empty()) {
        ALUS_DEBUG("speech: outputModule '{}' takes effect on the next "
                   "resetConfiguration",
                   wantedModule);
    }
}

// The configured voice, applied to the running driver, with every way it can
// fail SAID rather than swallowed.
//
// It exists because `voice=` is the one speech setting whose value ALUS cannot
// make up: it is a name the synthesizer chose, and the only places a user can
// have got it from are `alus-cli list-voices` and the settings window. A name
// that does not match anything -- a voice uninstalled since, a typo, or the
// `voice=\s` that an empty text entry wrote -- used to be handed to the driver
// anyway, silently (see SsipSynth::setVoice).
//
// Returns true when the voice actually changed, which is what tells the caller
// to reload the voice dictionary.
bool Service::applyVoiceFromConfiguration() {
    if (synth_ == nullptr) {
        return false;
    }
    const std::string wanted = config_.getString("speech", "voice", "");
    if (wanted.empty()) {
        // Nothing configured: whatever the driver starts with, which is the
        // user's speech-dispatcher default and a perfectly good answer.
        return false;
    }
    if (wanted == synth_->voice()) {
        return false;
    }
    const std::vector<VoiceInfo> voices = synth_->availableVoices();
    const auto match = std::find_if(voices.begin(), voices.end(), [&wanted](const VoiceInfo& v) {
        return v.id == wanted || v.name == wanted;
    });
    if (match == voices.end()) {
        std::string offered;
        for (const VoiceInfo& voice : voices) {
            if (!offered.empty()) {
                offered += ", ";
            }
            offered += voice.name.empty() ? voice.id : voice.name;
        }
        const std::string current = synth_->voice();
        ALUS_WARNING(
            "the configured voice '{}' is not one of the {} the driver offers ({}); keeping {}. "
            "Pick one with `alus-cli list-voices`, in the settings window, or with "
            "alus+control+rightArrow",
            wanted, voices.size(), offered.empty() ? "none at all" : offered,
            current.empty() ? std::string{"the driver's own default"}
                            : std::format("'{}'", current));
        return false;
    }
    if (!synth_->setVoice(match->id)) {
        ALUS_WARNING("the driver refused the configured voice '{}'", match->id);
        return false;
    }
    ALUS_INFO("voice: {}", synth_->voice());
    return true;
}

void Service::initialize() {
    if (running_) {
        return;
    }
    dataDir_ = resolveDataDirectory(options_.dataDir);
    symbols_.setDataDirectory(dataDir_.empty() ? std::filesystem::path{}
                                               : dataDir_ / "symbols");
    symbols_.setUserDirectory(config_.paths().configDir);
    dictionaries_.setDataDirectory(dataDir_.empty() ? std::filesystem::path{}
                                                     : dataDir_ / "dictionaries");
    dictionaries_.setUserDirectory(config_.paths().configDir);
    dictionaries_.load();

    if (options_.enableAudio && !audio::isInitialized()) {
        audio::initialize(&loop_);
    }

    synth_ = createSynth(config_.getString("speech", "synth", "ssip"));
    synth_->setRate(static_cast<int>(config_.getInt("speech", "rate", 50)));
    synth_->setPitch(static_cast<int>(config_.getInt("speech", "pitch", 50)));
    synth_->setVolume(static_cast<int>(config_.getInt("speech", "volume", 100)));
    synth_->setInflection(static_cast<int>(config_.getInt("speech", "inflection", 50)));
    applyVoiceFromConfiguration();
    if (auto* ssip = dynamic_cast<SsipSynth*>(synth_.get())) {
        ssip->setIndexMarkOffset(
            std::chrono::milliseconds{config_.getInt("speech", "indexMarkOffsetMs", -40)});
    }
    // The voice dictionary belongs to the driver and the voice, so it is loaded
    // after both are known.
    dictionaries_.loadVoiceDict(std::string{synth_->name()}, synth_->voice());

    manager_ = std::make_unique<SpeechManager>(loop_);
    manager_->setLostIndexGrace(
        std::chrono::milliseconds{config_.getInt("speech", "lostIndexGraceMs", 300)});
    manager_->setStallTimeout(
        std::chrono::milliseconds{config_.getInt("speech", "stallTimeoutMs", 10000)});
    manager_->setSynth(synth_.get());

    presenter_ = std::make_unique<Presenter>(*manager_, symbols_, dictionaries_);
    presenter_->applyConfiguration(config_);
    ui::initialize(presenter_.get());

    // The settings ring, built from the driver that is running: which settings
    // it holds is the driver's answer, not a constant (SettingsRing).
    ring_ = std::make_unique<SettingsRing>(synth_.get(), &config_);

    running_ = true;
    ALUS_INFO("speech ready: driver {}, symbol level {}", synth_->name(),
              text::symbolLevelName(presenter_->settings().symbolLevel));
}

void Service::terminate() {
    if (!running_) {
        return;
    }
    running_ = false;
    ui::terminate();
    if (manager_ != nullptr) {
        // Stop talking before the driver goes away, or speech-dispatcher keeps
        // speaking into a closed connection.
        manager_->cancel();
        manager_->setSynth(nullptr);
    }
    ring_.reset();
    presenter_.reset();
    manager_.reset();
    if (synth_ != nullptr) {
        synth_->terminate();
        synth_.reset();
    }
    symbols_.invalidateAll();
}

}  // namespace alus::speech
