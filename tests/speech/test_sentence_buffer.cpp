// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T2.8 — ten lines with no full stop become ONE utterance, not ten.
//
// This is what makes say-all sound like reading rather than a list of lines
// (01-ARHITEKTURA.md §4.4). The second test here is the detail that is easy to
// miss: the language of the held fragment has to travel with it.
#include "alus/speech/sentence_buffer.hpp"

#include "alus/speech/synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using alus::speech::as;
using alus::speech::CommandType;
using alus::speech::LangChangeCommand;
using alus::speech::SentenceBuffer;
using alus::speech::SpeechSequence;

namespace {

std::string textOf(const SpeechSequence& sequence) {
    std::string out;
    for (const auto& item : sequence) {
        if (item.isText()) {
            out += item.text();
        }
    }
    return out;
}

class Recorder {
public:
    SentenceBuffer::SpeakFunction function() {
        return [this](SpeechSequence sequence) { spoken_.push_back(std::move(sequence)); };
    }
    const std::vector<SpeechSequence>& spoken() const { return spoken_; }
    std::vector<std::string> texts() const {
        std::vector<std::string> out;
        for (const auto& sequence : spoken_) {
            out.push_back(textOf(sequence));
        }
        return out;
    }

private:
    std::vector<SpeechSequence> spoken_;
};

SpeechSequence line(std::string value) {
    SpeechSequence sequence;
    sequence.emplace_back(std::move(value));
    return sequence;
}

}  // namespace

TEST_CASE("T2.8 ten lines without a full stop become one utterance",
          "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};

    for (int i = 1; i <= 10; ++i) {
        // No sentence-final punctuation anywhere: a line of a list, a table
        // cell, a line of code.
        const bool spoke = buffer.speakWithoutPauses(line("line " + std::to_string(i) + " "));
        CHECK_FALSE(spoke);
    }
    // Nothing has gone to the synthesizer yet — that is the whole point.
    CHECK(recorder.spoken().empty());

    CHECK(buffer.flush());
    REQUIRE(recorder.spoken().size() == 1);
    CHECK(recorder.texts().front() ==
          "line 1 line 2 line 3 line 4 line 5 line 6 line 7 line 8 line 9 line 10 ");
}

TEST_CASE("text is handed on at a sentence boundary", "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};

    CHECK(buffer.speakWithoutPauses(line("First sentence. Second half")));
    REQUIRE(recorder.spoken().size() == 1);
    CHECK(recorder.texts().front() == "First sentence. ");

    // The incomplete tail waits.
    CHECK(buffer.speakWithoutPauses(line(" of the second sentence? Third")) == true);
    REQUIRE(recorder.spoken().size() == 2);
    CHECK(recorder.texts()[1] == "Second half of the second sentence? ");

    buffer.flush();
    REQUIRE(recorder.spoken().size() == 3);
    CHECK(recorder.texts()[2] == "Third");
}

TEST_CASE("an abbreviation or ellipsis is not a sentence end", "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};
    // The lookbehind in the pattern requires a non-space, non-punctuation
    // character before the full stop, so "..." does not split.
    CHECK_FALSE(buffer.speakWithoutPauses(line("wait for it ...")));
    CHECK(recorder.spoken().empty());
}

TEST_CASE("a closing quote after the full stop stays with the sentence",
          "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};
    CHECK(buffer.speakWithoutPauses(line("He said \"go.\" Then he left")));
    REQUIRE(recorder.spoken().size() == 1);
    CHECK(recorder.texts().front() == "He said \"go.\" ");
}

TEST_CASE("the language of the held fragment travels with it", "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};

    SpeechSequence sequence;
    sequence.emplace_back(alus::speech::langChange("en"));
    sequence.emplace_back(std::string{"This is English. "});
    sequence.emplace_back(alus::speech::langChange("sr"));
    sequence.emplace_back(std::string{"Ово је почетак српске"});
    CHECK(buffer.speakWithoutPauses(sequence));

    REQUIRE(recorder.spoken().size() == 1);
    CHECK(recorder.texts().front() == "This is English. ");

    // THE DETAIL (§4.4): without carrying the last language change into the
    // held fragment, the Serbian text would be spoken by the English voice.
    const SpeechSequence& pending = buffer.pending();
    bool foundSerbian = false;
    for (const auto& item : pending) {
        if (const auto command = as<LangChangeCommand>(item)) {
            if (command->language() == "sr") {
                foundSerbian = true;
            }
        }
    }
    CHECK(foundSerbian);
    CHECK(textOf(pending) == "Ово је почетак српске");
}

TEST_CASE("an explicit utterance end flushes the buffer", "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};

    SpeechSequence sequence;
    sequence.emplace_back(std::string{"no full stop here"});
    sequence.emplace_back(alus::speech::endUtterance());
    sequence.emplace_back(std::string{"and this is next"});
    // The return value describes the LAST segment only (NVDA's
    // _detectBreaksAndGetSpeech overwrites it per segment), and the last
    // segment here has no sentence end, so it is false while the first
    // segment WAS spoken.
    CHECK_FALSE(buffer.speakWithoutPauses(sequence));

    // The break was explicit, so the incomplete text was spoken anyway.
    REQUIRE(recorder.spoken().size() == 1);
    CHECK(recorder.texts().front() == "no full stop here");
    CHECK(textOf(buffer.pending()) == "and this is next");
}

TEST_CASE("detectBreaks false keeps the utterance end in the sequence",
          "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"text. "});
    sequence.emplace_back(alus::speech::endUtterance());
    CHECK(buffer.speakWithoutPauses(sequence, false));
    REQUIRE(recorder.spoken().size() == 1);
    // The sentence ended inside the text item, so the text went out and the
    // command, which comes after the split point, is held with whatever
    // follows it — the same place NVDA leaves it.
    CHECK(alus::speech::utteranceText(recorder.spoken().front()) == "text. ");
    bool pendingHasEnd = false;
    for (const auto& item : buffer.pending()) {
        if (item.is(CommandType::EndUtterance)) {
            pendingHasEnd = true;
        }
    }
    CHECK(pendingHasEnd);
}

TEST_CASE("reset forgets what was held", "[speech][sentencebuffer]") {
    Recorder recorder;
    SentenceBuffer buffer{recorder.function()};
    buffer.speakWithoutPauses(line("half a sentence"));
    REQUIRE_FALSE(buffer.pending().empty());
    buffer.reset();
    CHECK(buffer.pending().empty());
    CHECK_FALSE(buffer.flush());
    CHECK(recorder.spoken().empty());
}
