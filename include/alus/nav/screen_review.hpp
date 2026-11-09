// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Screen review, without a display model.
//
// 01-ARHITEKTURA.md §10 is the whole design decision and it is worth restating
// here, because this file is the only place it is implemented:
//
//   NVDA's `displayModel.py` is a RECORD OF EVERY TEXT DRAWING CALL, captured
//   by hooking GDI and DirectWrite INSIDE the target process, with a bounding
//   rectangle per character. On Linux that is not available and will not be:
//   GTK4 and Qt6 draw through Cairo, Skia and QPainter into client-side
//   buffers, and under Wayland a client cannot read another client's surface
//   at all.
//
//   So "screen review" in ALUS means FLAT OBJECT REVIEW:
//
//     1. take the active top-level window;
//     2. walk every descendant whose presentationType is content;
//     3. sort them spatially by (top, left) from
//        `Component::get_extents(ATSPI_COORD_TYPE_SCREEN)`;
//     4. join their text into a synthetic text range, with a parallel array of
//        rectangles.
//
// What the user gets from that is the BEHAVIOUR of screen review -- spatial,
// flat, per window -- with no platform hook at all, and `ScreenExplorer`,
// mouse tracking and (later) touch exploration come out of it for free.
//
// WHAT ALUS ADDS TO §10's FOUR STEPS, and why: the objects are GROUPED INTO
// ROWS before being joined. Step 3's sort alone would put three side-by-side
// columns on three separate lines, and then `numpad8` (read the current line)
// would read a third of a row. Objects whose vertical centre falls inside the
// row being built belong to that row, so a window with three columns reads as
// one line with three pieces, left to right -- which is what a sighted user
// sees and what T5.9 measures.
//
// THE SNAPSHOT IS TAKEN ONCE AND SHARED. Phase 4 measured 578 µs PER OBJECT to
// build one (claude/FAZA4.md), so a window with sixty controls costs 35 ms --
// once. Every `copy()` of this range shares the same snapshot, which matters
// because `move()` copies, and the review commands move several times per key
// press.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/core/geometry.hpp"
#include "alus/text/offsets_range.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace alus::a11y {
class Registry;
}

namespace alus::nav {

// How much of the screen review snapshot is allowed to cost.
struct ScreenReviewLimits {
    // The number of objects visited. A window with more content than this is
    // a document, and a document is read through its own text range, not
    // through screen review.
    std::size_t maxObjects = 500;
    // How deep the walk goes. GTK nests four to six levels; Chromium nests
    // much deeper, and beyond this depth nothing is laid out on screen
    // separately anyway.
    int maxDepth = 24;
};

class ScreenReviewRange final : public text::OffsetsTextRange {
public:
    // One piece of the flattened screen: an object, where its text sits in the
    // synthetic story, and where it is on screen.
    struct Piece {
        a11y::ObjectPtr object;
        int start = 0;
        int end = 0;
        core::Rect rect;
    };

    // Null when the window has no content at all (which happens: a window
    // that is still being built).
    static std::unique_ptr<ScreenReviewRange> make(const a11y::ObjectPtr& window,
                                                   text::Position position,
                                                   ScreenReviewLimits limits = {});
    // Positioned at the given object's place in the flattened screen, which is
    // what switching INTO screen review does: the user keeps their place.
    static std::unique_ptr<ScreenReviewRange> makeAtObject(const a11y::ObjectPtr& window,
                                                           const a11y::ObjectPtr& at,
                                                           ScreenReviewLimits limits = {});
    static std::unique_ptr<ScreenReviewRange> makeAtPoint(const a11y::ObjectPtr& window, int x,
                                                          int y, ScreenReviewLimits limits = {});

    ~ScreenReviewRange() override;

    std::string_view rangeKind() const override { return "screen"; }
    std::unique_ptr<text::TextRange> copy() const override;

    // The object whose text covers this offset, or null.
    a11y::ObjectPtr objectAtOffset(int offset) const;
    a11y::ObjectPtr objectAtStart() const { return objectAtOffset(startOffset()); }
    const a11y::ObjectPtr& window() const;
    const std::vector<Piece>& pieces() const;
    // How many objects the walk saw, and whether it hit the budget. Reported
    // so that a window ALUS could not flatten completely is a measurement and
    // not a mystery.
    std::size_t visitedObjects() const;
    bool hitLimit() const;

    // The flattened screen, taken once and shared by every copy of the range.
    // Declared here rather than privately so the file-local builder in
    // src/nav/screen_review.cpp can fill it in; nothing else names it.
    struct Snapshot;

    // PUBLIC, unlike in the base class: the number of screen rows and the
    // offset under a screen point are what mouse tracking, touch exploration
    // and T5.9 ask about from outside.
    std::optional<int> lineCount() const override;
    std::optional<int> offsetFromPoint(int x, int y) const override;

protected:
    const void* bookmarkKind() const override;
    int storyLength() const override;
    std::string storyText() const override;
    std::string textRange(int start, int end) const override;
    std::optional<Offsets> lineOffsets(int offset) const override;
    std::optional<core::Rect> boundingRectFromOffset(int offset) const override;
    std::optional<core::Rect> objectLocation() const override;
    std::optional<int> lineNumberFromOffset(int offset) const override;
    // Screen review has no caret and no selection: it is a view OF the screen,
    // not a position IN a document. The base class returns the right answers
    // (no caret, no selection) and `updateCaret()` therefore fails, which is
    // what makes "move the caret to the review position" refuse honestly.

private:
    explicit ScreenReviewRange(std::shared_ptr<const Snapshot> snapshot);

    std::shared_ptr<const Snapshot> snapshot_;
};

}  // namespace alus::nav
