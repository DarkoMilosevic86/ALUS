// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Three kinds of paragraph.
//
// Port of NVDA's `documentNavigation/paragraphHelper.py` (301 lines) and
// `sentenceHelper.py`. 01-ARHITEKTURA.md §11.2 names the three styles and one
// convention that must be kept:
//
//     APPLICATION        trust the application's own paragraph unit
//     SINGLE_LINE_BREAK  every line break starts a paragraph
//     MULTI_LINE_BREAK   only a blank line starts one (the Markdown reading)
//
//     "The (passKey, moved) return convention allows falling back to the
//      application's behaviour -- keep it."
//
// WHY THE OTHER TWO STYLES EXIST AT ALL. An application's idea of a paragraph
// is whatever its text layer says, and for a code editor or a terminal that is
// a LINE -- so `control+downArrow` moves one line and the user who asked for a
// paragraph gets a line. The two break-counting styles compute paragraphs from
// the text itself, over the LINE unit, which every implementation has.
//
// `passKey` is how a style declines: there is no caret, or the range is one
// these styles cannot search. The caller then lets the application's own
// paragraph movement stand -- on Wayland it is already happening, because the
// application received the key in parallel (see editable_text.hpp, point A).
#pragma once

#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace alus::nav {

// NVDA's `(passKey, moved)` tuple, with the third thing a caller needs.
struct ParagraphMoveResult {
    // Let the application do it: ALUS declined.
    bool passKey = false;
    // The range was moved.
    bool moved = false;
    // Whether the new paragraph should be spoken by the caller. False when
    // this function already spoke it (the two break styles assemble the
    // paragraph text themselves, because it spans several line units).
    bool speakNew = false;
    // The assembled paragraph, for the caller to speak. Empty when there is
    // nothing to say, in which case "blank" is the honest answer.
    std::string text;
    // Set when the move failed: "No next paragraph" / "No previous paragraph".
    std::string message;
};

// How many lines a search gives up after. NVDA: MAX_LINES.
inline constexpr int kMaxParagraphSearchLines = 250;
// What makes a paragraph a TEXT paragraph: `p` and `shift+p` jump between the
// paragraphs whose text matches this, skipping headings, menus and rows of
// links. It is UPSTREAM'S expression, assembled from upstream's own named
// pieces (NVDA's config/configDefaults.py, DEFAULT_TEXT_PARAGRAPH_REGEX):
//
//   lookBehind  (?<=\w)(?<!\d)   a word character that is not a digit, so
//                                 "3." in a numbered list is not a sentence
//   optQuote    ["”»)]?           a closing quote, BEFORE or AFTER the stop:
//                                 upstream's template has this piece twice,
//                                 which is what makes both `ovako".` and
//                                 `ovako."` the end of a sentence
//   punc        [.…]{1,3}         a full stop, or an ellipsis
//   optWiki     (\[\d+\])*        "[1][2]" footnote markers
//   lookAhead   (?=[\r\n ...]|$)  end of line, or a space
//   punc2       [?!]              a question or an exclamation anywhere
//   cjk         [．！？：；]          the CJK full-width equivalents
//
// It is HERE and not inside browse_mode.cpp because what the key means to a
// user -- "skip the navigation, take me to the article" -- is entirely a
// property of this expression, and tests/nav asserts on it directly.
inline constexpr std::string_view kTextParagraphPattern =
    "((?<=\\w)(?<!\\d)[\"\u201d\u00bb)]?[.\u2026]{1,3}[\"\u201d\u00bb)]?"
    "(\\[\\d+\\])*(?=[\\r\\n \u00a0]|$)"
    "|[?!]"
    "|[\uff0e\uff01\uff1f\uff1a\uff1b])";

// NVDA: PREFERRED_CHUNK_SIZE. A long paragraph is handed to the synthesizer in
// pieces, because some synthesizers are slow to start on a large utterance --
// and the pieces are split AT SENTENCE ENDS, so the prosody survives.
inline constexpr std::size_t kPreferredChunkSize = 1024;

// Moves `range` to the previous or next paragraph in the given style. The
// range is left at the START of the new paragraph; the caller moves the caret.
ParagraphMoveResult moveToParagraph(text::TextRange& range, text::ParagraphStyle style,
                                   bool nextParagraph, bool speakNew);

// The text of the paragraph the range is in, assembled over as many line units
// as it takes. NVDA: `speakSingleLineBreakParagraph` /
// `speakMultiLineBreakParagraph`, minus the speaking.
std::string paragraphTextAt(const text::TextRange& range, text::ParagraphStyle style);

// NVDA: `_splitParagraphIntoChunks`. Splits at sentence ends, never mid
// sentence, and returns the whole thing in one piece when it is short enough.
std::vector<std::string> splitParagraphIntoChunks(const std::string& paragraph);

// NVDA: `nextParagraphStyle`. Cycles the style, which is a command of its own.
text::ParagraphStyle nextParagraphStyle(text::ParagraphStyle style);
// What is spoken when the style changes.
std::string_view paragraphStyleLabel(text::ParagraphStyle style);

}  // namespace alus::nav
