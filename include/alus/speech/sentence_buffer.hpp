// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The sentence buffer: why say-all sounds continuous.
//
// Port of NVDA's `speech/speechWithoutPauses.py`. See 01-ARHITEKTURA.md §4.4.
//
// A synthesizer inserts an utterance-final pause at the end of every utterance
// it is given. Feed it one line of a document at a time and say-all stutters
// once per line. So text is HELD until a sentence boundary is seen, and only
// then handed on; the incomplete tail waits for the next call.
//
// The detail that is easy to miss, and audible when missed: THE LAST
// LanguageChangeCommand BEFORE THE SPLIT POINT IS COPIED INTO THE HELD
// FRAGMENT. Without it the held text is spoken in the wrong language, because
// the command that selected its language went out with the previous chunk.
#pragma once

#include "alus/speech/commands.hpp"

#include <functional>
#include <string>
#include <vector>

namespace alus::text {
class Regex;
}

namespace alus::speech {

class SentenceBuffer {
public:
    using SpeakFunction = std::function<void(SpeechSequence)>;

    // `speak` is normally Presenter::speak bound to a priority.
    explicit SentenceBuffer(SpeakFunction speak = {});
    ~SentenceBuffer();

    SentenceBuffer(const SentenceBuffer&) = delete;
    SentenceBuffer& operator=(const SentenceBuffer&) = delete;

    void setSpeakFunction(SpeakFunction speak) { speak_ = std::move(speak); }

    // Forgets whatever is held. Called when speech is cancelled.
    void reset();

    // True when something was actually spoken, false when the text was only
    // buffered. Callers use the return value to decide whether to wait for the
    // speech to finish (say-all does). Note NVDA's exact semantics, which are
    // kept here: when the sequence contains explicit utterance ends, the
    // answer describes the LAST segment, not whether anything at all was
    // spoken.
    bool speakWithoutPauses(const SpeechSequence& sequence, bool detectBreaks = true);
    // Speaks whatever is held, however incomplete. NVDA spells this
    // `speakWithoutPauses(None)`.
    bool flush();

    // The sequences that may be spoken now. `spoke` reports the same thing the
    // return value of speakWithoutPauses() does. This is the form NVDA's
    // generator provides, for callers that want the sequences themselves.
    std::vector<SpeechSequence> getSpeechWithoutPauses(const SpeechSequence& sequence,
                                                       bool detectBreaks, bool* spoke);
    std::vector<SpeechSequence> flushSequences(bool* spoke);

    const SpeechSequence& pending() const noexcept { return pending_; }

private:
    std::vector<SpeechSequence> detectBreaksAndGetSpeech(const SpeechSequence& sequence,
                                                         bool* spoke);
    SpeechSequence flushPending();
    SpeechSequence getSpeech(const SpeechSequence& sequence);

    SpeakFunction speak_;
    SpeechSequence pending_;
    // Static would be tempting; a member keeps the engine's lifetime tied to
    // the object and avoids initialisation order surprises in tests.
    std::unique_ptr<text::Regex> lastPause_;
};

}  // namespace alus::speech
