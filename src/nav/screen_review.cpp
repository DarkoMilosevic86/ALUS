// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/screen_review.hpp.

#include "alus/nav/screen_review.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>

namespace alus::nav {
namespace {

// The text a flattened object contributes. An object with navigable text
// contributes its text; everything else contributes its name, which is what is
// drawn on the screen for a button or a label.
std::string textOf(const a11y::Object& object) {
    if (object.hasNavigableText() && object.characterCount() > 0) {
        return object.textRange(0, object.characterCount());
    }
    if (!object.name().empty()) {
        return object.name();
    }
    // A value is drawn too: a slider shows its number, an entry its contents.
    return object.value();
}

}  // namespace

struct ScreenReviewRange::Snapshot {
    a11y::ObjectPtr window;
    std::vector<Piece> pieces;
    // Where each ROW starts and ends in the story. A row is a line of screen
    // review, and `lineOffsets` answers from this: that is what makes
    // `numpad8` read a row of three columns rather than one column of it.
    std::vector<Offsets> rows;
    std::string story;
    std::size_t visited = 0;
    bool hitLimit = false;
};

namespace {

void collect(const a11y::ObjectPtr& object, int depth, const ScreenReviewLimits& limits,
             std::vector<ScreenReviewRange::Piece>& out, std::size_t& visited,
             bool& hitLimit) {
    if (object == nullptr || depth > limits.maxDepth) {
        return;
    }
    if (visited >= limits.maxObjects) {
        hitLimit = true;
        return;
    }
    ++visited;

    const a11y::PresentationType presentation = object->presentationType();
    if (presentation == a11y::PresentationType::Unavailable) {
        // Off screen or invisible: not on the screen, so not in screen review.
        // Its children are not either -- an invisible container hides them.
        return;
    }
    if (presentation == a11y::PresentationType::Content) {
        const core::Rect rect = object->location();
        std::string text = textOf(*object);
        if (!text.empty() && !rect.empty()) {
            ScreenReviewRange::Piece piece;
            piece.object = object;
            piece.rect = rect;
            // The offsets are filled in once the pieces are sorted.
            out.push_back(std::move(piece));
            // A content object that carries its own text is a LEAF for this
            // purpose: its children are the runs inside it, and flattening
            // them would say every word twice.
            if (object->hasNavigableText()) {
                return;
            }
        }
    }
    for (const a11y::ObjectPtr& child : object->children()) {
        collect(child, depth + 1, limits, out, visited, hitLimit);
        if (hitLimit) {
            return;
        }
    }
}

std::shared_ptr<const ScreenReviewRange::Snapshot> buildSnapshotImpl(
    const a11y::ObjectPtr& window, const ScreenReviewLimits& limits) {
    auto snapshot = std::make_shared<ScreenReviewRange::Snapshot>();
    snapshot->window = window;
    // Step 1-2 of §10: the window's DESCENDANTS, not the window. A frame has
    // a name (its title) and would otherwise be the first "line" of the
    // screen, which is both wrong and confusing -- the title bar is already
    // announced when the window is activated.
    if (window != nullptr) {
        for (const a11y::ObjectPtr& child : window->children()) {
            collect(child, 1, limits, snapshot->pieces, snapshot->visited,
                    snapshot->hitLimit);
            if (snapshot->hitLimit) {
                break;
            }
        }
    }

    // Step 3 of §10: spatial order. Top first, then left.
    std::stable_sort(snapshot->pieces.begin(), snapshot->pieces.end(),
                     [](const ScreenReviewRange::Piece& a,
                        const ScreenReviewRange::Piece& b) {
                         if (a.rect.y != b.rect.y) {
                             return a.rect.y < b.rect.y;
                         }
                         return a.rect.x < b.rect.x;
                     });

    // What ALUS adds: grouping into rows. A piece belongs to the row being
    // built when its VERTICAL CENTRE is above the row's bottom edge -- so a
    // tall button beside a short label is still on the same line, and the next
    // row of a form starts a new one.
    std::vector<std::vector<ScreenReviewRange::Piece>> rows;
    int rowBottom = 0;
    for (ScreenReviewRange::Piece& piece : snapshot->pieces) {
        const int centre = piece.rect.y + piece.rect.height / 2;
        if (rows.empty() || centre >= rowBottom) {
            rows.emplace_back();
            rowBottom = piece.rect.bottom();
        } else {
            // The row grows no taller than its tallest member so far.
            rowBottom = std::max(rowBottom, piece.rect.bottom());
        }
        rows.back().push_back(piece);
    }

    // Within a row, strictly left to right (the sort above only ordered the
    // ones that shared an exact top coordinate).
    for (std::vector<ScreenReviewRange::Piece>& row : rows) {
        std::stable_sort(row.begin(), row.end(),
                         [](const ScreenReviewRange::Piece& a,
                            const ScreenReviewRange::Piece& b) {
                             return a.rect.x < b.rect.x;
                         });
    }

    // Step 4: join into one story. Pieces in a row are separated by a space,
    // rows by a newline, so the text layer's own line navigation walks rows.
    std::vector<ScreenReviewRange::Piece> flattened;
    for (std::vector<ScreenReviewRange::Piece>& row : rows) {
        const int rowStart = static_cast<int>(snapshot->story.size());
        for (std::size_t index = 0; index < row.size(); ++index) {
            if (index > 0) {
                snapshot->story.push_back(' ');
            }
            ScreenReviewRange::Piece& piece = row[index];
            std::string text = textOf(*piece.object);
            // A newline inside a piece would create a row the rectangles know
            // nothing about, so it becomes a space.
            std::replace(text.begin(), text.end(), '\n', ' ');
            std::replace(text.begin(), text.end(), '\r', ' ');
            piece.start = static_cast<int>(snapshot->story.size());
            snapshot->story += text;
            piece.end = static_cast<int>(snapshot->story.size());
            flattened.push_back(piece);
        }
        snapshot->story.push_back('\n');
        // THE ROW INCLUDES ITS TERMINATOR, exactly as a line does everywhere
        // else in the text layer ("first line\n"). Without it, moving by line
        // would land on the newline and `expand` would give the row back
        // again -- the review cursor would stick on the first row.
        snapshot->rows.push_back(
            ScreenReviewRange::Offsets{rowStart, static_cast<int>(snapshot->story.size())});
    }
    if (!snapshot->story.empty() && snapshot->story.back() == '\n') {
        // The last row needs no trailing newline: it would be an empty line
        // the user can stand on and hear "blank".
        snapshot->story.pop_back();
        if (!snapshot->rows.empty()) {
            snapshot->rows.back().end = static_cast<int>(snapshot->story.size());
        }
    }
    snapshot->pieces = std::move(flattened);
    return snapshot;
}

}  // namespace

ScreenReviewRange::ScreenReviewRange(std::shared_ptr<const Snapshot> snapshot)
    : snapshot_(std::move(snapshot)) {}

ScreenReviewRange::~ScreenReviewRange() = default;

std::unique_ptr<ScreenReviewRange> ScreenReviewRange::make(const a11y::ObjectPtr& window,
                                                           text::Position position,
                                                           ScreenReviewLimits limits) {
    if (window == nullptr) {
        return nullptr;
    }
    std::shared_ptr<const Snapshot> snapshot = buildSnapshotImpl(window, limits);
    if (snapshot->pieces.empty()) {
        return nullptr;
    }
    std::unique_ptr<ScreenReviewRange> range(new ScreenReviewRange(std::move(snapshot)));
    range->initialize(position);
    return range;
}

std::unique_ptr<ScreenReviewRange> ScreenReviewRange::makeAtObject(
    const a11y::ObjectPtr& window, const a11y::ObjectPtr& at, ScreenReviewLimits limits) {
    std::unique_ptr<ScreenReviewRange> range = make(window, text::Position::First, limits);
    if (range == nullptr || at == nullptr) {
        return range;
    }
    for (const Piece& piece : range->snapshot_->pieces) {
        if (piece.object != nullptr && piece.object->sameAs(*at)) {
            range->setOffsets(piece.start, piece.start);
            return range;
        }
    }
    // The object is not on the flattened screen (a menu item in a popup that
    // belongs to another window, for example). Its POSITION still is, so the
    // nearest piece by screen coordinates is used rather than giving up.
    const core::Rect target = at->location();
    if (!target.empty()) {
        const Piece* best = nullptr;
        long bestDistance = 0;
        for (const Piece& piece : range->snapshot_->pieces) {
            const long dx = piece.rect.x - target.x;
            const long dy = piece.rect.y - target.y;
            const long distance = dx * dx + dy * dy;
            if (best == nullptr || distance < bestDistance) {
                best = &piece;
                bestDistance = distance;
            }
        }
        if (best != nullptr) {
            range->setOffsets(best->start, best->start);
        }
    }
    return range;
}

std::unique_ptr<ScreenReviewRange> ScreenReviewRange::makeAtPoint(
    const a11y::ObjectPtr& window, int x, int y, ScreenReviewLimits limits) {
    std::unique_ptr<ScreenReviewRange> range = make(window, text::Position::First, limits);
    if (range == nullptr) {
        return nullptr;
    }
    const std::optional<int> offset = range->offsetFromPoint(x, y);
    if (offset.has_value()) {
        range->setOffsets(*offset, *offset);
    }
    return range;
}

std::unique_ptr<text::TextRange> ScreenReviewRange::copy() const {
    // The snapshot is SHARED, not rebuilt: see the header, this is why the
    // review commands are cheap.
    std::unique_ptr<ScreenReviewRange> range(new ScreenReviewRange(snapshot_));
    range->copyOffsetsFrom(*this);
    return range;
}

const a11y::ObjectPtr& ScreenReviewRange::window() const {
    return snapshot_->window;
}

const std::vector<ScreenReviewRange::Piece>& ScreenReviewRange::pieces() const {
    return snapshot_->pieces;
}

std::size_t ScreenReviewRange::visitedObjects() const {
    return snapshot_->visited;
}

bool ScreenReviewRange::hitLimit() const {
    return snapshot_->hitLimit;
}

a11y::ObjectPtr ScreenReviewRange::objectAtOffset(int offset) const {
    for (const Piece& piece : snapshot_->pieces) {
        if (offset >= piece.start && offset < piece.end) {
            return piece.object;
        }
    }
    // Between two pieces (the separating space, or the newline): the piece
    // before it is the honest answer, because that is what the user last heard.
    const Piece* best = nullptr;
    for (const Piece& piece : snapshot_->pieces) {
        if (piece.start <= offset && (best == nullptr || piece.start > best->start)) {
            best = &piece;
        }
    }
    return best != nullptr ? best->object : nullptr;
}

const void* ScreenReviewRange::bookmarkKind() const {
    // The snapshot is the identity: two ranges over the same flattened screen
    // compare, and one taken after the window changed does not.
    return static_cast<const void*>(snapshot_.get());
}

int ScreenReviewRange::storyLength() const {
    return static_cast<int>(snapshot_->story.size());
}

std::string ScreenReviewRange::storyText() const {
    return snapshot_->story;
}

std::string ScreenReviewRange::textRange(int start, int end) const {
    const int length = storyLength();
    start = std::clamp(start, 0, length);
    end = std::clamp(end, 0, length);
    if (end <= start) {
        return {};
    }
    return snapshot_->story.substr(static_cast<std::size_t>(start),
                                   static_cast<std::size_t>(end - start));
}

std::optional<text::OffsetsTextRange::Offsets> ScreenReviewRange::lineOffsets(
    int offset) const {
    // A LINE IS A ROW OF THE SCREEN, which is the whole point of the grouping.
    for (std::size_t index = 0; index < snapshot_->rows.size(); ++index) {
        const Offsets& row = snapshot_->rows[index];
        const bool last = index + 1 == snapshot_->rows.size();
        if (offset >= row.start && (offset < row.end || (last && offset <= row.end))) {
            return row;
        }
    }
    if (!snapshot_->rows.empty()) {
        return offset < snapshot_->rows.front().start ? snapshot_->rows.front()
                                                      : snapshot_->rows.back();
    }
    return std::nullopt;
}

std::optional<core::Rect> ScreenReviewRange::boundingRectFromOffset(int offset) const {
    for (const Piece& piece : snapshot_->pieces) {
        if (offset < piece.start || offset >= piece.end) {
            continue;
        }
        // The piece's own rectangle, narrowed to the character: the objects
        // have no per-character extents (that is what a display model would
        // have given), so the width is divided evenly. It is an approximation
        // and it is the right one -- braille routing needs to know WHICH WORD
        // was touched, not which pixel.
        const int characters = std::max(piece.end - piece.start, 1);
        const int width = std::max(piece.rect.width / characters, 1);
        const int index = offset - piece.start;
        return core::Rect{piece.rect.x + index * width, piece.rect.y, width,
                          piece.rect.height};
    }
    return std::nullopt;
}

std::optional<int> ScreenReviewRange::offsetFromPoint(int x, int y) const {
    for (const Piece& piece : snapshot_->pieces) {
        if (!piece.rect.contains(x, y)) {
            continue;
        }
        const int characters = std::max(piece.end - piece.start, 1);
        const int width = std::max(piece.rect.width / characters, 1);
        const int index = std::clamp((x - piece.rect.x) / width, 0, characters - 1);
        return piece.start + index;
    }
    return std::nullopt;
}

std::optional<core::Rect> ScreenReviewRange::objectLocation() const {
    if (snapshot_->window == nullptr) {
        return std::nullopt;
    }
    const core::Rect rect = snapshot_->window->location();
    if (rect.empty()) {
        return std::nullopt;
    }
    return rect;
}

std::optional<int> ScreenReviewRange::lineNumberFromOffset(int offset) const {
    for (std::size_t index = 0; index < snapshot_->rows.size(); ++index) {
        const Offsets& row = snapshot_->rows[index];
        const bool last = index + 1 == snapshot_->rows.size();
        if (offset >= row.start && (offset < row.end || (last && offset <= row.end))) {
            return static_cast<int>(index);
        }
    }
    return std::nullopt;
}

std::optional<int> ScreenReviewRange::lineCount() const {
    return static_cast<int>(snapshot_->rows.size());
}

}  // namespace alus::nav
