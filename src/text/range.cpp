// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The base implementations of TextRange. See include/alus/text/range.hpp.

#include "alus/text/range.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"

namespace alus::text {

TextRange::~TextRange() = default;

std::string_view endPointPairName(EndPointPair which) {
    switch (which) {
        case EndPointPair::StartToStart:
            return "startToStart";
        case EndPointPair::StartToEnd:
            return "startToEnd";
        case EndPointPair::EndToStart:
            return "endToStart";
        case EndPointPair::EndToEnd:
            return "endToEnd";
    }
    return "startToStart";
}

EndPointPair reverseEndPointPair(EndPointPair which) {
    switch (which) {
        case EndPointPair::StartToStart:
            return EndPointPair::StartToStart;
        case EndPointPair::StartToEnd:
            return EndPointPair::EndToStart;
        case EndPointPair::EndToStart:
            return EndPointPair::StartToEnd;
        case EndPointPair::EndToEnd:
            return EndPointPair::EndToEnd;
    }
    return which;
}

TextWithFields TextRange::getTextWithFields(const FormatReportSettings& settings) const {
    (void)settings;
    // NVDA's base implementation: the text and nothing else.
    TextWithFields result;
    result.push_back(TextOrField::ofText(text()));
    return result;
}

bool TextRange::isOverlapping(const TextRange& other) const {
    // NVDA: `isOverlapping`, transferred including its oddity -- two ranges
    // that merely touch are not overlapping, and a collapsed range overlaps
    // nothing but an identical one.
    if (compareEndPoints(other, EndPointPair::StartToStart) == 0) {
        return true;
    }
    return compareEndPoints(other, EndPointPair::EndToStart) > 0 &&
           other.compareEndPoints(*this, EndPointPair::EndToStart) > 0;
}

std::vector<core::Rect> TextRange::boundingRects() const {
    return {};
}

std::optional<core::Point> TextRange::pointAtStart() const {
    const std::vector<core::Rect> rects = boundingRects();
    if (rects.empty()) {
        return std::nullopt;
    }
    return rects.front().topLeft();
}

std::optional<int> TextRange::unitIndex(TextUnit unit) const {
    (void)unit;
    return std::nullopt;
}

std::optional<int> TextRange::unitCount(TextUnit unit) const {
    (void)unit;
    return std::nullopt;
}

std::string TextRange::locationText() const {
    const std::optional<core::Point> point = pointAtStart();
    if (!point.has_value()) {
        return {};
    }
    // Translators: the current position's screen coordinates in pixels; the
    // first %s is the horizontal and the second the vertical coordinate.
    return i18n::substitute(_("Positioned at %s, %s"), std::to_string(point->x),
                            std::to_string(point->y));
}

TextUnit TextRange::resolveReadingChunk(TextUnit unit) const {
    if (unit != TextUnit::ReadingChunk) {
        return unit;
    }
    return readingUnitToTextUnit(readingUnit_);
}

}  // namespace alus::text
