// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// A region: one piece of the braille display's content.
//
// Port of NVDA's `braille/regions/base.py` (`Region`, `TextRegion`) and
// `braille/regions/NVDAObject.py` (`NVDAObjectRegion`).
//
// WHAT A REGION IS FOR. The display shows a line of cells, but that line is
// assembled from independent pieces: the focus ancestors, the focused control,
// the text inside it, a message. Each piece knows how to produce its own text,
// how to translate it, and what a routing key pressed over it should do. The
// `Buffer` concatenates the cells; it never looks inside.
//
// POSITIONS ARE CODEPOINT INDICES, NOT BYTES. `rawText` is UTF-8, but
// `cursorPos`, `selectionStart`, `rawToBraillePos` and `rawTextTypeforms` all
// count CODEPOINTS -- because liblouis counts `widechar`, and §7.3's rule is
// that the conversion happens at one boundary. That boundary is
// `Region::update()`. A caller that has a byte offset converts it with
// `text::OffsetConverter` before setting `cursorPos`.
//
// THE ONE THING SUBCLASSES MUST DO: set `rawText` (and optionally `cursorPos`,
// the selection and the typeforms) and then call `Region::update()`, which
// translates and fills `brailleCells`, `rawToBraillePos`, `brailleToRawPos`
// and `brailleCursorPos`. Forgetting the base call leaves a region with text
// and no cells, which shows as a hole in the display and nothing in the log.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/a11y/settings.hpp"
#include "alus/braille/display.hpp"
#include "alus/braille/louis.hpp"
#include "alus/braille/settings.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace alus::braille {

// Everything a region needs from the layer above it, as an interface rather
// than a pointer to the handler: it is what lets a region be built and
// translated in a test with no display, no object model and no speech. The
// same reasoning as `a11y::PresentationHooks` and `GestureManager::Hooks`.
class RegionHost {
public:
    virtual ~RegionHost() = default;

    virtual const Settings& brailleSettings() const = 0;
    virtual const a11y::Settings& objectSettings() const = 0;
    virtual const text::FormatReportSettings& formatSettings() const = 0;
    virtual Louis& louis() const = 0;
    // The display's shape right now. The buffer needs it for every window
    // calculation, and a region never does -- but one seam to the layer above
    // is easier to keep honest than two.
    virtual DisplayDimensions dimensions() const = 0;
    // The table list handed to liblouis: the output table, then
    // `braille-patterns.cti` so that the Unicode braille in the state shapes
    // and the formatting tags passes through unchanged.
    virtual std::vector<std::string> outputTables() const = 0;

    // ---- braille input, shown inside a text region while a word is typed ----
    virtual std::string untranslatedBraille() const { return {}; }
    virtual int untranslatedCursorPos() const { return 0; }
    virtual void setUntranslatedCursorPos(int /*position*/) {}
    virtual void updateForUntranslatedInput() {}

    // ---- speech, for the two settings that make braille talk ----
    // NVDA: `_speakOnRouting`. The character a routing key landed on.
    virtual void speakCharacterAt(const text::TextRange& /*at*/) {}
    // NVDA: `_speakOnNavigatingByUnit`.
    virtual void speakUnitAt(const text::TextRange& /*at*/, text::TextUnit /*unit*/) {}

    // NVDA: `braille.handler.autoScroll(enable=False)` when a line move fails.
    virtual void stopAutoScroll() {}
    // Whether a routing key over the review position should also move the
    // system caret (NVDA: `_routingShouldMoveSystemCaret`).
    virtual bool routingShouldMoveSystemCaret() const { return false; }
};

class Region {
public:
    virtual ~Region();

    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;

    // ---- what a subclass fills in ----
    // UTF-8.
    std::string rawText;
    // Codepoint indices into `rawText`; see the file comment.
    std::optional<int> cursorPos;
    std::optional<int> selectionStart;
    std::optional<int> selectionEnd;
    // One per codepoint of `rawText`, or empty for plain text.
    std::vector<Typeform> rawTextTypeforms;
    // NVDA: `hidePreviousRegions`. A text region that does not start at the
    // beginning of its object hides the context in front of it, because the
    // context is no longer where the display is.
    bool hidePreviousRegions = false;
    // NVDA: `focusToHardLeft`. This region is pinned to the left edge when
    // focused, rather than being scrolled to show as much context as fits.
    bool focusToHardLeft = false;

    // ---- what update() produces ----
    std::vector<std::uint8_t> brailleCells;
    // Codepoint index -> cell index, one entry per codepoint.
    std::vector<int> rawToBraillePos;
    // Cell index -> codepoint index, one entry per cell.
    std::vector<int> brailleToRawPos;
    std::optional<int> brailleCursorPos;
    std::optional<int> brailleSelectionStart;
    std::optional<int> brailleSelectionEnd;

    // NVDA: `Region.update`. Translates `rawText` and fills everything above.
    // A subclass extends it and calls this at the END.
    virtual void update();

    // NVDA: `Region.routeTo`. A routing key was pressed over this cell.
    virtual void routeTo(int /*braillePos*/) {}
    // NVDA: `Region.nextLine` / `previousLine`. Only a text region can move.
    virtual void nextLine() {}
    virtual void previousLine(bool /*toStart*/ = false) {}

    // The object this region is about, for the handler's "is this region
    // already showing that object" test. Null for a plain text region.
    virtual a11y::ObjectPtr object() const { return nullptr; }
    // False once what the region shows is gone, so the handler can skip the
    // update instead of letting it throw its way out (NVDA catches there).
    virtual bool isAlive() const { return true; }

    // For the log and the tests.
    virtual std::string describe() const;

    // NVDA: `_focusAncestorIndex`, set on a region built for a focus ancestor.
    // -1 means "not an ancestor region", which is what `_doNewObject` tests.
    int focusAncestorIndex = -1;

    // NVDA: `TextInfoRegion.pendingCaretUpdate`. Set by the handler when the
    // caret moved inside this region, so that after the coalesced update the
    // window scrolls to the cursor rather than staying where it was.
    bool pendingCaretUpdate = false;

protected:
    explicit Region(RegionHost& host);

    RegionHost& host() const noexcept { return *host_; }
    // The number of codepoints in `rawText`, computed once per update.
    int rawLength() const noexcept { return rawLength_; }

private:
    RegionHost* host_;
    int rawLength_ = 0;
};

using RegionPtr = std::unique_ptr<Region>;

// NVDA: `TextRegion`. A fixed string: a message, a piece of speech shown in
// braille, a label the caller already assembled.
class TextRegion final : public Region {
public:
    TextRegion(RegionHost& host, std::string text);

    std::string describe() const override;
};

// NVDA: `NVDAObjectRegion`. One control, as properties: name, role, states.
// A routing key over it performs the control's default action, which is how a
// braille user presses a button without a keyboard.
class ObjectRegion : public Region {
public:
    // `appendText` is NVDA's: a space is appended when this region is
    // followed by another, so that the pieces do not run together.
    ObjectRegion(RegionHost& host, a11y::ObjectPtr object, std::string appendText = {});

    void update() override;
    void routeTo(int braillePos) override;
    a11y::ObjectPtr object() const override { return object_; }
    bool isAlive() const override;
    std::string describe() const override;

protected:
    // NVDA: `ReviewNVDAObjectRegion.routeTo`. When the display is tethered to
    // review and the user asked for routing to move the system caret, a
    // routing key FOCUSES the object before activating it.
    bool review_ = false;

private:
    a11y::ObjectPtr object_;
    std::string appendText_;
};

// NVDA: `ReviewNVDAObjectRegion`.
class ReviewObjectRegion final : public ObjectRegion {
public:
    ReviewObjectRegion(RegionHost& host, a11y::ObjectPtr object, std::string appendText = {});
};

}  // namespace alus::braille
