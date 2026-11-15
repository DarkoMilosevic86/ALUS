// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/say_all.hpp.

#include "alus/nav/say_all.hpp"

#include "alus/a11y/object_speech.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/core/queue.hpp"
#include "alus/nav/browse_mode.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/speech/sentence_buffer.hpp"

#include <utility>

namespace alus::nav {

std::string_view sayAllCursorName(SayAllCursor cursor) {
    switch (cursor) {
        case SayAllCursor::Caret:
            return "caret";
        case SayAllCursor::Review:
            return "review";
        case SayAllCursor::Objects:
            return "objects";
    }
    return "caret";
}

SayAll::SayAll(Service& service) : service_(service) {
    buffer_ = std::make_unique<speech::SentenceBuffer>();
}

SayAll::~SayAll() = default;

// ---------------------------------------------------------------------------
// Starting and stopping
// ---------------------------------------------------------------------------

text::TextRangePtr SayAll::initialRange(SayAllCursor cursor) const {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return nullptr;
    }
    if (cursor == SayAllCursor::Review) {
        Review* review = service_.review();
        if (review == nullptr) {
            return nullptr;
        }
        text::TextRange* position = review->position();
        if (position == nullptr) {
            return nullptr;
        }
        text::TextRangePtr range = position->copy();
        range->collapse();
        return range;
    }
    // The caret. NVDA falls back to the FIRST position when there is no
    // caret, because say-all on a read-only document still has to work.
    const a11y::ObjectPtr focus = registry->api().focus();
    if (focus == nullptr) {
        return nullptr;
    }
    // N144, NVDA's script_sayAll: in a document in browse mode the caret is
    // browse mode's, and so is the text. The focus object's own text is not
    // the document: in Gecko (Thunderbird, Firefox) it is one U+FFFC per
    // child, and say-all read three empty chunks of it and stopped.
    if (const BrowseMode* document = documentForFocus()) {
        if (text::TextRangePtr range = document->makeTextRange(text::Position::Caret)) {
            range->collapse();
            return range;
        }
    }
    if (text::TextRangePtr range = focus->makeTextRange(text::Position::Caret)) {
        range->collapse();
        return range;
    }
    return focus->makeTextRange(text::Position::First);
}

BrowseMode* SayAll::documentForFocus() const {
    TreeInterceptorRegistry* interceptors = service_.interceptors();
    if (interceptors == nullptr) {
        return nullptr;
    }
    auto* document = dynamic_cast<BrowseMode*>(interceptors->current());
    if (document == nullptr || document->passThrough()) {
        return nullptr;
    }
    return document;
}

bool SayAll::readText(SayAllCursor cursor) {
    stop();
    text::TextRangePtr range = initialRange(cursor);
    if (range == nullptr) {
        return false;
    }
    a11y::Registry* registry = service_.registry();
    cursor_ = cursor;
    inDocument_ = cursor == SayAllCursor::Caret && documentForFocus() != nullptr;
    reader_ = std::move(range);
    readerObject_ = cursor == SayAllCursor::Review && service_.review() != nullptr
                        ? service_.review()->objectAtPosition()
                        : (registry != nullptr ? registry->api().focus() : nullptr);
    // The reading chunk the user configured: line, sentence or paragraph
    // (§7.1 -- ALUS synthesises this unit, and say-all is what it is for).
    reader_->setReadingUnit(service_.settings().sayAllReadingUnit);
    state_ = a11y::TextRangeSpeechState{};
    bufferedLines_ = 0;
    initialIteration_ = true;
    linesRead_ = 0;
    ++generation_;
    buffer_->reset();
    buffer_->setSpeakFunction([this](speech::SpeechSequence sequence) {
        if (speech::Presenter* presenter = service_.presenter()) {
            presenter->speak(std::move(sequence));
        }
    });
    nextLine();
    return true;
}

bool SayAll::readObjects(const a11y::ObjectPtr& root) {
    stop();
    if (root == nullptr) {
        return false;
    }
    cursor_ = SayAllCursor::Objects;
    objectsRoot_ = root;
    objectsCurrent_ = root;
    objectsPrevious_.reset();
    ++generation_;
    nextObject();
    return true;
}

void SayAll::stop() {
    // Before the early return: after finish() the reader is gone, and the
    // final stop() still has to forget which focus changes were say-all's.
    caretObjects_.clear();
    if (!isRunning()) {
        return;
    }
    ++generation_;
    reader_.reset();
    readerObject_.reset();
    objectsRoot_.reset();
    objectsCurrent_.reset();
    objectsPrevious_.reset();
    bufferedLines_ = 0;
    buffer_->reset();
    ALUS_DEBUG("say all stopped after {} lines", linesRead_);
}

void SayAll::rememberCaretObject(const a11y::ObjectPtr& object) {
    if (!caretObjects_.empty() && caretObjects_.back()->sameAs(*object)) {
        return;
    }
    caretObjects_.push_back(object);
    if (caretObjects_.size() > kRememberedCaretObjects) {
        caretObjects_.erase(caretObjects_.begin());
    }
}

bool SayAll::ownsFocusChangeTo(const a11y::Object& object) const {
    if (cursor_ != SayAllCursor::Caret) {
        return false;
    }
    // Not isRunning(): finish() drops the reader before the last line has
    // been spoken, and that line's focus event is still on its way.
    if (readerObject_ != nullptr && isRunning() && readerObject_->sameAs(object)) {
        return true;
    }
    for (const a11y::ObjectPtr& remembered : caretObjects_) {
        if (remembered->sameAs(object)) {
            return true;
        }
    }
    return false;
}

bool SayAll::willResume(script::SayAllCursor mode) const {
    if (mode == script::SayAllCursor::None || !isRunning()) {
        return false;
    }
    // NVDA: a caret-movement script resumes a CARET say-all, not a review one.
    if (mode == script::SayAllCursor::Caret) {
        return cursor_ == SayAllCursor::Caret;
    }
    return cursor_ == SayAllCursor::Review;
}

void SayAll::resumeIfRequested(script::SayAllCursor mode) {
    if (!willResume(mode)) {
        return;
    }
    // The cursor has moved; read on from where it is now.
    readText(cursor_);
}

// ---------------------------------------------------------------------------
// The text reader
// ---------------------------------------------------------------------------

bool SayAll::shouldReadInitialPosition() const {
    // NVDA's `_TextReader.shouldReadInitialPosition` is false for the caret
    // and review readers (only the table reader overrides it): the first
    // chunk is reached by moving, not by reading where the cursor already is.
    return false;
}

bool SayAll::advanceToNextChunk() {
    // NVDA uses `move(..., endPoint="end")` rather than `expand`, with a
    // reason worth keeping: the user may have started in the MIDDLE of a line,
    // and expanding would read from its start. It is also cheaper for every
    // line after the first, which already begins at a chunk boundary.
    if (reader_->move(text::TextUnit::ReadingChunk, 1, text::EndPoint::End) > 0) {
        return true;
    }
    // The end of this object is not the end of the document. Writer exposes
    // every paragraph as an object of its own, so reading stopped after the
    // first line (N127); NVDA's compound documents and Orca both continue
    // through FLOWS_TO. Only for the caret: the review cursor stays inside
    // the object it reviews, as NVDA's does. Empty paragraphs are skipped,
    // and the hops are bounded in case an application's relations loop.
    if (cursor_ == SayAllCursor::Caret && !inDocument_) {
        constexpr int kMaxHops = 200;
        a11y::ObjectPtr next = nextReadingObject(readerObject_);
        for (int hop = 0; hop < kMaxHops && next != nullptr; ++hop) {
            text::TextRangePtr range = next->makeTextRange(text::Position::First);
            if (range != nullptr) {
                range->collapse();
                range->setReadingUnit(service_.settings().sayAllReadingUnit);
                if (range->move(text::TextUnit::ReadingChunk, 1, text::EndPoint::End) > 0) {
                    reader_ = std::move(range);
                    readerObject_ = next;
                    return true;
                }
            }
            next = nextReadingObject(next);
        }
    }
    finish();
    return false;
}

a11y::ObjectPtr SayAll::nextReadingObject(const a11y::ObjectPtr& from) const {
    if (from == nullptr) {
        return nullptr;
    }
    if (a11y::ObjectPtr flow = from->flowsTo(); flow != nullptr && flow->isAlive()) {
        return flow;
    }
    const a11y::ObjectPtr parent = from->parent();
    if (parent == nullptr) {
        return nullptr;
    }
    bool after = false;
    for (const a11y::ObjectPtr& sibling : parent->children()) {
        if (sibling == nullptr) {
            continue;
        }
        if (after && sibling->hasInterface(a11y::Interface::Text)) {
            return sibling;
        }
        if (sibling->sameAs(*from)) {
            after = true;
        }
    }
    return nullptr;
}

void SayAll::nextLine() {
    if (reader_ == nullptr) {
        return;
    }
    if (readerObject_ != nullptr && !readerObject_->isAlive()) {
        // The document went away under us.
        finish();
        return;
    }
    if (!initialIteration_ || !shouldReadInitialPosition()) {
        if (!advanceToNextChunk()) {
            return;
        }
    }
    initialIteration_ = false;

    const text::Bookmark bookmark = reader_->bookmark();
    // A COPY of the speech state per line: the callback that runs when this
    // line starts being spoken must see the state as it was for THIS line,
    // not as it is after the reader has run ahead.
    a11y::TextRangeSpeechState lineState = state_;

    a11y::TextRangeSpeechOptions options;
    options.unit = text::TextUnit::ReadingChunk;
    options.reason = a11y::OutputReason::SayAll;

    const std::uint64_t lineGeneration = generation_;
    a11y::ObjectPtr object = readerObject_;
    options.prefixCommand = speech::callback(
        [this, lineGeneration, object, bookmark, lineState]() mutable {
            if (lineGeneration != generation_) {
                // A stopped say-all: its marks may still arrive.
                return;
            }
            lineReached(object, bookmark, std::move(lineState));
        },
        "say-all:lineReached");

    const a11y::TextRangeSpeechResult speech = a11y::getTextRangeSpeech(
        *reader_, &lineState, service_.formatSettings(), options, service_.presenter());

    speech::SpeechSequence sequence;
    for (const speech::SpeechSequence& part : speech.sequences) {
        for (const speech::SpeechItem& item : part) {
            sequence.push_back(item);
        }
    }
    if (options.prefixCommand != nullptr &&
        (sequence.empty() || !sequence.front().isCommand() ||
         sequence.front().command() != options.prefixCommand)) {
        // getTextRangeSpeech puts the prefix at the front of the FIRST
        // sequence; when it produced none at all (a blank line in say-all),
        // the mark still has to go out or the pipeline stalls.
        sequence.insert(sequence.begin(), speech::SpeechItem(options.prefixCommand));
    }

    const bool spoke = buffer_->speakWithoutPauses(sequence);
    state_ = lineState;
    ++linesRead_;

    reader_->collapse(/*toEnd=*/true);

    if (!spoke) {
        // The line held no sentence boundary, so nothing was handed to the
        // synthesizer yet. Read on -- but through the event queue, so that a
        // key press can still stop say-all between lines (NVDA's own reason).
        ++bufferedLines_;
        if (bufferedLines_ < kMaxBufferedLines) {
            const std::uint64_t queuedGeneration = generation_;
            service_.loop().queue().push(
                [this, queuedGeneration] {
                    if (queuedGeneration == generation_) {
                        nextLine();
                    }
                },
                "say-all:nextLine");
            // IMMEDIATE: the user is waiting to hear the next line, and the
            // ordinary pump delay (§3.1's pumpMaxDelay) would add it to every
            // line that held no sentence boundary.
            service_.loop().requestPump(/*immediate=*/true);
        } else {
            // Too much held back: force it out. The first buffered line starts
            // speaking now, and its mark will resume the pipeline.
            buffer_->flush();
            --bufferedLines_;
        }
    }
}

void SayAll::lineReached(a11y::ObjectPtr object, text::Bookmark bookmark,
                         a11y::TextRangeSpeechState state) {
    (void)state;
    // This line has just STARTED being spoken: now the cursor moves to it.
    moveCursorTo(object, bookmark);
    if (bufferedLines_ == 0) {
        nextLine();
    } else {
        --bufferedLines_;
    }
}

void SayAll::moveCursorTo(const a11y::ObjectPtr& object, const text::Bookmark& bookmark) {
    if (inDocument_) {
        // Browse mode's caret follows the speech, so that a key press stops
        // the reading where it is (NVDA moves the virtual caret the same way).
        if (BrowseMode* document = documentForFocus()) {
            text::TextRangePtr position = document->makeTextRange(text::Position::First);
            if (position != nullptr && position->moveToBookmark(bookmark)) {
                position->collapse();
                document->cursor().setCaret(*position);
                if (Review* review = service_.review()) {
                    review->handleCaretMove(*position, object);
                }
            }
        }
        return;
    }
    if (object == nullptr) {
        return;
    }
    text::TextRangePtr position = object->makeTextRange(text::Position::Caret);
    if (position == nullptr) {
        position = object->makeTextRange(text::Position::First);
    }
    if (position == nullptr || !position->moveToBookmark(bookmark)) {
        return;
    }
    if (cursor_ == SayAllCursor::Caret) {
        rememberCaretObject(object);
        position->updateCaret();
        if (Review* review = service_.review()) {
            review->handleCaretMove(*position, object);
        }
        return;
    }
    if (Review* review = service_.review()) {
        review->setPosition(position->copy(), /*clearNavigator=*/false);
    }
}

void SayAll::finish() {
    // NVDA: stop only AFTER the speech has finished, by putting the callback
    // at the start of a new utterance. Stopping sooner would switch the
    // synthesizer's profile back while the last line was still being spoken
    // and truncate it.
    const std::uint64_t generation = generation_;
    speech::SpeechSequence tail;
    tail.emplace_back(speech::endUtterance());
    tail.emplace_back(speech::callback(
        [this, generation] {
            if (generation == generation_) {
                stop();
            }
        },
        "say-all:stop"));
    tail.emplace_back(speech::endUtterance());
    buffer_->speakWithoutPauses(tail);
    // The reader is dropped now so that `isRunning()` is false for the key
    // handler, while the callback above still ends the speech cleanly.
    reader_.reset();
}

// ---------------------------------------------------------------------------
// The object reader (NVDA: _ObjectsReader)
// ---------------------------------------------------------------------------

void SayAll::nextObject() {
    if (objectsRoot_ == nullptr) {
        return;
    }
    a11y::Registry* registry = service_.registry();
    if (objectsPrevious_ != nullptr && registry != nullptr) {
        // The previous object has just started being spoken, so the navigator
        // moves to it now -- the same rule as the text reader's cursor.
        if (!registry->api().setNavigatorObject(objectsPrevious_,
                                                cursor_ == SayAllCursor::Caret)) {
            stop();
            return;
        }
    }

    // The walk: this object, then its first simple child, then siblings --
    // NVDA's `_ObjectsReader.walk`, flattened into a cursor so that no
    // generator is needed.
    a11y::ObjectPtr next;
    if (objectsCurrent_ != nullptr) {
        next = objectsCurrent_->firstChild();
        if (next == nullptr) {
            a11y::ObjectPtr walker = objectsCurrent_;
            while (walker != nullptr && !walker->sameAs(*objectsRoot_)) {
                if (const a11y::ObjectPtr sibling = walker->next()) {
                    next = sibling;
                    break;
                }
                walker = walker->parent();
            }
        }
    }
    objectsPrevious_ = objectsCurrent_;
    objectsCurrent_ = next;
    if (objectsCurrent_ == nullptr) {
        objectsRoot_.reset();
        objectsPrevious_.reset();
        return;
    }

    speech::Presenter* presenter = service_.presenter();
    if (presenter == nullptr) {
        stop();
        return;
    }
    const std::uint64_t generation = generation_;
    speech::SpeechSequence sequence;
    sequence.emplace_back(speech::callback(
        [this, generation] {
            if (generation == generation_) {
                nextObject();
            }
        },
        "say-all:nextObject"));
    speech::SpeechSequence objectSpeech =
        a11y::getObjectSpeech(*objectsCurrent_, a11y::OutputReason::SayAll, presenter);
    for (speech::SpeechItem& item : objectSpeech) {
        sequence.push_back(std::move(item));
    }
    ++linesRead_;
    presenter->speak(std::move(sequence));
}

}  // namespace alus::nav
