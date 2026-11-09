// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The speech manager: queueing, priorities, indexes, callbacks, profile latch.
//
// Port of NVDA's `speech/manager.py` (773 lines), which 01-ARHITEKTURA.md §4.1
// calls the hardest-won logic in NVDA. The flow, unchanged from NVDA:
//
//   1. speak() → queueSpeechSequence() for the caller's priority
//   2. processSpeechSequence() rewrites the caller's sequence:
//        - every callback becomes an IndexCommand, and the sequence is SPLIT
//          there, so the manager knows exactly what has finished speaking
//        - every utterance is guaranteed to END with an IndexCommand: that
//          final index IS the "utterance finished" signal
//        - a profile trigger is always preceded by an utterance end
//        - parameter changes are REPLAYED after an utterance break
//   3. pushNextSpeech() takes the highest priority queue with pending speech
//   4. buildNextUtterance() assembles one utterance and hands it to the synth
//   5. every index that comes back removes the completed sequence, runs the
//      callback bound to it, and pushes more speech if an utterance ended
//
// Four mechanisms in here are not obvious and are the reason the port is
// faithful rather than reinvented:
//
//  * INDEX WRAPPING. Indexes cycle 1..9999, and a cancel can race an index
//    notification, so "is A before B" uses a half-space window — the TCP
//    sequence number trick (§4.1).
//  * PRIORITIES. NOW interrupts lower priorities but NEVER a previously queued
//    NOW, and the interrupted queue RESUMES. Resuming requires replaying the
//    parameter commands that were in force, or the voice stays stuck at the
//    wrong rate — hence ParamChangeTracker.
//  * STALE ANNOUNCEMENT CANCELLATION. An utterance can carry a
//    CancellableSpeechCommand that is asked, right before the utterance goes to
//    the synthesizer, whether it is still relevant. This is why moving quickly
//    through a list speaks the current item instead of five old ones.
//  * THE PROFILE LATCH. A configuration profile switch waits for the
//    synthesizer to finish, or an utterance would start in voice A and end in
//    voice B.
//
// WHAT PHASE 0 ADDED TO NVDA'S DESIGN (spikes/RESULTS.md, risk R14):
//
//   espeak-ng SILENTLY LOSES an index mark that follows sentence-final
//   punctuation when the clause before it is long enough (findings N15/N16).
//   A manager that waits for an index that never arrives stops speaking
//   altogether, and nothing in the logs says why. Two measures, both here:
//
//     1. The marks the manager relies on are placed at the END of an utterance,
//        which is the form measured to survive. SsipSynth additionally splits a
//        message rather than emitting a mark right after terminal punctuation.
//     2. THE MANAGER HAS DEADLINES. `doneSpeaking` is useless for chaining
//        (it lags the audio by ~294 ms, finding N12) but it is an excellent
//        watchdog: if the synthesizer says it has finished while indexes are
//        still outstanding, the marks were lost. After a short grace period the
//        manager logs a DEBUG_WARNING and carries on. A second, absolute
//        deadline catches a backend that says nothing at all.
#pragma once

#include "alus/core/extension_points.hpp"
#include "alus/speech/commands.hpp"
#include "alus/speech/priority.hpp"
#include "alus/speech/synth.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace alus::core {
class Loop;
}

namespace alus::speech {

// Remembers the parameter commands that are in force, so they can be replayed
// when an utterance is broken up or a preempted queue resumes.
// Port of NVDA's ParamChangeTracker (manager.py:88-113).
class ParamChangeTracker {
public:
    // Only parameter commands (isParamCommand()); anything else is ignored.
    void update(const CommandPtr& command);
    // The commands that differ from the user's configured values, in a stable
    // order (NVDA relies on dict insertion order; here it is command type).
    std::vector<CommandPtr> changed() const;
    void clear() { commands_.clear(); }
    bool empty() const noexcept { return commands_.empty(); }

private:
    std::map<CommandType, CommandPtr> commands_;
};

class SpeechManager {
public:
    static constexpr int kMaxIndex = 9999;
    // Half the index space: beyond this distance, a smaller number is treated
    // as having wrapped rather than as being older.
    static constexpr int kWrapMagnitude = kMaxIndex / 2;

    explicit SpeechManager(core::Loop& loop);
    ~SpeechManager();

    SpeechManager(const SpeechManager&) = delete;
    SpeechManager& operator=(const SpeechManager&) = delete;

    // The manager subscribes to the synthesizer's notifications and marshals
    // them onto the main thread. Passing a different synthesizer drops whatever
    // the old one still had in flight: notifications from a replaced driver are
    // discarded (§4.1).
    void setSynth(ISynth* synth);
    ISynth* synth() const noexcept { return synth_; }

    // The caller's sequence. Main thread only.
    void speak(SpeechSequence sequence, Priority priority = Priority::Normal);
    // Stops everything and forgets every queue (NVDA: cancel()).
    void cancel();
    // Re-checks the cancellable commands of the utterance in flight and drops
    // what is no longer relevant. Called when the focus moves.
    void removeCancelledSpeechCommands();

    bool isSpeaking() const noexcept { return !indexesSpeaking_.empty(); }
    bool hasPendingSpeech() const noexcept { return curPriQueue_ != nullptr; }
    std::size_t pendingSequenceCount() const;

    // NVDA's featureFlag cancelExpiredFocusSpeech. On by default.
    void setCancelExpiredFocusSpeech(bool enabled) { cancelExpiredFocusSpeech_ = enabled; }
    bool cancelExpiredFocusSpeech() const noexcept { return cancelExpiredFocusSpeech_; }

    // Risk R14 deadlines. Zero disables a deadline (tests that drive every
    // index by hand).
    void setLostIndexGrace(std::chrono::milliseconds grace) { lostIndexGrace_ = grace; }
    std::chrono::milliseconds lostIndexGrace() const noexcept { return lostIndexGrace_; }
    void setStallTimeout(std::chrono::milliseconds timeout) { stallTimeout_ = timeout; }
    std::chrono::milliseconds stallTimeout() const noexcept { return stallTimeout_; }
    // How many indexes the manager had to give up on. Nonzero means the
    // backend is losing marks; the log says which index.
    std::uint64_t lostIndexCount() const noexcept { return lostIndexes_; }

    // NVDA: speech.extensions.pre_speechQueued.
    core::Action<const SpeechSequence&, Priority>& preSpeechQueued() noexcept {
        return preSpeechQueued_;
    }
    // NVDA: synthDriverHandler.pre_synthSpeak.
    core::Action<const SpeechSequence&>& preSynthSpeak() noexcept { return preSynthSpeak_; }

    // Was `a` created before `b`, given that indexes wrap at kMaxIndex?
    static bool isIndexABeforeB(int a, int b);
    static bool isIndexAAfterB(int a, int b);

    // Diagnostics for tests: the indexes the synthesizer has not reported yet.
    std::vector<int> indexesSpeaking() const { return indexesSpeaking_; }

private:
    struct PriorityQueue {
        Priority priority = Priority::Normal;
        // Split at indexes, so one utterance may span several sequences. A
        // sequence consisting of a single EndUtteranceCommand marks a boundary.
        std::vector<SpeechSequence> pendingSequences;
        std::vector<std::shared_ptr<ProfileTrigger>> enteredProfileTriggers;
        ParamChangeTracker paramTracker;
    };

    int nextIndex();
    void reset();

    // Returns whether speech should be interrupted.
    bool queueSpeechSequence(SpeechSequence sequence, Priority priority);
    void ensureEndUtterance(SpeechSequence& sequence, std::vector<SpeechSequence>& out,
                            SpeechSequence& paramsToReplay, ParamChangeTracker& tracker);
    std::vector<SpeechSequence> processSpeechSequence(const SpeechSequence& in);

    void pushNextSpeech(bool doneSpeaking);
    PriorityQueue* nextPriorityQueue();
    std::optional<SpeechSequence> buildNextUtterance();
    // True: speak it. False: it was cancelled and has been removed from the
    // queue. Nullopt: the utterance has no trailing index, so its completion
    // could never be detected (NVDA raises IndexError here).
    std::optional<bool> checkForCancellations(SpeechSequence& utterance);
    std::optional<int> utteranceIndexOf(const SpeechSequence& utterance) const;
    std::optional<int> mostRecentlyCancelledUtterance() const;
    void doRemoveCancelledSpeechCommands();

    struct RemovalResult {
        bool valid = false;
        bool endOfUtterance = false;
    };
    RemovalResult removeCompletedFromQueue(int index);
    void handleIndex(int index);
    void handleDoneSpeaking();

    void switchProfile();
    void exitProfileTriggers(std::vector<std::shared_ptr<ProfileTrigger>>& triggers);
    void restoreProfileTriggers(std::vector<std::shared_ptr<ProfileTrigger>>& triggers);

    void armStallTimer();
    void cancelStallTimer();
    void armLostIndexTimer();
    void cancelLostIndexTimer();
    // Risk R14: give up on the indexes that will never arrive.
    void giveUpOnOutstandingIndexes(const char* reason);

    core::Loop& loop_;
    ISynth* synth_ = nullptr;
    core::HandlerHandle indexHandle_;
    core::HandlerHandle doneHandle_;
    // Lets a queued notification tell that the manager is gone.
    std::shared_ptr<bool> alive_;

    int nextIndex_ = 0;
    std::map<Priority, PriorityQueue> priQueues_;
    PriorityQueue* curPriQueue_ = nullptr;
    std::map<int, CommandPtr> indexesToCallbacks_;
    std::vector<int> indexesSpeaking_;
    bool shouldPushWhenDoneSpeaking_ = false;
    bool cancelledLastSpeechWithSynth_ = false;
    bool cancelExpiredFocusSpeech_ = true;
    // Identity map, as in NVDA: the same command object may appear twice.
    std::vector<std::pair<std::shared_ptr<CancellableSpeechCommand>, int>> cancellablesInFlight_;

    std::chrono::milliseconds lostIndexGrace_{300};
    std::chrono::milliseconds stallTimeout_{10000};
    std::uint64_t lostIndexes_ = 0;
    std::uint64_t stallTimer_ = 0;
    std::uint64_t lostIndexTimer_ = 0;

    core::Action<const SpeechSequence&, Priority> preSpeechQueued_;
    core::Action<const SpeechSequence&> preSynthSpeak_;
};

}  // namespace alus::speech
