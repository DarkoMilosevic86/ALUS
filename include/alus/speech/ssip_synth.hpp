// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The speech-dispatcher driver. ALUS's default synthesizer.
//
// See 01-ARHITEKTURA.md §4.5. The choice is measured, not assumed
// (spikes/RESULTS.md S2, decision (b)): through SSIP with the espeak-ng module,
// speech starts 11.5 ms after speak() at p95 and stops 29.0 ms after cancel()
// at p95, against budgets of 50 and 30 ms. And it gives the user every voice
// they have installed, which a direct espeak-ng driver cannot.
//
// FIVE THINGS THIS DRIVER DOES BECAUSE PHASE 0 MEASURED THEM:
//
//  1. ALUS OWNS THE QUEUE, NOT SPEECH-DISPATCHER (risk R4). One utterance is in
//     flight at a time, at one SSIP priority. The manager decides what comes
//     next; the server is not allowed to hold a backlog ALUS cannot inspect or
//     cancel precisely.
//  2. `begin` AND `end` ARE NOT AUDIO EVENTS. `begin` arrives ~7.5 ms BEFORE
//     the first sample (finding N10) and `end` ~294 ms AFTER the last one
//     (N12). They are protocol acknowledgements. Chaining goes through index
//     marks; `doneSpeaking` is reported, but the manager uses it as a latch and
//     a watchdog, never as "the audio has finished".
//  3. AN INDEX MARK IS NEVER EMITTED DIRECTLY AFTER SENTENCE-FINAL
//     PUNCTUATION (findings N15/N16, risk R14). espeak-ng silently drops such
//     a mark when the clause before it is long enough. Where that would happen,
//     the utterance is split into separate SSIP messages so that the mark sits
//     at the END of its message — the one shape measured to survive. The
//     messages are sent back to back, so the server chains them itself and
//     ALUS does not wait for a round trip.
//  4. THE INDEX MARK OFFSET IS A SHIFT, NOT NOISE (finding N13). Marks arrive
//     ~40 ms before their position in the audio, consistently, and the amount
//     depends on the audio device. `indexMarkOffset()` reports it so the speech
//     cursor (phase 5) can compensate.
//  5. TIMING BUDGETS BELONG TO THE MODULE, NOT TO SSIP (finding N17). rhvoice
//     misses every budget and ignores <break>. `timingIsGuaranteed()` is true
//     only for the module ALUS has measured.
//
// THREADING: every method here is called from the MAIN THREAD, except the four
// notification entry points at the bottom, which libspeechd calls from its own
// reader thread. Those forward straight to the ISynth signals, and the
// SpeechManager marshals them onto the main thread.
//
// libspeechd's callbacks are C function pointers with no user data, so the
// instance has to be reachable from a file-static slot; there is therefore at
// most ONE connected SsipSynth per process. A screen reader needs exactly one,
// so this is a limitation on paper only — but it is enforced rather than
// assumed, and a second connection is logged.
#pragma once

#include "alus/speech/synth.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace alus::speech {

// One SSIP message: SSML plus the marks it carries, in order.
struct SsipMessage {
    std::string ssml;
    std::vector<int> marks;
};

class SsipSynth final : public ISynth {
public:
    // `clientName` appears in speech-dispatcher's logs and in its per-client
    // configuration, so it is part of the user's interface.
    explicit SsipSynth(std::string clientName = "alus", std::string connectionName = "core");
    ~SsipSynth() override;

    std::string_view name() const override { return "ssip"; }

    // Opens the connection, turns on SSML mode and the notifications, and THEN
    // ASKS THE SERVER FOR ITS VOICES. The last step is the liveness check every
    // external subsystem owes ALUS (the "silent success on failure" rule from
    // spikes/RESULTS.md): a connection that cannot answer that is not usable,
    // and ALUS must say so at startup rather than go quiet later.
    bool initialize(std::string* error = nullptr) override;
    void terminate() override;
    bool isConnected() const;

    std::set<CommandType> supportedCommands() const override;
    bool supports(SynthFeature feature) const override;

    void speak(const SpeechSequence& utterance) override;
    void cancel() override;
    void pause(bool switchOn) override;

    std::vector<VoiceInfo> availableVoices() const override;
    std::string voice() const override;
    bool setVoice(const std::string& id) override;

    int rate() const override { return rate_; }
    void setRate(int value) override;
    int pitch() const override { return pitch_; }
    void setPitch(int value) override;
    int volume() const override { return volume_; }
    void setVolume(int value) override;
    int inflection() const override { return inflection_; }
    void setInflection(int value) override;

    std::chrono::milliseconds indexMarkOffset() const override { return indexMarkOffset_; }
    void setIndexMarkOffset(std::chrono::milliseconds offset) { indexMarkOffset_ = offset; }

    bool timingIsGuaranteed() const override;
    // The speech-dispatcher output module in use ("espeak-ng", "rhvoice", ...).
    std::string outputModule() const override { return outputModule_; }
    // Applied by initialize(), BEFORE it judges whether the timing budgets
    // hold: the user's configured module is the one to report on, not whatever
    // the server happened to default to.
    void setPreferredOutputModule(std::string module) {
        preferredModule_ = std::move(module);
    }
    bool setOutputModule(const std::string& module) override;
    std::vector<std::string> availableModules() const override;

    // Exposed for tests: the SSML messages an utterance turns into, without a
    // connection. This is where measure 3 above lives, so it is tested
    // directly.
    static std::vector<SsipMessage> buildMessages(const SpeechSequence& utterance,
                                                  int baseRate = 50, int basePitch = 50,
                                                  int baseVolume = 100);
    // True when a mark placed at the end of `text` would be at risk of being
    // lost by espeak-ng (findings N15/N16).
    static bool markWouldFollowSentenceEnd(std::string_view text);

    // Called from libspeechd's reader thread.
    void onIndexMark(const char* mark);
    void onEnd();
    void onCancel();
    void onBegin();

private:
    void applyAllParameters();

    std::string clientName_;
    std::string connectionName_;
    // libspeechd's SPDConnection is a typedef of an anonymous struct, so it
    // cannot be forward declared; the .cpp casts this.
    void* connection_ = nullptr;
    std::string outputModule_;
    std::string preferredModule_;
    std::string voice_;
    int rate_ = 50;
    int pitch_ = 50;
    int volume_ = 100;
    int inflection_ = 50;
    // The measured shift on the target system (finding N13). Negative: the
    // notification precedes the audio.
    std::chrono::milliseconds indexMarkOffset_{-40};
    mutable std::mutex mutex_;
};

}  // namespace alus::speech
