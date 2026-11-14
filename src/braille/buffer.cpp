// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/buffer.hpp.

#include "alus/braille/buffer.hpp"

#include "alus/braille/formatting.hpp"
#include "alus/braille/text_region.hpp"
#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>

namespace alus::braille {
namespace {

// NVDA: `rindex`. The LAST index of `value` in [from, to], or -1.
int lastIndexOf(const std::vector<std::uint8_t>& cells, std::uint8_t value, int from, int to) {
    const int first = std::max(from, 0);
    const int last = std::min(to, static_cast<int>(cells.size()) - 1);
    for (int index = last; index >= first; --index) {
        if (cells[static_cast<std::size_t>(index)] == value) {
            return index;
        }
    }
    return -1;
}

// The first index of `value` in [from, to), or -1.
int firstIndexOf(const std::vector<std::uint8_t>& cells, std::uint8_t value, int from,
                 int to) {
    const int first = std::max(from, 0);
    const int last = std::min(to, static_cast<int>(cells.size()));
    for (int index = first; index < last; ++index) {
        if (cells[static_cast<std::size_t>(index)] == value) {
            return index;
        }
    }
    return -1;
}

}  // namespace

Buffer::Buffer(RegionHost& host) : host_(&host) {}
Buffer::~Buffer() = default;

void Buffer::clear() {
    regions.clear();
    rawText_.clear();
    cells_.clear();
    cursorPos_.reset();
    rawToBraillePos_.clear();
    brailleToRawPos_.clear();
    saved_ = RegionPosition{};
    setWindowStartPos(0);
}

std::vector<Region*> Buffer::visibleRegions() const {
    std::vector<Region*> visible;
    if (regions.empty()) {
        return visible;
    }
    if (regions.back()->hidePreviousRegions) {
        visible.push_back(regions.back().get());
        return visible;
    }
    visible.reserve(regions.size());
    for (const RegionPtr& region : regions) {
        visible.push_back(region.get());
    }
    return visible;
}

std::vector<Buffer::RegionSpan> Buffer::regionSpans() const {
    std::vector<RegionSpan> spans;
    int start = 0;
    for (Region* region : visibleRegions()) {
        const int end = start + static_cast<int>(region->brailleCells.size());
        spans.push_back(RegionSpan{region, start, end});
        start = end;
    }
    return spans;
}

void Buffer::update() {
    rawText_.clear();
    cells_.clear();
    cursorPos_.reset();
    rawToBraillePos_.clear();
    brailleToRawPos_.clear();

    int cellStart = 0;
    int rawStart = 0;
    for (Region* region : visibleRegions()) {
        rawText_ += region->rawText;
        cells_.insert(cells_.end(), region->brailleCells.begin(), region->brailleCells.end());
        for (const int position : region->rawToBraillePos) {
            rawToBraillePos_.push_back(position + cellStart);
        }
        for (const int position : region->brailleToRawPos) {
            brailleToRawPos_.push_back(position + rawStart);
        }
        if (region->brailleCursorPos.has_value()) {
            // The LAST region with a cursor wins, as in NVDA: the focused one
            // comes last, and its cursor is the one the user is moving.
            cursorPos_ = cellStart + *region->brailleCursorPos;
        }
        cellStart += static_cast<int>(region->brailleCells.size());
        rawStart += static_cast<int>(text::codepointCount(region->rawText));
    }
    if (core::Log::instance().isEnabledFor(core::LogLevel::Io)) {
        ALUS_IO("braille buffer: {} cells, window {}", cells_.size(),
                formatCellsForLog(windowBrailleCells()));
    }
    calculateRows(windowStartPos());
}

const std::vector<int>& Buffer::rawToBraillePos() const { return rawToBraillePos_; }
const std::vector<int>& Buffer::brailleToRawPos() const { return brailleToRawPos_; }
int Buffer::rawLength() const { return static_cast<int>(text::codepointCount(rawText_)); }

// ---------------------------------------------------------------------------
// Positions
// ---------------------------------------------------------------------------

std::optional<Buffer::RegionPosition> Buffer::bufferPosToRegionPos(int bufferPos) const {
    for (const RegionSpan& span : regionSpans()) {
        if (span.end > bufferPos) {
            return RegionPosition{span.region, bufferPos - span.start};
        }
    }
    return std::nullopt;
}

std::optional<int> Buffer::regionPosToBufferPos(const Region* region, int position,
                                                bool allowNearest) const {
    int lastStart = 0;
    for (const RegionSpan& span : regionSpans()) {
        if (span.region == region) {
            if (position < span.end - span.start) {
                return span.start + position;
            }
            if (allowNearest) {
                // The region is still here but has got shorter: its start is
                // the nearest honest answer.
                return span.start;
            }
            return std::nullopt;
        }
        lastStart = span.start;
    }
    if (allowNearest) {
        return lastStart;
    }
    return std::nullopt;
}

std::string Buffer::bufferPositionsToRawText(int startPos, int endPos) const {
    if (brailleToRawPos_.empty() || rawText_.empty()) {
        return {};
    }
    const int lastCell = static_cast<int>(brailleToRawPos_.size()) - 1;
    const int rawStart =
        brailleToRawPos_[static_cast<std::size_t>(std::clamp(startPos, 0, lastCell))];
    const int rawEnd =
        brailleToRawPos_[static_cast<std::size_t>(std::clamp(endPos, 0, lastCell))] + 1;
    const text::OffsetConverter converter{rawText_};
    const int rawCount = static_cast<int>(converter.codepointLength());
    const int from = std::clamp(rawStart, 0, rawCount);
    const int to = std::clamp(rawEnd, from, rawCount);
    const std::size_t byteFrom = converter.toByte(static_cast<std::size_t>(from));
    const std::size_t byteTo = converter.toByte(static_cast<std::size_t>(to));
    return rawText_.substr(byteFrom, byteTo - byteFrom);
}

std::optional<int> Buffer::bufferPosToWindowPos(int bufferPos) const {
    const int columns = std::max(host_->dimensions().numCols, 1);
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        if (rows_[row].start <= bufferPos && bufferPos < rows_[row].end) {
            return static_cast<int>(row) * columns + (bufferPos - rows_[row].start);
        }
    }
    return std::nullopt;
}

int Buffer::windowPosToBufferPos(int windowPos) const {
    const DisplayDimensions dimensions = host_->dimensions();
    if (dimensions.size() == 0) {
        return 0;
    }
    const int clamped = std::clamp(windowPos, 0, dimensions.size());
    const int columns = std::max(dimensions.numCols, 1);
    const std::size_t row = static_cast<std::size_t>(clamped / columns);
    const int column = clamped % columns;
    if (row >= rows_.size()) {
        // Past the last row the window has: the end of the content is the
        // only honest answer, and it is what a routing key on a blank cell
        // beyond the text gets.
        return rows_.empty() ? 0 : std::max(rows_.back().end - 1, 0);
    }
    return std::clamp(rows_[row].start + column, 0, std::max(rows_[row].end - 1, 0));
}

int Buffer::windowStartPos() const { return windowPosToBufferPos(0); }

void Buffer::setWindowStartPos(int position) { calculateRows(position); }

int Buffer::windowEndPos() const { return rows_.empty() ? 0 : rows_.back().end; }

// ---------------------------------------------------------------------------
// The window rows: word wrap (T7.5)
// ---------------------------------------------------------------------------

bool Buffer::isMidWordCut(int end, int bufferEnd) const {
    if (end >= bufferEnd || end <= 0 || end >= static_cast<int>(cells_.size())) {
        return false;
    }
    // Both the cell before the cut and the cell at it carry dots: the cut
    // falls inside a word.
    return cells_[static_cast<std::size_t>(end) - 1] != 0 &&
           cells_[static_cast<std::size_t>(end)] != 0;
}

void Buffer::calculateRows(int position) {
    rows_.clear();
    if (cells_.empty()) {
        rows_.push_back(Row{0, 0, false});
        return;
    }
    const DisplayDimensions dimensions = host_->dimensions();
    const TextWrap wrap = host_->brailleSettings().textWrap;
    const int bufferEnd = static_cast<int>(cells_.size());
    const int columns = std::max(dimensions.numCols, 1);
    const int numRows = std::max(dimensions.numRows, 1);
    int start = std::clamp(position, 0, bufferEnd);
    bool clippedEnd = false;

    for (int row = 0; row < numRows; ++row) {
        bool showContinuationMark = false;
        int end = start + columns;
        if (end > bufferEnd) {
            end = bufferEnd;
            clippedEnd = true;
        } else if (wrap == TextWrap::MarkWordCuts && isMidWordCut(end, bufferEnd)) {
            --end;
            showContinuationMark = true;
        } else if (wrap == TextWrap::AtWordBoundaries ||
                   wrap == TextWrap::AtWordOrSyllableBoundaries) {
            // The last blank cell that still fits. Searching up to and
            // INCLUDING `end` matters: a word that ends exactly at the display
            // edge needs no wrap at all.
            const int lastSpace = lastIndexOf(cells_, 0, start, end);
            if (lastSpace < 0) {
                // No blank in the whole row: there is nothing to wrap at, so
                // fall back to the display edge and mark the cut.
                if (isMidWordCut(end, bufferEnd)) {
                    --end;
                    showContinuationMark = true;
                }
            } else if (lastSpace < end) {
                const int oldEnd = end;
                end = lastSpace + 1;
                if (end < oldEnd && wrap == TextWrap::AtWordOrSyllableBoundaries) {
                    // The whole word was pushed to the next row. If the table
                    // knows where the word may be broken, break it closer to
                    // the edge instead of leaving most of the row blank.
                    const int nextSpace = firstIndexOf(cells_, 0, oldEnd, bufferEnd);
                    if (nextSpace > end) {
                        const std::string word = bufferPositionsToRawText(end, nextSpace - 1);
                        if (!word.empty() && end < static_cast<int>(brailleToRawPos_.size())) {
                            const int rawPos =
                                brailleToRawPos_[static_cast<std::size_t>(end)];
                            const std::vector<int> breaks =
                                host_->louis().hyphenationPositions(host_->outputTables(),
                                                                    word);
                            for (auto it = breaks.rbegin(); it != breaks.rend(); ++it) {
                                const int rawIndex = *it + rawPos;
                                if (rawIndex < 0 ||
                                    rawIndex >= static_cast<int>(rawToBraillePos_.size())) {
                                    continue;
                                }
                                const int newEnd =
                                    rawToBraillePos_[static_cast<std::size_t>(rawIndex)];
                                if (newEnd > start && newEnd < oldEnd) {
                                    end = newEnd;
                                    showContinuationMark = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (end <= start) {
            // A row must make progress, or scrolling forward would never end.
            end = std::min(start + 1, bufferEnd);
            if (end <= start) {
                clippedEnd = true;
            }
        }
        rows_.push_back(Row{start, end, showContinuationMark});
        if (clippedEnd) {
            break;
        }
        start = end;
    }
    if (rows_.empty()) {
        rows_.push_back(Row{std::clamp(position, 0, bufferEnd),
                            std::clamp(position, 0, bufferEnd), false});
    }
}

void Buffer::setWindowEndPos(int endPos) {
    const DisplayDimensions dimensions = host_->dimensions();
    const Settings& settings = host_->brailleSettings();
    int startPos = endPos - dimensions.size();

    // How far back the window may scroll. A region pinned hard left stops it,
    // so the context in front of the focus is not scrolled into.
    int restrictPos = 0;
    const std::vector<RegionSpan> spans = regionSpans();
    for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
        if (it->start < endPos) {
            if (it->region->focusToHardLeft) {
                restrictPos = it->start;
                break;
            }
            if (settings.focusContextPresentation != FocusContext::ChangedContext) {
                restrictPos = 0;
                break;
            }
        }
    }
    if (startPos <= restrictPos) {
        setWindowStartPos(restrictPos);
        return;
    }
    if (settings.textWrap == TextWrap::None || settings.textWrap == TextWrap::MarkWordCuts) {
        setWindowStartPos(startPos);
        return;
    }
    // Try not to split a word across two windows: break after the furthest
    // block of blanks. The search starts one cell earlier in case `startPos`
    // is just after a space.
    const int blank = firstIndexOf(cells_, 0, startPos - 1, endPos);
    if (blank >= 0) {
        int scan = blank;
        while (scan < endPos && scan < static_cast<int>(cells_.size()) &&
               cells_[static_cast<std::size_t>(scan)] == 0) {
            ++scan;
        }
        startPos = scan;
    }
    // Dropping the leading blanks can hide the paragraph marker, which is the
    // one thing at the start of a paragraph the user asked to see.
    const std::string marker = paragraphStartMarker(settings);
    if (!marker.empty() && !regions.empty()) {
        const std::string prefix = marker + std::string{kTextSeparator};
        if (regions.back()->rawText.rfind(prefix, 0) == 0) {
            const int markerCells =
                static_cast<int>(text::codepointCount(marker)) + 1;
            if (startPos <= markerCells) {
                if (const std::optional<int> regionStart =
                        regionPosToBufferPos(regions.back().get(), 0)) {
                    startPos = *regionStart;
                }
            }
        }
    }
    setWindowStartPos(startPos);
}

// ---------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------

bool Buffer::nextWindow() {
    const int oldStart = windowStartPos();
    const int end = windowEndPos();
    if (end < static_cast<int>(cells_.size())) {
        setWindowStartPos(end);
    }
    return windowStartPos() != oldStart;
}

bool Buffer::previousWindow() {
    const int start = windowStartPos();
    if (start > 0) {
        setWindowEndPos(start);
    }
    return windowStartPos() != start;
}

void Buffer::scrollForward() {
    if (!nextWindow()) {
        // The window is already at the end: ask the content to move instead,
        // which is how the display's own line keys read a document.
        if (!regions.empty()) {
            regions.back()->nextLine();
        }
        return;
    }
    if (updateDisplay_) {
        updateDisplay_();
    }
}

void Buffer::scrollBack() {
    if (!previousWindow()) {
        if (!regions.empty()) {
            regions.back()->previousLine();
        }
        return;
    }
    if (updateDisplay_) {
        updateDisplay_();
    }
}

void Buffer::scrollTo(const Region* region, int position) {
    const std::optional<int> bufferPos = regionPosToBufferPos(region, position);
    if (!bufferPos.has_value()) {
        return;
    }
    while (*bufferPos >= windowEndPos()) {
        if (!nextWindow()) {
            break;
        }
    }
    while (*bufferPos < windowStartPos()) {
        if (!previousWindow()) {
            break;
        }
    }
}

void Buffer::focus(const Region* region) {
    const std::optional<int> position = regionPosToBufferPos(region, 0);
    if (!position.has_value()) {
        return;
    }
    setWindowStartPos(*position);
    if (region->focusToHardLeft ||
        host_->brailleSettings().focusContextPresentation == FocusContext::Scroll) {
        return;
    }
    const int end = windowEndPos();
    if (end - *position < host_->dimensions().size()) {
        // There is room left on the display: scroll back so that as much
        // context as fits is shown while keeping this region visible.
        setWindowEndPos(end);
    }
}

// ---------------------------------------------------------------------------
// What the display shows
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> Buffer::windowBrailleCells() const {
    const DisplayDimensions dimensions = host_->dimensions();
    std::vector<std::uint8_t> window;
    if (dimensions.size() == 0) {
        return window;
    }
    const int columns = std::max(dimensions.numCols, 1);
    for (const Row& row : rows_) {
        const int from = std::clamp(row.start, 0, static_cast<int>(cells_.size()));
        const int to = std::clamp(row.end, from, static_cast<int>(cells_.size()));
        window.insert(window.end(), cells_.begin() + from, cells_.begin() + to);
        int remaining = columns - (to - from);
        if (remaining > 0 && row.showContinuationMark) {
            window.push_back(kContinuationShape);
            --remaining;
        }
        if (remaining > 0) {
            window.insert(window.end(), static_cast<std::size_t>(remaining), 0);
        }
    }
    window.resize(static_cast<std::size_t>(dimensions.size()), 0);
    return window;
}

std::string Buffer::windowRawText() const {
    const int start = windowStartPos();
    const int end = windowEndPos();
    if (end <= start) {
        return {};
    }
    // `bufferPositionsToRawText` is INCLUSIVE of the cell at `endPos` -- which
    // is what its other caller, the hyphenation search, needs -- so the
    // window's exclusive end has to be stepped back by one.
    //
    // NVDA passes `windowEndPos` here unchanged and so reports one character
    // too many; harmless for a log line, wrong for anything that measures the
    // window (docs/history/phase-7.md O8).
    return bufferPositionsToRawText(start, end - 1);
}

std::optional<int> Buffer::cursorWindowPos() const {
    if (!cursorPos_.has_value()) {
        return std::nullopt;
    }
    return bufferPosToWindowPos(*cursorPos_);
}

void Buffer::routeTo(int windowPos) {
    const int bufferPos = windowPosToBufferPos(windowPos);
    if (bufferPos >= windowEndPos()) {
        return;
    }
    if (const std::optional<RegionPosition> target = bufferPosToRegionPos(bufferPos)) {
        target->region->routeTo(target->position);
    }
}

text::TextRangePtr Buffer::rangeForWindowPos(int windowPos) const {
    const int bufferPos = windowPosToBufferPos(windowPos);
    if (bufferPos >= windowEndPos()) {
        return nullptr;
    }
    const std::optional<RegionPosition> target = bufferPosToRegionPos(bufferPos);
    if (!target.has_value()) {
        return nullptr;
    }
    auto* textRegion = dynamic_cast<TextInfoRegion*>(target->region);
    if (textRegion == nullptr) {
        return nullptr;
    }
    return textRegion->rangeForBraillePos(target->position);
}

// ---------------------------------------------------------------------------
// Save and restore (T7.8)
// ---------------------------------------------------------------------------

void Buffer::saveWindow() {
    if (const std::optional<RegionPosition> position = bufferPosToRegionPos(windowStartPos())) {
        saved_ = *position;
        return;
    }
    saved_ = RegionPosition{};
}

void Buffer::restoreWindow() {
    if (saved_.region == nullptr) {
        return;
    }
    if (const std::optional<int> position =
            regionPosToBufferPos(saved_.region, saved_.position, /*allowNearest=*/true)) {
        setWindowStartPos(*position);
    }
}

}  // namespace alus::braille
