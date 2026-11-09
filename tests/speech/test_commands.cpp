// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The command hierarchy, and above all the prosody semantics from
// 01-ARHITEKTURA.md §4.3: a prosody change is an OFFSET XOR a MULTIPLIER, and
// both are relative to THE VALUE THE USER CONFIGURED. "Raise the pitch on
// capital letters" is PitchCommand::byOffset(30) — plus 30 on top of whatever
// the user set, not an absolute pitch.
#include "alus/speech/commands.hpp"

#include "../support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

using alus::speech::as;
using alus::speech::CharacterModeCommand;
using alus::speech::CommandType;
using alus::speech::IndexCommand;
using alus::speech::LangChangeCommand;
using alus::speech::PitchCommand;
using alus::speech::RateCommand;
using alus::speech::SpeechItem;
using alus::speech::SpeechSequence;
using alus::speech::VolumeCommand;

TEST_CASE("a sequence item is text or a command", "[speech][commands]") {
    SpeechSequence sequence;
    sequence.emplace_back(std::string{"hello"});
    sequence.emplace_back(alus::speech::index(7));

    REQUIRE(sequence.size() == 2);
    CHECK(sequence[0].isText());
    CHECK(sequence[0].text() == "hello");
    CHECK(sequence[0].command() == nullptr);
    CHECK_FALSE(sequence[0].is(CommandType::Index));

    CHECK(sequence[1].isCommand());
    CHECK(sequence[1].is(CommandType::Index));
    REQUIRE(as<IndexCommand>(sequence[1]) != nullptr);
    CHECK(as<IndexCommand>(sequence[1])->index() == 7);
    // The wrong type yields null rather than undefined behaviour.
    CHECK(as<LangChangeCommand>(sequence[1]) == nullptr);

    // Text is mutable in place: the presenter rewrites it (symbols,
    // dictionaries, the chunk separator).
    sequence[0].text() += " there";
    CHECK(sequence[0].text() == "hello there");
}

TEST_CASE("a concrete command pointer converts to a sequence item", "[speech][commands]") {
    // Without the templated constructor this needs two user-defined
    // conversions, which C++ will not do implicitly.
    SpeechSequence sequence;
    sequence.emplace_back(PitchCommand::byOffset(30));
    sequence.push_back(RateCommand::toDefault());
    CHECK(sequence[0].is(CommandType::Pitch));
    CHECK(sequence[1].is(CommandType::Rate));
}

TEST_CASE("command categories decide what reaches the synthesizer", "[speech][commands]") {
    CHECK(alus::speech::index(1)->isSynthCommand());
    CHECK(alus::speech::breakFor(50)->isSynthCommand());
    CHECK(alus::speech::langChange("sr")->isSynthCommand());
    CHECK(alus::speech::characterMode(true)->isSynthCommand());
    CHECK(PitchCommand::byOffset(10)->isSynthCommand());

    // Handled inside ALUS; never passed on (§4.3).
    CHECK_FALSE(alus::speech::endUtterance()->isSynthCommand());
    CHECK_FALSE(alus::speech::callback([] {})->isSynthCommand());
    CHECK_FALSE(alus::speech::beep(440, 50)->isSynthCommand());
    CHECK_FALSE(alus::speech::waveFile("x.wav")->isSynthCommand());
    CHECK_FALSE(alus::speech::suppressUnicodeNormalization()->isSynthCommand());

    // Parameter commands are the ones that have to be replayed after an
    // interruption.
    CHECK(alus::speech::langChange("sr")->isParamCommand());
    CHECK(alus::speech::characterMode(true)->isParamCommand());
    CHECK(PitchCommand::byOffset(10)->isParamCommand());
    CHECK_FALSE(alus::speech::index(1)->isParamCommand());
    CHECK_FALSE(alus::speech::breakFor(10)->isParamCommand());

    // Callbacks become index marks.
    CHECK(alus::speech::callback([] {})->isCallback());
    CHECK(alus::speech::beep(440, 50)->isCallback());
    CHECK(alus::speech::waveFile("x.wav")->isCallback());
    CHECK_FALSE(alus::speech::index(1)->isCallback());
}

TEST_CASE("isDefault means back to the user's configured value", "[speech][commands]") {
    CHECK(PitchCommand::toDefault()->isDefault());
    CHECK_FALSE(PitchCommand::byOffset(10)->isDefault());
    CHECK_FALSE(PitchCommand::byMultiplier(0.5)->isDefault());

    // An empty language means "back to the ALUS locale".
    CHECK(alus::speech::langChange("")->isDefault());
    CHECK_FALSE(alus::speech::langChange("sr")->isDefault());
    // Character mode off is the default state.
    CHECK(alus::speech::characterMode(false)->isDefault());
    CHECK_FALSE(alus::speech::characterMode(true)->isDefault());
}

TEST_CASE("prosody: an offset is relative to the configured value", "[speech][commands]") {
    const auto command = PitchCommand::byOffset(30);
    // The user has pitch 50: +30 is 80.
    CHECK(command->newValue(50) == 80);
    CHECK(command->offset(50) == 30);
    // And the equivalent multiplier is derived from that, which is what SSML
    // needs.
    CHECK(command->multiplier(50) == 1.6);
    // The same command against a different configured value.
    CHECK(command->newValue(20) == 50);
    CHECK(command->multiplier(20) == 2.5);
}

TEST_CASE("prosody: a multiplier is relative to the configured value", "[speech][commands]") {
    const auto command = RateCommand::byMultiplier(0.5);
    CHECK(command->newValue(80) == 40);
    CHECK(command->multiplier(80) == 0.5);
    CHECK(command->offset(80) == -40);
}

TEST_CASE("prosody: returning to the default changes nothing", "[speech][commands]") {
    const auto command = VolumeCommand::toDefault();
    CHECK(command->newValue(70) == 70);
    CHECK(command->offset(70) == 0);
    CHECK(command->multiplier(70) == 1.0);
}

TEST_CASE("prosody: offset and multiplier together are a programming error",
          "[speech][commands]") {
    CHECK_THROWS_AS(PitchCommand(30, 2.0), std::invalid_argument);
    // A configured zero has no meaningful multiplier, and must not divide by
    // zero either.
    CHECK(PitchCommand::byOffset(10)->multiplier(0) == 1.0);
}

TEST_CASE("repr is readable, because it ends up in the log", "[speech][commands]") {
    CHECK(alus::speech::index(42)->repr() == "IndexCommand(42)");
    CHECK(alus::speech::breakFor(250)->repr() == "BreakCommand(time=250)");
    CHECK(alus::speech::langChange("sr")->repr() == "LangChangeCommand(sr)");
    CHECK(alus::speech::langChange("")->repr() == "LangChangeCommand(default)");
    CHECK(PitchCommand::byOffset(30)->repr() == "pitchCommand(offset=30)");

    SpeechSequence sequence;
    sequence.emplace_back(std::string{"hi"});
    sequence.emplace_back(alus::speech::index(1));
    CHECK(alus::speech::repr(sequence) == "[\"hi\", IndexCommand(1)]");
}

TEST_CASE("a cancellable command caches its cancellation", "[speech][commands]") {
    int checks = 0;
    bool valid = true;
    alus::speech::FunctionCancellableCommand command{[&] {
                                                         ++checks;
                                                         return valid;
                                                     },
                                                     "test"};
    CHECK_FALSE(command.isCancelled());
    CHECK(checks == 1);

    valid = false;
    CHECK(command.isCancelled());
    CHECK(checks == 2);
    // Once cancelled, the predicate is never consulted again: the object it
    // refers to may be gone.
    CHECK(command.isCancelled());
    CHECK(checks == 2);
}

TEST_CASE("a validity check that throws counts as cancelled", "[speech][commands]") {
    const alus::testing::QuietLog quiet;  // the failure is logged on purpose
    alus::speech::FunctionCancellableCommand command{
        []() -> bool { throw std::runtime_error{"dead object"}; }, "test"};
    CHECK(command.isCancelled());
}
