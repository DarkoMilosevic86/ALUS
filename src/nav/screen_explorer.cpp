// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/screen_explorer.hpp.

#include "alus/nav/screen_explorer.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_range.hpp"
#include "alus/core/log.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/presenter.hpp"

namespace alus::nav {

ScreenExplorer::ScreenExplorer(Service& service) : service_(service) {}

ScreenExplorer::~ScreenExplorer() = default;

void ScreenExplorer::reset() {
    object_.reset();
    position_.reset();
}

a11y::ObjectPtr ScreenExplorer::objectAtPoint(int x, int y) const {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return nullptr;
    }
    const a11y::ObjectPtr desktop = registry->desktop();
    if (desktop == nullptr) {
        return nullptr;
    }
    // One platform call: the deepest accessible at this screen point.
    const a11y::Ref found =
        registry->backend().accessibleAtPoint(desktop->ref(), x, y);
    if (found == 0) {
        return nullptr;
    }
    return registry->adoptObject(found);
}

void ScreenExplorer::moveTo(int x, int y, bool isNew, text::TextUnit unit) {
    a11y::ObjectPtr object = objectAtPoint(x, y);
    // NVDA walks up past anything transparent to the mouse, then insists on a
    // content object -- a layout pane under the pointer is not what the user
    // is pointing at.
    a11y::ObjectPtr previous;
    while (object != nullptr &&
           object->presentationType() != a11y::PresentationType::Content &&
           object->role() != a11y::Role::Paragraph) {
        previous = object;
        object = object->parent();
    }
    if (object == nullptr) {
        object = previous;
    }
    if (object == nullptr) {
        return;
    }

    bool hasNewObject = false;
    if (object_ == nullptr || !object_->sameAs(*object)) {
        object_ = object;
        hasNewObject = true;
        if (updateReview) {
            a11y::Registry* registry = service_.registry();
            if (registry != nullptr && !registry->api().setNavigatorObject(object)) {
                return;
            }
        }
    }

    // The position inside the object, which is what makes this more than
    // object announcement: the pointer lands on a WORD, not on a widget.
    text::TextRangePtr position;
    if (auto range = a11y::ObjectTextRange::makeAtPoint(object, x, y)) {
        range->expand(unit);
        position = std::move(range);
    }
    if (position != nullptr && updateReview) {
        if (Review* review = service_.review()) {
            review->setPosition(position->copy(), /*clearNavigator=*/false,
                                /*isCaret=*/false, /*isMouse=*/true);
        }
    }

    speech::Presenter* presenter = service_.presenter();
    bool cancelled = false;
    if (hasNewObject && presenter != nullptr) {
        // A new object: cancel what is being said and announce it. The user
        // has moved, so the previous announcement is about somewhere else.
        presenter->cancelSpeech();
        cancelled = true;
        a11y::Registry* registry = service_.registry();
        if (registry != nullptr) {
            if (const auto& speak = registry->presentation().speakObject) {
                speak(*object, a11y::OutputReason::Mouse);
            }
        }
    }

    if (position == nullptr) {
        return;
    }
    // THE SAME POSITION IS NOT ANNOUNCED TWICE. Without this a pointer resting
    // on a word repeats it as fast as the mouse driver sends events.
    const bool positionChanged =
        isNew || position_ == nullptr ||
        position_->rangeKind() != position->rangeKind() ||
        position->compareEndPoints(*position_, text::EndPointPair::StartToStart) != 0 ||
        position->compareEndPoints(*position_, text::EndPointPair::EndToEnd) != 0;
    if (!positionChanged) {
        return;
    }
    position_ = position->copy();
    if (presenter != nullptr && !cancelled) {
        presenter->cancelSpeech();
    }
    service_.speakRange(*position, unit, a11y::OutputReason::Caret, object);
}

}  // namespace alus::nav
