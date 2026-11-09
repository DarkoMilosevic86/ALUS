// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Text that changes by itself: `LiveText` and `Terminal`.
//
// Port of NVDA's `NVDAObjects/behaviors.py` `LiveText`, `Terminal` and
// `EnhancedTermTypedCharSupport`. These are the deliverables 02-FAZE.md calls
// `alus::a11y::behaviors::{LiveText, Terminal}`; they live in the nav library
// because they need the text layer, and they keep the `a11y::behaviors`
// namespace so that the gesture maps and the overlay chooser read as before
// (FAZA5.md O7).
//
// WHAT THE PROBLEM IS. A terminal, a chat window and a log viewer present
// their whole contents as ONE piece of text and only say "something changed".
// They do not say what. So the text is fetched, diffed against what it was,
// and only the NEW LINES are spoken. Get that wrong and `cat` on a long file
// reads the whole file again on every line.
//
// WHERE ALUS DIFFERS FROM NVDA, and it is not a detail: NVDA RUNS A MONITOR
// THREAD per live object, which wakes on the change event, sleeps for the
// stabilisation delay and fetches the text. ALUS cannot: §13 says every
// AT-SPI call is on the main thread, and fetching the text is an AT-SPI call.
// So the stabilisation delay is a TIMER on the loop and the diff runs in the
// pump -- which is also why a flood of change events costs one diff rather
// than one per event (the timer is re-armed, not queued).
//
// THE TYPED-CHARACTER QUEUE is NVDA's password protection, and it transfers
// for a reason that is easy to miss: in a terminal, a character the user types
// is echoed by the SHELL, so it arrives as new text. Announcing both the key
// press and the echo says everything twice -- and in `sudo`, where the shell
// echoes nothing, announcing the key press alone reads the password out loud.
// So typed characters are HELD, and released only when the screen update
// shows what the terminal really did with them.
#pragma once

#include "alus/a11y/behavior.hpp"
#include "alus/a11y/object.hpp"
#include "alus/core/loop.hpp"

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace alus::nav {

class Service;

// NVDA: `diffHandler`. The new lines of `newText` that were not in `oldText`.
//
// Line based, like NVDA's `Difflib` path. The character-based path (Google's
// diff-match-patch) is NOT ported: it exists in NVDA to catch a changed word
// in the middle of a line, and it costs a dependency plus an O(n·m) character
// diff on every screen update. What a terminal user needs is the new LINES,
// and FAZA5.md O8 records the measurement that led to this.
//
// The fast path is the common case: output that was APPENDED. When the old
// lines are a prefix of the new ones, the answer is the suffix, in one pass.
std::vector<std::string> diffNewLines(const std::string& oldText, const std::string& newText);

// A line with its leading and trailing whitespace removed. Used by the
// typed-character filter, which compares LENGTHS.
std::string trimLine(const std::string& line);

}  // namespace alus::nav

namespace alus::a11y::behaviors {

// NVDA: `LiveText`. Text that changes on its own and must be announced.
class LiveText : public a11y::Behavior {
public:
    explicit LiveText(nav::Service& service);
    ~LiveText() override;

    std::string_view className() const override { return "NVDAObjects.behaviors.LiveText"; }

    // If the text is live, it IS content -- NVDA says so in one line, and it
    // is what keeps a terminal out of the layout objects.
    std::optional<bool> isPresentableFocusAncestor(const Object& object) const override;

    bool handleEvent(Object& object, const core::Event& event) override;

    // NVDA: `startMonitoring` / `stopMonitoring`.
    void startMonitoring(Object& object);
    void stopMonitoring();
    bool monitoring() const noexcept { return monitoring_; }

    // NVDA: `STABILIZE_DELAY`. How long to wait after a change before reading
    // the text, so that a burst of updates costs one diff. Zero for a plain
    // live region; a terminal sets its own.
    std::chrono::milliseconds stabilizeDelay{0};

protected:
    // NVDA: `_getText`. The whole text of the object.
    virtual std::string textOf(Object& object);
    // NVDA: `_reportNewLines`. The hook `Terminal` overrides to filter typed
    // characters out.
    virtual void reportNewLines(Object& object, std::vector<std::string> lines);
    // NVDA: `_reportNewText`.
    void reportNewText(const std::string& line);

    nav::Service& service();

private:
    void scheduleDiff(Object& object);
    void runDiff(Object& object);

    nav::Service* service_;
    bool monitoring_ = false;
    std::string lastText_;
    core::Loop::TimerId timer_ = 0;
    // The object the timer is for. Weak on purpose: a terminal that closes
    // between the event and the timer must not be resurrected by it.
    std::weak_ptr<Object> pending_;
};

// NVDA: `Terminal` plus `EnhancedTermTypedCharSupport`. A live text that also
// accepts input, so monitoring follows the focus and typed characters are
// held until the screen shows what happened to them.
class Terminal final : public LiveText {
public:
    explicit Terminal(nav::Service& service);
    ~Terminal() override;

    std::string_view className() const override { return "NVDAObjects.behaviors.Terminal"; }

    std::optional<Role> role(const Object& object) const override;
    bool handleEvent(Object& object, const core::Event& event) override;

    // NVDA: `_queuedChars`. A character typed into the terminal, held until
    // the next screen update decides what to do with it.
    void queueTypedCharacter(const std::string& character);
    const std::deque<std::string>& queuedCharacters() const noexcept { return queued_; }

protected:
    void reportNewLines(Object& object, std::vector<std::string> lines) override;

private:
    void flushQueuedCharacters();

    std::deque<std::string> queued_;
    // NVDA: `_hasTab`. After a tab, completions may be short, so the
    // short-line filter is suspended for one update.
    bool hasTab_ = false;
};

}  // namespace alus::a11y::behaviors
