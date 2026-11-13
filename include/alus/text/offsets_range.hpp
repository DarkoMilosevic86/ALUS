// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `OffsetsTextRange`: a text range whose position is two numbers.
//
// Port of NVDA's `textInfos.offsets.OffsetsTextInfo` (952 lines), which is the
// base of nearly every text implementation NVDA has. 01-ARHITEKTURA.md §7.2
// maps its hooks onto `AtspiText` one for one, and the subclass that does that
// is `a11y::ObjectTextRange`.
//
// THE UNIT OF THE OFFSETS IS THE SUBCLASS'S, which is NVDA's `encoding`:
// `offsetUnit()` says whether an offset counts UTF-8 bytes or Unicode
// codepoints, and every hook takes and returns offsets in that unit. Nothing on
// the hot path converts anything; the conversion happens only where a locally
// held UTF-8 string is sliced or segmented -- the per-character and per-word
// steps, the fallback line search, `find` -- through `text::OffsetConverter`.
//
// THIS WAS "THE OFFSETS ARE UTF-8 BYTES", §7.3's original decision, on the
// belief that AT-SPI counts in bytes. It does not: AT-SPI text offsets are
// CODEPOINTS (measured on Chrome / AT-SPI 2.56.2 with an independent binding:
// "Prvi pasus je proza. Ima dve rečenice." reports 38 characters, is 39
// bytes, and get_text(31, 32) is "č"). With bytes assumed, every letter
// outside ASCII was a step of the wrong size: the right arrow on "Treći" read
// "ći" and then skipped the "i", and for Cyrillic every letter is two bytes
// (FAZA10.md N113). The mock backend counted in bytes too, so every test with
// a Serbian letter in it agreed with the defect.
//
// WHAT A SUBCLASS MUST PROVIDE: `storyLength()`. Nothing else is mandatory.
// Beyond that there are two ways to be useful, and NVDA's own words on the
// difference are worth keeping:
//
//   * implement `storyText()` only, and the base class finds lines and words
//     by SEARCHING THE WHOLE TEXT -- "very inefficient and should be avoided
//     if possible", because every arrow key press then costs the entire
//     contents of the document over D-Bus;
//   * implement `textRange()` and `lineOffsets()` and the platform answers
//     instead, which is what `AtspiText::get_string_at_offset` is for.
//
// The hooks are named as NVDA names them, minus the leading underscore.
#pragma once

#include "alus/text/fields.hpp"
#include "alus/text/range.hpp"

#include <optional>
#include <string>
#include <utility>

namespace alus::text {

class OffsetsTextRange : public TextRange {
public:
    // A byte range. `end` is exclusive.
    struct Offsets {
        int start = 0;
        int end = 0;

        bool operator==(const Offsets& other) const noexcept {
            return start == other.start && end == other.end;
        }
    };

    ~OffsetsTextRange() override;

    std::string_view rangeKind() const override { return "offsets"; }

    // ---- construction ----
    // NVDA does this in the constructor; a C++ constructor cannot call the
    // subclass's virtual hooks, so it is a separate step the subclass's own
    // factory calls. Returns false when the position is not obtainable (no
    // caret, no selection), leaving the range collapsed at 0.
    bool initialize(Position position);
    void setOffsets(int start, int end);
    // The offsets as they stand. Public for the tests and for the subclass's
    // own hooks; see property 4 in range.hpp -- callers above the text layer
    // use bookmarks instead.
    int startOffset() const noexcept { return startOffset_; }
    int endOffset() const noexcept { return endOffset_; }

    // ---- TextRange ----
    std::string text() const override;
    TextWithFields getTextWithFields(const FormatReportSettings& settings) const override;
    int compareEndPoints(const TextRange& other, EndPointPair which) const override;
    bool setEndPoint(const TextRange& other, EndPointPair which) override;
    bool expand(TextUnit unit) override;
    void collapse(bool toEnd = false) override;
    int move(TextUnit unit, int direction,
             std::optional<EndPoint> endPoint = std::nullopt) override;
    bool find(std::string_view text, bool caseSensitive = false,
              bool reverse = false) override;
    bool updateCaret() override;
    bool updateSelection() override;
    Bookmark bookmark() const override;
    bool moveToBookmark(const Bookmark& bookmark) override;
    std::vector<core::Rect> boundingRects() const override;
    std::optional<core::Point> pointAtStart() const override;
    std::optional<int> unitIndex(TextUnit unit) const override;
    std::optional<int> unitCount(TextUnit unit) const override;
    std::string locationText() const override;

    // The byte range of one unit around an offset, which is what `expand` and
    // `move` are built from. Public because `nav::ParagraphNav` and the review
    // layer ask for a unit's bounds without moving anything.
    std::optional<Offsets> unitOffsets(TextUnit unit, int offset) const;

protected:
    OffsetsTextRange() = default;

    // The identity used by `bookmark()`: all ranges over the same object must
    // return the same pointer, and ranges over different objects must not.
    // NVDA compares the TextInfo CLASS plus the object; a bookmark that
    // compares equal across two different documents is how a caret-movement
    // detector concludes the caret did not move when it moved to another file.
    virtual const void* bookmarkKind() const = 0;

    // ---- the unit ----
    // NVDA: `encoding`. What one offset counts. The default is the one that
    // needs no conversion for a string ALUS holds itself (the screen review's
    // story); a range over a platform object answers what the PLATFORM counts
    // -- codepoints, for AT-SPI.
    enum class OffsetUnit { Utf8Bytes, Codepoints };
    virtual OffsetUnit offsetUnit() const { return OffsetUnit::Utf8Bytes; }

    // ---- the mandatory hook ----
    // NVDA: `_getStoryLength`. In `offsetUnit()`.
    virtual int storyLength() const = 0;

    // ---- text ----
    // NVDA: `_getStoryText`. Empty when the subclass does not implement it.
    virtual std::string storyText() const;
    // NVDA: `_getTextRange`. The default slices `storyText()`, which is the
    // inefficient path the file comment warns about.
    virtual std::string textRange(int start, int end) const;

    // ---- the caret and the selection ----
    // NVDA: `_getCaretOffset` and friends. An unset optional means the
    // implementation has no caret, which is NVDA's NotImplementedError.
    virtual std::optional<int> caretOffset() const;
    virtual bool setCaretOffset(int offset);
    virtual std::optional<Offsets> selectionOffsets() const;
    virtual bool setSelectionOffsets(int start, int end);

    // ---- unit boundaries ----
    // Each returns an unset optional for "ask the base class instead", which
    // is how the platform's own answer is preferred when it has one and the
    // ported search is used when it does not.
    virtual std::optional<Offsets> lineOffsets(int offset) const;
    virtual std::optional<Offsets> wordOffsets(int offset) const;
    virtual std::optional<Offsets> sentenceOffsets(int offset) const;
    virtual std::optional<Offsets> paragraphOffsets(int offset) const;
    virtual std::optional<Offsets> characterOffsets(int offset) const;

    // NVDA: `_getLineNumFromOffset`, zero based.
    virtual std::optional<int> lineNumberFromOffset(int offset) const;
    virtual std::optional<int> lineCount() const;

    // ---- the screen ----
    // NVDA: `_getBoundingRectFromOffset`, `_getOffsetFromPoint`.
    virtual std::optional<core::Rect> boundingRectFromOffset(int offset) const;
    virtual std::optional<int> offsetFromPoint(int x, int y) const;
    // The location of the object the text belongs to; `boundingRects` clips to
    // it, so that a scrolled-away line does not report coordinates outside the
    // widget (NVDA does this and it matters for braille routing).
    virtual std::optional<core::Rect> objectLocation() const;
    // NVDA: `_getFirstVisibleOffset` / `_getLastVisibleOffset`.
    virtual std::optional<int> firstVisibleOffset() const;
    virtual std::optional<int> lastVisibleOffset() const;

    // ---- formatting ----
    // NVDA: `_getFormatFieldAndOffsets`. The base implementation reports the
    // line number and nothing else, which is what the plan's "+ normalizator"
    // replaces in `a11y::ObjectTextRange`.
    virtual std::pair<FormatField, Offsets> formatFieldAndOffsets(
        int offset, const FormatReportSettings& settings, bool calculateOffsets) const;
    // NVDA: `detectFormattingAfterCursorMaybeSlow`. True means "honour the
    // user's detectFormatAfterCursor setting, because asking is expensive".
    virtual bool formatDetectionAfterCursorMaybeSlow() const { return true; }

    // NVDA: `allowMoveToUnitOffsetPastEnd` (#2096). True lets `move` reach one
    // offset past the end, so braille routing and the review cursor can sit on
    // the insertion point after the last character.
    virtual bool allowMoveToUnitOffsetPastEnd(TextUnit unit) const;

    // The locale used for segmentation diagnostics and for the spelling of
    // text read from this range. Empty means the ALUS locale.
    virtual std::string textLocale() const { return {}; }

    // Copies the offsets of another range of this class. The subclass's
    // `copy()` calls it, which is NVDA's "direct TextInfo to TextInfo copy".
    void copyOffsetsFrom(const OffsetsTextRange& other);

private:
    // A UTF-8 string held locally, and positions in it: `units` counted in
    // `offsetUnit()` from the start of `text` <-> a byte index into `text`.
    // Identity for Utf8Bytes; OffsetConverter for Codepoints.
    std::size_t unitsToBytes(std::string_view text, int units) const;
    int bytesToUnits(std::string_view text, std::size_t bytes) const;

    std::optional<Offsets> lineOffsetsResolved(int offset) const;
    std::optional<Offsets> characterOffsetsResolved(int offset) const;
    std::optional<Offsets> wordOffsetsResolved(int offset) const;
    std::optional<Offsets> sentenceOffsetsResolved(int offset) const;
    std::optional<Offsets> paragraphOffsetsResolved(int offset) const;
    const OffsetsTextRange* sameKind(const TextRange& other) const;

    int startOffset_ = 0;
    int endOffset_ = 0;
};

}  // namespace alus::text
