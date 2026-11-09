// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The caret in an editable field: has it moved, and what should be said.
//
// Port of NVDA's `editableText.py` (514 lines). 01-ARHITEKTURA.md §8.3 is
// blunt about why this is not simplified: `_hasCaretMoved` has THREE
// STRATEGIES WITH A TIME BUDGET, because applications lie about caret events,
// and "GTK, Qt, Electron, Java and LibreOffice lie in DIFFERENT ways". The
// strategies are transferred, not reduced:
//
//   1. THE EVENT, if it arrives within `useEventsMaxTimeout` (60 ms) and the
//      control is one whose events are believed. Fast and exact when true.
//   2. THE BOOKMARK. Poll the caret offset and compare. Works everywhere and
//      is the reason this is not simply "wait for an event".
//   3. THE WORD AT THE CARET. For Delete, where the caret does NOT move but
//      the text under it changes. Only consulted after `minWordTimeout`
//      (30 ms), because Gecko reports text and units out of step with each
//      other while a character is being deleted.
//
// TWO THINGS ARE DIFFERENT FROM NVDA, BOTH FORCED BY WAYLAND:
//
//  A. NVDA SENDS THE KEY AND THEN WAITS. ALUS CANNOT SEND KEYS AT ALL
//     (finding N18: `atspi_generate_keyboard_event` is a no-op on Wayland),
//     and it does not have to: the compositor's `WatchKeyboard` delivers every
//     key event to ALUS WITHOUT consuming it, so the application already has
//     the key by the time the script runs. The script therefore snapshots the
//     position and waits; there is no `gesture.send()`.
//
//     The consequence reaches the input layer: a caret key must NOT be
//     grabbed, or the application would never receive it and the caret would
//     never move. That is what `ScriptInfo::needsKeyPassThrough` is for.
//
//  B. THE WAIT DOES NOT BLOCK. NVDA spins and sleeps on its main thread for
//     up to 100 ms per key press, pumping events by hand. ALUS waits in a
//     `core::Task` resumed once per pump cycle (§3.2), so speech keeps
//     flowing, the watchdog stays happy and a frozen application costs
//     nothing but the timeout.
//
// THE PER-TOOLKIT TUNING IS EXPECTED WORK, not a sign of a bug -- §8.3 says
// so in as many words. `caretTuningFor()` is where it lives, with the reason
// for each number beside it.
#pragma once

#include "alus/a11y/behavior.hpp"
#include "alus/a11y/object.hpp"
#include "alus/core/loop.hpp"
#include "alus/core/task.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace alus::nav {

class Service;

// NVDA's four tunables, per control class.
struct CaretDetectionSettings {
    // NVDA: config.conf["editableText"]["caretMoveTimeoutMs"].
    std::chrono::milliseconds timeout{100};
    // NVDA: `_useEvents_maxTimeoutSec`. Beyond this an event is too late to be
    // about the key that was just pressed.
    std::chrono::milliseconds useEventsMaxTimeout{60};
    // NVDA: `_hasCaretMoved_minWordTimeoutSec`.
    std::chrono::milliseconds minWordTimeout{30};
    // NVDA: `_caretMovementTimeoutMultiplier`.
    double timeoutMultiplier = 1.0;
    // NVDA: `caretMovementDetectionUsesEvents`.
    bool detectionUsesEvents = true;
};

// The per-toolkit table. The toolkit name is compared WITHOUT CASE, because it
// is not canonically spelled (finding N41: `gtk`, `GTK`, `clutter`,
// `Chromium` on one session).
CaretDetectionSettings caretTuningFor(std::string_view toolkitName);

// What the caret wait concluded.
struct CaretMoveResult {
    bool moved = false;
    // How the move was detected, for the log and for the measurement in
    // tools/phase5-text-check.sh. "event", "bookmark", "word" or "timeout".
    std::string_view strategy = "timeout";
    // The caret position afterwards, or null when it could not be read.
    text::TextRangePtr position;
    std::chrono::milliseconds elapsed{0};
};

// Waits for the caret of one object to move. One of these is alive per key
// press at most: a second key press abandons the first (NVDA's
// `isScriptWaiting`).
class CaretWaiter {
public:
    using Callback = std::function<void(CaretMoveResult)>;

    // Takes the LOOP, not only its task registry: starting a wait has to
    // request a pump, or the task would sit there until something else woke
    // the loop -- and the "something else" in a screen reader is the next key
    // press, which is far too late.
    CaretWaiter(core::Loop& loop, CaretDetectionSettings settings);
    ~CaretWaiter();

    CaretWaiter(const CaretWaiter&) = delete;
    CaretWaiter& operator=(const CaretWaiter&) = delete;

    // Starts waiting. `before` is the position as it was BEFORE the key
    // reached the application; `originalWord` enables strategy 3 and is set
    // only by the delete commands. The callback runs on the main thread,
    // exactly once, however the wait ends.
    void start(a11y::ObjectPtr object, text::Bookmark before,
               std::optional<std::string> originalWord, Callback done);
    // Abandons the wait without calling back. NVDA's `isScriptWaiting`: a new
    // key press makes the old wait irrelevant.
    void abandon();
    bool waiting() const noexcept { return waiting_; }

    // Told by the behaviour when a caret or textChange event arrives for the
    // object being waited on (strategy 1).
    void noteCaretEvent();

    const CaretDetectionSettings& settings() const noexcept { return settings_; }
    void setSettings(CaretDetectionSettings settings) { settings_ = settings; }

private:
    core::Task run();

    core::Loop& loop_;
    CaretDetectionSettings settings_;
    bool waiting_ = false;
    bool sawEvent_ = false;
    std::uint64_t taskId_ = 0;
    a11y::ObjectPtr object_;
    text::Bookmark before_;
    std::optional<std::string> originalWord_;
    Callback done_;
    std::chrono::steady_clock::time_point started_;
};

// The behaviour that owns the caret commands for one object.
//
// NVDA's `editableText.EditableText` is a mixin on the NVDAObject; ALUS
// composes it as a behaviour (§6.2), and it answers to NVDA's class name so
// that the generated command table's `[editableText.EditableText]` bindings
// reach it.
class EditableTextBehavior final : public a11y::Behavior {
public:
    explicit EditableTextBehavior(Service& service);
    ~EditableTextBehavior() override;

    std::string_view className() const override { return "editableText.EditableText"; }

    void bindScripts(a11y::Object& object) override;
    bool handleEvent(a11y::Object& object, const core::Event& event) override;

    // NVDA: `initAutoSelectDetection` / `detectPossibleSelectionChange` /
    // `terminateAutoSelectDetection`. Public because the focus path calls the
    // first and the event path the second.
    void initAutoSelectDetection(a11y::Object& object);
    void detectPossibleSelectionChange(a11y::Object& object);
    void terminateAutoSelectDetection();

    // NVDA: `announceNewLineText` / `announceEntireNewLine`. Overridable per
    // toolkit; the defaults are NVDA's.
    bool announceNewLineText = true;
    bool announceEntireNewLine = false;
    // NVDA: `hasContentChangedSinceLastSelection`, set by the textChange
    // event. It makes the next selection report GENERALISE, because the
    // substrings either side of the change cannot be trusted.
    bool contentChangedSinceLastSelection = false;

private:
    void caretMovementScript(a11y::Object& object, input::Gesture& gesture,
                             text::TextUnit unit);
    void caretMoveBySentence(a11y::Object& object, input::Gesture& gesture, int direction);
    void deleteScript(a11y::Object& object, input::Gesture& gesture, text::TextUnit unit);
    void backspaceScript(a11y::Object& object, input::Gesture& gesture, text::TextUnit unit);
    void paragraphNavigationScript(a11y::Object& object, input::Gesture& gesture,
                                   bool nextParagraph);
    // NVDA: `_caretScriptPostMovedHelper`. What happens once the caret has
    // settled: forget the typed word, move the review cursor, speak the unit.
    void afterCaretMoved(a11y::Object& object, std::optional<text::TextUnit> speakUnit,
                         text::TextRangePtr position, bool resumingSayAll);
    void reportSelectionChange(a11y::Object& object, const text::TextRange& oldSelection);

    Service& service_;
    std::unique_ptr<CaretWaiter> waiter_;
    // NVDA: `_lastSelectionPos` and `isTextSelectionAnchoredAtStart`. The
    // anchor logic is transferred literally: §8.2 warns that getting it wrong
    // produces a selection that "jumps".
    //
    // A BOOKMARK AND NOT A RANGE, and that is not a style choice. A behaviour
    // is owned by its object, and an `ObjectTextRange` holds its object alive;
    // storing a range here would be a reference ring through the behaviour,
    // and every editable field the user ever focused would leak itself and its
    // cached subtree. Measured under valgrind, and it is the same shape as
    // claude/FAZA4.md N40 -- see FAZA5.md N49.
    std::optional<text::Bookmark> lastSelection_;
    bool selectionAnchoredAtStart_ = true;
    bool autoSelectDetection_ = false;
};

}  // namespace alus::nav
