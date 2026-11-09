// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The SSML the speech-dispatcher driver generates — tested without a server,
// because the one thing that must not regress here is not I/O but a SHAPE:
//
//   RISK R14 (spikes/RESULTS.md findings N15/N16): espeak-ng SILENTLY LOSES an
//   index mark that follows sentence-final punctuation when the clause before
//   it is long enough. A mark at the END of a message always survives. So the
//   driver never emits a mark directly after terminal punctuation: it ends the
//   message there instead.
//
// If this test fails, speech stops mid-sentence on the user's machine and
// nothing in the log says why. That is why it is a unit test and not a manual
// check.
#include "alus/speech/ssip_synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using alus::speech::PitchCommand;
using alus::speech::RateCommand;
using alus::speech::SpeechSequence;
using alus::speech::SsipMessage;
using alus::speech::SsipSynth;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("R14 the sentence-end test matches the measured failure shape",
          "[speech][ssip][r14]") {
    CHECK(SsipSynth::markWouldFollowSentenceEnd("A long enough clause."));
    CHECK(SsipSynth::markWouldFollowSentenceEnd("Really? "));
    CHECK(SsipSynth::markWouldFollowSentenceEnd("Stop!"));
    // One closing quote or bracket may sit between the punctuation and the mark.
    CHECK(SsipSynth::markWouldFollowSentenceEnd("He said \"go.\""));
    CHECK(SsipSynth::markWouldFollowSentenceEnd("(done.)"));
    CHECK(SsipSynth::markWouldFollowSentenceEnd("Да…не."));

    // Not a sentence end: no need to split, and splitting needlessly would
    // cost an SSIP round trip per chunk.
    CHECK_FALSE(SsipSynth::markWouldFollowSentenceEnd("mid sentence"));
    CHECK_FALSE(SsipSynth::markWouldFollowSentenceEnd("a number 3.50"));
    CHECK_FALSE(SsipSynth::markWouldFollowSentenceEnd(""));
    CHECK_FALSE(SsipSynth::markWouldFollowSentenceEnd("   "));
}

TEST_CASE("a simple utterance is one SSML message", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(std::string{"hello world"});
    utterance.emplace_back(alus::speech::index(7));

    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].ssml == "<speak>hello world<mark name=\"7\"/></speak>");
    CHECK(messages[0].marks == std::vector<int>{7});
}

TEST_CASE("R14 a mark after a sentence end ends its message", "[speech][ssip][r14]") {
    // This is the shape that loses the mark: a long clause, a full stop, then
    // a mark, then more text in the SAME message.
    SpeechSequence utterance;
    utterance.emplace_back(std::string{"This is a long enough first sentence."});
    utterance.emplace_back(alus::speech::index(1));
    utterance.emplace_back(std::string{" And this is the rest of it"});
    utterance.emplace_back(alus::speech::index(2));

    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    // Two messages, so that EVERY mark sits at the end of its own message —
    // the one position measured to survive.
    REQUIRE(messages.size() == 2);
    CHECK(messages[0].ssml.ends_with("<mark name=\"1\"/></speak>"));
    CHECK(messages[0].marks == std::vector<int>{1});
    CHECK(contains(messages[1].ssml, "And this is the rest of it"));
    CHECK(messages[1].ssml.ends_with("<mark name=\"2\"/></speak>"));
    CHECK(messages[1].marks == std::vector<int>{2});
}

TEST_CASE("a mark in the middle of a sentence stays inline", "[speech][ssip]") {
    // No sentence boundary, so no split: one message keeps the prosody
    // continuous, and the mark is not at risk.
    SpeechSequence utterance;
    utterance.emplace_back(std::string{"first part"});
    utterance.emplace_back(alus::speech::index(1));
    utterance.emplace_back(std::string{" second part"});
    utterance.emplace_back(alus::speech::index(2));

    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].ssml ==
          "<speak>first part<mark name=\"1\"/> second part<mark name=\"2\"/></speak>");
    CHECK(messages[0].marks == std::vector<int>{1, 2});
}

TEST_CASE("every mark of an utterance is emitted exactly once", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(std::string{"One sentence."});
    utterance.emplace_back(alus::speech::index(1));
    utterance.emplace_back(std::string{"Two sentences."});
    utterance.emplace_back(alus::speech::index(2));
    utterance.emplace_back(std::string{"Three"});
    utterance.emplace_back(alus::speech::index(3));

    std::vector<int> marks;
    for (const SsipMessage& message : SsipSynth::buildMessages(utterance)) {
        for (const int mark : message.marks) {
            marks.push_back(mark);
        }
        // Whatever the split, a message never ends without closing its root.
        CHECK(message.ssml.starts_with("<speak>"));
        CHECK(message.ssml.ends_with("</speak>"));
    }
    // The manager waits for each of these; one lost mark is a silent screen
    // reader.
    CHECK(marks == std::vector<int>{1, 2, 3});
}

TEST_CASE("text is XML escaped", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(std::string{"a < b & c > d \"quoted\""});
    utterance.emplace_back(alus::speech::index(1));
    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(contains(messages[0].ssml, "a &lt; b &amp; c &gt; d &quot;quoted&quot;"));
    // An unescaped angle bracket would make the server reject the message, and
    // the user would simply hear nothing.
    CHECK_FALSE(contains(messages[0].ssml, "a < b"));
}

TEST_CASE("a break becomes an SSML break", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(std::string{"before"});
    utterance.emplace_back(alus::speech::breakFor(250));
    utterance.emplace_back(std::string{"after"});
    utterance.emplace_back(alus::speech::index(1));
    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(contains(messages[0].ssml, "<break time=\"250ms\"/>"));
    // A zero or negative break is nothing at all.
    SpeechSequence zero;
    zero.emplace_back(alus::speech::breakFor(0));
    zero.emplace_back(std::string{"x"});
    zero.emplace_back(alus::speech::index(2));
    CHECK_FALSE(contains(SsipSynth::buildMessages(zero)[0].ssml, "<break"));
}

TEST_CASE("prosody is relative to the user's configured value", "[speech][ssip]") {
    SpeechSequence utterance;
    // The user has rate 50; +25 is 75, which is 150% of the configured rate.
    utterance.emplace_back(RateCommand::byOffset(25));
    utterance.emplace_back(std::string{"faster"});
    utterance.emplace_back(alus::speech::index(1));

    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance, 50, 50, 100);
    REQUIRE(messages.size() == 1);
    CHECK(contains(messages[0].ssml, "<prosody rate=\"150%\""));
    CHECK(contains(messages[0].ssml, "</prosody>"));
}

TEST_CASE("returning prosody to the default closes the element", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(PitchCommand::byOffset(30));
    utterance.emplace_back(std::string{"high"});
    utterance.emplace_back(PitchCommand::toDefault());
    utterance.emplace_back(std::string{"normal"});
    utterance.emplace_back(alus::speech::index(1));

    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance, 50, 50, 100);
    REQUIRE(messages.size() == 1);
    const std::string& ssml = messages[0].ssml;
    // "high" is inside a prosody element and "normal" is not.
    const std::size_t high = ssml.find("high");
    const std::size_t close = ssml.find("</prosody>");
    const std::size_t normal = ssml.find("normal");
    REQUIRE(high != std::string::npos);
    REQUIRE(close != std::string::npos);
    REQUIRE(normal != std::string::npos);
    CHECK(high < close);
    CHECK(close < normal);
}

TEST_CASE("a language change becomes a voice element", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(alus::speech::langChange("sr"));
    utterance.emplace_back(std::string{"здраво"});
    utterance.emplace_back(alus::speech::langChange("en"));
    utterance.emplace_back(std::string{"hello"});
    utterance.emplace_back(alus::speech::index(1));

    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(contains(messages[0].ssml, "<voice xml:lang=\"sr\">"));
    CHECK(contains(messages[0].ssml, "<voice xml:lang=\"en\">"));
    // Balanced: as many closes as opens.
    std::size_t opens = 0;
    std::size_t closes = 0;
    std::size_t position = 0;
    while ((position = messages[0].ssml.find("<voice ", position)) != std::string::npos) {
        ++opens;
        position += 7;
    }
    position = 0;
    while ((position = messages[0].ssml.find("</voice>", position)) != std::string::npos) {
        ++closes;
        position += 8;
    }
    CHECK(opens == closes);
}

TEST_CASE("character mode becomes say-as characters", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(alus::speech::characterMode(true));
    utterance.emplace_back(std::string{"abc"});
    utterance.emplace_back(alus::speech::index(1));
    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(contains(messages[0].ssml, "<say-as interpret-as=\"characters\">abc"));
    // The element is closed before the message ends; the mark stays inside it,
    // which is harmless and keeps the mark at the end of the message.
    CHECK(contains(messages[0].ssml, "</say-as></speak>"));
}

TEST_CASE("a phoneme command becomes a phoneme element", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(alus::speech::phoneme("təˈmeɪtoʊ", "tomato"));
    utterance.emplace_back(alus::speech::index(1));
    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(contains(messages[0].ssml, "<phoneme ph="));
    CHECK(contains(messages[0].ssml, ">tomato</phoneme>"));
}

TEST_CASE("commands ALUS handles itself never reach the SSML", "[speech][ssip]") {
    SpeechSequence utterance;
    utterance.emplace_back(std::string{"text"});
    utterance.emplace_back(alus::speech::endUtterance());
    utterance.emplace_back(alus::speech::suppressUnicodeNormalization());
    utterance.emplace_back(alus::speech::beep(440, 50));
    utterance.emplace_back(alus::speech::index(1));
    const std::vector<SsipMessage> messages = SsipSynth::buildMessages(utterance);
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].ssml == "<speak>text<mark name=\"1\"/></speak>");
}

TEST_CASE("an empty utterance produces no message at all", "[speech][ssip]") {
    CHECK(SsipSynth::buildMessages(SpeechSequence{}).empty());
}
