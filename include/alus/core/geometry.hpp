// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Points and rectangles, in screen coordinates.
//
// Port of the part of NVDA's `locationHelper.py` that ALUS needs: `RectLTWH`
// and `Point`. It lives in the core because by phase 5 THREE layers need the
// same rectangle and none of them should depend on the others: the object
// model gets it from `Component::get_extents`, the text layer returns it from
// `boundingRects` (per line, for braille routing and the mouse), and screen
// review SORTS by it -- that is what "flat object review" means (§10).
//
// `a11y::Rect` is an alias of `core::Rect`, so the phase 4 code that named it
// that still reads the same.
#pragma once

#include <algorithm>

namespace alus::core {

struct Point {
    int x = 0;
    int y = 0;

    bool operator==(const Point& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

// Left, top, width, height -- AT-SPI's own shape
// (`Component::get_extents(ATSPI_COORD_TYPE_SCREEN)`), not left/top/right/
// bottom. Mixing the two up is a classic source of off-by-a-window bugs, so
// there is one shape and the accessors name their edges.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool empty() const noexcept { return width <= 0 || height <= 0; }
    int left() const noexcept { return x; }
    int top() const noexcept { return y; }
    // Exclusive, as a width always is.
    int right() const noexcept { return x + width; }
    int bottom() const noexcept { return y + height; }

    Point topLeft() const noexcept { return Point{x, y}; }
    Point center() const noexcept { return Point{x + width / 2, y + height / 2}; }

    bool contains(int px, int py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    bool contains(const Point& point) const noexcept { return contains(point.x, point.y); }

    // The overlap, which is empty when they do not overlap. NVDA:
    // `RectLTWH.intersection`.
    Rect intersection(const Rect& other) const noexcept {
        const int newLeft = std::max(left(), other.left());
        const int newTop = std::max(top(), other.top());
        const int newRight = std::min(right(), other.right());
        const int newBottom = std::min(bottom(), other.bottom());
        if (newRight <= newLeft || newBottom <= newTop) {
            return Rect{};
        }
        return Rect{newLeft, newTop, newRight - newLeft, newBottom - newTop};
    }

    // The smallest rectangle containing both. NVDA:
    // `RectLTWH.fromCollection`, which is how a text range's per-line
    // rectangle is built from its first and last character.
    Rect united(const Rect& other) const noexcept {
        if (empty()) {
            return other;
        }
        if (other.empty()) {
            return *this;
        }
        const int newLeft = std::min(left(), other.left());
        const int newTop = std::min(top(), other.top());
        const int newRight = std::max(right(), other.right());
        const int newBottom = std::max(bottom(), other.bottom());
        return Rect{newLeft, newTop, newRight - newLeft, newBottom - newTop};
    }

    bool operator==(const Rect& other) const noexcept {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

}  // namespace alus::core
