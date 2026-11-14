// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The synthesizer interface.
//
// See architecture.md §4.5. Two implementations are planned; one exists.
// SsipSynth (speech-dispatcher) is the default, decided by measurement in
// spikes/RESULTS.md S2: 11.5 ms p95 to first audio, 29.0 ms p95 to silence
// after cancel, index marks accurate to 46 ms p95 against a 100 ms budget.
// EspeakDirectSynth is planned for v1.1, where sample-accurate marks matter
// (the speech cursor).
//
// THE PARTS OF THIS CONTRACT THAT MEASUREMENT CHANGED:
//
//  * `IndexCommand` support is MANDATORY (`supportedCommands()` must contain
//    CommandType::Index). Utterance chaining goes through index marks and
//    nothing else.
//  * `doneSpeaking` IS NOT AN AUDIO EVENT. On speech-dispatcher it arrives
//    ~294 ms after the last audible sample (finding N12), so chaining on it
//    would put a third of a second of dead air between every two utterances.
//    The manager uses it only as a latch (profile switches) and as the
//    watchdog that catches a lost index mark (finding N16, risk R14).
//  * `indexMarkOffset()` is not zero and not noise. On this system index marks
//    arrive 40 ms BEFORE their position in the audio, consistently (N13).
//    Whoever synchronises something with the audio — the speech cursor, word
//    tracking — has to compensate.
//  * Timing budgets are a property of the speech-dispatcher MODULE, not of the
//    protocol (N17): rhvoice misses every one of them. A driver reports what it
//    knows through `timingIsGuaranteed()`.
#pragma once

#include "alus/core/extension_points.hpp"
#include "alus/speech/commands.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace alus::speech {

struct VoiceInfo {
    std::string id;        // what setVoice() takes
    std::string name;      // what the user sees
    std::string language;  // BCP 47 ("sr", "en-US")
    std::string variant;
};

enum class SynthFeature {
    Rate,
    Pitch,
    Volume,
    Inflection,
    Voice,
    Language,
    // Honours <break time="..."/>. rhvoice does not (risk R13).
    Breaks,
};

class ISynth {
public:
    ISynth() = default;
    virtual ~ISynth() = default;

    ISynth(const ISynth&) = delete;
    ISynth& operator=(const ISynth&) = delete;

    virtual std::string_view name() const = 0;

    // A LIVENESS CHECK AT STARTUP IS PART OF THE CONTRACT, not an option.
    // Phase 0 found the same "silent success on failure" pattern in three
    // independent subsystems (findings N1, N7, N15/N16), so every external
    // subsystem is asked to prove it is there before ALUS trusts it.
    virtual bool initialize(std::string* error = nullptr) = 0;
    virtual void terminate() = 0;

    // Must contain CommandType::Index.
    virtual std::set<CommandType> supportedCommands() const = 0;
    virtual bool supports(SynthFeature feature) const = 0;

    // Speaks one utterance. Called on the main thread only.
    virtual void speak(const SpeechSequence& utterance) = 0;
    // Stops at once and empties whatever the backend has queued.
    virtual void cancel() = 0;
    virtual void pause(bool switchOn) = 0;

    virtual std::vector<VoiceInfo> availableVoices() const = 0;
    virtual std::string voice() const = 0;
    virtual bool setVoice(const std::string& id) = 0;

    // Output modules, for a driver that fronts several engines (speech-
    // dispatcher). The defaults say "none": a driver with one engine has no
    // module to choose. Changing the module changes availableVoices(); the
    // caller picks a voice afterwards (phase-10.md N126).
    virtual std::vector<std::string> availableModules() const { return {}; }
    virtual std::string outputModule() const { return {}; }
    virtual bool setOutputModule(const std::string& module) {
        (void)module;
        return false;
    }

    // 0–100 at the ALUS level, mapped by the driver onto whatever the backend
    // uses. A driver that does not support a parameter keeps the value anyway,
    // so the setting survives a driver change.
    virtual int rate() const = 0;
    virtual void setRate(int value) = 0;
    virtual int pitch() const = 0;
    virtual void setPitch(int value) = 0;
    virtual int volume() const = 0;
    virtual void setVolume(int value) = 0;
    virtual int inflection() const = 0;
    virtual void setInflection(int value) = 0;

    // How far an index notification precedes its position in the audio.
    // Negative means "the notification comes first" (the measured case).
    virtual std::chrono::milliseconds indexMarkOffset() const {
        return std::chrono::milliseconds{0};
    }

    // False when the backend is one whose latency ALUS has not measured, so
    // callers do not promise the user responsiveness it cannot deliver (N17).
    virtual bool timingIsGuaranteed() const { return false; }

    // Fired when the synthesizer reaches an index, and when it has finished
    // speaking. BOTH MAY FIRE ON ANY THREAD (speech-dispatcher calls back from
    // its reader thread). The SpeechManager marshals them onto the main thread,
    // exactly as NVDA does, so a driver does not have to know about the loop.
    core::Action<ISynth*, int> indexReached;
    core::Action<ISynth*> doneSpeaking;
};

// A synthesizer that says nothing but behaves correctly.
//
// It exists for three reasons: `alus-daemon --no-speech` must still run the
// whole speech pipeline (otherwise the queue would fill and block); the tests
// need a synthesizer whose timing they control; and a machine with no working
// speech-dispatcher must not leave ALUS with a half-initialised manager.
class NullSynth final : public ISynth {
public:
    explicit NullSynth(bool autoComplete = true) : autoComplete_(autoComplete) {}

    std::string_view name() const override { return "null"; }
    bool initialize(std::string* error = nullptr) override;
    void terminate() override {}

    std::set<CommandType> supportedCommands() const override;
    bool supports(SynthFeature feature) const override;

    void speak(const SpeechSequence& utterance) override;
    void cancel() override;
    void pause(bool switchOn) override;

    std::vector<VoiceInfo> availableVoices() const override;
    std::string voice() const override { return voice_; }
    bool setVoice(const std::string& id) override;

    int rate() const override { return rate_; }
    void setRate(int value) override { rate_ = value; }
    int pitch() const override { return pitch_; }
    void setPitch(int value) override { pitch_ = value; }
    int volume() const override { return volume_; }
    void setVolume(int value) override { volume_ = value; }
    int inflection() const override { return inflection_; }
    void setInflection(int value) override { inflection_ = value; }

    // ---- test and diagnostic surface ----

    // Every utterance handed to speak(), in order.
    std::vector<SpeechSequence> utterances() const;
    // The text of each utterance, concatenated — what the user would have heard.
    std::vector<std::string> spokenText() const;
    void clear();
    std::size_t cancelCount() const;
    bool isPaused() const;

    // With autoComplete off, the test decides when an index is reached.
    void setAutoComplete(bool autoComplete) { autoComplete_ = autoComplete; }
    // Fires indexReached, then doneSpeaking if `last` is set. Drives the
    // manager's state machine without any timing dependency.
    void completeIndex(int index, bool last = false);
    // Completes every index of the last utterance, in order.
    void completeLastUtterance();
    std::vector<int> pendingIndexes() const;

private:
    mutable std::mutex mutex_;
    bool autoComplete_;
    std::vector<SpeechSequence> utterances_;
    std::vector<int> pendingIndexes_;
    std::size_t cancels_ = 0;
    bool paused_ = false;
    std::string voice_ = "null";
    int rate_ = 50;
    int pitch_ = 50;
    int volume_ = 100;
    int inflection_ = 50;
};

// The text of an utterance with the commands left out. Used by the drivers, by
// the speech viewer and by tests.
std::string utteranceText(const SpeechSequence& utterance);

}  // namespace alus::speech
