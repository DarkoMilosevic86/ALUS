// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/api.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"

#include <algorithm>
#include <utility>

namespace alus::a11y {
namespace {

// NVDA's own safety limit in api.setFocusObject. A tree that says every object
// is its own grandparent exists in the wild; without a limit the screen reader
// walks it until the watchdog fires.
constexpr int kMaxAncestorDepth = 100;

}  // namespace

Api::Api(Registry& registry) : registry_(registry) {}

Api::~Api() = default;

bool Api::setFocusObject(ObjectPtr object) {
    if (object == nullptr) {
        ALUS_DEBUG_WARNING("setFocusObject: no object");
        return false;
    }
    // T4.9: a dead object is refused, and the caller is expected to look.
    if (!object->isAlive()) {
        ALUS_DEBUG("setFocusObject refused: the object is gone");
        return false;
    }

    if (focus_ != nullptr) {
        // The old focus is told it is losing the focus BEFORE the global state
        // changes, as in NVDA: a handler that asks "am I the focus" must still
        // get the truth it was written against.
        registry_.dispatcher().executeEvent(
            core::Event{std::string{events::kLoseFocus}, focus_->id(), nullptr});
    }

    // The old chain, with the old focus appended: the new chain is matched
    // against it to find where the two converge.
    std::vector<ObjectPtr> oldLine = ancestors_;
    if (focus_ != nullptr) {
        oldLine.push_back(focus_);
    }

    // Walk up from the new focus, looking for an object the old line already
    // has. Everything above the match is REUSED, which is what makes
    // focusDifferenceLevel meaningful (and what keeps the ancestors' property
    // caches from being rebuilt on every keystroke).
    std::vector<ObjectPtr> ancestors;
    int focusDifferenceLevel = 0;
    ObjectPtr current = object;
    bool matched = false;
    int depth = 0;
    while (current != nullptr) {
        if (++depth > kMaxAncestorDepth) {
            ALUS_ERROR("never ending focus ancestry at {} ({}); stopping at the desktop",
                       current->name(), roleName(current->role()));
            ancestors.insert(ancestors.begin(), desktop_);
            break;
        }
        for (int index = static_cast<int>(oldLine.size()) - 1; index >= 0; --index) {
            const ObjectPtr& old = oldLine[static_cast<std::size_t>(index)];
            if (old == nullptr || !old->sameAs(*current)) {
                continue;
            }
            // Converged. Keep the old objects up to and including this one.
            std::vector<ObjectPtr> reused(oldLine.begin(),
                                          oldLine.begin() + index + 1);
            reused.insert(reused.end(), ancestors.begin(), ancestors.end());
            ancestors = std::move(reused);
            focusDifferenceLevel = index + 1;
            matched = true;
            break;
        }
        if (matched) {
            break;
        }
        ancestors.insert(ancestors.begin(), current);
        current = current->parent();
    }
    // The last entry is the new focus itself; the chain holds only ancestors.
    if (!ancestors.empty()) {
        ancestors.pop_back();
    }

    const std::vector<ObjectPtr> previousLine = oldLine;
    const int previousDifference = focusDifferenceLevel;

    focusDifferenceLevel_ = focusDifferenceLevel;
    focus_ = std::move(object);
    ancestors_ = std::move(ancestors);

    if (registry_.settings().reviewCursorFollowFocus) {
        setNavigatorObject(focus_, true);
    }

    // focusExited for every old ancestor the new focus does not share. In
    // reverse, innermost first, as NVDA fires them.
    if (static_cast<int>(previousLine.size()) > previousDifference) {
        for (int index = static_cast<int>(previousLine.size()) - 2;
             index >= previousDifference; --index) {
            const ObjectPtr& old = previousLine[static_cast<std::size_t>(index)];
            if (old == nullptr) {
                continue;
            }
            registry_.dispatcher().executeEvent(
                core::Event{std::string{events::kFocusExited}, old->id(), nullptr});
        }
    }
    return true;
}

bool Api::setForegroundObject(ObjectPtr object) {
    if (object == nullptr || !object->isAlive()) {
        return false;
    }
    foreground_ = std::move(object);
    return true;
}

ObjectPtr Api::navigator() const {
    if (navigator_ != nullptr) {
        return navigator_;
    }
    // NVDA falls back to the review position's object; until phase 5 brings
    // the review cursor, the focus is that position.
    return focus_;
}

bool Api::setNavigatorObject(ObjectPtr object, bool isFocus) {
    if (object == nullptr || !object->isAlive()) {
        return false;
    }
    navigator_ = std::move(object);
    // NVDA clears the review position here so that it is rebuilt from the new
    // navigator object; phase 5 owns that position.
    auto payload = std::make_shared<EventPayload>();
    payload->detail1 = isFocus ? 1 : 0;
    registry_.dispatcher().executeEvent(
        core::Event{std::string{events::kBecomeNavigatorObject}, navigator_->id(),
                    std::move(payload)});
    // Phase 5's review position is rebuilt from the navigator object.
    registry_.notifyNavigatorChanged(navigator_, isFocus);
    return true;
}

bool Api::setMouseObject(ObjectPtr object) {
    if (object == nullptr || !object->isAlive()) {
        return false;
    }
    mouse_ = std::move(object);
    return true;
}

void Api::setDesktopObject(ObjectPtr object) {
    // NVDA cannot refuse this one: it is set at start-up, before anything else
    // exists, and without it there is no tree to walk.
    desktop_ = std::move(object);
}

bool Api::isTypingProtected() const {
    return focus_ != nullptr && focus_->isProtected();
}

bool Api::isFocusEditable() const {
    if (focus_ == nullptr) {
        return false;
    }
    const StateSet& states = focus_->states();
    if (states.has(State::Editable) && !states.has(State::ReadOnly)) {
        return true;
    }
    switch (focus_->role()) {
        case Role::EditableText:
        case Role::PasswordEdit:
        case Role::EditBar:
        case Role::Terminal:
            return !states.has(State::ReadOnly);
        default:
            return false;
    }
}

void Api::clear() {
    focus_.reset();
    ancestors_.clear();
    focusDifferenceLevel_ = 0;
    foreground_.reset();
    navigator_.reset();
    mouse_.reset();
    desktop_.reset();
}

}  // namespace alus::a11y
