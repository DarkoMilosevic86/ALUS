// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/synth.hpp"

#include "alus/core/log.hpp"

namespace alus::speech {

std::string utteranceText(const SpeechSequence& utterance) {
    std::string text;
    for (const SpeechItem& item : utterance) {
        if (item.isText()) {
            text += item.text();
        }
    }
    return text;
}

bool NullSynth::initialize(std::string* error) {
    (void)error;
    return true;
}

std::set<CommandType> NullSynth::supportedCommands() const {
    // Index is mandatory for every driver (§4.5).
    return {CommandType::Index, CommandType::CharacterMode, CommandType::LangChange,
            CommandType::Break, CommandType::Pitch,         CommandType::Rate,
            CommandType::Volume};
}

bool NullSynth::supports(SynthFeature feature) const {
    switch (feature) {
        case SynthFeature::Rate:
        case SynthFeature::Pitch:
        case SynthFeature::Volume:
        case SynthFeature::Inflection:
        case SynthFeature::Voice:
        case SynthFeature::Language:
        case SynthFeature::Breaks:
            return true;
    }
    return false;
}

void NullSynth::speak(const SpeechSequence& utterance) {
    std::vector<int> indexes;
    {
        const std::lock_guard lock{mutex_};
        utterances_.push_back(utterance);
        for (const SpeechItem& item : utterance) {
            if (const auto command = as<IndexCommand>(item)) {
                pendingIndexes_.push_back(command->index());
                indexes.push_back(command->index());
            }
        }
        if (!autoComplete_) {
            return;
        }
        pendingIndexes_.clear();
    }
    // Notified outside the lock: a handler is allowed to call back in (the
    // manager pushes the next utterance from inside its index handler).
    for (std::size_t i = 0; i < indexes.size(); ++i) {
        indexReached.notify(this, indexes[i]);
    }
    doneSpeaking.notify(this);
}

void NullSynth::cancel() {
    const std::lock_guard lock{mutex_};
    ++cancels_;
    pendingIndexes_.clear();
}

void NullSynth::pause(bool switchOn) {
    const std::lock_guard lock{mutex_};
    paused_ = switchOn;
}

std::vector<VoiceInfo> NullSynth::availableVoices() const {
    return {VoiceInfo{"null", "No speech", "", ""}};
}

bool NullSynth::setVoice(const std::string& id) {
    const std::lock_guard lock{mutex_};
    voice_ = id;
    return true;
}

std::vector<SpeechSequence> NullSynth::utterances() const {
    const std::lock_guard lock{mutex_};
    return utterances_;
}

std::vector<std::string> NullSynth::spokenText() const {
    const std::lock_guard lock{mutex_};
    std::vector<std::string> texts;
    texts.reserve(utterances_.size());
    for (const SpeechSequence& utterance : utterances_) {
        texts.push_back(utteranceText(utterance));
    }
    return texts;
}

void NullSynth::clear() {
    const std::lock_guard lock{mutex_};
    utterances_.clear();
    pendingIndexes_.clear();
    cancels_ = 0;
}

std::size_t NullSynth::cancelCount() const {
    const std::lock_guard lock{mutex_};
    return cancels_;
}

bool NullSynth::isPaused() const {
    const std::lock_guard lock{mutex_};
    return paused_;
}

void NullSynth::completeIndex(int index, bool last) {
    {
        const std::lock_guard lock{mutex_};
        for (auto it = pendingIndexes_.begin(); it != pendingIndexes_.end(); ++it) {
            if (*it == index) {
                pendingIndexes_.erase(it);
                break;
            }
        }
    }
    indexReached.notify(this, index);
    if (last) {
        doneSpeaking.notify(this);
    }
}

void NullSynth::completeLastUtterance() {
    std::vector<int> indexes;
    {
        const std::lock_guard lock{mutex_};
        if (utterances_.empty()) {
            return;
        }
        for (const SpeechItem& item : utterances_.back()) {
            if (const auto command = as<IndexCommand>(item)) {
                indexes.push_back(command->index());
            }
        }
    }
    for (std::size_t i = 0; i < indexes.size(); ++i) {
        completeIndex(indexes[i], i + 1 == indexes.size());
    }
}

std::vector<int> NullSynth::pendingIndexes() const {
    const std::lock_guard lock{mutex_};
    return pendingIndexes_;
}

}  // namespace alus::speech
