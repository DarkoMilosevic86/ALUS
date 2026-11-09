// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/sentence_buffer.hpp"

#include "alus/core/log.hpp"
#include "alus/text/regex.hpp"

#include <utility>

namespace alus::speech {

namespace {

// NVDA: SpeechWithoutPauses.re_last_pause.
//
//   ^(.*(?<=[^\s.!?])[.!?]["'”’)]?(?:\s+|$))(.*$)
//
// Group 1 is everything up to and including the LAST sentence end (greedy
// `.*`), group 2 is the incomplete tail. The lookbehind stops "..." and " ."
// from counting as a sentence end, and the optional closing quote keeps
// `He said "go."` in one piece.
constexpr std::string_view kLastPausePattern =
    "^(.*(?<=[^\\s.!?])[.!?][\"'”’)]?(?:\\s+|$))(.*$)";

}  // namespace

SentenceBuffer::SentenceBuffer(SpeakFunction speak) : speak_(std::move(speak)) {
    lastPause_ = std::make_unique<text::Regex>();
    std::string error;
    if (!lastPause_->compile(kLastPausePattern, text::RegexOption::DotAll, &error)) {
        ALUS_ERROR("could not compile the sentence boundary pattern: {}", error);
    }
}

SentenceBuffer::~SentenceBuffer() = default;

void SentenceBuffer::reset() { pending_.clear(); }

bool SentenceBuffer::speakWithoutPauses(const SpeechSequence& sequence, bool detectBreaks) {
    bool spoke = false;
    const std::vector<SpeechSequence> parts =
        getSpeechWithoutPauses(sequence, detectBreaks, &spoke);
    for (const SpeechSequence& part : parts) {
        if (speak_) {
            speak_(part);
        }
    }
    return spoke;
}

bool SentenceBuffer::flush() {
    bool spoke = false;
    const std::vector<SpeechSequence> parts = flushSequences(&spoke);
    for (const SpeechSequence& part : parts) {
        if (speak_) {
            speak_(part);
        }
    }
    return spoke;
}

std::vector<SpeechSequence> SentenceBuffer::flushSequences(bool* spoke) {
    std::vector<SpeechSequence> out;
    SpeechSequence pending = flushPending();
    const bool any = !pending.empty();
    if (any) {
        out.push_back(std::move(pending));
    }
    if (spoke != nullptr) {
        *spoke = any;
    }
    return out;
}

std::vector<SpeechSequence> SentenceBuffer::getSpeechWithoutPauses(const SpeechSequence& sequence,
                                                                   bool detectBreaks,
                                                                   bool* spoke) {
    if (detectBreaks && !sequence.empty()) {
        return detectBreaksAndGetSpeech(sequence, spoke);
    }
    std::vector<SpeechSequence> out;
    SpeechSequence speech = getSpeech(sequence);
    const bool any = !speech.empty();
    if (any) {
        out.push_back(std::move(speech));
    }
    if (spoke != nullptr) {
        *spoke = any;
    }
    return out;
}

std::vector<SpeechSequence> SentenceBuffer::detectBreaksAndGetSpeech(
    const SpeechSequence& sequence, bool* spoke) {
    std::vector<SpeechSequence> out;
    bool gotValidSpeech = false;
    std::size_t lastStart = 0;
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        if (!sequence[i].is(CommandType::EndUtterance)) {
            continue;
        }
        if (i > 0 && lastStart < i) {
            const SpeechSequence sub{sequence.begin() + static_cast<std::ptrdiff_t>(lastStart),
                                     sequence.begin() + static_cast<std::ptrdiff_t>(i)};
            SpeechSequence speech = getSpeech(sub);
            if (!speech.empty()) {
                out.push_back(std::move(speech));
            }
        }
        // An explicit break flushes what is held: the caller asked for the
        // utterance to end here.
        SpeechSequence pending = flushPending();
        if (!pending.empty()) {
            out.push_back(std::move(pending));
        }
        gotValidSpeech = true;
        lastStart = i + 1;
    }
    if (lastStart < sequence.size()) {
        const SpeechSequence sub{sequence.begin() + static_cast<std::ptrdiff_t>(lastStart),
                                 sequence.end()};
        SpeechSequence speech = getSpeech(sub);
        gotValidSpeech = !speech.empty();
        if (!speech.empty()) {
            out.push_back(std::move(speech));
        }
    }
    if (spoke != nullptr) {
        *spoke = gotValidSpeech;
    }
    return out;
}

SpeechSequence SentenceBuffer::flushPending() {
    SpeechSequence pending;
    pending.swap(pending_);
    return pending;
}

SpeechSequence SentenceBuffer::getSpeech(const SpeechSequence& sequence) {
    SpeechSequence final;    // to be spoken now
    SpeechSequence pending;  // held for later, built in reverse

    // Scanned from the end: the split point is the LAST sentence boundary.
    for (std::size_t position = sequence.size(); position > 0; --position) {
        const std::size_t i = position - 1;
        const SpeechItem& item = sequence[i];
        if (!item.isText()) {
            pending.push_back(item);
            continue;
        }

        std::string before;
        std::string after;
        bool matched = false;
        if (lastPause_->valid()) {
            lastPause_->search(item.text(), [&](const text::Match& match) {
                matched = true;
                before = match.group(1);
                after = match.group(2);
            });
        }
        if (!matched) {
            pending.push_back(item);
            continue;
        }

        if (!after.empty()) {
            pending.emplace_back(after);
        }
        if (before.empty()) {
            continue;
        }
        // Everything held so far, then everything before this item, then the
        // completed part of this item.
        SpeechSequence held = flushPending();
        final.insert(final.end(), held.begin(), held.end());
        final.insert(final.end(), sequence.begin(), sequence.begin() + static_cast<std::ptrdiff_t>(i));
        final.emplace_back(before);
        // THE DETAIL FROM §4.4: carry the language of the held fragment with
        // it. Any other parameter command introduced later belongs here too.
        for (std::size_t back = i; back > 0; --back) {
            const SpeechItem& candidate = sequence[back - 1];
            if (!candidate.is(CommandType::LangChange)) {
                continue;
            }
            pending.push_back(candidate);
            break;
        }
        break;
    }

    if (!pending.empty()) {
        // It was built from the end backwards.
        for (std::size_t i = pending.size(); i > 0; --i) {
            pending_.push_back(pending[i - 1]);
        }
    }
    return final;
}

}  // namespace alus::speech
