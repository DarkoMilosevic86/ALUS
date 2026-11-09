// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/formatting.hpp.

#include "alus/braille/formatting.hpp"

namespace alus::braille {
namespace {

bool fieldFlag(const text::Field* field, std::string_view key) {
    if (field == nullptr) {
        return false;
    }
    if (const std::optional<bool> value = field->getBool(key); value.has_value()) {
        return *value;
    }
    // A key that is present with a value the normaliser did not turn into a
    // boolean ("underline" carrying a style name, say) still means "on": the
    // marker reports presence, not a value.
    return field->contains(key) && !field->get(key).empty();
}

}  // namespace

const std::vector<FormattingMarker>& fontAttributeMarkers() {
    // The letter, and the letter plus dot 7 for the end. See the header.
    static const std::vector<FormattingMarker> markers{
        {"bold", "⠃", "⡃"},
        {"italic", "⠊", "⡊"},
        {"underline", "⠥", "⡥"},
        {"strikethrough", "⠎", "⡎"},
        {"invalid-spelling", "⠑", "⡑"},
        {"invalid-grammar", "⠛", "⡛"},
    };
    return markers;
}

bool shouldUseMarker(std::string_view attribute, const Settings& settings) {
    if (attribute == "invalid-spelling" || attribute == "invalid-grammar") {
        return settings.reportSpellingErrorsInBraille;
    }
    return settings.reportFontAttributes;
}

std::string paragraphStartMarker(const Settings& settings) {
    if (!settings.readByParagraph) {
        return {};
    }
    return settings.paragraphStartMarker;
}

std::optional<std::string> formattingTags(const text::FormatField& field,
                                          const text::FormatField* previous,
                                          const Settings& settings) {
    std::string inside;
    for (const FormattingMarker& marker : fontAttributeMarkers()) {
        if (!shouldUseMarker(marker.attribute, settings)) {
            continue;
        }
        const bool now = fieldFlag(&field, marker.attribute);
        const bool before = fieldFlag(previous, marker.attribute);
        if (now && !before) {
            inside += marker.start;
        } else if (before && !now) {
            inside += marker.end;
        }
    }
    if (inside.empty()) {
        return std::nullopt;
    }
    std::string tag{kFormatTagStart};
    tag += inside;
    tag += kFormatTagEnd;
    return tag;
}

}  // namespace alus::braille
