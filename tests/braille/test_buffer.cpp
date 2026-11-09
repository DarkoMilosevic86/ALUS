// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T7.5 — word wrap: all four modes correct at the line boundary
// T7.8 — saveWindow/restoreWindow: the window is stable across region updates
//
// Plus the two coordinate systems the buffer exists to keep apart
// (buffer.hpp): a position in the concatenated cells, and a position on the
// display. Every bug in that file is a confusion between them, so each
// conversion is asserted in both directions.
#include "alus/braille/buffer.hpp"

#include "alus/braille/region.hpp"
#include "braille/support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using alus::braille::Buffer;
using alus::braille::DisplayDimensions;
using alus::braille::Region;
using alus::braille::RegionPtr;
using alus::braille::TextRegion;
using alus::braille::TextWrap;
using alus::braille::formatCellsForLog;
using alus::braille::kContinuationShape;
using alus::testing::TestHost;

namespace {

// Adds a text region and translates it, which is what every caller of the
// buffer does before asking it anything.
Region* addText(TestHost& host, Buffer& buffer, const std::string& text) {
    auto region = std::make_unique<TextRegion>(host, text);
    region->update();
    Region* raw = region.get();
    buffer.regions.push_back(std::move(region));
    return raw;
}

}  // namespace

TEST_CASE("the buffer concatenates its regions' cells", "[braille][buffer]") {
    TestHost host;
    Buffer buffer{host};

    Region* first = addText(host, buffer, "ab");
    Region* second = addText(host, buffer, "cd");
    buffer.update();

    // en-us-comp8 is one cell per character, which is why the fixture chooses
    // it: a count here is a statement about the buffer, not about the table.
    CHECK(buffer.brailleCells().size() == 4);
    CHECK(buffer.rawText() == "abcd");

    const std::vector<Buffer::RegionSpan> spans = buffer.regionSpans();
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start == 0);
    CHECK(spans[0].end == 2);
    CHECK(spans[1].start == 2);
    CHECK(spans[1].end == 4);

    SECTION("a buffer position maps to a position inside a region, and back") {
        const auto inSecond = buffer.bufferPosToRegionPos(3);
        REQUIRE(inSecond.has_value());
        CHECK(inSecond->region == second);
        CHECK(inSecond->position == 1);
        CHECK(buffer.regionPosToBufferPos(second, 1) == 3);
        CHECK(buffer.regionPosToBufferPos(first, 1) == 1);
    }

    SECTION("a position past the end answers nothing rather than guessing") {
        CHECK_FALSE(buffer.bufferPosToRegionPos(4).has_value());
        CHECK_FALSE(buffer.regionPosToBufferPos(second, 9).has_value());
        // Unless the caller asks for the nearest, which is what
        // restoreWindow() does with a region that has got shorter.
        CHECK(buffer.regionPosToBufferPos(second, 9, /*allowNearest=*/true) == 2);
    }

    SECTION("a region that hides the ones in front of it is shown alone") {
        second->hidePreviousRegions = true;
        buffer.update();
        CHECK(buffer.brailleCells().size() == 2);
        CHECK(buffer.rawText() == "cd");
    }
}

TEST_CASE("T7.5 word wrap: the four modes end the row in four places",
          "[braille][buffer][T7.5]") {
    TestHost host;
    host.size = DisplayDimensions{1, 10};
    Buffer buffer{host};
    // 17 characters, so 17 cells, with blanks at cell 5 and cell 11. A
    // ten-cell display therefore has to decide what to do with "world",
    // which starts at 6 and would be cut at 10.
    addText(host, buffer, "hello world again");
    buffer.update();
    REQUIRE(buffer.brailleCells().size() == 17);
    REQUIRE(buffer.brailleCells()[5] == 0);
    REQUIRE(buffer.brailleCells()[11] == 0);

    SECTION("None cuts at the display edge, mid-word, with no mark") {
        host.settings.textWrap = TextWrap::None;
        buffer.update();
        CHECK(buffer.windowStartPos() == 0);
        CHECK(buffer.windowEndPos() == 10);
        CHECK(buffer.windowRawText() == "hello worl");
        const std::vector<std::uint8_t> window = buffer.windowBrailleCells();
        REQUIRE(window.size() == 10);
        CHECK(window[9] != kContinuationShape);
    }

    SECTION("MarkWordCuts cuts one cell earlier and marks it") {
        host.settings.textWrap = TextWrap::MarkWordCuts;
        buffer.update();
        CHECK(buffer.windowEndPos() == 9);
        CHECK(buffer.windowRawText() == "hello wor");
        const std::vector<std::uint8_t> window = buffer.windowBrailleCells();
        REQUIRE(window.size() == 10);
        // Dots 7 and 8 in the last cell: "this word continues".
        CHECK(window[9] == kContinuationShape);
    }

    SECTION("AtWordBoundaries ends after the last blank that fits") {
        host.settings.textWrap = TextWrap::AtWordBoundaries;
        buffer.update();
        CHECK(buffer.windowEndPos() == 6);
        CHECK(buffer.windowRawText() == "hello ");
        const std::vector<std::uint8_t> window = buffer.windowBrailleCells();
        REQUIRE(window.size() == 10);
        // No mark: nothing was split.
        CHECK(window[9] == 0);
        // And the next window starts where this one ended, so no cell is lost
        // -- and it wraps at the next blank in its turn.
        buffer.scrollForward();
        CHECK(buffer.windowStartPos() == 6);
        CHECK(buffer.windowRawText() == "world ");
        buffer.scrollForward();
        CHECK(buffer.windowRawText() == "again");
    }

    SECTION("a word longer than the display is still shown, and marked") {
        // There is no blank to wrap at, so even AtWordBoundaries has to cut.
        Buffer narrow{host};
        host.settings.textWrap = TextWrap::AtWordBoundaries;
        addText(host, narrow, "accessibility");
        narrow.update();
        CHECK(narrow.windowEndPos() == 9);
        const std::vector<std::uint8_t> window = narrow.windowBrailleCells();
        REQUIRE(window.size() == 10);
        CHECK(window[9] == kContinuationShape);
        CHECK(narrow.windowRawText() == "accessibi");
    }

    SECTION("AtWordOrSyllableBoundaries fills more of the row when the table knows how") {
        // The syllable mode needs a table with hyphenation rules. Of the
        // tables Debian ships, `de-g1.ctb` has them and the English ones do
        // not -- which is itself worth knowing, and is why this assertion is
        // RELATIONAL: the syllable mode must end the row no earlier than the
        // word mode, and later when a break point exists.
        host.settings.translationTable = "de-g1.ctb";
        host.size = DisplayDimensions{1, 8};
        Buffer german{host};
        addText(host, german, "x accessibility x");

        host.settings.textWrap = TextWrap::AtWordBoundaries;
        german.update();
        const int wordEnd = german.windowEndPos();
        const std::string wordText = german.windowRawText();

        host.settings.textWrap = TextWrap::AtWordOrSyllableBoundaries;
        german.update();
        const int syllableEnd = german.windowEndPos();
        const std::string syllableText = german.windowRawText();

        INFO("word mode: '" << wordText << "' (end " << wordEnd << "), syllable mode: '"
                            << syllableText << "' (end " << syllableEnd << ")");
        CHECK(syllableEnd > wordEnd);
        CHECK(syllableText.size() > wordText.size());
        // A split happened, so the user is told -- and the mark goes
        // immediately after the row's own cells, not at the display edge,
        // because that is where the reader's finger leaves the text.
        const std::vector<std::uint8_t> window = german.windowBrailleCells();
        REQUIRE(window.size() == 8);
        CHECK(window[static_cast<std::size_t>(syllableEnd)] == kContinuationShape);
    }
}

TEST_CASE("the window maps to the display in both directions", "[braille][buffer]") {
    TestHost host;
    host.size = DisplayDimensions{1, 5};
    host.settings.textWrap = TextWrap::None;
    Buffer buffer{host};
    addText(host, buffer, "abcdefghij");
    buffer.update();

    CHECK(buffer.windowPosToBufferPos(0) == 0);
    CHECK(buffer.windowPosToBufferPos(4) == 4);
    CHECK(buffer.bufferPosToWindowPos(2) == 2);
    // A buffer position outside the window has no window position at all --
    // and answering 0 instead would route a key to the wrong character.
    CHECK_FALSE(buffer.bufferPosToWindowPos(7).has_value());

    SECTION("after a scroll the mapping follows") {
        buffer.scrollForward();
        CHECK(buffer.windowStartPos() == 5);
        CHECK(buffer.windowPosToBufferPos(0) == 5);
        CHECK(buffer.bufferPosToWindowPos(7) == 2);
        CHECK(buffer.windowRawText() == "fghij");
    }

    SECTION("a multi-row display lays the rows out one after another") {
        host.size = DisplayDimensions{2, 5};
        buffer.setWindowStartPos(0);
        buffer.update();
        CHECK(buffer.windowRowCount() == 2);
        CHECK(buffer.windowEndPos() == 10);
        CHECK(buffer.windowBrailleCells().size() == 10);
        // Row 1, column 0 is window position 5 and buffer position 5.
        CHECK(buffer.windowPosToBufferPos(5) == 5);
        CHECK(buffer.bufferPosToWindowPos(5) == 5);
    }

    SECTION("scrolling back lands where scrolling forward came from") {
        buffer.scrollForward();
        REQUIRE(buffer.windowStartPos() == 5);
        buffer.scrollBack();
        CHECK(buffer.windowStartPos() == 0);
    }
}

TEST_CASE("T7.8 the window survives a region update", "[braille][buffer][T7.8]") {
    TestHost host;
    host.size = DisplayDimensions{1, 5};
    host.settings.textWrap = TextWrap::None;
    Buffer buffer{host};

    auto* context = static_cast<TextRegion*>(addText(host, buffer, "CTX "));
    auto* content = static_cast<TextRegion*>(addText(host, buffer, "0123456789"));
    buffer.update();
    // Scroll into the middle of the content: buffer position 5 is the "1" of
    // the content region, which is content-region position 1.
    buffer.setWindowStartPos(5);
    REQUIRE(buffer.windowRawText() == "12345");

    SECTION("the saved window is restored even though the region was re-translated") {
        buffer.saveWindow();
        REQUIRE(buffer.hasSavedWindow());
        // What the coalesced update does: the region's text changes under the
        // window, and every cell is translated again.
        content->rawText = "0123456789";
        content->update();
        buffer.update();
        buffer.restoreWindow();
        CHECK(buffer.windowStartPos() == 5);
        CHECK(buffer.windowRawText() == "12345");
    }

    SECTION("it is restored even though a region in FRONT of it grew") {
        buffer.saveWindow();
        // This is why the window is saved against a region and not against a
        // buffer position: the context region is one cell longer now, so
        // buffer position 5 is a different character than it was.
        context->rawText = "CTXT ";
        context->update();
        buffer.update();
        buffer.restoreWindow();
        CHECK(buffer.windowStartPos() == 6);
        CHECK(buffer.windowRawText() == "12345");
    }

    SECTION("a region that got shorter restores to its start rather than nowhere") {
        buffer.saveWindow();
        content->rawText = "0";
        content->update();
        buffer.update();
        buffer.restoreWindow();
        // The saved position (1 inside the content) no longer exists, so the
        // nearest honest answer is the start of that region.
        CHECK(buffer.windowStartPos() == 4);
    }

    SECTION("a region that went away entirely does not leave the window stranded") {
        buffer.saveWindow();
        buffer.regions.pop_back();
        buffer.update();
        buffer.restoreWindow();
        CHECK(buffer.windowStartPos() <= 4);
    }
}

TEST_CASE("focus puts a region on the display and scrolls back for context",
          "[braille][buffer]") {
    TestHost host;
    host.size = DisplayDimensions{1, 10};
    host.settings.textWrap = TextWrap::None;
    Buffer buffer{host};
    Region* context = addText(host, buffer, "dlg ");
    Region* focused = addText(host, buffer, "btn");
    buffer.update();
    (void)context;

    SECTION("there is room for the context, so it is shown too") {
        buffer.focus(focused);
        // "dlg btn" is 7 cells and the display has 10, so the window is
        // scrolled back to show all of it.
        CHECK(buffer.windowStartPos() == 0);
        CHECK(buffer.windowRawText() == "dlg btn");
    }

    SECTION("a region pinned hard left stays at the left edge") {
        focused->focusToHardLeft = true;
        buffer.focus(focused);
        CHECK(buffer.windowStartPos() == 4);
        CHECK(buffer.windowRawText() == "btn");
    }
}

TEST_CASE("the cursor and the selection reach the buffer", "[braille][buffer]") {
    TestHost host;
    host.size = DisplayDimensions{1, 10};
    Buffer buffer{host};
    auto region = std::make_unique<TextRegion>(host, "abcdef");
    region->cursorPos = 2;
    region->selectionStart = 1;
    region->selectionEnd = 3;
    region->update();
    buffer.regions.push_back(std::move(region));
    buffer.update();

    REQUIRE(buffer.cursorPos().has_value());
    CHECK(*buffer.cursorPos() == 2);
    CHECK(buffer.cursorWindowPos() == 2);

    // The selection is dots 7 and 8 ORed into the cells it covers, so the
    // cells differ from the same text without a selection.
    const std::vector<std::uint8_t> window = buffer.windowBrailleCells();
    CHECK((window[1] & 0xC0) == 0xC0);
    CHECK((window[2] & 0xC0) == 0xC0);
    CHECK((window[3] & 0xC0) == 0);

    SECTION("showSelection off leaves the cells alone") {
        host.settings.showSelection = false;
        buffer.regions.back()->update();
        buffer.update();
        const std::vector<std::uint8_t> plain = buffer.windowBrailleCells();
        CHECK((plain[1] & 0xC0) == 0);
    }
}
