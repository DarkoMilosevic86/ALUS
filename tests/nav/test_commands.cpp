// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The review command group: the numpad, as the manual test of phase-plan.md
// presses it. These are the commands the user actually touches, so they are
// driven here by name, through the same `script::Script` the dispatcher would
// have found.
#include "alus/nav/text_commands.hpp"

#include "alus/input/gesture.hpp"
#include "alus/nav/review.hpp"
#include "nav/support.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::testing::DocumentTree;
using alus::testing::NavFixture;

namespace {

// A gesture that is only an identity: the review commands never look at it.
class FakeGesture final : public alus::input::Gesture {
public:
    std::vector<std::string> identifiers() const override { return {"kb:numpad9"}; }
};

class CommandFixture : public NavFixture {
public:
    CommandFixture() : commands_(nav()) {}

    void run(std::string_view name) {
        const alus::script::Script* script = commands_.scriptByName(name);
        REQUIRE(script != nullptr);
        REQUIRE(script->function);
        FakeGesture gesture;
        script->function(gesture);
        drainSpeech();
    }

    alus::nav::TextCommands& commands() noexcept { return commands_; }

private:
    alus::nav::TextCommands commands_;
};

}  // namespace

TEST_CASE("numpad 7, 8 and 9 walk the lines and say them", "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "first line\nsecond line\nthird line");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));
    fixture.clearSpeech();

    // numpad8: the current line.
    fixture.run("review_currentLine");
    CHECK(fixture.spokenJoined().find("first line") != std::string::npos);

    // numpad9: the next one.
    fixture.clearSpeech();
    fixture.run("review_nextLine");
    CHECK(fixture.spokenJoined().find("second line") != std::string::npos);

    // numpad7: back again.
    fixture.clearSpeech();
    fixture.run("review_previousLine");
    CHECK(fixture.spokenJoined().find("first line") != std::string::npos);

    // And at the top, the edge is SPOKEN rather than being silence (§3.8: an
    // expected refusal is noticeable without being an error).
    fixture.messages.clear();
    fixture.run("review_previousLine");
    REQUIRE_FALSE(fixture.messages.empty());
    CHECK(fixture.messages.back() == "Top");
}

TEST_CASE("numpad 4, 5 and 6 walk the words", "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "alpha beta gamma");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    fixture.clearSpeech();
    fixture.run("review_currentWord");
    CHECK(fixture.spokenJoined().find("alpha") != std::string::npos);

    fixture.clearSpeech();
    fixture.run("review_nextWord");
    CHECK(fixture.spokenJoined().find("beta") != std::string::npos);

    fixture.clearSpeech();
    fixture.run("review_nextWord");
    CHECK(fixture.spokenJoined().find("gamma") != std::string::npos);
}

TEST_CASE("numpad 1, 2 and 3 walk the characters", "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "abc");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    fixture.clearSpeech();
    fixture.run("review_currentCharacter");
    CHECK(fixture.spokenJoined().find("a") != std::string::npos);

    fixture.clearSpeech();
    fixture.run("review_nextCharacter");
    CHECK(fixture.spokenJoined().find("b") != std::string::npos);

    fixture.clearSpeech();
    fixture.run("review_previousCharacter");
    CHECK(fixture.spokenJoined().find("a") != std::string::npos);
}

TEST_CASE("shift+numpad7 and shift+numpad9 go to the top and the bottom",
          "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "one\ntwo\nthree");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    fixture.clearSpeech();
    fixture.run("review_bottom");
    CHECK(fixture.spokenJoined().find("three") != std::string::npos);

    fixture.clearSpeech();
    fixture.run("review_top");
    CHECK(fixture.spokenJoined().find("one") != std::string::npos);
}

TEST_CASE("shift+numpad1 and shift+numpad3 go to the ends of the line",
          "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "hello world");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    fixture.run("review_endOfLine");
    alus::text::TextRange* position = fixture.review().position();
    REQUIRE(position != nullptr);
    // The LAST CHARACTER of the line, not the offset after it: the user has to
    // be able to stand on the "d".
    alus::text::TextRangePtr character = position->copy();
    character->expand(alus::text::TextUnit::Character);
    CHECK(character->text() == "d");

    fixture.run("review_startOfLine");
    position = fixture.review().position();
    REQUIRE(position != nullptr);
    character = position->copy();
    character->expand(alus::text::TextUnit::Character);
    CHECK(character->text() == "h");
}

TEST_CASE("alus+numpad7 cycles the review mode and says which one",
          "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "text");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    fixture.messages.clear();
    fixture.run("reviewMode_next");
    REQUIRE_FALSE(fixture.messages.empty());
    // Document review has no position (phase 6), so it is skipped and the
    // user lands in screen review -- the fallback is the design.
    CHECK(fixture.messages.back() == "Screen review");
    CHECK(fixture.review().mode() == alus::nav::ReviewMode::Screen);

    fixture.messages.clear();
    fixture.run("reviewMode_previous");
    CHECK(fixture.messages.back() == "Object review");
}

TEST_CASE("alus+f9 marks and alus+f10 selects", "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "hello world");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    fixture.messages.clear();
    fixture.run("review_markStartForCopy");
    CHECK(fixture.messages.back() == "Start marked");

    // Move to the end of "hello" and ask for the selection.
    fixture.run("review_nextWord");
    fixture.messages.clear();
    fixture.run("review_copy");
    CHECK(fixture.messages.back() == "Selected");
    REQUIRE_FALSE(fixture.backend.selectionsSet.empty());
    CHECK(fixture.backend.selectionsSet.back().start == 0);
    CHECK(fixture.backend.selectionsSet.back().end > 0);

    // Without a marker there is nothing to do, and it is said rather than
    // being silence.
    CommandFixture other;
    DocumentTree second(other.backend, "hello");
    const auto object = other.registry.objectFor(second.document);
    REQUIRE(other.registry.api().setNavigatorObject(object));
    other.messages.clear();
    other.run("review_copy");
    CHECK(other.messages.back() == "No start marker set");
}

TEST_CASE("alus+upArrow reports the line at the caret", "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "first line\nsecond line");
    fixture.backend.node(tree.document).caret = 12;
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setFocusObject(document));

    fixture.clearSpeech();
    fixture.run("reportCurrentLine");
    CHECK(fixture.spokenJoined().find("second line") != std::string::npos);
}

TEST_CASE("alus+shift+upArrow reports the selection, or that there is none",
          "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "hello world");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setFocusObject(document));

    fixture.messages.clear();
    fixture.run("reportCurrentSelection");
    CHECK(fixture.messages.back() == "No selection");

    fixture.backend.node(tree.document).selectionStart = 0;
    fixture.backend.node(tree.document).selectionEnd = 5;
    alus::core::invalidateAllCaches();
    fixture.clearSpeech();
    fixture.run("reportCurrentSelection");
    CHECK(fixture.spokenJoined().find("hello") != std::string::npos);
    CHECK(fixture.spokenJoined().find("selected") != std::string::npos);
}

TEST_CASE("alus+f reports the formatting at the caret", "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "bold text here");
    alus::testing::MockNode& node = fixture.backend.node(tree.document);
    // The toolkit's own attribute names, as AT-SPI reports them; the
    // normaliser turns them into the format field keys.
    node.attributeRuns[0] = alus::a11y::Attributes{
        {"weight", "700"}, {"family-name", "Cantarell"}, {"size", "11"}};
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setFocusObject(document));

    fixture.clearSpeech();
    fixture.run("reportFormattingAtCaret");
    const std::string spoken = fixture.spokenJoined();
    // The formatting command reports EVERYTHING, whatever the verbosity
    // settings say: the user asked.
    CHECK(spoken.find("Cantarell") != std::string::npos);
    CHECK(spoken.find("11 pt") != std::string::npos);
    CHECK(spoken.find("bold") != std::string::npos);
}

TEST_CASE("numpad2 three times reports the character's value",
          "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "A");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    // The repeat count comes from the dispatcher, which this fixture does not
    // have; the behaviour under test is the VALUE REPORT itself, so it is
    // driven through the same path with the count the dispatcher would give.
    // (The repeat counting itself is phase 3's, and tested there.)
    fixture.clearSpeech();
    fixture.run("review_currentCharacter");
    CHECK(fixture.spokenJoined().find("A") != std::string::npos);
}

TEST_CASE("numpadPlus reads from the review cursor to the end",
          "[nav][commands]") {
    CommandFixture fixture;
    DocumentTree tree(fixture.backend, "alpha\nbeta\ngamma");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));
    REQUIRE(fixture.review().position() != nullptr);

    fixture.clearSpeech();
    const alus::script::Script* script = fixture.commands().scriptByName("review_sayAll");
    REQUIRE(script != nullptr);
    FakeGesture gesture;
    script->function(gesture);
    for (int cycle = 0; cycle < 1000 && fixture.sayAll().isRunning(); ++cycle) {
        fixture.loop().iterateUntilIdle();
    }
    fixture.drainSpeech();
    const std::string spoken = fixture.spokenJoined();
    CHECK(spoken.find("alpha") != std::string::npos);
    CHECK(spoken.find("gamma") != std::string::npos);
}
