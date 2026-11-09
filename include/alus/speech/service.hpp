// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The speech subsystem as one restartable unit.
//
// Rule 3 of the phase plan: EVERY SUBSYSTEM IS RESTARTABLE IN PROCESS (NVDA's
// resetConfiguration model). A user who changes synthesizer, voice or symbol
// level must not have to restart the screen reader, and a user who cannot see
// cannot tell a hung screen reader from a silent one — so the restart path is
// built now, with the subsystem, not bolted on later.
//
// This is the composition root for speech: it owns the driver, the symbol
// registry, the dictionaries, the manager and the presenter, builds them from
// the configuration, and takes them down in the right order.
#pragma once

#include "alus/config/config.hpp"
#include "alus/speech/dictionaries.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/speech/settings_ring.hpp"
#include "alus/speech/synth.hpp"
#include "alus/text/symbols.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace alus::core {
class Loop;
}

namespace alus::speech {

// At namespace scope, not nested in Service, for the same reason LoopConfig is:
// a nested class's default member initializers are not usable in a default
// argument of the enclosing class.
struct ServiceOptions {
    // `--no-speech`: everything runs, nothing is heard. The pipeline must still
    // complete utterances or the queue would fill up, which is why this is
    // NullSynth and not a null pointer.
    bool noSpeech = false;
    // Empty means: $ALUS_DATA_DIR, then the installed data directory, then
    // `data/` beside the working directory (a build tree).
    std::filesystem::path dataDir;
    // Open the audio device for tones and sounds.
    bool enableAudio = true;
};

class Service final : public config::Subsystem {
public:
    using Options = ServiceOptions;

    Service(core::Loop& loop, config::Manager& config, ServiceOptions options = {});
    ~Service() override;

    std::string_view name() const override { return "speech"; }
    void initialize() override;
    void terminate() override;

    bool isRunning() const noexcept { return running_; }

    // Null before initialize() and after terminate().
    SpeechManager* manager() noexcept { return manager_.get(); }
    Presenter* presenter() noexcept { return presenter_.get(); }
    ISynth* synth() noexcept { return synth_.get(); }
    // The synthesizer settings ring (alus+control+arrows). Null before
    // initialize() and after terminate(), exactly as the three above.
    SettingsRing* settingsRing() noexcept { return ring_.get(); }
    text::SymbolRegistry& symbols() noexcept { return symbols_; }
    Dictionaries& dictionaries() noexcept { return dictionaries_; }

    // Where the built-in data was found.
    const std::filesystem::path& dataDirectory() const noexcept { return dataDir_; }

    // The default configuration keys for the [speech] section. Registered by
    // the daemon before load(), so an empty configuration file still works.
    static void registerDefaults(config::Manager& config);

    // Re-reads the settings that can change WITHOUT rebuilding the synthesizer:
    // rate, pitch, volume, inflection, voice, and everything the presenter
    // reads. Phase 9's settings window calls it, because changing the speech
    // rate must not take the braille display down with a full
    // resetConfiguration (rule 3 of 02-FAZE.md, read narrowly).
    //
    // Changing `synth` or `outputModule` is NOT applied here -- those need the
    // driver replaced -- and the function says so in the log, so a user who
    // changed the module and heard nothing change has an answer.
    void applyConfiguration();

    // Resolves the data directory by the rule in Options::dataDir.
    static std::filesystem::path resolveDataDirectory(
        const std::filesystem::path& preferred = {});

private:
    std::unique_ptr<ISynth> createSynth(const std::string& name);
    bool applyVoiceFromConfiguration();

    core::Loop& loop_;
    config::Manager& config_;
    ServiceOptions options_;
    std::filesystem::path dataDir_;

    text::SymbolRegistry symbols_;
    Dictionaries dictionaries_;
    std::unique_ptr<ISynth> synth_;
    std::unique_ptr<SpeechManager> manager_;
    std::unique_ptr<Presenter> presenter_;
    std::unique_ptr<SettingsRing> ring_;
    bool running_ = false;
};

}  // namespace alus::speech
