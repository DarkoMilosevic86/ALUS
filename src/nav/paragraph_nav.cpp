// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/paragraph_nav.hpp.

#include "alus/nav/paragraph_nav.hpp"

#include "alus/i18n/i18n.hpp"
#include "alus/text/offsets.hpp"
#include "alus/text/segment.hpp"

#include <algorithm>

namespace alus::nav {
namespace {

std::string trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

// NVDA: `_isLastLineOfParagraph`. A line whose text ends in a break IS the end
// of a paragraph -- that is the only signal the single-line-break style has.
bool isLastLineOfParagraph(std::string_view lineText) {
    std::string_view stripped = lineText;
    while (!stripped.empty() && (stripped.back() == ' ' || stripped.back() == '\t')) {
        stripped.remove_suffix(1);
    }
    return !stripped.empty() && (stripped.back() == '\r' || stripped.back() == '\n');
}

bool isBlankLine(const text::TextRange& range) {
    text::TextRangePtr line = range.copy();
    line->expand(text::TextUnit::Line);
    return trimmed(line->text()).empty();
}

std::string lineTextOf(const text::TextRange& range) {
    text::TextRangePtr line = range.copy();
    line->expand(text::TextUnit::Line);
    return line->text();
}

// NVDA: `_moveTextInfoToSingleLineBreakParagraph`, transferred including the
// `moveBackTwice` rule -- standing on the LAST line of a paragraph and moving
// backwards has to cross two paragraph markers, not one, or the command does
// nothing the first time it is pressed.
bool moveToSingleLineBreakParagraph(text::TextRange& range, bool nextParagraph) {
    bool moved = false;
    range.expand(text::TextUnit::Line);
    range.collapse();
    const int offset = nextParagraph ? 1 : -1;
    int lines = 0;
    bool moveBackTwice = isLastLineOfParagraph(lineTextOf(range));

    while (lines < kMaxParagraphSearchLines) {
        if (isLastLineOfParagraph(lineTextOf(range))) {
            if (!nextParagraph) {
                if (!moveBackTwice) {
                    while (lines < kMaxParagraphSearchLines) {
                        if (range.move(text::TextUnit::Line, -1) == 0) {
                            // Pinned to the beginning of the text, which still
                            // counts as having moved.
                            moved = true;
                            break;
                        }
                        if (isLastLineOfParagraph(lineTextOf(range))) {
                            range.move(text::TextUnit::Line, 1);
                            moved = true;
                            break;
                        }
                        ++lines;
                    }
                    break;
                }
                moveBackTwice = false;
            } else {
                if (range.move(text::TextUnit::Line, 1) != 0) {
                    moved = true;
                }
                break;
            }
        }
        if (range.move(text::TextUnit::Line, offset) == 0) {
            break;
        }
        ++lines;
    }
    return moved;
}

// NVDA: `_moveTextInfoToMultiLineBreakParagraph`. Find a blank line, then the
// next non-blank one; moving backwards then climbs to the TOP of the paragraph
// it landed in, which is the exception in NVDA's own comment.
bool moveToMultiLineBreakParagraph(text::TextRange& range, bool nextParagraph) {
    bool moved = false;
    bool lookingForBlank = true;
    const int offset = nextParagraph ? 1 : -1;
    int lines = 0;

    while (lines < kMaxParagraphSearchLines) {
        const bool blank = isBlankLine(range);
        if (lookingForBlank && blank) {
            lookingForBlank = false;
        }
        if (!lookingForBlank && !blank) {
            moved = true;
            break;
        }
        if (range.move(text::TextUnit::Line, offset) == 0) {
            break;
        }
        ++lines;
    }

    if (moved && !nextParagraph) {
        moved = false;
        while (lines < kMaxParagraphSearchLines) {
            if (range.move(text::TextUnit::Line, -1) == 0) {
                // The top of the text.
                moved = true;
                break;
            }
            if (isBlankLine(range)) {
                range.move(text::TextUnit::Line, 1);
                moved = true;
                break;
            }
            ++lines;
        }
    }
    return moved;
}

}  // namespace

std::string paragraphTextAt(const text::TextRange& range, text::ParagraphStyle style) {
    std::string paragraph;
    int lines = 0;
    text::TextRangePtr cursor = range.copy();
    while (lines < kMaxParagraphSearchLines) {
        cursor->expand(text::TextUnit::Line);
        const std::string raw = cursor->text();
        const std::string line = trimmed(raw);
        if (style == text::ParagraphStyle::MultiLineBreak) {
            // A blank line ENDS the paragraph in this style.
            if (line.empty()) {
                break;
            }
            paragraph += line;
            paragraph += "\n";
        } else {
            if (!line.empty()) {
                paragraph += line;
                paragraph += " ";
            }
            if (isLastLineOfParagraph(raw)) {
                break;
            }
        }
        if (cursor->move(text::TextUnit::Line, 1) == 0) {
            break;
        }
        ++lines;
    }
    return trimmed(paragraph);
}

std::vector<std::string> splitParagraphIntoChunks(const std::string& paragraph) {
    std::vector<std::string> chunks;
    if (paragraph.size() <= kPreferredChunkSize) {
        chunks.push_back(paragraph);
        return chunks;
    }
    // THE SPLIT IS AT SENTENCE ENDS, NEVER MID SENTENCE: a synthesizer handed
    // half a sentence reads it with the wrong prosody, and the pause lands in
    // the middle of a thought.
    const text::Segmenter segmenter(paragraph);
    std::size_t chunkStart = 0;
    std::size_t offset = 0;
    std::size_t lastEnd = 0;
    while (offset < paragraph.size()) {
        const text::Segment sentence = segmenter.sentenceAt(offset);
        if (sentence.end <= offset) {
            break;
        }
        if (sentence.end - chunkStart > kPreferredChunkSize) {
            const std::size_t end = lastEnd > chunkStart ? lastEnd : sentence.end;
            chunks.push_back(paragraph.substr(chunkStart, end - chunkStart));
            chunkStart = end;
        }
        lastEnd = sentence.end;
        offset = sentence.end;
    }
    if (chunkStart < paragraph.size()) {
        chunks.push_back(paragraph.substr(chunkStart));
    }
    return chunks;
}

ParagraphMoveResult moveToParagraph(text::TextRange& range, text::ParagraphStyle style,
                                   bool nextParagraph, bool speakNew) {
    ParagraphMoveResult result;
    if (style == text::ParagraphStyle::Application) {
        // Nothing to compute: the application's own unit is the answer, and
        // the caller moves by TextUnit::Paragraph.
        result.passKey = true;
        return result;
    }
    const bool moved = style == text::ParagraphStyle::SingleLineBreak
                           ? moveToSingleLineBreakParagraph(range, nextParagraph)
                           : moveToMultiLineBreakParagraph(range, nextParagraph);
    result.moved = moved;
    if (!moved) {
        // Translators: there is no paragraph in that direction.
        // Translators: there is no paragraph after this one.
        // Translators: there is no paragraph before this one.
        result.message = nextParagraph ? _("No next paragraph") : _("No previous paragraph");
        return result;
    }
    if (speakNew) {
        result.text = paragraphTextAt(range, style);
        if (result.text.empty()) {
            // Translators: spoken when a paragraph is blank.
            result.text = _("blank");
        }
        // The caller speaks `text`, not the line unit: a paragraph in these
        // styles spans several line units and `speakRange` would read only
        // the first of them.
        result.speakNew = false;
    }
    return result;
}

text::ParagraphStyle nextParagraphStyle(text::ParagraphStyle style) {
    switch (style) {
        case text::ParagraphStyle::Application:
            return text::ParagraphStyle::SingleLineBreak;
        case text::ParagraphStyle::SingleLineBreak:
            return text::ParagraphStyle::MultiLineBreak;
        case text::ParagraphStyle::MultiLineBreak:
            return text::ParagraphStyle::Application;
    }
    return text::ParagraphStyle::Application;
}

std::string_view paragraphStyleLabel(text::ParagraphStyle style) {
    switch (style) {
        // Translators: one of the paragraph navigation styles; the
        // application decides where a paragraph ends.
        case text::ParagraphStyle::Application:
            return _("Application paragraph navigation");
        // Translators: one of the paragraph navigation styles; a single line
        // break ends a paragraph.
        case text::ParagraphStyle::SingleLineBreak:
            return _("Single line break paragraph navigation");
        // Translators: one of the paragraph navigation styles; a blank line
        // ends a paragraph.
        case text::ParagraphStyle::MultiLineBreak:
            return _("Multi line break paragraph navigation");
    }
    return _("Application paragraph navigation");
}

}  // namespace alus::nav
