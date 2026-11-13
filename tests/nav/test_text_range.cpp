// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T5.2 — `get_string_at_offset(LINE)` boundaries agree with ALUS's UNIT_LINE.
//
// And the rest of the offsets text range: expand, move, collapse, compare,
// bookmarks, the caret and the selection. Everything here runs over the mock
// platform, so every platform call is counted and nothing depends on a
// toolkit being installed.
#include "alus/a11y/text_range.hpp"
#include "nav/support.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::a11y::ObjectTextRange;
using alus::testing::DocumentTree;
using alus::testing::NavFixture;
using alus::text::EndPointPair;
using alus::text::Position;
using alus::text::TextRangePtr;
using alus::text::TextUnit;

namespace {

// Three lines, with the second one empty: an empty line is where line
// navigation usually breaks.
constexpr const char* kThreeLines = "first line\n\nthird line";

}  // namespace

TEST_CASE("T5.2 the platform's line granularity is used when it has one",
          "[nav][text][T5.2]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, kThreeLines);
    // The toolkit answers the LINE granularity: its boundaries are the ones
    // ALUS must use, because only the toolkit knows where it wrapped the text.
    fixture.backend.node(tree.document).answersLineGranularity = true;

    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(document != nullptr);
    TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    REQUIRE(range->expand(TextUnit::Line));
    CHECK(range->text() == "first line\n");

    // The count proves the platform was asked rather than the text searched.
    CHECK(fixture.backend.callsFor("stringAtOffset") > 0);

    REQUIRE(range->move(TextUnit::Line, 1) == 1);
    REQUIRE(range->expand(TextUnit::Line));
    CHECK(range->text() == "\n");

    REQUIRE(range->move(TextUnit::Line, 1) == 1);
    REQUIRE(range->expand(TextUnit::Line));
    CHECK(range->text() == "third line");

    // NVDA #2096: ONE move past the last unit is allowed, so that the review
    // cursor and braille routing can reach the insertion point at the end of
    // the text. The move after that one is refused.
    CHECK(range->move(TextUnit::Line, 1) == 1);
    CHECK(range->move(TextUnit::Line, 1) == 0);
}

TEST_CASE("T5.2 without the platform's granularity the text layer agrees with it",
          "[nav][text][T5.2]") {
    // THE SAME ANSWERS, computed by the ported line search instead of asked
    // for. This is the half of T5.2 that matters: the two paths must not
    // disagree, or a toolkit upgrade would change how the user's document
    // reads.
    NavFixture fixture;
    DocumentTree tree(fixture.backend, kThreeLines);
    fixture.backend.node(tree.document).answersLineGranularity = false;

    const auto document = fixture.registry.objectFor(tree.document);
    TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    REQUIRE(range->expand(TextUnit::Line));
    CHECK(range->text() == "first line\n");
    REQUIRE(range->move(TextUnit::Line, 1) == 1);
    REQUIRE(range->expand(TextUnit::Line));
    CHECK(range->text() == "\n");
    REQUIRE(range->move(TextUnit::Line, 1) == 1);
    REQUIRE(range->expand(TextUnit::Line));
    CHECK(range->text() == "third line");
    // The platform WAS asked (the backend counts the attempt) and declined by
    // answering with no segment, so the ported line search produced all three
    // answers -- which is the half of T5.2 that matters: the two paths must
    // not disagree.
    CHECK(fixture.backend.callsFor("text") > 0);
}

TEST_CASE("the character unit steps over a multi-byte character once",
          "[nav][text]") {
    NavFixture fixture;
    // "čaj" -- the first letter is two bytes. One press of the arrow key must
    // cross both of them (§7.3).
    DocumentTree tree(fixture.backend, "čaj");
    const auto document = fixture.registry.objectFor(tree.document);
    TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    REQUIRE(range->expand(TextUnit::Character));
    CHECK(range->text() == "č");
    REQUIRE(range->move(TextUnit::Character, 1) == 1);
    REQUIRE(range->expand(TextUnit::Character));
    CHECK(range->text() == "a");
    REQUIRE(range->move(TextUnit::Character, 1) == 1);
    REQUIRE(range->expand(TextUnit::Character));
    CHECK(range->text() == "j");
}

TEST_CASE("N113: the arrow keys step one letter at a time through Serbian text",
          "[nav][text][N113]") {
    // The second live pass, in Chrome: the right arrow on "Treći" read "ći"
    // and then skipped the "i". AT-SPI offsets are CODEPOINTS and the text
    // layer treated them as UTF-8 bytes; at the start of a line the two agree
    // for long enough to hide it ("čaj" above passed throughout), and in the
    // middle of one they do not. Cyrillic is the worst case: every letter is
    // two bytes, so every step was wrong.
    NavFixture fixture;
    const std::string text = "Treći dan, Ђорђе.";
    DocumentTree tree(fixture.backend, text);
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(document != nullptr);

    SECTION("the platform's count is characters, and so is the range's") {
        CHECK(document->characterCount() == 17);  // 21 bytes in UTF-8
    }

    SECTION("by character, forwards: every letter alone, none skipped") {
        TextRangePtr range = document->makeTextRange(Position::First);
        REQUIRE(range != nullptr);
        std::vector<std::string> letters;
        for (int step = 0; step < 17; ++step) {
            REQUIRE(range->expand(TextUnit::Character));
            letters.push_back(range->text());
            range->collapse();
            if (range->move(TextUnit::Character, 1) != 1) {
                break;
            }
        }
        const std::vector<std::string> expected{"T", "r", "e", "ć", "i", " ", "d", "a", "n",
                                                ",", " ", "Ђ", "о", "р", "ђ", "е", "."};
        CHECK(letters == expected);
    }

    SECTION("by character, backwards from the end") {
        TextRangePtr range = document->makeTextRange(Position::Last);
        REQUIRE(range != nullptr);
        REQUIRE(range->expand(TextUnit::Character));
        CHECK(range->text() == ".");
        range->collapse();
        REQUIRE(range->move(TextUnit::Character, -1) == -1);
        REQUIRE(range->expand(TextUnit::Character));
        CHECK(range->text() == "е");
        range->collapse();
        REQUIRE(range->move(TextUnit::Character, -1) == -1);
        REQUIRE(range->expand(TextUnit::Character));
        CHECK(range->text() == "ђ");
    }

    SECTION("by word: the Cyrillic word is one word, whole") {
        TextRangePtr range = document->makeTextRange(Position::First);
        REQUIRE(range != nullptr);
        std::vector<std::string> words;
        for (int step = 0; step < 8; ++step) {
            REQUIRE(range->expand(TextUnit::Word));
            words.push_back(range->text());
            range->collapse();
            if (range->move(TextUnit::Word, 1) != 1) {
                break;
            }
        }
        INFO("words: " << [&words] {
            std::string all;
            for (const std::string& word : words) {
                all += "[" + word + "]";
            }
            return all;
        }());
        REQUIRE_FALSE(words.empty());
        CHECK(words.front() == "Treći ");
        // The Cyrillic word arrives WHOLE: with bytes for codepoints its
        // boundaries fell in the middle of a letter.
        CHECK(std::find(words.begin(), words.end(), "Ђорђе") != words.end());
    }
}

TEST_CASE("the word unit includes the whitespace after the word", "[nav][text]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "one two three");
    const auto document = fixture.registry.objectFor(tree.document);
    TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    REQUIRE(range->expand(TextUnit::Word));
    // NVDA's word unit, which no toolkit offers: the word PLUS the space, or
    // moving by word takes two presses per word.
    CHECK(range->text() == "one ");
    REQUIRE(range->move(TextUnit::Word, 1) == 1);
    REQUIRE(range->expand(TextUnit::Word));
    CHECK(range->text() == "two ");
}

TEST_CASE("expand, collapse and compare behave as a range", "[nav][text]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "hello world");
    const auto document = fixture.registry.objectFor(tree.document);
    TextRangePtr range = document->makeTextRange(Position::All);
    REQUIRE(range != nullptr);
    CHECK(range->text() == "hello world");
    CHECK_FALSE(range->isCollapsed());

    TextRangePtr other = range->copy();
    CHECK(other->compareEndPoints(*range, EndPointPair::StartToStart) == 0);
    CHECK(other->compareEndPoints(*range, EndPointPair::EndToEnd) == 0);

    other->collapse();
    CHECK(other->isCollapsed());
    CHECK(other->compareEndPoints(*range, EndPointPair::EndToEnd) == -1);

    // setEndPoint moves ONE end and leaves the other, clamping only when the
    // start would end up after the end.
    TextRangePtr third = range->copy();
    third->collapse(/*toEnd=*/true);
    CHECK(third->isCollapsed());
    third->setEndPoint(*range, EndPointPair::StartToStart);
    CHECK_FALSE(third->isCollapsed());
    CHECK(third->text() == "hello world");
    // And here the clamp bites: moving the END back before the start drags the
    // start with it, so a range is never inverted.
    third->setEndPoint(*other, EndPointPair::EndToStart);
    CHECK(third->isCollapsed());
}

TEST_CASE("a bookmark returns to the same place and only within one object",
          "[nav][text]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "hello world");
    const auto document = fixture.registry.objectFor(tree.document);
    const auto other = fixture.registry.objectFor(
        fixture.backend.add(tree.window, alus::a11y::PlatformRole::Text, {}));
    fixture.backend.node(other->ref()).text = "hello world";
    fixture.backend.node(other->ref()).interfaces |= alus::a11y::Interface::Text;

    TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);
    range->move(TextUnit::Character, 4);
    const alus::text::Bookmark mark = range->bookmark();
    CHECK(mark.valid());

    TextRangePtr fresh = document->makeTextRange(Position::First);
    REQUIRE(fresh->moveToBookmark(mark));
    CHECK(fresh->compareEndPoints(*range, EndPointPair::StartToStart) == 0);

    // THE OBJECT IS PART OF THE IDENTITY. A bookmark from another object must
    // not apply here: that is what makes the caret-movement detector mean
    // "the caret moved" rather than "the focus moved".
    TextRangePtr elsewhere = other->makeTextRange(Position::First);
    REQUIRE(elsewhere != nullptr);
    CHECK_FALSE(elsewhere->moveToBookmark(mark));
    CHECK(elsewhere->bookmark() != mark);
}

TEST_CASE("the caret and the selection reach the platform", "[nav][text]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "hello world");
    fixture.backend.node(tree.document).caret = 6;
    const auto document = fixture.registry.objectFor(tree.document);

    TextRangePtr caret = document->makeTextRange(Position::Caret);
    REQUIRE(caret != nullptr);
    REQUIRE(caret->expand(TextUnit::Word));
    CHECK(caret->text() == "world");

    TextRangePtr target = document->makeTextRange(Position::First);
    REQUIRE(target->updateCaret());
    REQUIRE_FALSE(fixture.backend.caretsSet.empty());
    CHECK(fixture.backend.caretsSet.back().offset == 0);

    TextRangePtr selection = document->makeTextRange(Position::All);
    REQUIRE(selection->updateSelection());
    REQUIRE_FALSE(fixture.backend.selectionsSet.empty());
    CHECK(fixture.backend.selectionsSet.back().start == 0);
    CHECK(fixture.backend.selectionsSet.back().end == 11);

    // A widget that refuses must be reported as refusing, not assumed to have
    // worked -- the review cursor has to cope either way.
    fixture.backend.node(tree.document).caretSettable = false;
    CHECK_FALSE(target->updateCaret());
}

TEST_CASE("an object with no text interface has no range at all", "[nav][text]") {
    NavFixture fixture;
    const alus::a11y::Ref application = fixture.backend.addApplication("Test", "gtk");
    const alus::a11y::Ref button =
        fixture.backend.add(application, alus::a11y::PlatformRole::Button, "Save");
    const auto object = fixture.registry.objectFor(button);
    CHECK(object->makeTextRange(Position::First) == nullptr);
}

TEST_CASE("boundingRects gives one rectangle per line, clipped to the object",
          "[nav][text]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "one\ntwo");
    alus::testing::MockNode& node = fixture.backend.node(tree.document);
    node.extents = alus::core::Rect{100, 200, 80, 32};
    node.characterWidth = 8;
    node.characterHeight = 16;

    const auto document = fixture.registry.objectFor(tree.document);
    TextRangePtr range = document->makeTextRange(Position::All);
    REQUIRE(range != nullptr);
    const std::vector<alus::core::Rect> rects = range->boundingRects();
    // Two lines, two rectangles -- which is what braille routing and the mouse
    // need, rather than one box around both.
    REQUIRE(rects.size() == 2);
    CHECK(rects[0].y == 200);
    CHECK(rects[1].y == 216);
    for (const alus::core::Rect& rect : rects) {
        CHECK(rect.intersection(node.extents) == rect);
    }
}

TEST_CASE("find searches forwards and backwards from the position", "[nav][text]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "alpha beta alpha gamma");
    const auto document = fixture.registry.objectFor(tree.document);
    TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    REQUIRE(range->find("alpha"));
    // The search starts one PAST the position, so the match under the cursor
    // is not found again.
    REQUIRE(range->expand(TextUnit::Word));
    CHECK(range->text() == "alpha ");
    CHECK(range->compareEndPoints(*document->makeTextRange(Position::First),
                                  EndPointPair::StartToStart) == 1);

    REQUIRE(range->find("beta", false, /*reverse=*/true));
    REQUIRE(range->expand(TextUnit::Word));
    CHECK(range->text() == "beta ");

    CHECK_FALSE(range->find("not here"));
}
