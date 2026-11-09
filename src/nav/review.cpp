// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/review.hpp.

#include "alus/nav/review.hpp"

#include "alus/i18n/i18n.hpp"
#include "alus/nav/tree_interceptor.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"
#include "alus/nav/screen_review.hpp"

#include <array>

namespace alus::nav {
namespace {

constexpr std::array<ReviewMode, 3> kModes{ReviewMode::Object, ReviewMode::Document,
                                           ReviewMode::Screen};

int modeIndex(ReviewMode mode) {
    switch (mode) {
        case ReviewMode::Object:
            return 0;
        case ReviewMode::Document:
            return 1;
        case ReviewMode::Screen:
            return 2;
    }
    return 0;
}

}  // namespace

std::string_view reviewModeName(ReviewMode mode) {
    switch (mode) {
        case ReviewMode::Object:
            return "object";
        case ReviewMode::Document:
            return "document";
        case ReviewMode::Screen:
            return "screen";
    }
    return "object";
}

std::string_view reviewModeLabel(ReviewMode mode) {
    switch (mode) {
        // Translators: one of the review modes.
        case ReviewMode::Object:
            return _("Object review");
        // Translators: one of the review modes.
        case ReviewMode::Document:
            return _("Document review");
        // Translators: one of the review modes.
        case ReviewMode::Screen:
            return _("Screen review");
    }
    return _("Object review");
}

std::optional<ReviewMode> parseReviewMode(std::string_view name) {
    if (name == "object") {
        return ReviewMode::Object;
    }
    if (name == "document") {
        return ReviewMode::Document;
    }
    if (name == "screen") {
        return ReviewMode::Screen;
    }
    return std::nullopt;
}

Review::Review(a11y::Registry& registry, const a11y::Settings& settings)
    : registry_(registry), settings_(settings) {}

Review::~Review() = default;

// ---------------------------------------------------------------------------
// The three positions
// ---------------------------------------------------------------------------

text::TextRangePtr Review::objectPosition(const a11y::ObjectPtr& object) {
    if (object == nullptr) {
        return nullptr;
    }
    // NVDA: the caret first, then the first position. An object with text but
    // no caret (a label, a read-only field) is still reviewable from its
    // start, and that fallback is why review works on static text at all.
    if (text::TextRangePtr range = object->makeTextRange(text::Position::Caret)) {
        return range;
    }
    return object->makeTextRange(text::Position::First);
}

text::TextRangePtr Review::documentPosition(const a11y::ObjectPtr& object) {
    if (object == nullptr) {
        return nullptr;
    }
    // A tree interceptor that is not there means document review has no
    // position, and `positionForCurrentMode` falls back to object review --
    // which is exactly what a user on a plain button should get (NVDA #3320).
    if (object->treeInterceptor() == nullptr || !object->treeInterceptorReady()) {
        return nullptr;
    }
    // Phase 6 fills this. The ROOT PROXY range, not the interceptor's own:
    // its identity has to be the root object's, because the caret handling and
    // `shouldReportTextContent` compare a range's object against the focus --
    // see TreeInterceptor::makeRootProxyRange.
    auto* interceptor = dynamic_cast<TreeInterceptor*>(object->treeInterceptor());
    if (interceptor == nullptr) {
        return nullptr;
    }
    text::TextRangePtr position = interceptor->makeRootProxyRange(text::Position::Caret);
    if (position == nullptr) {
        position = interceptor->makeRootProxyRange(text::Position::First);
    }
    return position;
}

text::TextRangePtr Review::screenPosition(const a11y::ObjectPtr& object) {
    if (object == nullptr) {
        return nullptr;
    }
    // §10: the active top-level window, flattened. NVDA walks up to the
    // Window; on AT-SPI the window is found the same way, by walking the
    // ancestor chain to the frame.
    a11y::ObjectPtr window = object;
    while (window != nullptr) {
        const a11y::Role role = window->role();
        if (role == a11y::Role::Window || role == a11y::Role::Dialog ||
            role == a11y::Role::Frame || role == a11y::Role::Pane) {
            break;
        }
        const a11y::ObjectPtr parent = window->parent();
        if (parent == nullptr || parent->role() == a11y::Role::Application ||
            parent->role() == a11y::Role::DesktopPane) {
            break;
        }
        window = parent;
    }
    if (window == nullptr) {
        window = registry_.activeWindow();
    }
    return ScreenReviewRange::makeAtObject(window, object);
}

text::TextRangePtr Review::positionForCurrentMode(const a11y::ObjectPtr& object) {
    // NVDA: try this mode, then every lower one. The fallback is the design.
    for (int index = modeIndex(mode_); index >= 0; --index) {
        text::TextRangePtr range;
        switch (kModes[static_cast<std::size_t>(index)]) {
            case ReviewMode::Object:
                range = objectPosition(object);
                break;
            case ReviewMode::Document:
                range = documentPosition(object);
                break;
            case ReviewMode::Screen:
                range = screenPosition(object);
                break;
        }
        if (range != nullptr) {
            return range;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// The mode
// ---------------------------------------------------------------------------

std::optional<std::string_view> Review::setMode(ReviewMode mode, bool updatePosition) {
    const a11y::ObjectPtr object = registry_.api().navigator();
    text::TextRangePtr range;
    switch (mode) {
        case ReviewMode::Object:
            range = objectPosition(object);
            break;
        case ReviewMode::Document:
            range = documentPosition(object);
            break;
        case ReviewMode::Screen:
            range = screenPosition(object);
            break;
    }
    if (range == nullptr) {
        // NVDA: the mode is not changed when it has no position. That is what
        // makes `nextMode` skip it instead of trapping the user in a mode
        // where nothing works.
        return std::nullopt;
    }
    mode_ = mode;
    if (updatePosition) {
        setPosition(std::move(range), /*clearNavigator=*/false);
    }
    return reviewModeLabel(mode);
}

std::optional<std::string_view> Review::nextMode(bool previous) {
    int index = modeIndex(mode_);
    while (true) {
        index += previous ? -1 : 1;
        if (index < 0 || index >= static_cast<int>(kModes.size())) {
            // NVDA does not wrap: the ends of the list are the ends.
            return std::nullopt;
        }
        if (const std::optional<std::string_view> label =
                setMode(kModes[static_cast<std::size_t>(index)])) {
            return label;
        }
    }
}

// ---------------------------------------------------------------------------
// The position
// ---------------------------------------------------------------------------

text::TextRange* Review::position() {
    if (position_ != nullptr) {
        return position_.get();
    }
    const a11y::ObjectPtr object = registry_.api().navigator();
    if (object == nullptr) {
        return nullptr;
    }
    position_ = positionForCurrentMode(object);
    positionObject_ = object;
    return position_.get();
}

bool Review::setPosition(text::TextRangePtr position, bool clearNavigator, bool isCaret,
                         bool isMouse) {
    (void)isMouse;
    if (position == nullptr) {
        return false;
    }
    position_ = std::move(position);
    if (clearNavigator) {
        // NVDA sets the navigator object to null so that the next ask derives
        // it from the review position. ALUS's Api falls back to the focus
        // instead of holding null, so the navigator is set to the object the
        // position is in -- the same user-visible result, without a null that
        // every caller would have to test.
        if (const a11y::ObjectPtr object = objectAtPosition()) {
            positionObject_ = object;
            registry_.api().setNavigatorObject(object, /*isFocus=*/false);
        }
    }
    (void)isCaret;
    if (positionChanged_) {
        positionChanged_();
    }
    return true;
}

void Review::handleCaretMove(const a11y::ObjectPtr& object) {
    if (object == nullptr) {
        return;
    }
    if (!settings_.reviewCursorFollowCaret) {
        return;
    }
    // NVDA: in object review the caret only drags the review cursor when it is
    // moving in THE NAVIGATOR OBJECT itself. A caret in another window must
    // not move what the user is reviewing.
    if (mode_ == ReviewMode::Object) {
        const a11y::ObjectPtr navigator = registry_.api().navigator();
        if (navigator == nullptr || !navigator->sameAs(*object)) {
            return;
        }
    }
    text::TextRangePtr range = object->makeTextRange(text::Position::Caret);
    if (range == nullptr) {
        return;
    }
    setPosition(std::move(range), /*clearNavigator=*/false, /*isCaret=*/true);
}

void Review::handleCaretMove(const text::TextRange& at, const a11y::ObjectPtr& object) {
    if (!settings_.reviewCursorFollowCaret) {
        return;
    }
    if (mode_ == ReviewMode::Object && object != nullptr) {
        const a11y::ObjectPtr navigator = registry_.api().navigator();
        if (navigator == nullptr || !navigator->sameAs(*object)) {
            return;
        }
    }
    setPosition(at.copy(), /*clearNavigator=*/false, /*isCaret=*/true);
}

void Review::onNavigatorObjectChanged(const a11y::ObjectPtr& object, bool isFocus) {
    (void)isFocus;
    // The position is dropped, not recomputed: recomputing here would cost a
    // screen-review snapshot on every focus change (phase 4 measured 578 µs
    // per object). The next ask rebuilds it, and by then the user has asked
    // for something.
    position_.reset();
    positionObject_ = object;

    // NVDA #3320: in document review with no document to review, the mode is
    // forced back to object review.
    if (mode_ == ReviewMode::Document && object != nullptr &&
        (object->treeInterceptor() == nullptr || !object->treeInterceptorReady())) {
        mode_ = ReviewMode::Object;
    }
}

void Review::clear() {
    position_.reset();
    positionObject_.reset();
    mode_ = ReviewMode::Object;
}

a11y::ObjectPtr Review::objectAtPosition() {
    text::TextRange* current = position();
    if (current == nullptr) {
        return positionObject_;
    }
    // A screen-review position knows which object it is over; an object-review
    // position IS one object.
    if (auto* screen = dynamic_cast<ScreenReviewRange*>(current)) {
        if (const a11y::ObjectPtr object = screen->objectAtStart()) {
            return object;
        }
        return screen->window();
    }
    return positionObject_ != nullptr ? positionObject_ : registry_.api().navigator();
}

}  // namespace alus::nav
