// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T5.6 — the LiveText diff: 1000 lines of output, and only the new ones are
//        announced.
#include "alus/nav/text_behaviors.hpp"

#include "alus/a11y/events.hpp"
#include "nav/support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

using alus::nav::diffNewLines;
using alus::testing::MockNode;
using alus::testing::NavFixture;

namespace {

std::string lines(int from, int to) {
    std::string text;
    for (int index = from; index <= to; ++index) {
        text += "line " + std::to_string(index) + "\n";
    }
    return text;
}

}  // namespace

TEST_CASE("T5.6 a thousand lines of appended output: only the new ones",
          "[nav][livetext][T5.6]") {
    const std::string before = lines(1, 1000);
    const std::string after = before + "line 1001\nline 1002\n";

    const std::vector<std::string> added = diffNewLines(before, after);
    REQUIRE(added.size() == 2);
    CHECK(added[0] == "line 1001");
    CHECK(added[1] == "line 1002");

    // And nothing at all when nothing changed -- which is the case a terminal
    // produces most often, because a redraw is not a change.
    CHECK(diffNewLines(before, before).empty());
}

TEST_CASE("T5.6 a scrolled terminal reports only what scrolled in",
          "[nav][livetext][T5.6]") {
    // The screen holds 25 lines; the output scrolled by three. Everything the
    // user already heard must stay silent.
    const std::string before = lines(1, 25);
    const std::string after = lines(4, 28);

    const std::vector<std::string> added = diffNewLines(before, after);
    REQUIRE(added.size() == 3);
    CHECK(added[0] == "line 26");
    CHECK(added[1] == "line 27");
    CHECK(added[2] == "line 28");
}

TEST_CASE("T5.6 a line inserted in the middle is the only new line",
          "[nav][livetext][T5.6]") {
    const std::string before = "alpha\nbeta\ngamma\n";
    const std::string after = "alpha\nbeta\ninserted\ngamma\n";
    const std::vector<std::string> added = diffNewLines(before, after);
    REQUIRE(added.size() == 1);
    CHECK(added[0] == "inserted");
}

TEST_CASE("T5.6 blank lines are never reported as new text",
          "[nav][livetext][T5.6]") {
    const std::vector<std::string> added =
        diffNewLines("alpha\n", "alpha\n\n   \n\t\nbeta\n");
    REQUIRE(added.size() == 1);
    CHECK(added[0] == "beta");
}

TEST_CASE("T5.6 a redrawn screen beyond the diff budget still answers",
          "[nav][livetext][T5.6]") {
    // A scrollback buffer replaced wholesale: too much changed for a real
    // diff, so the set comparison answers instead -- and it must still report
    // only what was not there before.
    const std::string before = lines(1, 1000);
    std::string after = lines(500, 1500);
    const std::vector<std::string> added = diffNewLines(before, after);
    // Everything from 1001 to 1500 is new; nothing below 1000 is.
    REQUIRE(added.size() == 500);
    CHECK(added.front() == "line 1001");
    CHECK(added.back() == "line 1500");
}

TEST_CASE("T5.6 the live text behaviour announces only the new lines",
          "[nav][livetext][T5.6]") {
    NavFixture fixture;
    const alus::a11y::Ref application = fixture.backend.addApplication("Terminal", "gtk");
    const alus::a11y::Ref window =
        fixture.backend.add(application, alus::a11y::PlatformRole::Frame, "Terminal");
    fixture.backend.addState(window, alus::a11y::PlatformState::Active);
    const alus::a11y::Ref terminal =
        fixture.backend.add(window, alus::a11y::PlatformRole::Terminal, {});
    MockNode& node = fixture.backend.node(terminal);
    node.text = lines(1, 3);
    node.interfaces |= alus::a11y::Interface::Text;
    node.interfaces |= alus::a11y::Interface::EditableText;
    fixture.backend.addState(terminal, alus::a11y::PlatformState::Focusable);

    const auto object = fixture.registry.objectFor(terminal);
    REQUIRE(object != nullptr);
    // The terminal behaviour was chosen for it, and with it the live text.
    CHECK(object->hasBehavior("NVDAObjects.behaviors.Terminal"));

    // Focus starts the monitoring, with the three lines already there as the
    // baseline: what is on screen when the user arrives is not new.
    fixture.registry.queueEvent(std::string(alus::a11y::events::kGainFocus), object);
    fixture.pumpOnce();
    fixture.drainSpeech();
    fixture.clearSpeech();

    // Four more lines of output.
    fixture.backend.node(terminal).text = lines(1, 7);
    alus::core::invalidateAllCaches();
    fixture.registry.queueEvent(std::string(alus::a11y::events::kTextChange), object);
    fixture.pumpOnce();
    // The terminal's stabilisation delay: a burst of updates costs one diff.
    for (int cycle = 0; cycle < 200; ++cycle) {
        fixture.loop().iterateUntilIdle();
        if (!fixture.spokenText().empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    fixture.drainSpeech();

    const std::string spoken = fixture.spokenJoined();
    CHECK(spoken.find("line 4") != std::string::npos);
    CHECK(spoken.find("line 7") != std::string::npos);
    // The three lines that were already on screen stay silent.
    CHECK(spoken.find("line 1") == std::string::npos);
    CHECK(spoken.find("line 2") == std::string::npos);
}

TEST_CASE("a single new character is treated as a typed echo", "[nav][livetext]") {
    // NVDA's rule, and it is the quietest one in the whole behaviour: a
    // single new character is almost certainly the echo of a key the user
    // just pressed, and the key press was already announced. Announcing the
    // echo as well says everything twice -- and in a terminal, where the
    // SHELL does the echoing, that is every character the user types.
    alus::testing::NavFixture fixture;
    const alus::a11y::Ref application = fixture.backend.addApplication("Terminal", "gtk");
    const alus::a11y::Ref terminal =
        fixture.backend.add(application, alus::a11y::PlatformRole::Terminal, {});
    MockNode& node = fixture.backend.node(terminal);
    node.text = "ready\n";
    node.interfaces |= alus::a11y::Interface::Text;

    const auto object = fixture.registry.objectFor(terminal);
    fixture.registry.queueEvent(std::string(alus::a11y::events::kGainFocus), object);
    fixture.pumpOnce();
    fixture.drainSpeech();
    fixture.clearSpeech();

    const auto settle = [&fixture] {
        for (int cycle = 0; cycle < 100; ++cycle) {
            fixture.loop().iterateUntilIdle();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        fixture.drainSpeech();
    };

    // One new character: silence.
    fixture.backend.node(terminal).text = "ready\nx\n";
    alus::core::invalidateAllCaches();
    fixture.registry.queueEvent(std::string(alus::a11y::events::kTextChange), object);
    fixture.pumpOnce();
    settle();
    CHECK(fixture.spokenJoined().empty());

    // A real line of output: spoken.
    fixture.clearSpeech();
    fixture.backend.node(terminal).text = "ready\nx\ncommand finished\n";
    alus::core::invalidateAllCaches();
    fixture.registry.queueEvent(std::string(alus::a11y::events::kTextChange), object);
    fixture.pumpOnce();
    settle();
    CHECK(fixture.spokenJoined().find("command finished") != std::string::npos);
}
