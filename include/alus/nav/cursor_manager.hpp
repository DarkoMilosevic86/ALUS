// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `CursorManager`: the caret ALUS owns.
//
// Port of NVDA's `cursorManager.py` as 01-ARHITEKTURA.md §8.2 specifies: every
// movement command by line, word, character, paragraph, sentence and page;
// selection with shift; `control+a`; `control+c`; and find
// (`alus+control+f`, `alus+f3`, `alus+shift+f3`).
//
// WHY IT IS NOT A ScriptableObject. One object owns the scripts of a document
// (so that one gesture map section addresses them, and so that the script
// lookup finds them in one place), and that object is `nav::BrowseMode`. This
// class is the CARET and its movement rules; BrowseMode binds the keys and
// delegates. That split is what lets the anchor logic below be tested without
// a gesture, a catalogue or a dispatcher.
//
// THE ANCHOR LOGIC IS THE PART THAT IS EASY TO GET WRONG, and §8.2 says so:
// "the selection anchor logic is transferred verbatim -- it is easy to get
// wrong and end up with a selection that jumps". The rule is:
//
//   * a movement WITHOUT shift sets the caret and clears the anchor;
//   * the FIRST movement with shift plants the anchor where the caret was;
//   * every later movement with shift moves the ACTIVE end only, and the
//     selection is (anchor, active) ordered by document position.
//
// The jumping selection comes from planting the anchor on every shifted
// press, which makes shift+down twice select only the second line.
//
// THE CARET IS A BOOKMARK, not a range. See tree_interceptor.hpp: an
// interceptor lives alongside the object it intercepts, so holding a range
// over that object would close the reference ring that N49 was about.
//
// THREADING: main thread only.
#pragma once

#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <functional>
#include <optional>
#include <string>

namespace alus::nav {

// What moving did, so the caller knows what to say. NVDA returns nothing and
// speaks from inside the move; separating them is what makes the movement
// testable with no synthesizer.
struct CursorMove {
    bool moved = false;
    // The range to read out: the unit the caret landed on, or the selection
    // that changed.
    text::TextRangePtr toRead;
    // The unit `toRead` was expanded to, for the speech layer.
    std::optional<text::TextUnit> unit;
    // True when the move hit the start or the end of the document. NVDA plays
    // a sound and says nothing; the caller decides.
    bool atEdge = false;
    // Set when the selection changed, so the caller reports the difference
    // rather than reading the line.
    bool selectionChanged = false;
    text::TextRangePtr oldSelection;
};

class CursorManager {
public:
    // Builds a fresh range at a position. The manager never holds one.
    using RangeFactory = std::function<text::TextRangePtr(text::Position)>;
    // Rebuilds a range from a bookmark; null when the bookmark is stale, which
    // is the normal answer after the document has changed under the caret.
    using BookmarkFactory = std::function<text::TextRangePtr(const text::Bookmark&)>;

    CursorManager(RangeFactory ranges, BookmarkFactory fromBookmark);
    ~CursorManager();

    CursorManager(const CursorManager&) = delete;
    CursorManager& operator=(const CursorManager&) = delete;

    // The caret, as a range. Null when the document has none yet, or when the
    // remembered position no longer exists -- in which case the caret falls
    // back to the start of the document, which is what NVDA does and what
    // keeps a reloaded page readable.
    text::TextRangePtr caretRange() const;
    const text::Bookmark& caret() const noexcept { return caret_; }
    bool setCaret(const text::TextRange& at, bool clearSelection = true);
    // NVDA: `_caretMovementScriptHelper`. `select` is the shift state.
    CursorMove move(text::TextUnit unit, int direction, bool select = false);
    // NVDA: `script_moveByLine_back` and friends land at the START of a unit;
    // `startOfLine` / `endOfLine` are the two that land at an EDGE instead.
    CursorMove moveToEdge(text::TextUnit unit, bool toEnd, bool select = false);
    CursorMove moveToDocumentEdge(bool toEnd, bool select = false);

    // NVDA: `script_selectAll`.
    CursorMove selectAll();
    // The selection as it stands, or null when there is none.
    text::TextRangePtr selection() const;
    bool hasSelection() const noexcept { return anchor_.valid(); }
    void clearSelection();

    // NVDA: `script_find` / `findNext` / `findPrevious`. The term is
    // remembered here because `alus+f3` means "the previous term" and the
    // dialog that asks for it belongs to phase 9.
    bool find(const std::string& term, bool reverse = false, bool caseSensitive = false);
    bool findNext(bool reverse = false);
    const std::string& lastSearchTerm() const noexcept { return lastTerm_; }
    bool lastSearchCaseSensitive() const noexcept { return lastCaseSensitive_; }

    // Pushes the caret into the application, where the application has one.
    // Browse mode calls it when leaving for focus mode, so the application's
    // own caret is where the user left ALUS's.
    bool applyToApplication() const;

private:
    // The two ends ordered by document position, which is the invariant every
    // selection command restores before it returns.
    text::TextRangePtr selectionBetween(const text::Bookmark& a,
                                        const text::Bookmark& b) const;
    text::TextRangePtr rangeAt(const text::Bookmark& mark) const;

    RangeFactory ranges_;
    BookmarkFactory fromBookmark_;
    text::Bookmark caret_;
    // Where the selection started. Invalid means "no selection", which is why
    // it is not an optional: `Bookmark::valid()` already says it.
    text::Bookmark anchor_;
    std::string lastTerm_;
    bool lastCaseSensitive_ = false;
};

}  // namespace alus::nav
