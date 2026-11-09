// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T7.6 — the single-slot write: 100 frames in one cycle are 1 write (the last)
// T7.7 — `_regionsPendingUpdate`: 50 caret movements in one cycle are 1 update
//
// Both criteria are COUNTS, which is the only honest way to test a coalescing
// mechanism: "it is fast" is an opinion, "a hundred frames became one write"
// is a measurement. The fixture's display records every frame and the writer
// counts both sides, so each number below is read off the mechanism itself
// (tests/braille/support.hpp says why the display is not brltty).
//
// Plus the rest of the handler's contract, which the two criteria would pass
// without: that the display follows the focus, that the tether can be pinned
// away from it, and that a routing key reaches the application's caret.
#include "alus/braille/handler.hpp"

#include "alus/a11y/api.hpp"
#include "braille/support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using alus::braille::TetherTo;
using alus::testing::BrailleFixture;
using alus::testing::DocumentTree;

namespace {

// Focuses the document the way the daemon does: the platform says the state
// changed, the registry's own pump turns that into the focus, and only then
// does the braille handler hear about it. Going through the event is what
// makes `focusAncestors()` -- which the context regions are built from --
// answer about this object and not about the desktop.
alus::a11y::ObjectPtr focus(BrailleFixture& fixture, alus::a11y::Ref ref) {
    fixture.backend.addState(ref, alus::a11y::PlatformState::Focused);
    fixture.backend.emit("object:state-changed:focused", ref, 1);
    fixture.pumpOnce();
    const alus::a11y::ObjectPtr object = fixture.registry.api().focus();
    REQUIRE(object != nullptr);
    fixture.handler().handleGainFocus(object);
    fixture.pumpBraille();
    return object;
}

}  // namespace

TEST_CASE("the focus reaches the display", "[braille][handler]") {
    BrailleFixture fixture;
    DocumentTree tree{fixture.backend, "first line\nsecond line\n"};
    const alus::a11y::ObjectPtr document = focus(fixture, tree.document);

    // A multi-line editable control is pinned hard left, so the window opens
    // on the text itself rather than on the control's own properties.
    CHECK(fixture.windowText() == "first line ");
    CHECK_FALSE(fixture.display().frames.empty());
    CHECK(fixture.handler().isEnabled());

    SECTION("a routing key moves the application's caret") {
        fixture.handler().routeTo(6);
        REQUIRE(fixture.backend.caretsSet.size() == 1);
        CHECK(fixture.backend.caretsSet.back().offset == 6);
    }

    SECTION("a window position answers which text it stands for") {
        const alus::text::TextRangePtr at = fixture.handler().rangeForWindowPos(6);
        REQUIRE(at != nullptr);
        alus::text::TextRangePtr word = at->copy();
        word->expand(alus::text::TextUnit::Word);
        CHECK(word->text() == "line\n");
    }

    SECTION("a message takes the display, and a routing key gives it back") {
        fixture.handler().message("saved");
        CHECK(fixture.handler().isShowingMessage());
        CHECK(fixture.windowText() == "saved");
        fixture.handler().routeTo(0);
        CHECK_FALSE(fixture.handler().isShowingMessage());
        CHECK(fixture.windowText() == "first line ");
    }
}

TEST_CASE("T7.6 a hundred frames in one cycle are one write, the last",
          "[braille][handler][T7.6]") {
    BrailleFixture fixture;
    DocumentTree tree{fixture.backend, "first line\nsecond line\n"};
    focus(fixture, tree.document);

    const std::size_t writesBefore = fixture.display().frames.size();
    const std::uint64_t storedBefore = fixture.handler().stats().framesStored;

    // A hundred states, each one a frame: every message replaces the one in
    // front of it, and on a real display each would be 10-40 ms of writing
    // that nobody would ever read.
    for (int index = 0; index < 100; ++index) {
        fixture.handler().message("message " + std::to_string(index));
    }

    CHECK(fixture.handler().stats().framesStored - storedBefore == 100);
    // NOTHING has been written yet: the slot is drained in the pump stage, so
    // the count does not depend on how fast the hardware is (display.hpp).
    CHECK(fixture.display().frames.size() == writesBefore);

    fixture.pumpBraille();

    CHECK(fixture.display().frames.size() == writesBefore + 1);
    CHECK(fixture.handler().stats().framesWritten == writesBefore + 1);
    // And it is the LAST state, not the first: an intermediate frame is not a
    // frame the user missed, it is one that was never true.
    CHECK(fixture.windowText() == "message 99");
    CHECK(fixture.display().lastFrame() == fixture.handler().currentCells());
}

TEST_CASE("T7.7 fifty caret movements in one cycle are one region update",
          "[braille][handler][T7.7]") {
    BrailleFixture fixture;
    DocumentTree tree{fixture.backend, "first line\nsecond line\n"};
    const alus::a11y::ObjectPtr document = focus(fixture, tree.document);

    const alus::braille::Handler::Stats before = fixture.handler().stats();
    const std::size_t writesBefore = fixture.display().frames.size();

    // Holding an arrow key down: fifty caret events inside one pump cycle,
    // ending in the second line. Updating the region per event would mean
    // fifty liblouis translations and fifty reads of the document's text.
    for (int offset = 0; offset < 50; ++offset) {
        fixture.backend.node(tree.document).caret = offset % 23;
        fixture.handler().handleCaretMove(document);
        // THE SET IS THE MECHANISM: the same region added fifty times is one
        // entry, and that is why this number never grows.
        CHECK(fixture.handler().pendingUpdateCount() == 1);
    }
    fixture.backend.node(tree.document).caret = 14;
    fixture.handler().handleCaretMove(document);

    CHECK(fixture.handler().stats().coalescedRegionUpdates - before.coalescedRegionUpdates ==
          51);
    // Not one region has been re-translated yet, and nothing has been written.
    CHECK(fixture.handler().stats().regionUpdates == before.regionUpdates);
    CHECK(fixture.display().frames.size() == writesBefore);

    fixture.pumpBraille();

    CHECK(fixture.handler().stats().regionUpdates - before.regionUpdates == 1);
    CHECK(fixture.handler().stats().pendingUpdateBatches - before.pendingUpdateBatches == 1);
    CHECK(fixture.handler().pendingUpdateCount() == 0);
    // One write, and it shows where the caret ENDED UP -- the coalescing
    // drops the states in between, it does not drop the last one.
    CHECK(fixture.display().frames.size() == writesBefore + 1);
    CHECK(fixture.windowText() == "second line ");
}

TEST_CASE("the tether decides whether a focus event owns the display",
          "[braille][handler]") {
    BrailleFixture fixture;
    DocumentTree tree{fixture.backend, "first line\nsecond line\n"};
    const alus::a11y::ObjectPtr document = focus(fixture, tree.document);
    REQUIRE(fixture.handler().tether() == TetherTo::Focus);

    SECTION("pinned to review, a focus event builds nothing") {
        fixture.handler().setTether(TetherTo::Review);
        CHECK(fixture.handler().tether() == TetherTo::Review);
        // The regions belonged to the focus, which is no longer what is being
        // followed, so they are dropped rather than left to go stale.
        CHECK(fixture.handler().mainBuffer().regions.empty());

        fixture.handler().handleGainFocus(document);
        fixture.pumpBraille();
        CHECK(fixture.handler().mainBuffer().regions.empty());
        // The CELLS are still the last ones written: the tether changing is
        // not itself a frame, and `braille_toggleTether` is what repaints by
        // calling handleReviewMove() straight afterwards (commands.cpp). A
        // display that blanked on every tether change would flicker for no
        // reason.
        CHECK(fixture.windowText() == "first line ");
    }

    SECTION("an automatic tether is refused unless the user asked for auto") {
        fixture.handler().setTether(TetherTo::Review);
        REQUIRE_FALSE(fixture.handler().shouldAutoTether());
        // This is `handleGainFocus`'s own call: a focus event must not take
        // the display back from a user who pinned it to the review cursor.
        fixture.handler().setTether(TetherTo::Focus, /*automatic=*/true);
        CHECK(fixture.handler().tether() == TetherTo::Review);
    }

    SECTION("in auto, whichever moved last owns the display") {
        alus::braille::Settings settings = fixture.handler().settings();
        settings.tetherTo = TetherTo::Auto;
        fixture.applySettings(settings);
        REQUIRE(fixture.handler().shouldAutoTether());

        fixture.handler().setTether(TetherTo::Review, /*automatic=*/true);
        CHECK(fixture.handler().tether() == TetherTo::Review);
        fixture.handler().handleGainFocus(document);
        fixture.pumpBraille();
        CHECK(fixture.handler().tether() == TetherTo::Focus);
        CHECK(fixture.windowText() == "first line ");
    }
}
