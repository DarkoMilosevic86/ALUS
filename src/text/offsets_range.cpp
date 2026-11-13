// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/text/offsets_range.hpp.

#include "alus/text/offsets_range.hpp"

#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"
#include "alus/text/segment.hpp"

#include <glib.h>

#include <algorithm>
#include <string>

namespace alus::text {
namespace {

int clampOffset(int offset, int length) {
    return std::max(0, std::min(offset, length));
}

// NVDA translates NUL and the non-breaking space to a plain space before
// segmenting a line into words ("to make sure that words will break on them").
// Without it a toolkit that pads with NBSP reports one word per line.
std::string normaliseForWordBreaking(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte == 0) {
            result.push_back(' ');
            ++index;
            continue;
        }
        // U+00A0 is 0xC2 0xA0 in UTF-8; replacing it with one space would
        // change every offset after it, so it becomes TWO spaces -- the byte
        // count has to stay identical or every offset this function's caller
        // computes is wrong.
        if (byte == 0xC2U && index + 1 < text.size() &&
            static_cast<unsigned char>(text[index + 1]) == 0xA0U) {
            result.append("  ");
            index += 2;
            continue;
        }
        result.push_back(text[index]);
        ++index;
    }
    return result;
}

std::string caseFold(std::string_view text) {
    gchar* folded = g_utf8_casefold(text.data(), static_cast<gssize>(text.size()));
    if (folded == nullptr) {
        return std::string(text);
    }
    std::string result(folded);
    g_free(folded);
    return result;
}

}  // namespace

OffsetsTextRange::~OffsetsTextRange() = default;

// ---------------------------------------------------------------------------
// The unit
// ---------------------------------------------------------------------------

std::size_t OffsetsTextRange::unitsToBytes(std::string_view text, int units) const {
    const std::size_t clamped = static_cast<std::size_t>(std::max(0, units));
    if (offsetUnit() == OffsetUnit::Utf8Bytes) {
        return std::min(clamped, text.size());
    }
    return OffsetConverter(text).toByte(clamped);
}

int OffsetsTextRange::bytesToUnits(std::string_view text, std::size_t bytes) const {
    const std::size_t clamped = std::min(bytes, text.size());
    if (offsetUnit() == OffsetUnit::Utf8Bytes) {
        return static_cast<int>(clamped);
    }
    return static_cast<int>(OffsetConverter(text).toCodepoint(clamped));
}

// ---------------------------------------------------------------------------
// The hooks' default implementations
// ---------------------------------------------------------------------------

std::string OffsetsTextRange::storyText() const {
    return {};
}

std::string OffsetsTextRange::textRange(int start, int end) const {
    const std::string story = storyText();
    const std::size_t from = unitsToBytes(story, start);
    const std::size_t to = unitsToBytes(story, end);
    if (to <= from) {
        return {};
    }
    return story.substr(from, to - from);
}

std::optional<int> OffsetsTextRange::caretOffset() const {
    return std::nullopt;
}

bool OffsetsTextRange::setCaretOffset(int offset) {
    (void)offset;
    return false;
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::selectionOffsets() const {
    return std::nullopt;
}

bool OffsetsTextRange::setSelectionOffsets(int start, int end) {
    (void)start;
    (void)end;
    return false;
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::lineOffsets(int offset) const {
    (void)offset;
    return std::nullopt;
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::wordOffsets(int offset) const {
    (void)offset;
    return std::nullopt;
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::sentenceOffsets(int offset) const {
    (void)offset;
    return std::nullopt;
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::paragraphOffsets(int offset) const {
    (void)offset;
    return std::nullopt;
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::characterOffsets(int offset) const {
    (void)offset;
    return std::nullopt;
}

std::optional<int> OffsetsTextRange::lineNumberFromOffset(int offset) const {
    (void)offset;
    return std::nullopt;
}

std::optional<int> OffsetsTextRange::lineCount() const {
    return std::nullopt;
}

std::optional<core::Rect> OffsetsTextRange::boundingRectFromOffset(int offset) const {
    (void)offset;
    return std::nullopt;
}

std::optional<int> OffsetsTextRange::offsetFromPoint(int x, int y) const {
    (void)x;
    (void)y;
    return std::nullopt;
}

std::optional<core::Rect> OffsetsTextRange::objectLocation() const {
    return std::nullopt;
}

std::optional<int> OffsetsTextRange::firstVisibleOffset() const {
    const std::optional<core::Rect> location = objectLocation();
    if (!location.has_value() || location->empty()) {
        return std::nullopt;
    }
    return offsetFromPoint(location->left(), location->top());
}

std::optional<int> OffsetsTextRange::lastVisibleOffset() const {
    const std::optional<core::Rect> location = objectLocation();
    if (!location.has_value() || location->empty()) {
        return std::nullopt;
    }
    const std::optional<int> offset =
        offsetFromPoint(location->right() - 1, location->bottom() - 1);
    if (!offset.has_value()) {
        return std::nullopt;
    }
    // NVDA: a zero here means the toolkit did not understand the question, and
    // believing it would clip every range to the first character.
    if (*offset == 0 && storyLength() > 0) {
        return std::nullopt;
    }
    return offset;
}

std::pair<FormatField, OffsetsTextRange::Offsets> OffsetsTextRange::formatFieldAndOffsets(
    int offset, const FormatReportSettings& settings, bool calculateOffsets) const {
    FormatField field;
    Offsets bounds{startOffset_, endOffset_};
    if (settings.reportLineNumber) {
        if (calculateOffsets) {
            if (const std::optional<Offsets> line = lineOffsetsResolved(offset)) {
                bounds = *line;
            }
        }
        if (const std::optional<int> number = lineNumberFromOffset(offset)) {
            field.set("line-number", *number + 1);
        }
    }
    return {std::move(field), bounds};
}

bool OffsetsTextRange::allowMoveToUnitOffsetPastEnd(TextUnit unit) const {
    (void)unit;
    return true;
}

// ---------------------------------------------------------------------------
// Unit boundaries
// ---------------------------------------------------------------------------

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::lineOffsetsResolved(
    int offset) const {
    if (const std::optional<Offsets> platform = lineOffsets(offset)) {
        return platform;
    }
    // The ported search over the whole text. See the header: this is the
    // expensive path, and it exists so that an object with no line granularity
    // is still navigable.
    const std::string story = storyText();
    if (story.empty()) {
        return Offsets{0, 0};
    }
    const std::size_t position = unitsToBytes(story, offset);
    const std::size_t start = findStartOfLine(story, position);
    const std::size_t end = findEndOfLine(story, position);
    return Offsets{bytesToUnits(story, start), bytesToUnits(story, end)};
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::characterOffsetsResolved(
    int offset) const {
    if (const std::optional<Offsets> platform = characterOffsets(offset)) {
        return platform;
    }
    // One GRAPHEME CLUSTER, taken within the line so that the cost does not
    // grow with the document. Not one codepoint: a letter and its combining
    // mark are one press of the arrow key, which is the whole reason
    // `Segmenter` exists (§7.3).
    const std::optional<Offsets> line = lineOffsetsResolved(offset);
    if (!line.has_value()) {
        return std::nullopt;
    }
    const std::string lineText = textRange(line->start, line->end);
    if (lineText.empty()) {
        return Offsets{offset, offset + 1};
    }
    // The line is a UTF-8 string and the segmenter answers in its bytes; the
    // offset and the answer are in the range's own unit. Converting at both
    // ends is the whole of N113's fix for the arrow keys.
    const std::size_t relative = unitsToBytes(lineText, offset - line->start);
    const Segment segment = Segmenter(lineText).characterAt(relative);
    return Offsets{line->start + bytesToUnits(lineText, segment.start),
                   line->start + bytesToUnits(lineText, segment.end)};
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::wordOffsetsResolved(
    int offset) const {
    if (const std::optional<Offsets> platform = wordOffsets(offset)) {
        return platform;
    }
    const std::optional<Offsets> line = lineOffsetsResolved(offset);
    if (!line.has_value()) {
        return std::nullopt;
    }
    const std::string original = textRange(line->start, line->end);
    // Normalised for SEGMENTING only. It keeps every byte where it was (NBSP,
    // two bytes, becomes two spaces) but not every codepoint, so the offsets
    // are converted against the ORIGINAL text and only the segmenter sees the
    // normalised one.
    const std::string lineText = normaliseForWordBreaking(original);
    if (lineText.empty()) {
        return Offsets{offset, offset + 1};
    }
    const std::size_t relative = unitsToBytes(original, offset - line->start);
    const Segment segment = Segmenter(lineText).wordAt(relative);
    return Offsets{line->start + bytesToUnits(original, segment.start),
                   line->start + bytesToUnits(original, segment.end)};
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::paragraphOffsetsResolved(
    int offset) const {
    if (const std::optional<Offsets> platform = paragraphOffsets(offset)) {
        return platform;
    }
    // NVDA's default: a paragraph is a line unless the implementation knows
    // better. `nav::ParagraphNav` is what gives the user the other two
    // interpretations (§11.2).
    return lineOffsetsResolved(offset);
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::sentenceOffsetsResolved(
    int offset) const {
    if (const std::optional<Offsets> platform = sentenceOffsets(offset)) {
        return platform;
    }
    // Sentences are segmented over the containing PARAGRAPH, not the line, so
    // a sentence may span several lines -- which is NVDA's own note on
    // `_getSentenceOffsets` and the reason it is not simply a line search.
    const std::optional<Offsets> paragraph = paragraphOffsetsResolved(offset);
    if (!paragraph.has_value()) {
        return std::nullopt;
    }
    const std::string paragraphText = textRange(paragraph->start, paragraph->end);
    if (paragraphText.empty()) {
        return Offsets{offset, offset + 1};
    }
    const std::size_t relative = unitsToBytes(paragraphText, offset - paragraph->start);
    const Segment segment = Segmenter(paragraphText).sentenceAt(relative);
    return Offsets{paragraph->start + bytesToUnits(paragraphText, segment.start),
                   paragraph->start + bytesToUnits(paragraphText, segment.end)};
}

std::optional<OffsetsTextRange::Offsets> OffsetsTextRange::unitOffsets(TextUnit unit,
                                                                       int offset) const {
    switch (resolveReadingChunk(unit)) {
        case TextUnit::Character:
            return characterOffsetsResolved(offset);
        case TextUnit::Word:
            return wordOffsetsResolved(offset);
        case TextUnit::Line:
            return lineOffsetsResolved(offset);
        case TextUnit::Sentence:
            return sentenceOffsetsResolved(offset);
        case TextUnit::Paragraph:
            return paragraphOffsetsResolved(offset);
        case TextUnit::Story:
            return Offsets{0, storyLength()};
        case TextUnit::Offset:
            return Offsets{offset, offset + 1};
        default:
            // Page, table, row, column, cell, screen: units an offsets-based
            // range cannot answer. NVDA raises here; the caller sees false
            // from `expand` and 0 from `move`, and passes the key on.
            return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

bool OffsetsTextRange::initialize(Position position) {
    switch (position) {
        case Position::First:
            startOffset_ = endOffset_ = 0;
            return true;
        case Position::Last:
            startOffset_ = endOffset_ = std::max(storyLength() - 1, 0);
            return true;
        case Position::All:
            startOffset_ = 0;
            endOffset_ = storyLength();
            return true;
        case Position::Caret: {
            const std::optional<int> caret = caretOffset();
            if (!caret.has_value()) {
                startOffset_ = endOffset_ = 0;
                return false;
            }
            startOffset_ = endOffset_ = clampOffset(*caret, storyLength());
            return true;
        }
        case Position::Selection: {
            const std::optional<Offsets> selection = selectionOffsets();
            if (!selection.has_value()) {
                startOffset_ = endOffset_ = 0;
                return false;
            }
            setOffsets(selection->start, selection->end);
            return true;
        }
    }
    return false;
}

void OffsetsTextRange::setOffsets(int start, int end) {
    const int length = storyLength();
    startOffset_ = clampOffset(start, length);
    endOffset_ = clampOffset(end, length);
    if (endOffset_ < startOffset_) {
        std::swap(startOffset_, endOffset_);
    }
}

void OffsetsTextRange::copyOffsetsFrom(const OffsetsTextRange& other) {
    startOffset_ = other.startOffset_;
    endOffset_ = other.endOffset_;
    readingUnit_ = other.readingUnit_;
}

// ---------------------------------------------------------------------------
// TextRange
// ---------------------------------------------------------------------------

std::string OffsetsTextRange::text() const {
    return textRange(startOffset_, endOffset_);
}

TextWithFields OffsetsTextRange::getTextWithFields(
    const FormatReportSettings& settings) const {
    TextWithFields result;
    // NVDA: when asking for formatting after the cursor is expensive and the
    // user did not ask for it, ONE format field for the whole range and the
    // text behind it.
    if (formatDetectionAfterCursorMaybeSlow() && !settings.detectFormatAfterCursor) {
        auto [field, bounds] = formatFieldAndOffsets(startOffset_, settings, false);
        (void)bounds;
        result.push_back(TextOrField::ofCommand(FieldCommand::formatChange(std::move(field))));
        result.push_back(TextOrField::ofText(text()));
        return result;
    }
    int offset = startOffset_;
    while (offset < endOffset_) {
        auto [field, bounds] = formatFieldAndOffsets(offset, settings, true);
        int boundEnd = bounds.end;
        if (boundEnd <= bounds.start) {
            boundEnd = bounds.start + 1;
        }
        if (boundEnd <= offset) {
            boundEnd = offset + 1;
        }
        result.push_back(TextOrField::ofCommand(FieldCommand::formatChange(std::move(field))));
        result.push_back(TextOrField::ofText(textRange(offset, std::min(boundEnd, endOffset_))));
        offset = boundEnd;
    }
    return result;
}

const OffsetsTextRange* OffsetsTextRange::sameKind(const TextRange& other) const {
    const auto* typed = dynamic_cast<const OffsetsTextRange*>(&other);
    if (typed == nullptr || typed->bookmarkKind() != bookmarkKind()) {
        return nullptr;
    }
    return typed;
}

int OffsetsTextRange::compareEndPoints(const TextRange& other, EndPointPair which) const {
    const OffsetsTextRange* typed = sameKind(other);
    if (typed == nullptr) {
        ALUS_DEBUG_WARNING("comparing text ranges of different kinds ({} and {})",
                           rangeKind(), other.rangeKind());
        return 0;
    }
    int difference = 0;
    switch (which) {
        case EndPointPair::StartToStart:
            difference = startOffset_ - typed->startOffset_;
            break;
        case EndPointPair::StartToEnd:
            difference = startOffset_ - typed->endOffset_;
            break;
        case EndPointPair::EndToStart:
            difference = endOffset_ - typed->startOffset_;
            break;
        case EndPointPair::EndToEnd:
            difference = endOffset_ - typed->endOffset_;
            break;
    }
    if (difference < 0) {
        return -1;
    }
    return difference > 0 ? 1 : 0;
}

bool OffsetsTextRange::setEndPoint(const TextRange& other, EndPointPair which) {
    const OffsetsTextRange* typed = sameKind(other);
    if (typed == nullptr) {
        ALUS_DEBUG_WARNING("setEndPoint across text range kinds ({} and {})", rangeKind(),
                           other.rangeKind());
        return false;
    }
    switch (which) {
        case EndPointPair::StartToStart:
            startOffset_ = typed->startOffset_;
            break;
        case EndPointPair::StartToEnd:
            startOffset_ = typed->endOffset_;
            break;
        case EndPointPair::EndToStart:
            endOffset_ = typed->startOffset_;
            break;
        case EndPointPair::EndToEnd:
            endOffset_ = typed->endOffset_;
            break;
    }
    if (startOffset_ > endOffset_) {
        // NVDA: the start must never be after the end, and WHICH END MOVES
        // depends on which one the caller was setting.
        if (which == EndPointPair::StartToStart || which == EndPointPair::StartToEnd) {
            endOffset_ = startOffset_;
        } else {
            startOffset_ = endOffset_;
        }
    }
    return true;
}

bool OffsetsTextRange::expand(TextUnit unit) {
    const std::optional<Offsets> offsets = unitOffsets(unit, startOffset_);
    if (!offsets.has_value()) {
        return false;
    }
    startOffset_ = offsets->start;
    endOffset_ = offsets->end;
    return true;
}

void OffsetsTextRange::collapse(bool toEnd) {
    if (toEnd) {
        startOffset_ = endOffset_;
    } else {
        endOffset_ = startOffset_;
    }
}

int OffsetsTextRange::move(TextUnit unit, int direction, std::optional<EndPoint> endPoint) {
    // Ported literally from `OffsetsTextInfo.move`. The loop conditions look
    // redundant and are not: the `lastOffset` test is what stops a unit that
    // reports the same bounds twice (an empty line, a toolkit that clamps)
    // from spinning forever, and say-all would hang on it.
    if (direction == 0) {
        return 0;
    }
    int offset = 0;
    if (endPoint == EndPoint::End) {
        offset = endOffset_;
    } else if (endPoint == EndPoint::Start) {
        offset = startOffset_;
    } else {
        collapse();
        offset = startOffset_;
    }

    std::optional<int> lastOffset;
    int count = 0;
    const int lowLimit = 0;
    int highLimit = storyLength();
    if (allowMoveToUnitOffsetPastEnd(resolveReadingChunk(unit))) {
        ++highLimit;
    }
    while (count != direction &&
           (!lastOffset.has_value() || (direction > 0 && offset > *lastOffset) ||
            (direction < 0 && offset < *lastOffset)) &&
           (offset < highLimit || direction < 0) && (offset > lowLimit || direction > 0)) {
        lastOffset = offset;
        if (direction < 0 && offset > lowLimit) {
            --offset;
        }
        const std::optional<Offsets> offsets = unitOffsets(unit, offset);
        if (!offsets.has_value()) {
            return 0;
        }
        offset = direction < 0 ? offsets->start : offsets->end;
        count += direction > 0 ? 1 : -1;
    }

    if (endPoint == EndPoint::Start) {
        if ((direction > 0 && offset <= startOffset_) ||
            (direction < 0 && offset >= startOffset_) || offset < lowLimit ||
            offset >= highLimit) {
            return 0;
        }
        startOffset_ = offset;
    } else if (endPoint == EndPoint::End) {
        if ((direction > 0 && offset <= endOffset_) ||
            (direction < 0 && offset >= endOffset_) || offset < lowLimit ||
            offset > highLimit) {
            return 0;
        }
        endOffset_ = offset;
    } else {
        if ((direction > 0 && offset <= startOffset_) ||
            (direction < 0 && offset >= startOffset_) || offset < lowLimit ||
            offset >= highLimit) {
            return 0;
        }
        startOffset_ = endOffset_ = offset;
    }
    if (startOffset_ > endOffset_) {
        std::swap(startOffset_, endOffset_);
    }
    return count;
}

bool OffsetsTextRange::find(std::string_view needle, bool caseSensitive, bool reverse) {
    if (needle.empty()) {
        return false;
    }
    const std::string pattern = caseSensitive ? std::string(needle) : caseFold(needle);
    if (reverse) {
        // Searching backwards from the start, so the current match is not
        // found again. NVDA reverses both strings and searches forwards; a
        // reverse find does the same job without reversing UTF-8, which
        // reversing by byte would corrupt.
        const std::string before = textRange(0, startOffset_);
        const std::string haystack = caseSensitive ? before : caseFold(before);
        const std::size_t position = haystack.rfind(pattern);
        if (position == std::string::npos) {
            return false;
        }
        // A byte position in the haystack, which is the text in this range's
        // unit only when the unit is bytes. (When case folding changes a
        // letter's length -- German ß, a handful of others -- the position
        // is approximate either way; Serbian folds one to one.)
        startOffset_ = endOffset_ = bytesToUnits(haystack, position);
        return true;
    }
    // One past the start, so the current match is not found again.
    const int from = std::min(startOffset_ + 1, storyLength());
    const std::string after = textRange(from, storyLength());
    const std::string haystack = caseSensitive ? after : caseFold(after);
    const std::size_t position = haystack.find(pattern);
    if (position == std::string::npos) {
        return false;
    }
    startOffset_ = endOffset_ = from + bytesToUnits(haystack, position);
    return true;
}

bool OffsetsTextRange::updateCaret() {
    return setCaretOffset(startOffset_);
}

bool OffsetsTextRange::updateSelection() {
    return setSelectionOffsets(startOffset_, endOffset_);
}

Bookmark OffsetsTextRange::bookmark() const {
    Bookmark mark;
    mark.kind = bookmarkKind();
    mark.start = startOffset_;
    mark.end = endOffset_;
    return mark;
}

bool OffsetsTextRange::moveToBookmark(const Bookmark& mark) {
    if (mark.kind != bookmarkKind()) {
        return false;
    }
    setOffsets(static_cast<int>(mark.start), static_cast<int>(mark.end));
    return true;
}

std::vector<core::Rect> OffsetsTextRange::boundingRects() const {
    // Ported from `_get_boundingRects`: ONE RECTANGLE PER LINE, clipped to the
    // object, and nothing at all for a collapsed range.
    if (isCollapsed()) {
        return {};
    }
    int start = startOffset_;
    if (const std::optional<int> firstVisible = firstVisibleOffset()) {
        if (*firstVisible >= 0) {
            start = std::max(start, *firstVisible);
        }
    }
    const std::optional<core::Rect> startRect = boundingRectFromOffset(start);
    if (!startRect.has_value()) {
        return {};
    }
    int inclusiveEnd = endOffset_ - 1;
    if (const std::optional<int> lastVisible = lastVisibleOffset()) {
        if (*lastVisible >= start) {
            inclusiveEnd = std::min(inclusiveEnd, *lastVisible);
        }
    }
    const std::optional<core::Rect> location = objectLocation();
    if (!location.has_value() || location->empty()) {
        return {};
    }

    std::vector<core::Rect> rects;
    if (inclusiveEnd > start) {
        int offset = start;
        // A hard cap on the number of lines examined: a selection over a
        // million-line document must not turn into a million D-Bus calls, and
        // nothing on screen has more lines than this.
        constexpr int kMaxLines = 2000;
        for (int line = 0; line < kMaxLines && offset <= inclusiveEnd; ++line) {
            const std::optional<Offsets> lineBounds = lineOffsetsResolved(offset);
            if (!lineBounds.has_value()) {
                break;
            }
            const int lineStart = std::max(lineBounds->start, start);
            int inclusiveLineEnd = lineBounds->end - 1;
            if (inclusiveLineEnd > inclusiveEnd) {
                inclusiveLineEnd = inclusiveEnd;
            }
            if (inclusiveLineEnd < lineStart) {
                break;
            }
            const std::optional<core::Rect> first =
                lineStart == start ? startRect : boundingRectFromOffset(lineStart);
            const std::optional<core::Rect> last = boundingRectFromOffset(inclusiveLineEnd);
            if (first.has_value() && last.has_value()) {
                rects.push_back(first->united(*last));
            }
            offset = inclusiveLineEnd + 1;
        }
    } else {
        rects.push_back(*startRect);
    }

    std::vector<core::Rect> clipped;
    clipped.reserve(rects.size());
    for (const core::Rect& rect : rects) {
        const core::Rect intersection = rect.intersection(*location);
        if (intersection.empty()) {
            continue;
        }
        clipped.push_back(intersection);
    }
    return clipped;
}

std::optional<core::Point> OffsetsTextRange::pointAtStart() const {
    if (const std::optional<core::Rect> rect = boundingRectFromOffset(startOffset_)) {
        if (!rect->empty()) {
            return rect->topLeft();
        }
    }
    return std::nullopt;
}

std::optional<int> OffsetsTextRange::unitIndex(TextUnit unit) const {
    if (unit != TextUnit::Line) {
        return std::nullopt;
    }
    const std::optional<int> number = lineNumberFromOffset(startOffset_);
    if (!number.has_value()) {
        return std::nullopt;
    }
    // NVDA's unitIndex is 1 based.
    return *number + 1;
}

std::optional<int> OffsetsTextRange::unitCount(TextUnit unit) const {
    if (unit != TextUnit::Line) {
        return std::nullopt;
    }
    return lineCount();
}

std::string OffsetsTextRange::locationText() const {
    // NVDA: the percentage through the text, then the screen coordinates.
    const int length = std::max(storyLength(), 1);
    const int percent =
        static_cast<int>((static_cast<double>(startOffset_) / static_cast<double>(length)) *
                         100.0);
    std::string result = std::to_string(percent) + "%";
    if (const std::optional<core::Point> point = pointAtStart()) {
        result += ", at " + std::to_string(point->x) + ", " + std::to_string(point->y);
    }
    return result;
}

}  // namespace alus::text
