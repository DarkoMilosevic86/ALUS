// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `TextRange`: a range of text, and everything that can be asked of it.
//
// Port of NVDA's `textInfos.TextInfo` in full (01-ARHITEKTURA.md §7.1). This
// is the interface the whole of phases 5 and 6 is built on: review, say-all,
// the caret, braille (phase 7) and browse mode all speak to text through it,
// and the two implementations behind it are `OffsetsTextRange` (over
// `AtspiText`) and phase 6's document range.
//
// THE NAME. NVDA calls it TextInfo, which says nothing; it IS a range, with
// two endpoints that move. The method names stay NVDA's, because they are what
// the ported code reads like and an app module author (phase 8) knows them.
//
// FOUR PROPERTIES OF THE CONTRACT THAT MATTER MORE THAN THE METHOD LIST:
//
//  1. A RANGE IS A VALUE THAT MOVES, NOT A POSITION. `expand`, `collapse`,
//     `move` and `setEndPoint` mutate it; `copy()` is how a caller keeps the
//     old one. Every ported algorithm depends on that, and the ones that
//     forget to copy are the ones with the famous NVDA bugs.
//  2. `move` RETURNS HOW FAR IT GOT. Not whether it worked: the number of
//     units, negative for backwards. A caller that ignores it walks off the
//     end of a document (say-all's "no more text" test is exactly this).
//  3. FAILURE IS A VALUE, NOT AN EXCEPTION. NVDA raises NotImplementedError
//     and RuntimeError everywhere and catches them at every call site. ALUS
//     returns false or an empty optional, for the same reason the object model
//     does (§6.4, the error contract in backend.hpp): an application closing
//     a window under the caret is the normal case in a screen reader.
//  4. OFFSETS NEVER LEAVE THE IMPLEMENTATION. A caller compares positions with
//     `compareEndPoints` and remembers them with `bookmark()`. That is what
//     lets phase 6 hand out a range over a document tree where "offset" means
//     nothing at all.
#pragma once

#include "alus/core/geometry.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/unit.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::text {

// NVDA: the `POSITION_*` constants a range is constructed at.
enum class Position {
    First,
    Last,
    Caret,
    Selection,
    All,
};

// One end of a range.
enum class EndPoint { Start, End };

// NVDA: the `which` argument of compareEndPoints / setEndPoint, spelled
// "startToStart" and so on.
enum class EndPointPair { StartToStart, StartToEnd, EndToStart, EndToEnd };

std::string_view endPointPairName(EndPointPair which);

// NVDA: `textInfos.Bookmark`. A position that can be returned to later, and
// the one thing a caller may hold on to across pump cycles -- a range itself
// is cheap to make and a stale one is a lie.
//
// `kind` is the implementation's own identity: two bookmarks from different
// kinds of range never compare equal, which is NVDA's `infoClass` test. The
// caret-movement detection of `nav::EditableText` rests on that comparison,
// so it is spelled out rather than left to a pointer comparison on the range.
struct Bookmark {
    const void* kind = nullptr;
    std::int64_t start = 0;
    std::int64_t end = 0;
    // For implementations whose position is not two numbers (phase 6).
    std::string data;

    bool valid() const noexcept { return kind != nullptr; }
    bool operator==(const Bookmark& other) const noexcept {
        return kind == other.kind && start == other.start && end == other.end &&
               data == other.data;
    }
    bool operator!=(const Bookmark& other) const noexcept { return !(*this == other); }
};

class TextRange {
public:
    virtual ~TextRange();

    TextRange(const TextRange&) = delete;
    TextRange& operator=(const TextRange&) = delete;

    // A name for the log and for diagnostics: "atspi", "screen", "document".
    virtual std::string_view rangeKind() const = 0;

    // NVDA: `copy()`. An independent range at the same position.
    virtual std::unique_ptr<TextRange> copy() const = 0;

    // ---- the text ----
    // NVDA: `text`. Not guaranteed to be the same length as the range in
    // offsets -- that is NVDA's own note, and it is why nothing computes an
    // offset from a string length.
    virtual std::string text() const = 0;
    // NVDA: `getTextWithFields`. The base implementation returns the text and
    // nothing else, which is exactly what `OffsetsTextRange` needs until
    // phase 6 brings `getTextWithFields` over a document tree (§9.3).
    virtual TextWithFields getTextWithFields(const FormatReportSettings& settings) const;

    // ---- comparison ----
    // -1, 0 or 1. Comparing ranges of different kinds is not meaningful and
    // answers 0 with a warning in the log, because the alternative is a
    // caller silently believing two documents are the same place.
    virtual int compareEndPoints(const TextRange& other, EndPointPair which) const = 0;
    // NVDA: `setEndPoint`. False when the other range is of another kind.
    virtual bool setEndPoint(const TextRange& other, EndPointPair which) = 0;
    bool isCollapsed() const { return compareEndPoints(*this, EndPointPair::StartToEnd) == 0; }
    // NVDA: `isOverlapping`. Note its warning: in terms of offsets (4,4) and
    // (4,5) do NOT overlap, so a collapsed range is expanded by at least one
    // character before this is asked.
    bool isOverlapping(const TextRange& other) const;

    // ---- moving ----
    // NVDA: `expand`. False when the unit is not supported.
    virtual bool expand(TextUnit unit) = 0;
    virtual void collapse(bool toEnd = false) = 0;
    // NVDA: `move`. The number of units moved: negative backwards, positive
    // forwards, 0 for no movement. See property 2 in the file comment.
    virtual int move(TextUnit unit, int direction,
                     std::optional<EndPoint> endPoint = std::nullopt) = 0;
    // NVDA: `find`. Positions the range at the start of the match.
    virtual bool find(std::string_view text, bool caseSensitive = false,
                      bool reverse = false) = 0;

    // ---- the caret and the selection ----
    virtual bool updateCaret() = 0;
    virtual bool updateSelection() = 0;

    // ---- position memory ----
    virtual Bookmark bookmark() const = 0;
    virtual bool moveToBookmark(const Bookmark& bookmark) = 0;

    // ---- the screen ----
    // NVDA: `boundingRects`. ONE RECTANGLE PER LINE of the range, clipped to
    // the object, and empty when the range is collapsed or off screen. Braille
    // routing and the mouse both need the per-line shape, not a bounding box
    // around a three-line selection.
    virtual std::vector<core::Rect> boundingRects() const;
    virtual std::optional<core::Point> pointAtStart() const;

    // ---- counting ----
    // NVDA: `unitIndex` / `unitCount`. Only `Line` is answerable in general,
    // and only when the implementation knows line numbers.
    virtual std::optional<int> unitIndex(TextUnit unit) const;
    virtual std::optional<int> unitCount(TextUnit unit) const;

    // NVDA: `locationText`. "34%, at 1200, 480" -- what `alus+numpad5` adds
    // when the user asks where they are.
    virtual std::string locationText() const;

    // ---- the reading chunk ----
    // What `TextUnit::ReadingChunk` means right now (§7.1: ALUS synthesises
    // this unit). Say-all asks for the chunk; the user configured what it is.
    ReadingUnit readingUnit() const noexcept { return readingUnit_; }
    void setReadingUnit(ReadingUnit unit) noexcept { readingUnit_ = unit; }
    // NVDA: `_resolveReadingChunkUnit`. Any other unit passes through.
    TextUnit resolveReadingChunk(TextUnit unit) const;

protected:
    TextRange() = default;

    ReadingUnit readingUnit_ = ReadingUnit::Line;
};

using TextRangePtr = std::unique_ptr<TextRange>;

// NVDA's `which` flips when the comparison is made the other way round. Used
// by `setEndPoint` and by the selection-change reporting, where getting it
// wrong produces a selection that jumps.
EndPointPair reverseEndPointPair(EndPointPair which);

}  // namespace alus::text
