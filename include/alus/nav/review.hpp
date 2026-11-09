// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The review cursor: where the user is LOOKING, as opposed to where the focus
// is.
//
// Port of NVDA's `review.py` (190 lines, transferred in full per
// 01-ARHITEKTURA.md §8.1) plus the review half of `api.py`
// (`getReviewPosition`, `setReviewPosition`). Three modes:
//
//     object    the text inside the navigator object      Object::makeTextRange
//     document  the whole document containing it          tree interceptor (phase 6)
//     screen    the spatial content of the screen         flat object review (§10)
//
// THE FALLBACK IS THE DESIGN. `positionForCurrentMode` tries the current mode
// and then every LOWER one, so a user in document review who moves to a plain
// button silently gets object review rather than nothing. NVDA does this and
// it is why the review cursor never dies.
//
// WHAT OWNS WHAT. The navigator object belongs to `a11y::Api` (phase 4); the
// review POSITION belongs here. They are kept consistent the way NVDA keeps
// them: setting the navigator clears the position (so the next ask rebuilds it
// from the new object), and setting the position optionally clears the
// navigator (so the next ask derives it from the position). Phase 4's handover
// point 3 asked for exactly this, and `becomeNavigatorObject` is the event it
// hangs on.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/a11y/settings.hpp"
#include "alus/text/range.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace alus::a11y {
class Registry;
}

namespace alus::nav {

enum class ReviewMode { Object, Document, Screen };

std::string_view reviewModeName(ReviewMode mode);
// What is spoken when the mode changes ("Object review").
std::string_view reviewModeLabel(ReviewMode mode);
std::optional<ReviewMode> parseReviewMode(std::string_view name);

class Review {
public:
    // `settings` is the TEXT LAYER's settings, not the object model's: the
    // review cursor's own keys ([reviewCursor]) are read by nav::Service, and
    // a registry built by hand in a test has only the defaults. The reference
    // must outlive the Review, which nav::Service guarantees by owning both.
    Review(a11y::Registry& registry, const a11y::Settings& settings);
    ~Review();

    Review(const Review&) = delete;
    Review& operator=(const Review&) = delete;

    ReviewMode mode() const noexcept { return mode_; }
    // NVDA: `setCurrentMode`. Returns the label to speak, or nothing when that
    // mode has no position for the current object -- in which case the mode is
    // NOT changed, which is what makes `nextMode` skip unavailable modes.
    std::optional<std::string_view> setMode(ReviewMode mode, bool updatePosition = true);
    // NVDA: `nextMode`. Skips a mode with no position; returns nothing at the
    // end of the list rather than wrapping, as NVDA does.
    std::optional<std::string_view> nextMode(bool previous = false);

    // NVDA: `review.getPositionForCurrentMode`. Falls back down the modes.
    text::TextRangePtr positionForCurrentMode(const a11y::ObjectPtr& object);
    // NVDA: `review.getObjectPosition` / `getDocumentPosition` /
    // `getScreenPosition`. Public because the tests check each one, and
    // because phase 6 replaces the middle one.
    text::TextRangePtr objectPosition(const a11y::ObjectPtr& object);
    text::TextRangePtr documentPosition(const a11y::ObjectPtr& object);
    text::TextRangePtr screenPosition(const a11y::ObjectPtr& object);

    // NVDA: `api.getReviewPosition`. Built from the navigator object when it
    // has not been set. Null only when there is no navigator object at all.
    text::TextRange* position();
    // NVDA: `api.setReviewPosition`.
    bool setPosition(text::TextRangePtr position, bool clearNavigator = true,
                     bool isCaret = false, bool isMouse = false);
    // NVDA: `review.handleCaretMove`. Called after every caret movement; does
    // nothing unless the user asked the review cursor to follow the caret, and
    // nothing unless the object IS the navigator object -- a caret moving in a
    // window the user is not reviewing must not drag the review cursor along.
    void handleCaretMove(const a11y::ObjectPtr& object);
    void handleCaretMove(const text::TextRange& at, const a11y::ObjectPtr& object);

    // Called from the becomeNavigatorObject event: the position is rebuilt
    // from the new object next time it is asked for (NVDA's
    // `setNavigatorObject` does this inline).
    void onNavigatorObjectChanged(const a11y::ObjectPtr& object, bool isFocus);

    void clear();

    // The object the review position is in, which is what `alus+numpad5` in
    // screen review announces and what the review-to-navigator command uses.
    a11y::ObjectPtr objectAtPosition();

    // NVDA: `braille.handler.handleReviewMove`, called from
    // `api.setReviewPosition`. Phase 7 needs to know when the review cursor
    // moved, for the same reason phase 5 needed to know when the navigator
    // object changed: the braille display follows the review cursor and
    // nothing else tells it.
    //
    // A hook rather than a chain level, for the reason
    // `a11y::Registry::eventDispatched` gives: braille only watches.
    void setPositionChangedHook(std::function<void()> hook) {
        positionChanged_ = std::move(hook);
    }

    // Set by nav::Service so that a mode change can be announced. Optional:
    // the tests install nothing and read `mode()` instead.
    void setMessageHook(std::function<void(const std::string&)> hook) {
        message_ = std::move(hook);
    }

private:
    a11y::Registry& registry_;
    const a11y::Settings& settings_;
    ReviewMode mode_ = ReviewMode::Object;
    text::TextRangePtr position_;
    // The object the current position belongs to (NVDA:
    // `globalVars.reviewPositionObj`).
    a11y::ObjectPtr positionObject_;
    std::function<void(const std::string&)> message_;
    std::function<void()> positionChanged_;
};

}  // namespace alus::nav
