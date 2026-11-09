// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Exploring by pointer: what is under this screen position.
//
// Port of NVDA's `screenExplorer.py` (89 lines), which §10 says transfers
// almost verbatim -- the only platform-dependent parts are
// `objectFromPoint` -> `Component::get_accessible_at_point` and
// `makeTextInfo(Point)` -> `Text::get_offset_at_point`, and both are already
// behind `a11y::Backend`.
//
// It is used by mouse tracking today and by touch exploration later, and it is
// the reason flat object review (`nav::ScreenReview`) is worth having beyond
// the review commands: a position on screen maps to an object AND to a place
// in that object's text, so moving the pointer reads what is under it rather
// than announcing the same control again and again.
//
// THE TWO RULES THAT KEEP IT QUIET, both NVDA's:
//   * a NEW OBJECT is announced in full (name, role, states), and the old
//     speech is cancelled first -- the user has moved, so what was being said
//     is stale;
//   * THE SAME position is not announced twice. Without that, a pointer
//     resting on a word repeats it as fast as the mouse driver sends events.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

namespace alus::nav {

class Service;

class ScreenExplorer {
public:
    explicit ScreenExplorer(Service& service);
    ~ScreenExplorer();

    ScreenExplorer(const ScreenExplorer&) = delete;
    ScreenExplorer& operator=(const ScreenExplorer&) = delete;

    // NVDA: `ScreenExplorer.updateReview`. Whether exploring also moves the
    // navigator object and the review cursor. Off by default: mouse tracking
    // should not move what the keyboard user is reviewing.
    bool updateReview = false;

    // NVDA: `moveTo(x, y, new, unit)`. `isNew` forces the announcement even
    // when the position has not changed, which is what a tap does.
    void moveTo(int x, int y, bool isNew = false,
                text::TextUnit unit = text::TextUnit::Line);

    void reset();

    const a11y::ObjectPtr& currentObject() const noexcept { return object_; }

private:
    a11y::ObjectPtr objectAtPoint(int x, int y) const;

    Service& service_;
    a11y::ObjectPtr object_;
    text::TextRangePtr position_;
};

}  // namespace alus::nav
