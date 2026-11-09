// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Say all: read continuously from here to the end, and move the cursor with
// the voice.
//
// Port of NVDA's `speech/sayAll.py` (486 lines): `_SayAllHandler`,
// `_TextReader`, `_CaretTextReader`, `_ReviewTextReader` and `_ObjectsReader`.
//
// WHY IT IS CALLBACK DRIVEN AND NOT A LOOP. The hard part of say-all is not
// reading the text; it is that THE CURSOR MUST FOLLOW THE SOUND. A loop that
// spoke every line and moved the caret as it went would move the caret to the
// end of the document while the synthesizer was still on the first line, and
// the user pressing a key would land somewhere they never heard.
//
// So the line is handed to the synthesizer with an index mark in front of it
// (`speech::CallbackCommand`, which the SpeechManager turns into an
// `IndexCommand` -- §4.3), and the cursor is moved WHEN THAT MARK IS REACHED.
// The same callback asks for the next line. The result is a pipeline whose
// depth is bounded by `kMaxBufferedLines`, with the cursor always on the line
// being spoken (T5.7 measures exactly this: a caret within one line of the
// sound over 100 lines).
//
// AND WHY IT SOUNDS CONTINUOUS: every line goes through
// `speech::SentenceBuffer` (§4.4), which holds text back until a sentence
// boundary. Without it the synthesizer inserts an utterance-final pause at the
// end of every line and say-all stutters once per line.
//
// 02-FAZE.md calls this deliverable a coroutine. It is not one, and
// FAZA5.md O6 says why: a coroutine resumed once per pump cycle is a POLLING
// shape, and what this needs is to be woken by the synthesizer reaching a
// mark. The one place a `core::Task` is right -- waiting for an application to
// move its caret -- does use one (nav::EditableText).
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/a11y/text_speech.hpp"
#include "alus/script/script.hpp"
#include "alus/text/range.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace alus::speech {
class SentenceBuffer;
}

namespace alus::nav {

class Service;

// NVDA: `sayAll.CURSOR`. Which cursor say-all moves as it reads.
enum class SayAllCursor {
    // The system caret: `alus+downArrow`, and what a caret movement command
    // resumes (`resumeSayAllMode`).
    Caret,
    // The review cursor: `numpadPlus`.
    Review,
    // Reading objects rather than text: `alus+numpad+` on a container.
    Objects,
};

std::string_view sayAllCursorName(SayAllCursor cursor);

class SayAll {
public:
    // NVDA: MAX_BUFFERED_LINES. How far ahead of the voice the reader may get.
    // Ten lines is NVDA's number: enough that a slow text layer does not make
    // the speech stutter, few enough that stopping is immediate.
    static constexpr std::size_t kMaxBufferedLines = 10;

    explicit SayAll(Service& service);
    ~SayAll();

    SayAll(const SayAll&) = delete;
    SayAll& operator=(const SayAll&) = delete;

    // NVDA: `isRunning`. The answer phase 3 has been waiting for: its
    // `GestureManager::Hooks::isSayAllRunning` is filled from this
    // (claude/FAZA4.md handover point 5).
    bool isRunning() const noexcept { return reader_ != nullptr || objectsRoot_ != nullptr; }

    // NVDA: `readText(cursor)`. False when there is nothing to read.
    bool readText(SayAllCursor cursor);
    // NVDA: `readObjects(obj)`. Reads an object and its descendants, one
    // announcement at a time -- which is say-all on a dialog or a list.
    bool readObjects(const a11y::ObjectPtr& root);
    // NVDA: `stop()`. Idempotent, and safe to call from the key handler.
    void stop();

    // NVDA: `willSayAllResume(gesture)` plus the resumption itself. A caret
    // movement script with `resumeSayAllMode` set carries on reading from the
    // new position instead of announcing the line.
    bool willResume(script::SayAllCursor mode) const;
    void resumeIfRequested(script::SayAllCursor mode);

    SayAllCursor cursor() const noexcept { return cursor_; }
    // How many lines were read, for the measurement in
    // tools/phase5-text-check.sh and for T5.7.
    std::size_t linesRead() const noexcept { return linesRead_; }

private:
    void nextLine();
    bool advanceToNextChunk();
    void lineReached(a11y::ObjectPtr object, text::Bookmark bookmark,
                     a11y::TextRangeSpeechState state);
    void finish();
    void moveCursorTo(const a11y::ObjectPtr& object, const text::Bookmark& bookmark);
    bool shouldReadInitialPosition() const;
    text::TextRangePtr initialRange(SayAllCursor cursor) const;

    // The object reader (NVDA: `_ObjectsReader`).
    void nextObject();

    Service& service_;
    std::unique_ptr<speech::SentenceBuffer> buffer_;

    // The text reader's state.
    text::TextRangePtr reader_;
    a11y::ObjectPtr readerObject_;
    a11y::TextRangeSpeechState state_;
    SayAllCursor cursor_ = SayAllCursor::Caret;
    std::size_t bufferedLines_ = 0;
    bool initialIteration_ = true;
    std::size_t linesRead_ = 0;
    // A generation counter: a callback from a say-all that has been stopped
    // must do nothing. Without it, stopping and starting again in quick
    // succession makes the old pipeline move the new cursor.
    std::uint64_t generation_ = 0;

    // The object reader's state.
    a11y::ObjectPtr objectsRoot_;
    a11y::ObjectPtr objectsCurrent_;
    a11y::ObjectPtr objectsPrevious_;
};

}  // namespace alus::nav
