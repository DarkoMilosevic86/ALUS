// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/manager.hpp"

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <algorithm>
#include <utility>

namespace alus::speech {

namespace {

int priorityValue(Priority priority) { return static_cast<int>(priority); }

bool isProfileTriggerSequence(const SpeechSequence& sequence) {
    return !sequence.empty() && sequence.front().is(CommandType::ProfileTrigger);
}

}  // namespace

// ---------------------------------------------------------------------------
// ParamChangeTracker
// ---------------------------------------------------------------------------

void ParamChangeTracker::update(const CommandPtr& command) {
    if (command == nullptr || !command->isParamCommand()) {
        return;
    }
    if (command->isDefault()) {
        // Back to the user's configured value: there is nothing to replay.
        commands_.erase(command->type());
    } else {
        commands_[command->type()] = command;
    }
}

std::vector<CommandPtr> ParamChangeTracker::changed() const {
    std::vector<CommandPtr> out;
    out.reserve(commands_.size());
    for (const auto& [type, command] : commands_) {
        out.push_back(command);
    }
    return out;
}

// ---------------------------------------------------------------------------
// SpeechManager
// ---------------------------------------------------------------------------

SpeechManager::SpeechManager(core::Loop& loop)
    : loop_(loop), alive_(std::make_shared<bool>(true)) {}

SpeechManager::~SpeechManager() {
    // Deregistering first means no NEW notification can be marshalled; the
    // guard below is what covers one that is already in flight on the
    // synthesizer's thread.
    indexHandle_.reset();
    doneHandle_.reset();
    cancelStallTimer();
    cancelLostIndexTimer();
    // `alive_` is destroyed with this object, which is what the weak_ptr in
    // every queued function tests: anything already on the loop finds the
    // manager gone and does nothing.
}

void SpeechManager::setSynth(ISynth* synth) {
    indexHandle_.reset();
    doneHandle_.reset();
    synth_ = synth;
    indexesSpeaking_.clear();
    cancellablesInFlight_.clear();
    if (synth_ == nullptr) {
        return;
    }

    std::weak_ptr<bool> alive = alive_;
    ISynth* registered = synth_;
    // The notification may arrive on the driver's own thread (speech-dispatcher
    // calls back from its reader thread), so everything here is marshalled onto
    // the main thread; the whole manager state lives there (§13).
    //
    // Note what the OUTER lambda is allowed to touch: the weak_ptr (a copy),
    // the two pointers (the loop outlives every manager by construction), and
    // nothing of `*this`. The manager could be destroyed on the main thread
    // between the guard and the queueFunction call, so `self` is dereferenced
    // only in the INNER lambda — which runs on the main thread, where
    // destruction happens too, and whose guard therefore cannot be raced.
    core::Loop* loop = &loop_;
    SpeechManager* self = this;
    indexHandle_ = synth_->indexReached.add(
        [loop, self, alive, registered](ISynth* from, int index) {
            if (alive.expired() || from != registered) {
                return;
            }
            loop->queueFunction(
                [self, alive, registered, index] {
                    // A notification from a driver that has since been replaced
                    // is dropped (§4.1).
                    if (alive.expired() || self->synth_ != registered) {
                        return;
                    }
                    self->handleIndex(index);
                },
                true, "speech-index");
        },
        "speech-manager-index");

    doneHandle_ = synth_->doneSpeaking.add(
        [loop, self, alive, registered](ISynth* from) {
            if (alive.expired() || from != registered) {
                return;
            }
            loop->queueFunction(
                [self, alive, registered] {
                    if (alive.expired() || self->synth_ != registered) {
                        return;
                    }
                    self->handleDoneSpeaking();
                },
                true, "speech-done");
        },
        "speech-manager-done");
}

int SpeechManager::nextIndex() {
    // 1..kMaxIndex, cycling. Indexes are not reused quickly, because a cancel
    // can race an index notification and an index from a previous utterance
    // must not be mistaken for one in this utterance.
    nextIndex_ = (nextIndex_ % kMaxIndex) + 1;
    return nextIndex_;
}

bool SpeechManager::isIndexABeforeB(int a, int b) {
    return a != b && ((a < b && kWrapMagnitude >= b - a) || (b < a && kWrapMagnitude < a - b));
}

bool SpeechManager::isIndexAAfterB(int a, int b) { return a != b && !isIndexABeforeB(a, b); }

std::size_t SpeechManager::pendingSequenceCount() const {
    std::size_t count = 0;
    for (const auto& [priority, queue] : priQueues_) {
        count += queue.pendingSequences.size();
    }
    return count;
}

void SpeechManager::reset() {
    priQueues_.clear();
    curPriQueue_ = nullptr;
    indexesToCallbacks_.clear();
    indexesSpeaking_.clear();
    shouldPushWhenDoneSpeaking_ = false;
    cancellablesInFlight_.clear();
    cancelledLastSpeechWithSynth_ = false;
    cancelStallTimer();
    cancelLostIndexTimer();
}

void SpeechManager::speak(SpeechSequence sequence, Priority priority) {
    if (core::Log::instance().isEnabledFor(core::LogLevel::Io)) {
        // Deferred on purpose: rendering a whole sequence is expensive, and
        // this is the hot path from a key press to speech.
        ALUS_IO("speech queued at priority {}: {}", priorityName(priority), repr(sequence));
    }
    preSpeechQueued_.notify(sequence, priority);

    const bool interrupt = queueSpeechSequence(std::move(sequence), priority);
    doRemoveCancelledSpeechCommands();
    // Nothing in progress, or the synthesizer has nothing of ours in flight.
    bool push = !hasPendingSpeech() || !isSpeaking();
    if (interrupt) {
        ALUS_DEBUG("interrupting speech for a new NOW utterance");
        if (synth_ != nullptr) {
            synth_->cancel();
        }
        indexesSpeaking_.clear();
        cancellablesInFlight_.clear();
        cancelStallTimer();
        cancelLostIndexTimer();
        push = true;
    }
    if (push) {
        pushNextSpeech(true);
    }
}

bool SpeechManager::queueSpeechSequence(SpeechSequence sequence, Priority priority) {
    std::vector<SpeechSequence> processed = processSpeechSequence(sequence);
    PriorityQueue& queue = priQueues_[priority];
    queue.priority = priority;
    const bool first = queue.pendingSequences.empty();
    for (SpeechSequence& part : processed) {
        queue.pendingSequences.push_back(std::move(part));
    }
    // Only the FIRST utterance at NOW interrupts. A NOW utterance queued behind
    // another NOW waits its turn (§4.1) — otherwise two urgent messages would
    // cut each other off and the user would hear neither.
    return priority == Priority::Now && first;
}

void SpeechManager::ensureEndUtterance(SpeechSequence& sequence, std::vector<SpeechSequence>& out,
                                       SpeechSequence& paramsToReplay,
                                       ParamChangeTracker& tracker) {
    if (!sequence.empty()) {
        SpeechSequence combined = paramsToReplay;
        combined.insert(combined.end(), sequence.begin(), sequence.end());
        out.push_back(std::move(combined));
        paramsToReplay.clear();
        sequence.clear();
        // Re-apply the parameters that differ from the user's configuration, so
        // the next utterance of this sequence starts with the same voice.
        for (const CommandPtr& command : tracker.changed()) {
            paramsToReplay.emplace_back(command);
        }
    }
    if (out.empty() || out.back().empty()) {
        // Nothing has been produced yet: an utterance end would be meaningless.
        return;
    }
    SpeechSequence& last = out.back();
    const CommandPtr& lastCommand = last.back().command();
    if (lastCommand != nullptr && (lastCommand->type() == CommandType::EndUtterance ||
                                   lastCommand->type() == CommandType::ProfileTrigger)) {
        // It makes no sense to repeat an utterance end, or to end an utterance
        // immediately after a profile switch.
        return;
    }
    if (lastCommand == nullptr || lastCommand->type() != CommandType::Index) {
        // The index at the end of an utterance is how its completion is
        // detected. Every utterance gets one.
        last.emplace_back(index(nextIndex()));
    }
    out.push_back(SpeechSequence{endUtterance()});
}

std::vector<SpeechSequence> SpeechManager::processSpeechSequence(const SpeechSequence& in) {
    ParamChangeTracker tracker;
    std::vector<std::shared_ptr<ProfileTrigger>> entered;
    std::vector<SpeechSequence> out;
    SpeechSequence paramsToReplay;
    SpeechSequence current;

    const bool synthKnowsLanguage = synth_ == nullptr || synth_->supports(SynthFeature::Language);

    for (const SpeechItem& item : in) {
        if (item.isText()) {
            current.push_back(item);
            continue;
        }
        const CommandPtr& command = item.command();
        if (command == nullptr) {
            continue;
        }

        if (command->isCallback()) {
            // The callback becomes an index, and the sequence is split there:
            // when that index comes back, everything before it has been spoken.
            const int speechIndex = nextIndex();
            current.emplace_back(index(speechIndex));
            indexesToCallbacks_[speechIndex] = command;
            SpeechSequence combined = paramsToReplay;
            combined.insert(combined.end(), current.begin(), current.end());
            out.push_back(std::move(combined));
            paramsToReplay.clear();
            current.clear();
            continue;
        }

        if (command->type() == CommandType::ProfileTrigger) {
            const auto trigger = as<ProfileTriggerCommand>(command);
            if (trigger == nullptr || trigger->trigger() == nullptr ||
                !trigger->trigger()->hasProfile()) {
                continue;  // a trigger with no profile mapped to it is ignored
            }
            const auto found = std::find(entered.begin(), entered.end(), trigger->trigger());
            const bool alreadyEntered = found != entered.end();
            if (trigger->isEnter() && alreadyEntered) {
                ALUS_DEBUG_WARNING("request to enter an already entered trigger: {}",
                                   trigger->trigger()->spec());
                continue;
            }
            if (!trigger->isEnter() && !alreadyEntered) {
                ALUS_DEBUG_WARNING("request to exit a trigger that was not entered: {}",
                                   trigger->trigger()->spec());
                continue;
            }
            ensureEndUtterance(current, out, paramsToReplay, tracker);
            out.push_back(SpeechSequence{command});
            if (trigger->isEnter()) {
                entered.push_back(trigger->trigger());
            } else {
                entered.erase(found);
            }
            continue;
        }

        if (command->type() == CommandType::EndUtterance) {
            ensureEndUtterance(current, out, paramsToReplay, tracker);
            continue;
        }

        if (command->type() == CommandType::SuppressUnicodeNormalization) {
            continue;  // handled by the presenter, before the manager sees it
        }

        if (command->isParamCommand()) {
            if (command->type() == CommandType::LangChange && !synthKnowsLanguage) {
                continue;  // this driver cannot switch language; do not confuse it
            }
            tracker.update(command);
        }
        current.push_back(item);
    }

    ensureEndUtterance(current, out, paramsToReplay, tracker);
    // Exit any trigger the caller left entered, in reverse order.
    for (auto it = entered.rbegin(); it != entered.rend(); ++it) {
        out.push_back(SpeechSequence{std::make_shared<ProfileTriggerCommand>(*it, false)});
    }
    return out;
}

SpeechManager::PriorityQueue* SpeechManager::nextPriorityQueue() {
    for (const Priority priority : kPriorities) {
        const auto it = priQueues_.find(priority);
        if (it == priQueues_.end()) {
            continue;
        }
        if (!it->second.pendingSequences.empty()) {
            return &it->second;
        }
    }
    return nullptr;
}

void SpeechManager::pushNextSpeech(bool doneSpeaking) {
    // NVDA recurses here; the loop is the same thing without the stack.
    for (;;) {
        PriorityQueue* queue = nextPriorityQueue();
        if (queue == nullptr) {
            ALUS_DEBUG("no more speech");
            curPriQueue_ = nullptr;
            return;
        }
        if (curPriQueue_ == nullptr) {
            curPriQueue_ = queue;
        } else if (priorityValue(queue->priority) > priorityValue(curPriQueue_->priority)) {
            // Preempted by higher priority speech.
            if (!curPriQueue_->enteredProfileTriggers.empty()) {
                if (!doneSpeaking) {
                    // The profile latch: wait for the synthesizer to finish.
                    shouldPushWhenDoneSpeaking_ = true;
                    return;
                }
                exitProfileTriggers(curPriQueue_->enteredProfileTriggers);
            }
            curPriQueue_ = queue;
        } else if (priorityValue(queue->priority) < priorityValue(curPriQueue_->priority)) {
            // Resuming a queue that was preempted.
            if (!queue->enteredProfileTriggers.empty()) {
                if (!doneSpeaking) {
                    shouldPushWhenDoneSpeaking_ = true;
                    return;
                }
                restoreProfileTriggers(queue->enteredProfileTriggers);
            }
            curPriQueue_ = queue;
        }

        while (!queue->pendingSequences.empty() &&
               isProfileTriggerSequence(queue->pendingSequences.front())) {
            if (!doneSpeaking) {
                shouldPushWhenDoneSpeaking_ = true;
                return;
            }
            switchProfile();
        }
        if (queue->pendingSequences.empty()) {
            // The last thing in this queue was a profile switch; another queue
            // may be waiting.
            doneSpeaking = true;
            continue;
        }

        std::optional<SpeechSequence> utterance = buildNextUtterance();
        if (!utterance.has_value()) {
            return;
        }
        for (const SpeechItem& item : *utterance) {
            if (const auto command = as<IndexCommand>(item)) {
                indexesSpeaking_.push_back(command->index());
            }
        }
        cancelledLastSpeechWithSynth_ = false;
        if (core::Log::instance().isEnabledFor(core::LogLevel::Io)) {
            ALUS_IO("synth gets: {}", repr(*utterance));
        }
        preSynthSpeak_.notify(*utterance);
        armStallTimer();
        if (synth_ != nullptr) {
            synth_->speak(*utterance);
        }
        return;
    }
}

std::optional<SpeechSequence> SpeechManager::buildNextUtterance() {
    // An utterance may be spread over several sequences (it was split at every
    // index); this assembles one whole utterance for the synthesizer.
    for (;;) {
        if (curPriQueue_ == nullptr) {
            return std::nullopt;
        }
        const std::size_t sequencesBefore = curPriQueue_->pendingSequences.size();
        SpeechSequence utterance;
        // If this utterance was preempted, the parameters that applied before
        // the preemption are applied again.
        for (const CommandPtr& command : curPriQueue_->paramTracker.changed()) {
            utterance.emplace_back(command);
        }
        std::size_t lastAdded = 0;
        bool addedAny = false;
        for (std::size_t i = 0; i < curPriQueue_->pendingSequences.size(); ++i) {
            const SpeechSequence& sequence = curPriQueue_->pendingSequences[i];
            if (!sequence.empty() && sequence.front().is(CommandType::EndUtterance)) {
                break;  // the utterance ends here
            }
            utterance.insert(utterance.end(), sequence.begin(), sequence.end());
            lastAdded = i;
            addedAny = true;
        }

        if (utterance.empty()) {
            return utterance;
        }
        const std::optional<bool> valid = checkForCancellations(utterance);
        if (!valid.has_value()) {
            // No index at the end: the utterance cannot be tracked, so it
            // cannot be spoken. Drop the offending sequences instead of
            // looping forever (NVDA does the same on IndexError).
            ALUS_ERROR("no utterance index; dropping {}", repr(utterance));
            if (addedAny) {
                auto& sequences = curPriQueue_->pendingSequences;
                sequences.erase(sequences.begin(),
                                sequences.begin() + static_cast<std::ptrdiff_t>(lastAdded) + 1);
            } else {
                return std::nullopt;
            }
            continue;
        }
        if (*valid) {
            return utterance;
        }
        // Cancelled: removeCompletedFromQueue() has already dropped it. If it
        // did not shrink the queue, stop rather than spin.
        if (curPriQueue_ == nullptr || curPriQueue_->pendingSequences.size() >= sequencesBefore) {
            return std::nullopt;
        }
    }
}

std::optional<bool> SpeechManager::checkForCancellations(SpeechSequence& utterance) {
    if (!cancelExpiredFocusSpeech_) {
        return true;
    }
    const std::optional<int> utteranceIndex = utteranceIndexOf(utterance);
    if (!utteranceIndex.has_value()) {
        return std::nullopt;
    }
    // Synthesizers know nothing about cancellable commands, so they are taken
    // out of the utterance here.
    std::vector<std::shared_ptr<CancellableSpeechCommand>> cancellables;
    for (auto it = utterance.begin(); it != utterance.end();) {
        if (const auto command = as<CancellableSpeechCommand>(*it)) {
            cancellables.push_back(command);
            it = utterance.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& command : cancellables) {
        if (command->isCancelled()) {
            ALUS_DEBUG("utterance already cancelled; dropping up to index {}", *utteranceIndex);
            removeCompletedFromQueue(*utteranceIndex);
            return false;
        }
        command->setUtteranceIndex(*utteranceIndex);
        cancellablesInFlight_.emplace_back(command, *utteranceIndex);
    }
    return true;
}

std::optional<int> SpeechManager::utteranceIndexOf(const SpeechSequence& utterance) const {
    if (utterance.empty()) {
        return std::nullopt;
    }
    const auto command = as<IndexCommand>(utterance.back());
    if (command == nullptr) {
        ALUS_ERROR("expected the last item of an utterance to be an index: {}", repr(utterance));
        return std::nullopt;
    }
    return command->index();
}

std::optional<int> SpeechManager::mostRecentlyCancelledUtterance() const {
    std::optional<int> latest;
    for (const auto& [command, utteranceIndex] : cancellablesInFlight_) {
        if (!command->isCancelled()) {
            continue;
        }
        if (!latest.has_value() || isIndexABeforeB(*latest, utteranceIndex)) {
            latest = utteranceIndex;
        }
    }
    return latest;
}

void SpeechManager::removeCancelledSpeechCommands() { doRemoveCancelledSpeechCommands(); }

void SpeechManager::doRemoveCancelledSpeechCommands() {
    if (!cancelExpiredFocusSpeech_) {
        return;
    }
    const std::optional<int> latest = mostRecentlyCancelledUtterance();
    if (!latest.has_value()) {
        return;
    }
    // Using the most recently cancelled utterance removes everything older in
    // one go — this is the mechanism that keeps fast navigation from speaking a
    // backlog of stale names.
    ALUS_DEBUG("cancelling expired speech up to index {}", *latest);
    removeCompletedFromQueue(*latest);
    if (synth_ != nullptr) {
        synth_->cancel();
    }
    cancelledLastSpeechWithSynth_ = true;
    cancellablesInFlight_.clear();
    indexesSpeaking_.clear();
    cancelStallTimer();
    cancelLostIndexTimer();
    pushNextSpeech(true);
}

SpeechManager::RemovalResult SpeechManager::removeCompletedFromQueue(int index) {
    if (curPriQueue_ == nullptr) {
        // Nothing in progress: probably an index from an utterance that was
        // cancelled.
        return {};
    }
    auto& sequences = curPriQueue_->pendingSequences;
    std::size_t found = 0;
    bool haveFound = false;
    bool endOfUtterance = false;
    for (std::size_t i = 0; i < sequences.size(); ++i) {
        const SpeechSequence& sequence = sequences[i];
        if (sequence.empty()) {
            continue;
        }
        const auto lastIndex = as<IndexCommand>(sequence.back());
        if (lastIndex == nullptr) {
            continue;
        }
        if (isIndexAAfterB(index, lastIndex->index())) {
            ALUS_DEBUG_WARNING("reached speech index {}, but index {} was never handled", index,
                               lastIndex->index());
            continue;
        }
        if (index != lastIndex->index()) {
            continue;
        }
        found = i;
        haveFound = true;
        endOfUtterance = i + 1 < sequences.size() && !sequences[i + 1].empty() &&
                         sequences[i + 1].front().is(CommandType::EndUtterance);
        if (endOfUtterance) {
            ++found;  // remove the EndUtterance marker as well
        }
        break;
    }
    if (!haveFound) {
        ALUS_DEBUG("unknown index {}; probably from a cancelled utterance", index);
        return {};
    }

    if (endOfUtterance) {
        // These parameters may not apply to the next utterance if it was queued
        // separately. It will carry them again if they still apply.
        curPriQueue_->paramTracker.clear();
    } else {
        // Keep track of the parameters changed so far, in case this utterance
        // is preempted by higher priority speech.
        for (std::size_t i = 0; i <= found; ++i) {
            for (const SpeechItem& item : sequences[i]) {
                if (item.isCommand()) {
                    curPriQueue_->paramTracker.update(item.command());
                }
            }
        }
    }

    if (cancelExpiredFocusSpeech_) {
        for (std::size_t i = 0; i <= found; ++i) {
            for (const SpeechItem& item : sequences[i]) {
                const auto command = as<CancellableSpeechCommand>(item);
                if (command == nullptr) {
                    continue;
                }
                std::erase_if(cancellablesInFlight_,
                              [&command](const auto& entry) { return entry.first == command; });
            }
        }
    }
    sequences.erase(sequences.begin(), sequences.begin() + static_cast<std::ptrdiff_t>(found) + 1);
    return {true, endOfUtterance};
}

void SpeechManager::handleIndex(int index) {
    cancelStallTimer();
    cancelLostIndexTimer();

    // A synthesizer may skip an index when there is no audible text between two
    // of them. Detect that and handle everything that was skipped.
    std::vector<int> toHandle;
    for (const int outstanding : indexesSpeaking_) {
        if (isIndexABeforeB(outstanding, index)) {
            toHandle.push_back(outstanding);
        }
    }
    toHandle.push_back(index);

    bool endOfUtterance = false;
    for (const int current : toHandle) {
        const auto it = std::find(indexesSpeaking_.begin(), indexesSpeaking_.end(), current);
        if (it == indexesSpeaking_.end()) {
            ALUS_DEBUG("unknown index {}; speech was probably cancelled from the main thread",
                       current);
            break;
        }
        indexesSpeaking_.erase(it);
        if (current != index) {
            ALUS_DEBUG_WARNING("handling skipped index {}", current);
        }
        const RemovalResult result = removeCompletedFromQueue(current);
        endOfUtterance = endOfUtterance || result.endOfUtterance;
        if (!result.valid) {
            continue;
        }
        const auto callbackIt = indexesToCallbacks_.find(current);
        if (callbackIt == indexesToCallbacks_.end()) {
            continue;
        }
        const CommandPtr callbackCommand = callbackIt->second;
        indexesToCallbacks_.erase(callbackIt);
        try {
            // On the main thread, inside a pump cycle: this is the contract
            // BeepCommand and the say-all caret tracking rely on.
            callbackCommand->run();
        } catch (...) {
            core::Log::instance().exception("error running a speech callback");
        }
    }

    doRemoveCancelledSpeechCommands();
    if (endOfUtterance && !isSpeaking()) {
        // Even with several indexes handled, speech is pushed once.
        pushNextSpeech(false);
    } else if (isSpeaking()) {
        armStallTimer();
    }
}

void SpeechManager::handleDoneSpeaking() {
    if (isSpeaking()) {
        // Risk R14. `doneSpeaking` lags the audio (finding N12), so by the time
        // it arrives every index of this utterance should have been reported.
        // Outstanding indexes mean the backend lost the marks; after a short
        // grace period the manager gives up on them rather than going quiet
        // forever.
        armLostIndexTimer();
    }
    if (shouldPushWhenDoneSpeaking_) {
        shouldPushWhenDoneSpeaking_ = false;
        pushNextSpeech(true);
    }
}

void SpeechManager::switchProfile() {
    if (curPriQueue_ == nullptr || curPriQueue_->pendingSequences.empty()) {
        return;
    }
    SpeechSequence sequence = std::move(curPriQueue_->pendingSequences.front());
    curPriQueue_->pendingSequences.erase(curPriQueue_->pendingSequences.begin());
    const auto command = sequence.empty() ? nullptr : as<ProfileTriggerCommand>(sequence.front());
    if (command == nullptr || command->trigger() == nullptr) {
        ALUS_ERROR("the first pending command should have been a profile trigger");
        return;
    }
    if (command->isEnter()) {
        try {
            command->trigger()->enter();
        } catch (...) {
            core::Log::instance().exception("error entering a profile trigger");
        }
        curPriQueue_->enteredProfileTriggers.push_back(command->trigger());
    } else {
        try {
            command->trigger()->exit();
        } catch (...) {
            core::Log::instance().exception("error exiting a profile trigger");
        }
        std::erase(curPriQueue_->enteredProfileTriggers, command->trigger());
    }
}

void SpeechManager::exitProfileTriggers(
    std::vector<std::shared_ptr<ProfileTrigger>>& triggers) {
    for (auto it = triggers.rbegin(); it != triggers.rend(); ++it) {
        try {
            (*it)->exit();
        } catch (...) {
            core::Log::instance().exception("error exiting a profile trigger");
        }
    }
    triggers.clear();
}

void SpeechManager::restoreProfileTriggers(
    std::vector<std::shared_ptr<ProfileTrigger>>& triggers) {
    for (const auto& trigger : triggers) {
        try {
            trigger->enter();
        } catch (...) {
            core::Log::instance().exception("error entering a profile trigger");
        }
    }
}

void SpeechManager::cancel() {
    if (synth_ != nullptr) {
        synth_->cancel();
    }
    if (curPriQueue_ != nullptr && !curPriQueue_->enteredProfileTriggers.empty()) {
        exitProfileTriggers(curPriQueue_->enteredProfileTriggers);
    }
    reset();
}

// ---------------------------------------------------------------------------
// Risk R14: deadlines
// ---------------------------------------------------------------------------

void SpeechManager::armStallTimer() {
    cancelStallTimer();
    if (stallTimeout_.count() <= 0) {
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    stallTimer_ = loop_.callLater(
        stallTimeout_,
        [this, alive] {
            if (alive.expired()) {
                return;
            }
            stallTimer_ = 0;
            giveUpOnOutstandingIndexes("no index and no end notification");
        },
        "speech-stall-deadline");
}

void SpeechManager::cancelStallTimer() {
    if (stallTimer_ != 0) {
        loop_.cancelLater(stallTimer_);
        stallTimer_ = 0;
    }
}

void SpeechManager::armLostIndexTimer() {
    cancelLostIndexTimer();
    if (lostIndexGrace_.count() <= 0) {
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    lostIndexTimer_ = loop_.callLater(
        lostIndexGrace_,
        [this, alive] {
            if (alive.expired()) {
                return;
            }
            lostIndexTimer_ = 0;
            giveUpOnOutstandingIndexes("the synthesizer reported it had finished");
        },
        "speech-lost-index-deadline");
}

void SpeechManager::cancelLostIndexTimer() {
    if (lostIndexTimer_ != 0) {
        loop_.cancelLater(lostIndexTimer_);
        lostIndexTimer_ = 0;
    }
}

void SpeechManager::giveUpOnOutstandingIndexes(const char* reason) {
    if (indexesSpeaking_.empty()) {
        return;
    }
    // Handling the newest outstanding index sweeps up everything before it.
    int newest = indexesSpeaking_.front();
    for (const int candidate : indexesSpeaking_) {
        if (isIndexAAfterB(candidate, newest)) {
            newest = candidate;
        }
    }
    lostIndexes_ += indexesSpeaking_.size();
    ALUS_DEBUG_WARNING(
        "index {} never arrived ({}); {} outstanding, continuing without it. "
        "espeak-ng loses a mark that follows sentence-final punctuation (risk R14)",
        newest, reason, indexesSpeaking_.size());
    handleIndex(newest);
}

}  // namespace alus::speech
