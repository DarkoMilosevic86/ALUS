// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/cursor_manager.hpp.
#include "alus/nav/cursor_manager.hpp"

#include "alus/core/log.hpp"

#include <utility>

namespace alus::nav {

CursorManager::CursorManager(RangeFactory ranges, BookmarkFactory fromBookmark)
    : ranges_(std::move(ranges)), fromBookmark_(std::move(fromBookmark)) {}

CursorManager::~CursorManager() = default;

text::TextRangePtr CursorManager::rangeAt(const text::Bookmark& mark) const {
    if (!mark.valid() || fromBookmark_ == nullptr) {
        return nullptr;
    }
    return fromBookmark_(mark);
}

text::TextRangePtr CursorManager::caretRange() const {
    if (text::TextRangePtr range = rangeAt(caret_)) {
        return range;
    }
    // The remembered position is gone -- a reloaded page, a collapsed section.
    // The start of the document is the only honest fallback, and it is what
    // keeps the document readable instead of silent.
    if (ranges_ == nullptr) {
        return nullptr;
    }
    return ranges_(text::Position::First);
}

bool CursorManager::setCaret(const text::TextRange& at, bool clearSelectionToo) {
    caret_ = at.bookmark();
    if (clearSelectionToo) {
        anchor_ = text::Bookmark{};
    }
    return caret_.valid();
}

void CursorManager::clearSelection() { anchor_ = text::Bookmark{}; }

text::TextRangePtr CursorManager::selectionBetween(const text::Bookmark& a,
                                                   const text::Bookmark& b) const {
    text::TextRangePtr first = rangeAt(a);
    text::TextRangePtr second = rangeAt(b);
    if (first == nullptr || second == nullptr) {
        return nullptr;
    }
    // Order them, which is the invariant: a selection whose start is after its
    // end reads backwards and copies nothing.
    const int order = first->compareEndPoints(*second, text::EndPointPair::StartToStart);
    text::TextRangePtr lower = order <= 0 ? std::move(first) : std::move(second);
    const text::TextRangePtr& upper = order <= 0 ? second : first;
    lower->setEndPoint(*upper, text::EndPointPair::EndToStart);
    return lower;
}

text::TextRangePtr CursorManager::selection() const {
    if (!anchor_.valid() || !caret_.valid()) {
        return nullptr;
    }
    return selectionBetween(anchor_, caret_);
}

CursorMove CursorManager::move(text::TextUnit unit, int direction, bool select) {
    CursorMove result;
    text::TextRangePtr range = caretRange();
    if (range == nullptr) {
        return result;
    }

    // THE ANCHOR RULE, and the order of these three lines is the rule: the
    // anchor is planted from the caret BEFORE the caret moves, and only when
    // there is not one already. Planting it after the move, or on every press,
    // is the "selection that jumps" §8.2 warns about.
    text::TextRangePtr oldSelection;
    if (select) {
        if (!anchor_.valid()) {
            anchor_ = caret_;
        } else {
            oldSelection = selection();
        }
    } else {
        anchor_ = text::Bookmark{};
    }

    range->collapse();
    const int moved = range->move(unit, direction);
    if (moved == 0) {
        result.atEdge = true;
        // At the edge the caret does not move, but a shifted press that
        // planted an anchor must not leave a half-made selection behind.
        if (select && !oldSelection) {
            anchor_ = text::Bookmark{};
        }
        return result;
    }

    caret_ = range->bookmark();
    result.moved = true;

    if (select) {
        result.selectionChanged = true;
        result.oldSelection = std::move(oldSelection);
        result.toRead = selection();
        result.unit = unit;
        return result;
    }

    // Reading what the caret landed on: the unit, expanded. A caret is a
    // position and a position has no text, which is why nothing reads the
    // collapsed range.
    text::TextRangePtr toRead = range->copy();
    if (toRead->expand(unit)) {
        result.toRead = std::move(toRead);
        result.unit = unit;
    }
    return result;
}

CursorMove CursorManager::moveToEdge(text::TextUnit unit, bool toEnd, bool select) {
    CursorMove result;
    text::TextRangePtr range = caretRange();
    if (range == nullptr) {
        return result;
    }
    text::TextRangePtr oldSelection;
    if (select) {
        if (!anchor_.valid()) {
            anchor_ = caret_;
        } else {
            oldSelection = selection();
        }
    } else {
        anchor_ = text::Bookmark{};
    }

    range->collapse();
    if (!range->expand(unit)) {
        return result;
    }
    range->collapse(toEnd);
    caret_ = range->bookmark();
    result.moved = true;
    if (select) {
        result.selectionChanged = true;
        result.oldSelection = std::move(oldSelection);
        result.toRead = selection();
        return result;
    }
    // At the end of a line there is nothing to the right to read, so NVDA
    // reads the character the caret is on; at the start it reads the line.
    text::TextRangePtr toRead = range->copy();
    if (toRead->expand(toEnd ? text::TextUnit::Character : unit)) {
        result.toRead = std::move(toRead);
        result.unit = toEnd ? text::TextUnit::Character : unit;
    }
    return result;
}

CursorMove CursorManager::moveToDocumentEdge(bool toEnd, bool select) {
    CursorMove result;
    if (ranges_ == nullptr) {
        return result;
    }
    text::TextRangePtr range = ranges_(toEnd ? text::Position::Last : text::Position::First);
    if (range == nullptr) {
        return result;
    }
    text::TextRangePtr oldSelection;
    if (select) {
        if (!anchor_.valid()) {
            anchor_ = caret_;
        } else {
            oldSelection = selection();
        }
    } else {
        anchor_ = text::Bookmark{};
    }
    range->collapse();
    caret_ = range->bookmark();
    result.moved = true;
    if (select) {
        result.selectionChanged = true;
        result.oldSelection = std::move(oldSelection);
        result.toRead = selection();
        return result;
    }
    text::TextRangePtr toRead = range->copy();
    if (toRead->expand(text::TextUnit::Line)) {
        result.toRead = std::move(toRead);
        result.unit = text::TextUnit::Line;
    }
    return result;
}

CursorMove CursorManager::selectAll() {
    CursorMove result;
    if (ranges_ == nullptr) {
        return result;
    }
    text::TextRangePtr whole = ranges_(text::Position::All);
    if (whole == nullptr) {
        return result;
    }
    text::TextRangePtr startOnly = whole->copy();
    startOnly->collapse(false);
    text::TextRangePtr endOnly = whole->copy();
    endOnly->collapse(true);
    anchor_ = startOnly->bookmark();
    caret_ = endOnly->bookmark();
    result.moved = true;
    result.selectionChanged = true;
    result.toRead = std::move(whole);
    return result;
}

bool CursorManager::find(const std::string& term, bool reverse, bool caseSensitive) {
    if (term.empty()) {
        return false;
    }
    lastTerm_ = term;
    lastCaseSensitive_ = caseSensitive;
    text::TextRangePtr range = caretRange();
    if (range == nullptr) {
        return false;
    }
    range->collapse();
    if (!range->find(term, caseSensitive, reverse)) {
        return false;
    }
    anchor_ = text::Bookmark{};
    caret_ = range->bookmark();
    return true;
}

bool CursorManager::findNext(bool reverse) {
    if (lastTerm_.empty()) {
        return false;
    }
    return find(lastTerm_, reverse, lastCaseSensitive_);
}

bool CursorManager::applyToApplication() const {
    text::TextRangePtr range = caretRange();
    if (range == nullptr) {
        return false;
    }
    range->collapse();
    return range->updateCaret();
}

}  // namespace alus::nav
