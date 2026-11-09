// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/editable_text.hpp.

#include "alus/nav/editable_text.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_speech.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/core/task.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/paragraph_nav.hpp"
#include "alus/nav/say_all.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>
#include <cctype>

namespace alus::nav {
namespace {

std::string lowerAscii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// The per-toolkit tuning
// ---------------------------------------------------------------------------

CaretDetectionSettings caretTuningFor(std::string_view toolkitName) {
    CaretDetectionSettings settings;
    // The comparison ignores case, because the toolkit name is not canonically
    // spelled: one session reported `gtk`, `GTK`, `clutter` and `Chromium`
    // (finding N41).
    const std::string toolkit = lowerAscii(toolkitName);

    if (toolkit == "gtk") {
        // GTK4 emits `object:text-caret-moved` reliably and promptly, from the
        // same main-loop iteration that handled the key. Its events are
        // believed, and the timeout is short because a GTK application that
        // has not moved its caret in 60 ms is not going to.
        settings.detectionUsesEvents = true;
        settings.useEventsMaxTimeout = std::chrono::milliseconds{60};
        settings.timeout = std::chrono::milliseconds{80};
        return settings;
    }
    if (toolkit == "qt") {
        // Qt6 emits the event too, but AFTER its own event loop has settled,
        // which on a busy application is past the 60 ms window. The bookmark
        // strategy carries it, so a longer overall timeout is what matters.
        settings.detectionUsesEvents = true;
        settings.useEventsMaxTimeout = std::chrono::milliseconds{60};
        settings.timeout = std::chrono::milliseconds{120};
        return settings;
    }
    if (toolkit == "clutter") {
        // gnome-shell. Its text widgets are few (the overview search entry)
        // and its caret events are reliable.
        settings.detectionUsesEvents = true;
        settings.timeout = std::chrono::milliseconds{80};
        return settings;
    }
    if (toolkit == "chromium" || toolkit == "blink") {
        // Chromium posts caret events from the renderer process, so they can
        // arrive long after the key and sometimes out of order. NVDA has the
        // same experience with Chrome on Windows and does not rely on them.
        settings.detectionUsesEvents = false;
        settings.timeout = std::chrono::milliseconds{150};
        return settings;
    }
    if (toolkit == "libreoffice" || toolkit == "vcl") {
        // LibreOffice. §8.3 names it as one of the five that lie: its caret
        // event arrives before the text has settled, so the bookmark is read
        // instead, and the multiplier is what NVDA's own LibreOffice support
        // uses -- a slow document needs more than 100 ms.
        settings.detectionUsesEvents = false;
        settings.timeout = std::chrono::milliseconds{100};
        settings.timeoutMultiplier = 2.0;
        settings.minWordTimeout = std::chrono::milliseconds{50};
        return settings;
    }
    if (toolkit == "java" || toolkit == "swing" || toolkit == "swt") {
        // The Java access bridge. Events exist and are late; the bookmark is
        // the strategy that works.
        settings.detectionUsesEvents = false;
        settings.timeout = std::chrono::milliseconds{150};
        return settings;
    }
    // An unknown toolkit gets NVDA's own defaults and the events believed:
    // being wrong about that costs one slow key press, while not trying costs
    // every key press.
    return settings;
}

// ---------------------------------------------------------------------------
// CaretWaiter
// ---------------------------------------------------------------------------

CaretWaiter::CaretWaiter(core::Loop& loop, CaretDetectionSettings settings)
    : loop_(loop), settings_(settings) {}

CaretWaiter::~CaretWaiter() {
    abandon();
}

void CaretWaiter::abandon() {
    if (taskId_ != 0) {
        loop_.tasks().cancel(taskId_);
        taskId_ = 0;
    }
    waiting_ = false;
    object_.reset();
    done_ = nullptr;
    originalWord_.reset();
}

void CaretWaiter::noteCaretEvent() {
    sawEvent_ = true;
}

void CaretWaiter::start(a11y::ObjectPtr object, text::Bookmark before,
                        std::optional<std::string> originalWord, Callback done) {
    // A new key press makes any earlier wait irrelevant: NVDA's
    // `isScriptWaiting()` has the same effect, and without it two arrow
    // presses in quick succession announce the first line twice.
    abandon();
    if (object == nullptr || !done) {
        return;
    }
    object_ = std::move(object);
    before_ = before;
    originalWord_ = std::move(originalWord);
    done_ = std::move(done);
    sawEvent_ = false;
    waiting_ = true;
    started_ = std::chrono::steady_clock::now();
    taskId_ = loop_.tasks().add(run(), "caret-wait");
    // The first step has to be asked for; after that a live task keeps the
    // loop pumping by itself (core/task.hpp).
    loop_.requestPump(/*immediate=*/true);
}

core::Task CaretWaiter::run() {
    const auto deadline =
        std::chrono::milliseconds(static_cast<long long>(
            static_cast<double>(settings_.timeout.count()) * settings_.timeoutMultiplier));

    while (true) {
        // One check per pump cycle. A live task makes the loop pump again, so
        // this polls at the loop's own rate without a timer and without a
        // sleep (§3.2).
        co_yield core::nextCycle;

        if (!waiting_ || object_ == nullptr) {
            co_return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_);

        CaretMoveResult result;
        result.elapsed = elapsed;

        // STRATEGY 1: the event, but only inside its window. An event that
        // arrives later than this is not about the key that was pressed.
        if (settings_.detectionUsesEvents && sawEvent_ &&
            elapsed <= settings_.useEventsMaxTimeout) {
            // The caret is fetched AFTER the event is seen, not before, or a
            // value read a moment earlier would be stale -- NVDA's own note on
            // the race with its background thread.
            result.moved = true;
            result.strategy = "event";
            result.position = object_->makeTextRange(text::Position::Caret);
            ALUS_DEBUG("caret move detected by event after {} ms", elapsed.count());
            Callback callback = std::move(done_);
            abandon();
            callback(std::move(result));
            co_return;
        }

        text::TextRangePtr position = object_->makeTextRange(text::Position::Caret);

        // STRATEGY 2: the bookmark.
        if (position != nullptr && position->bookmark() != before_) {
            result.moved = true;
            result.strategy = "bookmark";
            result.position = std::move(position);
            ALUS_DEBUG("caret move detected by bookmark after {} ms", elapsed.count());
            Callback callback = std::move(done_);
            abandon();
            callback(std::move(result));
            co_return;
        }

        // STRATEGY 3: the word under the caret. Only for Delete, where the
        // caret does not move, and only after the settling time -- Gecko and
        // LibreOffice report text and units out of step while a character is
        // being removed.
        if (originalWord_.has_value() && position != nullptr &&
            elapsed >= settings_.minWordTimeout) {
            text::TextRangePtr word = position->copy();
            word->expand(text::TextUnit::Word);
            if (word->text() != *originalWord_) {
                result.moved = true;
                result.strategy = "word";
                result.position = std::move(position);
                ALUS_DEBUG("word at caret changed after {} ms", elapsed.count());
                Callback callback = std::move(done_);
                abandon();
                callback(std::move(result));
                co_return;
            }
        }

        if (elapsed >= deadline) {
            ALUS_DEBUG("caret did not move before the timeout ({} ms)", elapsed.count());
            result.moved = false;
            result.strategy = "timeout";
            result.position = std::move(position);
            Callback callback = std::move(done_);
            abandon();
            callback(std::move(result));
            co_return;
        }
    }
}

// ---------------------------------------------------------------------------
// EditableTextBehavior
// ---------------------------------------------------------------------------

EditableTextBehavior::EditableTextBehavior(Service& service) : service_(service) {
    waiter_ = std::make_unique<CaretWaiter>(service_.loop(), CaretDetectionSettings{});
}

EditableTextBehavior::~EditableTextBehavior() = default;

void EditableTextBehavior::bindScripts(a11y::Object& object) {
    // EVERY ONE OF THESE SCRIPTS ONLY WATCHES ITS KEY. The application is
    // acting on the same key at the same time; see the header, point A, and
    // `needsKeyPassThrough` in script.hpp. Getting this flag wrong makes the
    // caret stop moving at all, which is the most visible bug phase 5 could
    // have.
    const script::ScriptSpec watch{.needsKeyPassThrough = true};
    script::ScriptSpec watchAndResumeSayAll = watch;
    watchAndResumeSayAll.resumeSayAllMode = script::SayAllCursor::Caret;

    // The per-toolkit tuning, chosen from the object's own application.
    waiter_->setSettings(caretTuningFor(object.appInfo().toolkitName));
    // The user's own timeout wins where they set one.
    CaretDetectionSettings tuned = waiter_->settings();
    const int configured = service_.settings().caretMoveTimeoutMilliseconds;
    if (configured > 0) {
        tuned.timeout = std::chrono::milliseconds{configured};
    }
    waiter_->setSettings(tuned);

    a11y::Object* target = &object;
    object.bindBehaviorScript(
        "caret_moveByLine",
        [this, target](input::Gesture& gesture) {
            caretMovementScript(*target, gesture, text::TextUnit::Line);
        },
        watchAndResumeSayAll);
    object.bindBehaviorScript(
        "caret_moveByCharacter",
        [this, target](input::Gesture& gesture) {
            caretMovementScript(*target, gesture, text::TextUnit::Character);
        },
        watch);
    object.bindBehaviorScript(
        "caret_moveByWord",
        [this, target](input::Gesture& gesture) {
            caretMovementScript(*target, gesture, text::TextUnit::Word);
        },
        watch);
    object.bindBehaviorScript(
        "caret_moveByParagraph",
        [this, target](input::Gesture& gesture) {
            caretMovementScript(*target, gesture, text::TextUnit::Paragraph);
        },
        watchAndResumeSayAll);
    object.bindBehaviorScript(
        "caret_previousParagraph",
        [this, target](input::Gesture& gesture) {
            paragraphNavigationScript(*target, gesture, /*nextParagraph=*/false);
        },
        watchAndResumeSayAll);
    object.bindBehaviorScript(
        "caret_nextParagraph",
        [this, target](input::Gesture& gesture) {
            paragraphNavigationScript(*target, gesture, /*nextParagraph=*/true);
        },
        watchAndResumeSayAll);
    object.bindBehaviorScript(
        "caret_previousSentence",
        [this, target](input::Gesture& gesture) {
            caretMoveBySentence(*target, gesture, -1);
        },
        watchAndResumeSayAll);
    object.bindBehaviorScript(
        "caret_nextSentence",
        [this, target](input::Gesture& gesture) { caretMoveBySentence(*target, gesture, 1); },
        watchAndResumeSayAll);
    object.bindBehaviorScript(
        "caret_deleteCharacter",
        [this, target](input::Gesture& gesture) {
            deleteScript(*target, gesture, text::TextUnit::Character);
        },
        watch);
    object.bindBehaviorScript(
        "caret_deleteWord",
        [this, target](input::Gesture& gesture) {
            deleteScript(*target, gesture, text::TextUnit::Word);
        },
        watch);
    object.bindBehaviorScript(
        "caret_backspaceCharacter",
        [this, target](input::Gesture& gesture) {
            backspaceScript(*target, gesture, text::TextUnit::Character);
        },
        watch);
    object.bindBehaviorScript(
        "caret_backspaceWord",
        [this, target](input::Gesture& gesture) {
            backspaceScript(*target, gesture, text::TextUnit::Word);
        },
        watch);
}

void EditableTextBehavior::caretMovementScript(a11y::Object& object, input::Gesture& gesture,
                                               text::TextUnit unit) {
    text::TextRangePtr before = object.makeTextRange(text::Position::Caret);
    if (before == nullptr) {
        // No caret to watch: nothing to report, and the application has the
        // key anyway.
        return;
    }
    const text::Bookmark bookmark = before->bookmark();
    const bool resumingSayAll = gesture.wasInSayAll;
    a11y::ObjectPtr held = object.registry().objectFor(object.ref());
    waiter_->start(held, bookmark, std::nullopt,
                   [this, held, unit, resumingSayAll](CaretMoveResult result) {
                       if (held == nullptr) {
                           return;
                       }
                       afterCaretMoved(*held, unit, std::move(result.position),
                                       resumingSayAll);
                   });
}

void EditableTextBehavior::caretMoveBySentence(a11y::Object& object, input::Gesture& gesture,
                                               int direction) {
    text::TextRangePtr before = object.makeTextRange(text::Position::Caret);
    if (before == nullptr) {
        return;
    }
    const text::Bookmark bookmark = before->bookmark();
    const bool resumingSayAll = gesture.wasInSayAll;
    a11y::ObjectPtr held = object.registry().objectFor(object.ref());
    // NVDA tries the application's own sentence navigation first (the key may
    // do it), and only moves the caret itself when nothing happened. On
    // Wayland the application has already had the key, so the same shape
    // holds: wait, and if the caret did not move, move it.
    waiter_->start(held, bookmark, std::nullopt,
                   [this, held, direction, resumingSayAll](CaretMoveResult result) {
                       if (held == nullptr) {
                           return;
                       }
                       if (result.moved) {
                           afterCaretMoved(*held, text::TextUnit::Line,
                                           std::move(result.position), resumingSayAll);
                           return;
                       }
                       text::TextRangePtr range =
                           held->makeTextRange(text::Position::Caret);
                       if (range == nullptr) {
                           return;
                       }
                       if (range->move(text::TextUnit::Sentence, direction) == 0) {
                           return;
                       }
                       range->updateCaret();
                       afterCaretMoved(*held, text::TextUnit::Sentence, std::move(range),
                                       resumingSayAll);
                   });
}

void EditableTextBehavior::paragraphNavigationScript(a11y::Object& object,
                                                     input::Gesture& gesture,
                                                     bool nextParagraph) {
    // §11.2: three paragraph styles, and the `(passKey, moved)` convention
    // that lets the application's own behaviour stand when ALUS declines.
    const text::ParagraphStyle style = service_.settings().paragraphStyle;
    if (style == text::ParagraphStyle::Application) {
        caretMovementScript(object, gesture, text::TextUnit::Paragraph);
        return;
    }
    text::TextRangePtr caret = object.makeTextRange(text::Position::Caret);
    if (caret == nullptr) {
        caretMovementScript(object, gesture, text::TextUnit::Paragraph);
        return;
    }
    const ParagraphMoveResult result =
        moveToParagraph(*caret, style, nextParagraph,
                        /*speakNew=*/!gesture.wasInSayAll);
    if (result.passKey) {
        // ALUS could not do it: let the application's own paragraph movement
        // stand, which is what it is already doing with the key.
        caretMovementScript(object, gesture, text::TextUnit::Paragraph);
        return;
    }
    if (!result.moved) {
        return;
    }
    caret->updateCaret();
    afterCaretMoved(object, result.speakNew
                                ? std::optional<text::TextUnit>(text::TextUnit::Paragraph)
                                : std::nullopt,
                    caret->copy(), gesture.wasInSayAll);
}

void EditableTextBehavior::deleteScript(a11y::Object& object, input::Gesture& gesture,
                                        text::TextUnit unit) {
    text::TextRangePtr caret = object.makeTextRange(text::Position::Caret);
    if (caret == nullptr) {
        return;
    }
    const text::Bookmark bookmark = caret->bookmark();
    // The word under the caret BEFORE the deletion: strategy 3's input.
    text::TextRangePtr word = caret->copy();
    word->expand(text::TextUnit::Word);
    const std::string originalWord = word->text();
    const bool resumingSayAll = gesture.wasInSayAll;
    a11y::ObjectPtr held = object.registry().objectFor(object.ref());
    waiter_->start(held, bookmark, originalWord,
                   [this, held, unit, resumingSayAll](CaretMoveResult result) {
                       if (held == nullptr) {
                           return;
                       }
                       // NVDA does not care whether the caret moved here: what
                       // matters is what is under it now.
                       afterCaretMoved(*held, unit, std::move(result.position),
                                       resumingSayAll);
                   });
}

void EditableTextBehavior::backspaceScript(a11y::Object& object, input::Gesture& gesture,
                                           text::TextUnit unit) {
    text::TextRangePtr caret = object.makeTextRange(text::Position::Caret);
    if (caret == nullptr) {
        return;
    }
    const text::Bookmark bookmark = caret->bookmark();
    // What is ABOUT to be deleted: the unit before the caret, read before the
    // application removes it. This is the only way to say what was deleted.
    std::string deleted;
    text::TextRangePtr test = caret->copy();
    if (test->move(text::TextUnit::Character, -1) < 0) {
        test->expand(unit);
        deleted = test->text();
    }
    const bool resumingSayAll = gesture.wasInSayAll;
    a11y::ObjectPtr held = object.registry().objectFor(object.ref());
    waiter_->start(
        held, bookmark, std::nullopt,
        [this, held, deleted, resumingSayAll](CaretMoveResult result) {
            if (held == nullptr || !result.moved) {
                // Nothing was deleted: an empty field, or the start of the
                // text. Saying nothing is right.
                return;
            }
            speech::Presenter* presenter = service_.presenter();
            if (presenter != nullptr && !deleted.empty()) {
                // NVDA: a whole word is spoken, a single character is SPELLED
                // -- "a" deleted must not be read as the word "a".
                std::string text = deleted;
                // Scintilla and others report \r\n; one newline is enough.
                const std::size_t crlf = text.find("\r\n");
                if (crlf != std::string::npos) {
                    text.replace(crlf, 2, "\n");
                }
                if (text::isSingleCharacter(text)) {
                    presenter->speakSpelling(text);
                } else {
                    presenter->speakMessage(text);
                }
            }
            afterCaretMoved(*held, std::nullopt, std::move(result.position), resumingSayAll);
        });
}

void EditableTextBehavior::afterCaretMoved(a11y::Object& object,
                                           std::optional<text::TextUnit> speakUnit,
                                           text::TextRangePtr position,
                                           bool resumingSayAll) {
    if (position == nullptr) {
        position = object.makeTextRange(text::Position::Caret);
        if (position == nullptr) {
            return;
        }
    }
    // NVDA: forget the word being typed -- the user has moved somewhere else,
    // and echoing it when they next press space would be wrong.
    if (input::Service* input = service_.inputService()) {
        if (input::GestureManager* manager = input->manager()) {
            manager->typedChars().clearWordBuffer();
        }
    }

    a11y::ObjectPtr held = object.registry().objectFor(object.ref());
    if (Review* review = service_.review()) {
        review->handleCaretMove(*position, held);
    }

    if (speakUnit.has_value() && !resumingSayAll) {
        text::TextRangePtr unitRange = position->copy();
        unitRange->expand(*speakUnit);
        service_.speakRange(*unitRange, speakUnit, a11y::OutputReason::Caret, held);
    }
}

// ---------------------------------------------------------------------------
// The selection
// ---------------------------------------------------------------------------

void EditableTextBehavior::initAutoSelectDetection(a11y::Object& object) {
    lastSelection_.reset();
    if (const text::TextRangePtr selection =
            object.makeTextRange(text::Position::Selection)) {
        lastSelection_ = selection->bookmark();
    }
    selectionAnchoredAtStart_ = true;
    contentChangedSinceLastSelection = false;
    autoSelectDetection_ = true;
}

void EditableTextBehavior::terminateAutoSelectDetection() {
    lastSelection_.reset();
    autoSelectDetection_ = false;
}

void EditableTextBehavior::detectPossibleSelectionChange(a11y::Object& object) {
    if (!autoSelectDetection_) {
        return;
    }
    text::TextRangePtr current = object.makeTextRange(text::Position::Selection);
    if (current == nullptr) {
        // Leaving the old selection in place is better than forgetting it:
        // NVDA's own comment, and the next change will then be right.
        return;
    }
    // The previous selection is rebuilt from its bookmark; see the header for
    // why it is not kept as a range.
    text::TextRangePtr previous;
    if (lastSelection_.has_value()) {
        previous = object.makeTextRange(text::Position::First);
        if (previous != nullptr && !previous->moveToBookmark(*lastSelection_)) {
            previous.reset();
        }
    }
    lastSelection_ = current->bookmark();
    if (previous == nullptr) {
        selectionAnchoredAtStart_ = true;
        return;
    }
    // The anchor: which END of the selection the user is dragging. §8.2 warns
    // that getting this wrong makes the announcement jump.
    if (current->compareEndPoints(*previous, text::EndPointPair::StartToStart) != 0) {
        selectionAnchoredAtStart_ = false;
    } else if (current->compareEndPoints(*previous, text::EndPointPair::EndToEnd) != 0) {
        selectionAnchoredAtStart_ = true;
    }
    const bool generalize = contentChangedSinceLastSelection;
    contentChangedSinceLastSelection = false;
    if (speech::Presenter* presenter = service_.presenter()) {
        a11y::speakSelectionChange(*presenter, *previous, *current, true, true, generalize);
    }
}

void EditableTextBehavior::reportSelectionChange(a11y::Object& object,
                                                 const text::TextRange& oldSelection) {
    text::TextRangePtr current = object.makeTextRange(text::Position::Selection);
    if (current == nullptr) {
        return;
    }
    if (current->compareEndPoints(oldSelection, text::EndPointPair::StartToStart) != 0) {
        selectionAnchoredAtStart_ = false;
    } else if (current->compareEndPoints(oldSelection, text::EndPointPair::EndToEnd) != 0) {
        selectionAnchoredAtStart_ = true;
    }
    if (speech::Presenter* presenter = service_.presenter()) {
        a11y::speakSelectionChange(*presenter, oldSelection, *current);
    }
    lastSelection_ = current->bookmark();
}

bool EditableTextBehavior::handleEvent(a11y::Object& object, const core::Event& event) {
    if (event.name == a11y::events::kGainFocus) {
        initAutoSelectDetection(object);
        return false;
    }
    if (event.name == a11y::events::kLoseFocus) {
        terminateAutoSelectDetection();
        waiter_->abandon();
        return false;
    }
    if (event.name == a11y::events::kCaret) {
        // Strategy 1's input. The waiter decides whether the event is early
        // enough to be believed.
        waiter_->noteCaretEvent();
        if (!waiter_->waiting()) {
            // A caret move ALUS did not ask for: the application moved it
            // itself (an autocomplete, a click, a script). The review cursor
            // follows, and nothing is spoken -- speaking here would double
            // every announcement the caret commands already make.
            if (Review* review = service_.review()) {
                a11y::ObjectPtr held = object.registry().objectFor(object.ref());
                review->handleCaretMove(held);
            }
        }
        return false;
    }
    if (event.name == a11y::events::kTextChange) {
        waiter_->noteCaretEvent();
        contentChangedSinceLastSelection = true;
        return false;
    }
    if (event.name == a11y::events::kTextSelectionChange) {
        detectPossibleSelectionChange(object);
        return false;
    }
    return false;
}

}  // namespace alus::nav
