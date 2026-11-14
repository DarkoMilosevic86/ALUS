// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The region that shows TEXT: a line of an edit field, a paragraph of a
// document, the line the review cursor is on.
//
// Port of NVDA's `braille/regions/textInfo.py` -- `TextInfoRegion`,
// `CursorManagerRegion`, `ReviewTextInfoRegion` and
// `ReviewCursorManagerRegion`.
//
// THIS IS THE SECOND CONSUMER OF `text::TextWithFields`, which is the promise
// phase 6 made (phase-6.md, handover point 1). `getControlFieldBraille` reads
// exactly the fields `getControlFieldSpeech` reads, from the same stream, and
// the only reason this file is long is that braille also has to keep a
// POSITION MAP: every cell must be traceable back to a place in the document,
// or a routing key cannot move the caret.
//
// THREE MAPS, AND WHY EACH ONE EXISTS:
//
//   rawTextTypeforms   codepoint -> bold/italic/underline, for liblouis
//   rawToBraillePos    codepoint -> cell (Region::update fills it)
//   rawToContentPos    codepoint -> position in the DOCUMENT's content
//
// The third is the one NVDA needed and the one that is easy to miss: the
// region's text contains cells that are not content -- "lst", "h2", the
// untranslated-input indicator -- so the Nth cell is not the Nth character.
// `rawToContentPos` is what makes a routing key over "h2" land on the start of
// the heading rather than two characters into it.
//
// THE READING UNIT IS BUILT IN THREE PIECES (NVDA's own comment says why):
// not every text implementation can give the offset of the selection within
// the line, so the line is fetched as "start to selection", "the selection",
// "selection to end". That is also what lets the selection be marked without
// asking the platform for a second set of offsets.
//
// THE N49 RULE. This region holds its SOURCE, and the source holds an
// `a11y::ObjectPtr` -- never a `text::TextRange`. The reading range is built
// fresh in every `update()` and dropped at the end of it, except for
// `readingInfo_`, which lives only between an update and the routing key that
// follows it and is rebuilt by the next update. A region is owned by the
// handler, not by an object, so there is no reference ring.
#pragma once

#include "alus/braille/region.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace alus::nav {
class BrowseMode;
class CursorManager;
class Review;
}

namespace alus::braille {

// Where a text region's text and caret come from. NVDA has four subclasses
// for the four combinations of (object | cursor manager) x (focus | review);
// ALUS has one region and three sources, because the two dimensions are
// independent and spelling out four classes hides that.
class TextSource {
public:
    virtual ~TextSource() = default;

    // NVDA: `_getSelection`. The selection, or a collapsed range at the
    // caret, or null when there is nothing to show.
    virtual text::TextRangePtr selection() const = 0;
    // NVDA: `_setCursor`. Where a routing key puts the caret.
    virtual bool setCursor(const text::TextRange& at) = 0;
    // NVDA: `_isMultiline`. A multi-line control is pinned hard left when
    // focused, because its text is what the user is reading.
    virtual bool isMultiline() const = 0;
    // The object the region is about: what the handler matches against an
    // event, and what a dead source answers null for.
    virtual a11y::ObjectPtr object() const = 0;
    // NVDA: `obj.isTextSelectionAnchoredAtStart`. Which end of a selection
    // the user is moving; it decides which end the display follows.
    virtual bool selectionAnchoredAtStart() const { return true; }
    // NVDA: `info.activate()`. A routing key on the cell the cursor is
    // already on activates instead of moving.
    virtual bool activate(const text::TextRange& /*at*/) { return false; }
    virtual bool isAlive() const = 0;
    // NVDA: the `DocumentWithPageTurns` test. No Linux toolkit exposes page
    // turns through AT-SPI, so this is false everywhere in v1.0 and exists so
    // that the line-movement code reads like NVDA's.
    virtual bool supportsPageTurns() const { return false; }
    virtual bool turnPage(bool /*previous*/) { return false; }
};

// The text of one accessible: an edit field, a terminal, a static label.
class ObjectTextSource final : public TextSource {
public:
    explicit ObjectTextSource(a11y::ObjectPtr object);

    text::TextRangePtr selection() const override;
    bool setCursor(const text::TextRange& at) override;
    bool isMultiline() const override;
    a11y::ObjectPtr object() const override { return object_; }
    bool activate(const text::TextRange& at) override;
    bool isAlive() const override;

private:
    a11y::ObjectPtr object_;
};

// Browse mode: the caret ALUS owns (`nav::CursorManager`). Always multiline,
// because a document is. It takes the whole `BrowseMode` and not just the
// cursor, because activating what a routing key landed on is
// `script_activatePosition` -- the one thing the cursor alone cannot do.
class CursorManagerSource final : public TextSource {
public:
    explicit CursorManagerSource(nav::BrowseMode& browse);

    text::TextRangePtr selection() const override;
    bool setCursor(const text::TextRange& at) override;
    bool isMultiline() const override { return true; }
    a11y::ObjectPtr object() const override { return object_; }
    bool activate(const text::TextRange& at) override;
    bool isAlive() const override;

private:
    nav::BrowseMode* browse_;
    a11y::ObjectPtr object_;
};

// The review cursor. Its "caret" is the review position, and a routing key
// optionally drags the system caret along with it.
class ReviewSource final : public TextSource {
public:
    ReviewSource(nav::Review& review, a11y::ObjectPtr object);

    text::TextRangePtr selection() const override;
    bool setCursor(const text::TextRange& at) override;
    bool isMultiline() const override { return true; }
    a11y::ObjectPtr object() const override { return object_; }
    bool activate(const text::TextRange& at) override;
    bool isAlive() const override;

private:
    nav::Review* review_;
    a11y::ObjectPtr object_;
};

class TextInfoRegion : public Region {
public:
    // `review` is NVDA's `ReviewTextInfoRegion` dimension: a routing key may
    // also move the system caret, and page turns are not attempted.
    TextInfoRegion(RegionHost& host, std::unique_ptr<TextSource> source, bool review = false);
    ~TextInfoRegion() override;

    void update() override;
    void routeTo(int braillePos) override;
    void nextLine() override;
    void previousLine(bool toStart = false) override;
    a11y::ObjectPtr object() const override;
    bool isAlive() const override;
    std::string describe() const override;

    // NVDA: `getTextInfoForBraillePos`. A collapsed range at the document
    // position this cell came from. Null when the region has no reading range
    // (it was never updated, or the text is gone).
    text::TextRangePtr rangeForBraillePos(int braillePos) const;

    // NVDA: `_getReadingUnit`.
    text::TextUnit readingUnit() const;

    TextSource& source() const noexcept { return *source_; }

private:
    // NVDA: `_addTextWithFields`.
    void addTextWithFields(const text::TextRange& range, bool isSelection);
    // NVDA: `_addFieldText`. `contentPos` may be absent, for text that maps
    // to no content at all (the untranslated-input indicator).
    void addFieldText(const std::string& text, std::optional<int> contentPos,
                      bool separate = true);
    void appendContent(const std::string& text, Typeform typeform);
    // NVDA: `_getTypeformFromFormatField`.
    Typeform typeformFor(const text::FormatField& field) const;
    // NVDA: `_routeToTextInfo`.
    void routeToRange(const text::TextRange& at);
    // NVDA's two "speak when braille moved" settings.
    void speakAfterUnitMove(const text::TextRange& at);

    std::unique_ptr<TextSource> source_;
    bool review_ = false;

    // The reading unit as it was last rendered. Kept so that a routing key
    // and the line-movement commands work against the same text the user is
    // touching, which is NVDA's `_readingInfo`.
    text::TextRangePtr readingInfo_;

    // Codepoint index in `rawText` -> codepoint offset within the reading
    // unit's content. See the file comment.
    std::vector<int> rawToContentPos_;
    int currentContentPos_ = 0;
    // NVDA: `_endsWithField`, `_isFormatFieldAtStart`.
    bool endsWithField_ = false;
    bool formatFieldAtStart_ = true;
    // NVDA: `_skipFieldsNotAtStartOfNode`. False for the first chunk of the
    // reading unit and true afterwards, so a container is rendered once even
    // though all three chunks re-open it.
    bool skipFieldsNotAtStartOfNode_ = false;
    // The control fields rendered in THIS update, by unique id. The second
    // and third chunk re-emit the open containers; this is what tells them
    // apart from a container that genuinely begins there.
    std::set<std::string> renderedFields_;

    // NVDA keeps this on the object (`_brailleFormatFieldAttributesCache`) so
    // it survives a region rebuild. Here it is on the region, which is the
    // same lifetime in practice -- a region exists for as long as the display
    // is showing that object -- and it is what makes a bold run report its
    // marker once instead of on every caret movement.
    text::FormatField formatCache_;

    // The untranslated braille input area, in cells. NVDA's
    // `_brailleInputIndStart` and friends.
    std::optional<int> inputIndicatorStart_;
    int inputIndicatorEnd_ = 0;
    int inputStart_ = 0;
    int inputEnd_ = 0;
};

// NVDA: `CursorManagerRegion`. The named deliverable `CursorRegion` of
// phase-plan.md: a text region over the caret ALUS owns rather than over the
// application's.
class CursorRegion final : public TextInfoRegion {
public:
    CursorRegion(RegionHost& host, nav::BrowseMode& browse, bool review = false);
};

// NVDA: `ReviewTextInfoRegion`.
class ReviewTextRegion final : public TextInfoRegion {
public:
    ReviewTextRegion(RegionHost& host, nav::Review& review, a11y::ObjectPtr object);
};

}  // namespace alus::braille
