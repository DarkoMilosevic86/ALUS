// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 2 exit criteria that belong to the speech manager:
//
//   T2.2  — NOW interrupts NORMAL, and NORMAL RESUMES afterwards
//   T2.3  — NOW does NOT interrupt a previously queued NOW
//   T2.4  — index wrapping: isIndexABeforeB is right across 9999 → 1
//   T2.5  — a CallbackCommand runs on the main thread, at the right moment
//   T2.6  — a stale focus announcement is dropped from the queue BEFORE it is
//           spoken (five queued, only the last still valid → one spoken)
//   T2.7  — after an interruption the rate and pitch are restored
//
// Plus the deadlines phase 0 added to NVDA's design (risk R14): a lost index
// mark must not stop speech for good.
#include "alus/speech/manager.hpp"

#include "../support/loop_fixture.hpp"
#include "alus/speech/synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <thread>
#include <vector>

using alus::speech::as;
using alus::speech::CancellableSpeechCommand;
using alus::speech::CommandType;
using alus::speech::FunctionCancellableCommand;
using alus::speech::IndexCommand;
using alus::speech::ISynth;
using alus::speech::NullSynth;
using alus::speech::Priority;
using alus::speech::RateCommand;
using alus::speech::SpeechManager;
using alus::speech::SpeechSequence;
using alus::testing::LoopFixture;
using alus::testing::QuietLog;
using namespace std::chrono_literals;

namespace {

// The synthesizer completes nothing by itself: the test decides when an index
// is reached, so no assertion depends on timing.
class ManagerFixture : public LoopFixture {
public:
    ManagerFixture() : manager_(loop()) {
        manager_.setSynth(&synth_);
        // The deadlines are tested on their own; everywhere else they must not
        // fire behind the test's back.
        manager_.setLostIndexGrace(0ms);
        manager_.setStallTimeout(0ms);
    }

    NullSynth& synth() noexcept { return synth_; }
    SpeechManager& manager() noexcept { return manager_; }

    // The index at the end of the utterance the synthesizer was last given.
    int lastUtteranceIndex() {
        const std::vector<SpeechSequence> utterances = synth_.utterances();
        REQUIRE_FALSE(utterances.empty());
        const auto command = as<IndexCommand>(utterances.back().back());
        REQUIRE(command != nullptr);
        return command->index();
    }

    // Completes every index of the last utterance, oldest first, then lets the
    // manager react.
    void completeLast() {
        synth_.completeLastUtterance();
        drain();
    }

    std::vector<std::string> spoken() { return synth_.spokenText(); }

private:
    NullSynth synth_{false};
    SpeechManager manager_;
};

SpeechSequence text(std::string value) {
    SpeechSequence sequence;
    sequence.emplace_back(std::move(value));
    return sequence;
}

}  // namespace

TEST_CASE("T2.4 index comparison is correct across the wrap boundary", "[speech][manager]") {
    // Plain cases.
    CHECK(SpeechManager::isIndexABeforeB(1, 2));
    CHECK_FALSE(SpeechManager::isIndexABeforeB(2, 1));
    CHECK_FALSE(SpeechManager::isIndexABeforeB(5, 5));
    CHECK(SpeechManager::isIndexAAfterB(2, 1));

    // Across the wrap: 9999 → 1 is FORWARD, not 9998 backwards.
    CHECK(SpeechManager::isIndexABeforeB(9999, 1));
    CHECK(SpeechManager::isIndexABeforeB(9990, 5));
    CHECK(SpeechManager::isIndexAAfterB(1, 9999));
    CHECK_FALSE(SpeechManager::isIndexABeforeB(1, 9999));

    // Beyond half the index space the relation flips: that is the whole point
    // of the half-space window (§4.1).
    CHECK(SpeechManager::isIndexABeforeB(1, SpeechManager::kWrapMagnitude));
    CHECK_FALSE(SpeechManager::isIndexABeforeB(1, SpeechManager::kWrapMagnitude + 2));

    // The relation is antisymmetric everywhere except on equality.
    for (const int a : {1, 17, 4000, 5000, 9000, 9999}) {
        for (const int b : {1, 17, 4000, 5000, 9000, 9999}) {
            if (a == b) {
                CHECK_FALSE(SpeechManager::isIndexABeforeB(a, b));
                CHECK_FALSE(SpeechManager::isIndexAAfterB(a, b));
            } else {
                CHECK(SpeechManager::isIndexABeforeB(a, b) !=
                      SpeechManager::isIndexABeforeB(b, a));
            }
        }
    }
}

TEST_CASE("every utterance ends with an index command", "[speech][manager]") {
    ManagerFixture fixture;
    fixture.manager().speak(text("one"), Priority::Normal);
    fixture.drain();

    const std::vector<SpeechSequence> utterances = fixture.synth().utterances();
    REQUIRE(utterances.size() == 1);
    // That final index is how the end of an utterance is detected, and the one
    // position in which espeak-ng is known not to lose a mark (finding N16).
    CHECK(utterances[0].back().is(CommandType::Index));
    // Commands ALUS handles itself never reach the driver (§4.3).
    for (const auto& item : utterances[0]) {
        CHECK_FALSE(item.is(CommandType::EndUtterance));
        CHECK_FALSE(item.is(CommandType::Cancellable));
    }
}

TEST_CASE("T2.2 NOW interrupts NORMAL and NORMAL resumes afterwards", "[speech][manager]") {
    ManagerFixture fixture;
    fixture.manager().speak(text("normal"), Priority::Normal);
    fixture.drain();
    REQUIRE(fixture.spoken() == std::vector<std::string>{"normal"});
    REQUIRE(fixture.synth().cancelCount() == 0);

    // The urgent message arrives while "normal" is still being spoken.
    fixture.manager().speak(text("urgent"), Priority::Now);
    fixture.drain();
    CHECK(fixture.synth().cancelCount() == 1);
    REQUIRE(fixture.spoken() == std::vector<std::string>{"normal", "urgent"});

    // When the urgent message finishes, the interrupted queue is picked up
    // again — the user hears what was cut off, rather than losing it.
    fixture.completeLast();
    CHECK(fixture.spoken() == std::vector<std::string>{"normal", "urgent", "normal"});
}

TEST_CASE("T2.3 NOW does not interrupt a previously queued NOW", "[speech][manager]") {
    ManagerFixture fixture;
    fixture.manager().speak(text("first urgent"), Priority::Now);
    fixture.drain();
    REQUIRE(fixture.spoken() == std::vector<std::string>{"first urgent"});
    // The first NOW interrupted whatever was going on, which is its job.
    const std::size_t cancelsAfterFirst = fixture.synth().cancelCount();

    fixture.manager().speak(text("second urgent"), Priority::Now);
    fixture.drain();
    // No FURTHER cancel, and nothing new sent: two urgent messages must not
    // cut each other off, or the user hears neither.
    CHECK(fixture.synth().cancelCount() == cancelsAfterFirst);
    CHECK(fixture.spoken() == std::vector<std::string>{"first urgent"});

    fixture.completeLast();
    CHECK(fixture.spoken() == std::vector<std::string>{"first urgent", "second urgent"});
}

TEST_CASE("NEXT sits between NORMAL and NOW", "[speech][manager]") {
    ManagerFixture fixture;
    fixture.manager().speak(text("normal"), Priority::Normal);
    fixture.drain();
    fixture.manager().speak(text("next"), Priority::Next);
    fixture.manager().speak(text("another normal"), Priority::Normal);
    fixture.drain();
    // Neither of those interrupts: only a first NOW does.
    CHECK(fixture.synth().cancelCount() == 0);

    fixture.completeLast();  // "normal" finishes
    CHECK(fixture.spoken().back() == "next");
    fixture.completeLast();
    CHECK(fixture.spoken().back() == "another normal");
}

TEST_CASE("T2.5 a callback runs on the main thread when its index is reached",
          "[speech][manager]") {
    ManagerFixture fixture;
    const std::thread::id mainThread = std::this_thread::get_id();
    int runs = 0;
    std::thread::id ranOn;
    std::vector<std::string> order;

    SpeechSequence sequence;
    sequence.emplace_back(std::string{"before"});
    sequence.emplace_back(alus::speech::callback(
        [&] {
            ++runs;
            ranOn = std::this_thread::get_id();
            order.emplace_back("callback");
        },
        "test"));
    sequence.emplace_back(std::string{"after"});
    fixture.manager().speak(std::move(sequence), Priority::Normal);
    fixture.drain();

    // The callback became an index; the utterance was split there but NOT
    // ended, so the synthesizer still gets one utterance.
    const std::vector<SpeechSequence> utterances = fixture.synth().utterances();
    REQUIRE(utterances.size() == 1);
    const SpeechSequence& utterance = utterances.front();
    CHECK(alus::speech::utteranceText(utterance) == "beforeafter");
    // Two indexes: the callback's, and the one that ends the utterance.
    int indexes = 0;
    for (const auto& item : utterance) {
        if (item.is(CommandType::Index)) {
            ++indexes;
        }
    }
    CHECK(indexes == 2);
    CHECK(runs == 0);  // nothing has been spoken yet

    fixture.completeLast();
    CHECK(runs == 1);
    CHECK(ranOn == mainThread);
}

TEST_CASE("a callback that throws does not stop the queue", "[speech][manager]") {
    QuietLog quiet;
    ManagerFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"one"});
    sequence.emplace_back(
        alus::speech::callback([] { throw std::runtime_error{"from a callback"}; }, "boom"));
    fixture.manager().speak(std::move(sequence), Priority::Normal);
    fixture.drain();
    fixture.completeLast();

    fixture.manager().speak(text("two"), Priority::Normal);
    fixture.drain();
    CHECK(fixture.spoken().back() == "two");
}

TEST_CASE("T2.6 a stale focus announcement is dropped before it is spoken",
          "[speech][manager]") {
    ManagerFixture fixture;
    // Something is already being spoken, so the announcements queue up behind
    // it — which is exactly what happens when the user moves through a list
    // faster than the synthesizer can speak.
    fixture.manager().speak(text("busy"), Priority::Normal);
    fixture.drain();
    REQUIRE(fixture.spoken() == std::vector<std::string>{"busy"});

    // Each announcement knows whether it is still the current focus. The flag
    // is what the object model will consult in phase 4.
    std::vector<std::shared_ptr<bool>> stillFocused;
    for (int i = 1; i <= 5; ++i) {
        auto valid = std::make_shared<bool>(true);
        stillFocused.push_back(valid);
        auto command = std::make_shared<FunctionCancellableCommand>(
            [valid] { return *valid; }, "focus " + std::to_string(i));
        SpeechSequence sequence;
        sequence.emplace_back(command);
        sequence.emplace_back("name " + std::to_string(i));
        fixture.manager().speak(std::move(sequence), Priority::Normal);
        // The first four go stale the moment the focus moves on.
        if (i < 5) {
            *valid = false;
        }
    }
    fixture.drain();
    // Still nothing new: the synthesizer is busy with "busy".
    CHECK(fixture.spoken() == std::vector<std::string>{"busy"});

    fixture.completeLast();
    // Five queued, four stale: ONE is spoken, and it is the current one.
    CHECK(fixture.spoken() == std::vector<std::string>{"busy", "name 5"});
}

TEST_CASE("a cancellable utterance already with the synth is cancelled on demand",
          "[speech][manager]") {
    ManagerFixture fixture;
    auto command = std::make_shared<FunctionCancellableCommand>(nullptr, "focus");
    SpeechSequence sequence;
    sequence.emplace_back(command);
    sequence.emplace_back(std::string{"old name"});
    fixture.manager().speak(std::move(sequence), Priority::Normal);
    fixture.drain();
    REQUIRE(fixture.spoken() == std::vector<std::string>{"old name"});

    fixture.manager().speak(text("new name"), Priority::Normal);
    fixture.drain();
    // Queued behind, nothing cancelled yet.
    REQUIRE(fixture.synth().cancelCount() == 0);

    command->cancelUtterance();
    fixture.manager().removeCancelledSpeechCommands();
    fixture.drain();
    // The utterance in flight was dropped and the next one started at once,
    // rather than waiting for stale speech to finish.
    CHECK(fixture.synth().cancelCount() == 1);
    CHECK(fixture.spoken() == std::vector<std::string>{"old name", "new name"});
}

TEST_CASE("T2.7 parameters are replayed when an interrupted queue resumes",
          "[speech][manager]") {
    ManagerFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(RateCommand::byOffset(20));
    sequence.emplace_back(std::string{"first part"});
    sequence.emplace_back(alus::speech::callback([] {}, "split"));
    sequence.emplace_back(std::string{"second part"});
    fixture.manager().speak(std::move(sequence), Priority::Normal);
    fixture.drain();

    REQUIRE(fixture.synth().utterances().size() == 1);
    REQUIRE(as<RateCommand>(fixture.synth().utterances().front().front()) != nullptr);

    // The first half has been spoken; the rate change is now part of the
    // queue's remembered state.
    const std::vector<int> pending = fixture.synth().pendingIndexes();
    REQUIRE(pending.size() == 2);
    fixture.synth().completeIndex(pending.front());
    fixture.drain();

    // An urgent message interrupts in the middle of the utterance.
    fixture.manager().speak(text("urgent"), Priority::Now);
    fixture.drain();
    REQUIRE(fixture.synth().cancelCount() == 1);
    fixture.completeLast();

    // The resumed utterance MUST carry the rate command again: the
    // synthesizer was cancelled and has forgotten it.
    const std::vector<SpeechSequence> utterances = fixture.synth().utterances();
    REQUIRE(utterances.size() == 3);
    const auto rate = as<RateCommand>(utterances.back().front());
    REQUIRE(rate != nullptr);
    CHECK(rate->rawOffset() == 20);
    CHECK(alus::speech::utteranceText(utterances.back()) == "second part");
}

TEST_CASE("a parameter returned to its default is not replayed", "[speech][manager]") {
    ManagerFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(RateCommand::byOffset(20));
    sequence.emplace_back(std::string{"fast"});
    sequence.emplace_back(RateCommand::toDefault());
    sequence.emplace_back(alus::speech::callback([] {}, "split"));
    sequence.emplace_back(std::string{"normal again"});
    fixture.manager().speak(std::move(sequence), Priority::Normal);
    fixture.drain();

    const std::vector<int> pending = fixture.synth().pendingIndexes();
    REQUIRE(pending.size() == 2);
    fixture.synth().completeIndex(pending.front());
    fixture.drain();
    fixture.manager().speak(text("urgent"), Priority::Now);
    fixture.drain();
    fixture.completeLast();

    const std::vector<SpeechSequence> utterances = fixture.synth().utterances();
    REQUIRE(utterances.size() == 3);
    // isDefault() means "back to what the user configured", so there is
    // nothing to restore.
    CHECK(as<RateCommand>(utterances.back().front()) == nullptr);
}

TEST_CASE("an explicit utterance end splits one sequence into two utterances",
          "[speech][manager]") {
    ManagerFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"first"});
    sequence.emplace_back(alus::speech::endUtterance());
    sequence.emplace_back(std::string{"second"});
    fixture.manager().speak(std::move(sequence), Priority::Normal);
    fixture.drain();
    CHECK(fixture.spoken() == std::vector<std::string>{"first"});
    fixture.completeLast();
    CHECK(fixture.spoken() == std::vector<std::string>{"first", "second"});
}

TEST_CASE("a skipped index is handled, not left outstanding", "[speech][manager]") {
    QuietLog quiet;
    ManagerFixture fixture;
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"one"});
    sequence.emplace_back(alus::speech::callback([] {}, "first"));
    sequence.emplace_back(std::string{"two"});
    fixture.manager().speak(std::move(sequence), Priority::Normal);
    fixture.drain();

    const std::vector<int> pending = fixture.synth().pendingIndexes();
    REQUIRE(pending.size() == 2);
    // The synthesizer reports only the LAST index: a real behaviour of synths
    // that skip an index when there is no audible text between two of them.
    fixture.synth().completeIndex(pending.back(), true);
    fixture.drain();
    CHECK(fixture.manager().indexesSpeaking().empty());
    CHECK_FALSE(fixture.manager().hasPendingSpeech());
}

TEST_CASE("R14 a lost index mark does not stop speech for good", "[speech][manager][r14]") {
    QuietLog quiet;
    ManagerFixture fixture;
    // The grace period after the synthesizer says it has finished. Short, so
    // the test is quick; 300 ms in production.
    fixture.manager().setLostIndexGrace(20ms);
    fixture.manager().speak(text("one"), Priority::Normal);
    fixture.manager().speak(text("two"), Priority::Normal);
    fixture.drain();
    REQUIRE(fixture.spoken() == std::vector<std::string>{"one"});

    // espeak-ng lost the mark (findings N15/N16): the audio finished, the
    // notification never came. Without a deadline the manager would wait for
    // ever and the screen reader would go silent.
    fixture.synth().doneSpeaking.notify(&fixture.synth());
    REQUIRE(fixture.pumpUntil([&] { return fixture.manager().lostIndexCount() > 0; }));
    CHECK(fixture.spoken() == std::vector<std::string>{"one", "two"});
}

TEST_CASE("R14 the absolute deadline catches a backend that says nothing",
          "[speech][manager][r14]") {
    QuietLog quiet;
    ManagerFixture fixture;
    fixture.manager().setStallTimeout(20ms);
    fixture.manager().speak(text("one"), Priority::Normal);
    fixture.manager().speak(text("two"), Priority::Normal);
    fixture.drain();
    REQUIRE(fixture.spoken() == std::vector<std::string>{"one"});

    // No index, no end notification, nothing at all.
    REQUIRE(fixture.pumpUntil([&] { return fixture.manager().lostIndexCount() > 0; }));
    CHECK(fixture.spoken() == std::vector<std::string>{"one", "two"});
}

TEST_CASE("notifications from a replaced driver are discarded", "[speech][manager]") {
    ManagerFixture fixture;
    fixture.manager().speak(text("one"), Priority::Normal);
    fixture.drain();
    const int index = fixture.lastUtteranceIndex();

    NullSynth replacement{false};
    fixture.manager().setSynth(&replacement);
    // The old driver reports an index after it has been replaced. Acting on it
    // would advance a queue that belongs to a different driver (§4.1).
    fixture.synth().completeIndex(index, true);
    fixture.drain();
    CHECK(replacement.utterances().empty());
}

TEST_CASE("cancel clears every priority queue", "[speech][manager]") {
    ManagerFixture fixture;
    fixture.manager().speak(text("normal"), Priority::Normal);
    fixture.manager().speak(text("next"), Priority::Next);
    fixture.drain();
    REQUIRE(fixture.manager().pendingSequenceCount() > 0);

    fixture.manager().cancel();
    fixture.drain();
    CHECK(fixture.manager().pendingSequenceCount() == 0);
    CHECK_FALSE(fixture.manager().hasPendingSpeech());
    CHECK_FALSE(fixture.manager().isSpeaking());
    CHECK(fixture.synth().cancelCount() == 1);

    // And speech works again afterwards.
    fixture.manager().speak(text("after cancel"), Priority::Normal);
    fixture.drain();
    CHECK(fixture.spoken().back() == "after cancel");
}

TEST_CASE("the index counter wraps at 9999 and never yields zero", "[speech][manager]") {
    ManagerFixture fixture;
    // Every callback consumes one index, so a sequence with a hundred of them
    // advances the counter a hundred steps per round. The counter really does
    // have to be walked all the way to kMaxIndex — that is the only way to see
    // it wrap — but it need not be walked one utterance at a time.
    constexpr int kCallbacksPerRound = 100;
    const int rounds = (SpeechManager::kMaxIndex / (kCallbacksPerRound + 1)) + 2;

    std::vector<int> seen;
    seen.reserve(static_cast<std::size_t>(rounds) * (kCallbacksPerRound + 1));
    for (int round = 0; round < rounds; ++round) {
        SpeechSequence sequence;
        for (int i = 0; i < kCallbacksPerRound; ++i) {
            sequence.emplace_back(std::string{"x"});
            sequence.emplace_back(alus::speech::callback([] {}, "tick"));
        }
        fixture.manager().speak(std::move(sequence), Priority::Normal);
        fixture.drain();
        const std::vector<SpeechSequence> utterances = fixture.synth().utterances();
        REQUIRE_FALSE(utterances.empty());
        for (const auto& item : utterances.back()) {
            if (const auto command = as<IndexCommand>(item)) {
                seen.push_back(command->index());
            }
        }
        fixture.completeLast();
        fixture.synth().clear();
    }

    REQUIRE(seen.size() > static_cast<std::size_t>(SpeechManager::kMaxIndex));
    for (const int index : seen) {
        // Never zero, never past the maximum: a synthesizer may not be able to
        // handle a huge number, and zero is not a valid mark.
        CHECK(index >= 1);
        CHECK(index <= SpeechManager::kMaxIndex);
    }
    // It really did wrap: the sequence is not monotonic.
    bool wrapped = false;
    for (std::size_t i = 1; i < seen.size(); ++i) {
        if (seen[i] < seen[i - 1]) {
            wrapped = true;
        }
    }
    CHECK(wrapped);
}
