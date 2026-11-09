// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The buffer: every region's cells end to end, and the window onto them.
//
// Port of NVDA's `braille/buffers.py` (`BrailleBuffer`).
//
// TWO COORDINATE SYSTEMS, AND EVERY BUG IN THIS FILE IS A CONFUSION BETWEEN
// THEM:
//
//   BUFFER position   an index into the concatenated cells of every visible
//                     region. There are usually more of these than the
//                     display has cells.
//   WINDOW position   an index into what the display is showing right now:
//                     0 .. numRows*numCols-1.
//
// `windowPosToBufferPos` and `bufferPosToWindowPos` are the only two places
// that cross, and they are not a simple addition, because of word wrap: a row
// of the window may be SHORTER than numCols when the buffer's next cell would
// split a word. That is what `_WindowRow` records and what makes the window a
// list of rows rather than a start and a length.
//
// WORD WRAP (02-FAZE.md T7.5) has four modes and they differ only in where a
// row ends:
//   None                        at the display edge, mid-word, no mark
//   MarkWordCuts                one cell earlier, with dots 7-8 as the mark
//   AtWordBoundaries            after the last space that fits
//   AtWordOrSyllableBoundaries  as above, but a word that would be pushed off
//                               entirely is broken at a hyphenation point
//
// SAVE AND RESTORE (T7.8) is what keeps the display still while the content
// under it changes. The window is saved as `(region, offset within region)`,
// NOT as a buffer position: regions are added, removed and re-translated
// between the save and the restore -- the whole point of the coalesced update
// -- and a buffer position would mean a different place afterwards.
#pragma once

#include "alus/braille/region.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace alus::braille {

class Buffer {
public:
    explicit Buffer(RegionHost& host);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // NVDA keeps `regions` public and so does this: the focus-context builder
    // reuses the regions already here, which is the whole reason they are
    // owned by the buffer and not by the handler.
    std::vector<RegionPtr> regions;

    // NVDA: `clear`. Drops every region and puts the window back at 0.
    void clear();

    // NVDA: `visibleRegions`. When the last region hides what is in front of
    // it, it is the only one shown.
    std::vector<Region*> visibleRegions() const;

    struct RegionSpan {
        Region* region = nullptr;
        int start = 0;  // inclusive, buffer position
        int end = 0;    // exclusive
    };
    std::vector<RegionSpan> regionSpans() const;

    // NVDA: `update`. Concatenates the regions' cells and text and
    // recalculates the window rows. Does NOT re-translate the regions: a
    // region is updated by whoever owns the reason it changed.
    void update();

    const std::vector<std::uint8_t>& brailleCells() const noexcept { return cells_; }
    const std::string& rawText() const noexcept { return rawText_; }
    // The cursor in BUFFER coordinates, absent when no visible region has one.
    const std::optional<int>& cursorPos() const noexcept { return cursorPos_; }

    // ---- the two coordinate systems ----
    int windowStartPos() const;
    void setWindowStartPos(int position);
    int windowEndPos() const;
    // NVDA's `_set_windowEndPos`: scrolling BACKWARDS, which has to decide
    // where the window would have to start to end here.
    void setWindowEndPos(int position);

    // Throws nothing: an unknown position answers nullopt, because every
    // caller here is reacting to a key press on content that may have changed.
    std::optional<int> bufferPosToWindowPos(int bufferPos) const;
    int windowPosToBufferPos(int windowPos) const;
    struct RegionPosition {
        Region* region = nullptr;
        int position = 0;
    };
    std::optional<RegionPosition> bufferPosToRegionPos(int bufferPos) const;
    // `allowNearest` is what `restoreWindow` passes: a position that is no
    // longer inside its region answers with the start of that region rather
    // than with nothing.
    std::optional<int> regionPosToBufferPos(const Region* region, int position,
                                            bool allowNearest = false) const;
    // The raw text between two buffer positions, inclusive of the character
    // under `endPos`.
    std::string bufferPositionsToRawText(int startPos, int endPos) const;

    // ---- the window ----
    // What the display shows: exactly `dimensions().size()` cells, padded.
    std::vector<std::uint8_t> windowBrailleCells() const;
    std::string windowRawText() const;
    std::optional<int> cursorWindowPos() const;

    // NVDA: `scrollForward` / `scrollBack`. When the window cannot move, the
    // last region is asked to move its own reading unit instead -- which is
    // how the display's line keys walk a document.
    void scrollForward();
    void scrollBack();
    // NVDA: `scrollTo`. Brings a position in a region into the window.
    void scrollTo(const Region* region, int position);
    // NVDA: `focus`. Puts a region at the start of the display, then scrolls
    // back as far as the context presentation setting allows.
    void focus(const Region* region);

    // NVDA: `routeTo`.
    void routeTo(int windowPos);
    // NVDA: `getTextInfoForWindowPos`.
    text::TextRangePtr rangeForWindowPos(int windowPos) const;

    // T7.8.
    void saveWindow();
    void restoreWindow();
    bool hasSavedWindow() const noexcept { return saved_.region != nullptr; }

    // Set by the handler so that a scroll redraws the display. Without it the
    // buffer would have to know what a display is.
    using UpdateHook = std::function<void()>;
    void setUpdateHook(UpdateHook hook) { updateDisplay_ = std::move(hook); }

    // For the tests and the log.
    std::size_t windowRowCount() const noexcept { return rows_.size(); }

private:
    // NVDA: `_WindowRowPositions`.
    struct Row {
        int start = 0;
        int end = 0;
        bool showContinuationMark = false;
    };

    void calculateRows(int startPos);
    bool isMidWordCut(int end, int bufferEnd) const;
    bool nextWindow();
    bool previousWindow();
    const std::vector<int>& rawToBraillePos() const;
    const std::vector<int>& brailleToRawPos() const;
    int rawLength() const;

    RegionHost* host_;
    std::vector<std::uint8_t> cells_;
    std::string rawText_;
    std::optional<int> cursorPos_;
    std::vector<Row> rows_{Row{0, 0, false}};
    // Rebuilt by update(); the maps over the whole buffer.
    std::vector<int> rawToBraillePos_;
    std::vector<int> brailleToRawPos_;
    RegionPosition saved_;
    UpdateHook updateDisplay_;
};

}  // namespace alus::braille
