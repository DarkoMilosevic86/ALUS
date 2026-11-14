// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The review and caret command group: the numpad, and `alus+arrows`.
//
// The commands of NVDA's `globalCommands.GlobalCommands` that belong to phase
// 5 -- its "Text review" and "System caret" categories. Like phase 4's
// `a11y::ObjectCommands`, this is another step-10 command group, not a second
// `GlobalCommands`: it ANSWERS TO that NVDA class name, so the generated
// command table (data/scripts.csv, §5.4) gives every script here NVDA's own
// description, category and keys, and nothing is retyped (docs/history/phase-4.md O4).
//
// WHAT THE USER GETS, which is the manual test of phase-plan.md:
//
//     numpad7/8/9     previous / current / next line
//     numpad4/5/6     previous / current / next word
//     numpad1/2/3     previous / current / next character
//     numpad2 twice   the character described;  three times its value
//     numpadPlus      say all from the review cursor
//     alus+downArrow  say all from the caret
//     alus+upArrow    the current line;  twice spells it;  three times describes it
//     alus+numpad7/1  the review mode
//     alus+f / alus+shift+f   formatting at the caret / at the review cursor
//     alus+f9 / alus+f10      mark and copy
//
// THE REPEAT COUNT IS THE INTERFACE. Nearly every command here does three
// different things depending on how many times it was pressed, and NVDA's
// rule is the same everywhere: once says it, twice spells it, three times
// describes the characters. `review_currentCharacter` goes further and reports
// the codepoint value on the third press, which is what a user debugging a
// strange character needs.
#pragma once

#include "alus/script/scriptable.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <memory>
#include <optional>
#include <string>

namespace alus::input {
class Gesture;
}

namespace alus::script {
class Catalog;
class Dispatcher;
}

namespace alus::nav {

class Service;

class TextCommands final : public script::ScriptableObject {
public:
    TextCommands(Service& service, const script::Catalog* catalog = nullptr,
                 script::Dispatcher* dispatcher = nullptr);
    ~TextCommands() override;

private:
    // ---- review movement ----
    void review_top(input::Gesture& gesture);
    void review_previousLine(input::Gesture& gesture);
    void review_currentLine(input::Gesture& gesture);
    void review_nextLine(input::Gesture& gesture);
    void review_bottom(input::Gesture& gesture);
    void review_previousWord(input::Gesture& gesture);
    void review_currentWord(input::Gesture& gesture);
    void review_nextWord(input::Gesture& gesture);
    void review_startOfLine(input::Gesture& gesture);
    void review_previousCharacter(input::Gesture& gesture);
    void review_currentCharacter(input::Gesture& gesture);
    void review_nextCharacter(input::Gesture& gesture);
    void review_endOfLine(input::Gesture& gesture);
    void review_previousPage(input::Gesture& gesture);
    void review_nextPage(input::Gesture& gesture);
    void review_sayAll(input::Gesture& gesture);

    // ---- the review mode ----
    void reviewMode_next(input::Gesture& gesture);
    void reviewMode_previous(input::Gesture& gesture);

    // ---- mark, select and copy ----
    void review_markStartForCopy(input::Gesture& gesture);
    void review_moveToStartMarkedForCopy(input::Gesture& gesture);
    void review_copy(input::Gesture& gesture);
    void review_startOfSelection(input::Gesture& gesture);
    void review_endOfSelection(input::Gesture& gesture);

    // ---- the caret ----
    void sayAll(input::Gesture& gesture);
    // NVDA: `script_speakForeground`, alus+b. Reads the whole active window,
    // control by control, rather than from the caret.
    void speakForeground(input::Gesture& gesture);
    // NVDA: `script_caretPos_currentDimensions`, alus+numpadDelete. Where the
    // caret is on the screen, in pixels -- the caret's answer to the question
    // `alus+shift+numpadDelete` asks about the navigator object.
    void caretPos_currentDimensions(input::Gesture& gesture);
    // NVDA: `script_moveToParentTreeInterceptor`, alus+control+space. Out of
    // this document and into the one that contains it -- an iframe to its
    // page, an embedded viewer to the application around it.
    void moveToParentTreeInterceptor(input::Gesture& gesture);
    void reportCurrentLine(input::Gesture& gesture);
    void reportCurrentSelection(input::Gesture& gesture);
    void reportCaretLocation(input::Gesture& gesture);
    void reportReviewCursorLocation(input::Gesture& gesture);
    void reportCaretLanguage(input::Gesture& gesture);

    // ---- formatting ----
    void reportFormatting(input::Gesture& gesture);
    void reportFormattingAtCaret(input::Gesture& gesture);
    void reportFormattingAtReview(input::Gesture& gesture);
    void reportOrShowFormattingAtCaret(input::Gesture& gesture);

    // ---- settings the user cycles ----
    void cycleParagraphStyle(input::Gesture& gesture);
    void toggleReportLineIndentation(input::Gesture& gesture);

    // ---- the helpers every command above shares ----
    int repeatCount() const;
    void say(const std::string& text) const;
    // The review position, or null with a spoken complaint.
    text::TextRange* reviewOrComplain() const;
    // NVDA's movement pattern, including the #12808 guard: some
    // implementations move one unit further than they should and land on the
    // exclusive end of the text, so the new unit is checked to be genuinely
    // past the old position before the move is accepted.
    void moveReview(text::TextUnit unit, int direction, const char* edgeMessage);
    // NVDA's reporting pattern: once speak, twice spell, three times describe.
    void reportReviewUnit(text::TextUnit unit);
    // review_top / review_bottom / review_startOfLine / review_endOfLine.
    void moveReviewToEdge(text::TextUnit unit, bool toEnd, bool wholeText);
    void reportFormattingFor(const text::TextRange& range, bool browseMode);

    Service& service_;
    script::Dispatcher* dispatcher_;
    // NVDA: `_copyStartMarker`. Where `alus+f9` was pressed.
    text::TextRangePtr copyStartMarker_;
};

}  // namespace alus::nav
