// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T5.9 — flat object review: the spatial sort is right for a window with three
//        columns.
//
// And the review cursor itself: the three modes, the fallback between them,
// and the rule that keeps a caret in another window from dragging the review
// position along.
#include "alus/nav/review.hpp"

#include "alus/nav/screen_review.hpp"
#include "nav/support.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::nav::ReviewMode;
using alus::nav::ScreenReviewRange;
using alus::testing::DocumentTree;
using alus::testing::MockNode;
using alus::testing::NavFixture;
using alus::text::TextUnit;

namespace {

// A window laid out as three columns of two rows: the shape T5.9 names.
//
//      (10,10) Name        (210,10) Size       (410,10) Modified
//      (10,40) report.txt  (210,40) 12 KB      (410,40) yesterday
//
// Built out of order on purpose, so that the test measures the SORT and not
// the order the tree happened to be in.
struct ThreeColumnWindow {
    explicit ThreeColumnWindow(alus::testing::MockBackend& backend) {
        application = backend.addApplication("Files", "gtk");
        window = backend.add(application, alus::a11y::PlatformRole::Frame, "Files");
        backend.addState(window, alus::a11y::PlatformState::Active);

        add(backend, "12 KB", 210, 40);
        add(backend, "Modified", 410, 10);
        add(backend, "report.txt", 10, 40);
        add(backend, "Name", 10, 10);
        add(backend, "yesterday", 410, 40);
        add(backend, "Size", 210, 10);
    }

    void add(alus::testing::MockBackend& backend, const char* name, int x, int y) {
        // A table cell, as a file list really reports them: a Label is a
        // LAYOUT object (NVDA's own list), so it is not screen content.
        const alus::a11y::Ref cell =
            backend.add(window, alus::a11y::PlatformRole::TableCell, name);
        MockNode& node = backend.node(cell);
        node.extents = alus::core::Rect{x, y, 180, 20};
    }

    alus::a11y::Ref application = 0;
    alus::a11y::Ref window = 0;
};

}  // namespace

TEST_CASE("T5.9 flat object review sorts three columns into two rows",
          "[nav][review][T5.9]") {
    NavFixture fixture;
    ThreeColumnWindow tree(fixture.backend);
    const auto window = fixture.registry.objectFor(tree.window);
    REQUIRE(window != nullptr);

    auto range = ScreenReviewRange::make(window, alus::text::Position::First);
    REQUIRE(range != nullptr);

    // The pieces are in reading order: left to right within a row, then down.
    REQUIRE(range->pieces().size() == 6);
    CHECK(range->pieces()[0].object->name() == "Name");
    CHECK(range->pieces()[1].object->name() == "Size");
    CHECK(range->pieces()[2].object->name() == "Modified");
    CHECK(range->pieces()[3].object->name() == "report.txt");
    CHECK(range->pieces()[4].object->name() == "12 KB");
    CHECK(range->pieces()[5].object->name() == "yesterday");

    // And A LINE IS A ROW. This is what the grouping is for: without it
    // `numpad8` would read one third of a row.
    REQUIRE(range->expand(TextUnit::Line));
    // A row includes its terminator, exactly as a line does in a document.
    CHECK(range->text() == "Name Size Modified\n");
    REQUIRE(range->move(TextUnit::Line, 1) == 1);
    REQUIRE(range->expand(TextUnit::Line));
    CHECK(range->text() == "report.txt 12 KB yesterday");

    CHECK(range->lineCount().value_or(0) == 2);
    CHECK_FALSE(range->hitLimit());
}

TEST_CASE("T5.9 screen review knows which object each offset belongs to",
          "[nav][review][T5.9]") {
    NavFixture fixture;
    ThreeColumnWindow tree(fixture.backend);
    const auto window = fixture.registry.objectFor(tree.window);
    auto range = ScreenReviewRange::make(window, alus::text::Position::First);
    REQUIRE(range != nullptr);

    // The first row: "Name Size Modified".
    CHECK(range->objectAtOffset(0)->name() == "Name");
    CHECK(range->objectAtOffset(5)->name() == "Size");
    CHECK(range->objectAtOffset(10)->name() == "Modified");

    // And a screen point maps back to an offset, which is what mouse and
    // touch exploration need.
    const std::optional<int> offset = range->offsetFromPoint(215, 15);
    REQUIRE(offset.has_value());
    CHECK(range->objectAtOffset(*offset)->name() == "Size");
}

TEST_CASE("switching into screen review keeps the user's place",
          "[nav][review]") {
    NavFixture fixture;
    ThreeColumnWindow tree(fixture.backend);
    const auto window = fixture.registry.objectFor(tree.window);
    // The third cell of the second row.
    const auto target = fixture.registry.objectFor(window->children()[4]->ref());
    REQUIRE(target != nullptr);

    auto range = ScreenReviewRange::makeAtObject(window, target);
    REQUIRE(range != nullptr);
    REQUIRE(range->expand(TextUnit::Word));
    CHECK(range->objectAtStart()->sameAs(*target));
}

TEST_CASE("object review uses the caret, then the first position",
          "[nav][review]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "first line\nsecond line");
    fixture.backend.node(tree.document).caret = 12;
    const auto document = fixture.registry.objectFor(tree.document);

    alus::text::TextRangePtr position = fixture.review().objectPosition(document);
    REQUIRE(position != nullptr);
    REQUIRE(position->expand(TextUnit::Line));
    CHECK(position->text() == "second line");

    // A widget with no caret at all still reviews, from its start.
    fixture.backend.node(tree.document).caret = -1;
    alus::core::invalidateAllCaches();
    position = fixture.review().objectPosition(document);
    REQUIRE(position != nullptr);
    REQUIRE(position->expand(TextUnit::Line));
    CHECK(position->text() == "first line\n");
}

TEST_CASE("the review mode falls back when a mode has no position",
          "[nav][review]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "hello");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    CHECK(fixture.review().mode() == ReviewMode::Object);
    // Document review needs a tree interceptor, which is phase 6: the mode
    // change is refused rather than leaving the user somewhere nothing works.
    CHECK_FALSE(fixture.review().setMode(ReviewMode::Document).has_value());
    CHECK(fixture.review().mode() == ReviewMode::Object);

    // Screen review exists, so cycling forward skips document and lands there.
    const std::optional<std::string_view> label = fixture.review().nextMode();
    REQUIRE(label.has_value());
    CHECK(*label == "Screen review");
    CHECK(fixture.review().mode() == ReviewMode::Screen);

    // And the end of the list is the end: NVDA does not wrap.
    CHECK_FALSE(fixture.review().nextMode().has_value());
}

TEST_CASE("the review position is rebuilt when the navigator object changes",
          "[nav][review]") {
    NavFixture fixture;
    DocumentTree first(fixture.backend, "first document");
    const alus::a11y::Ref secondRef =
        fixture.backend.add(first.window, alus::a11y::PlatformRole::Text, {});
    MockNode& second = fixture.backend.node(secondRef);
    second.text = "second document";
    second.interfaces |= alus::a11y::Interface::Text;

    const auto one = fixture.registry.objectFor(first.document);
    const auto two = fixture.registry.objectFor(secondRef);

    REQUIRE(fixture.registry.api().setNavigatorObject(one));
    alus::text::TextRange* position = fixture.review().position();
    REQUIRE(position != nullptr);
    position->expand(TextUnit::Line);
    CHECK(position->text() == "first document");

    // The hook phase 4's handover asked for: the position follows the
    // navigator object without being recomputed on every focus change.
    REQUIRE(fixture.registry.api().setNavigatorObject(two));
    position = fixture.review().position();
    REQUIRE(position != nullptr);
    position->expand(TextUnit::Line);
    CHECK(position->text() == "second document");
}

TEST_CASE("the caret does not drag the review cursor from another object",
          "[nav][review]") {
    NavFixture fixture;
    DocumentTree first(fixture.backend, "reviewed document");
    const alus::a11y::Ref otherRef =
        fixture.backend.add(first.window, alus::a11y::PlatformRole::Text, {});
    MockNode& other = fixture.backend.node(otherRef);
    other.text = "another document";
    other.caret = 8;
    other.interfaces |= alus::a11y::Interface::Text;

    const auto reviewed = fixture.registry.objectFor(first.document);
    const auto elsewhere = fixture.registry.objectFor(otherRef);
    REQUIRE(fixture.registry.api().setNavigatorObject(reviewed));
    REQUIRE(fixture.review().position() != nullptr);

    // A caret moving in an object that is NOT the navigator object changes
    // nothing: the user is reviewing somewhere else.
    fixture.review().handleCaretMove(elsewhere);
    alus::text::TextRange* position = fixture.review().position();
    REQUIRE(position != nullptr);
    position->expand(TextUnit::Line);
    CHECK(position->text() == "reviewed document");

    // In the navigator object it does.
    fixture.backend.node(first.document).caret = 9;
    alus::core::invalidateAllCaches();
    fixture.review().handleCaretMove(reviewed);
    position = fixture.review().position();
    REQUIRE(position != nullptr);
    alus::text::TextRangePtr word = position->copy();
    word->expand(TextUnit::Word);
    CHECK(word->text() == "document");
}

TEST_CASE("following the caret can be turned off", "[nav][review]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "one two three");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));
    REQUIRE(fixture.review().position() != nullptr);

    fixture.config().setString("reviewCursor", "followCaret", "false");
    fixture.nav().terminate();
    fixture.nav().initialize();
    REQUIRE(fixture.registry.api().setNavigatorObject(document));

    // Put the review cursor at the start explicitly, so that the test is not
    // measuring the position being DERIVED from the caret.
    REQUIRE(fixture.nav().review()->setPosition(
        document->makeTextRange(alus::text::Position::First), false));
    REQUIRE(fixture.nav().review()->position()->bookmark().start == 0);

    fixture.backend.node(tree.document).caret = 8;
    alus::core::invalidateAllCaches();
    fixture.nav().review()->handleCaretMove(document);
    alus::text::TextRange* position = fixture.nav().review()->position();
    REQUIRE(position != nullptr);
    // Still at the start: the user said the review cursor stays put.
    CHECK(position->bookmark().start == 0);
}
