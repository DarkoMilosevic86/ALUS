// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Font formatting, as braille shows it.
//
// Port of NVDA's `braille/formatting.py`.
//
// THERE ARE TWO WAYS TO SHOW BOLD ON A BRAILLE DISPLAY and they must not both
// be on at once:
//
//   LIBLOUIS   the translation table is told, through `typeform`, that these
//              characters are bold, and the table decides what to do -- in
//              most grades an emphasis sign before the word. This is correct
//              braille and it is the default.
//   TAGS       ALUS inserts a visible marker into the text: `⣋⠃⣙` before the
//              bold run and `⣋⡃⣙` after it. Ugly, unambiguous, and the only
//              way to see WHERE the formatting starts when the table's
//              emphasis signs are hard to spot.
//
// `Settings::fontFormattingDisplay` chooses, and this file implements the
// second; the first is `TextRegion`'s `typeformFor`.
//
// THE MARKERS ARE THE BRAILLE LETTER, AND THE LETTER PLUS DOT 7 FOR THE END:
// b/⠃ for bold, i/⠊ for italic, u/⠥ for underline, s/⠎ for strikethrough,
// e/⠑ for a spelling error, g/⠛ for a grammar error. Dot 7 is braille's own
// "the thing just ended", so the pairs read as one convention rather than
// twelve symbols.
#pragma once

#include "alus/braille/settings.hpp"
#include "alus/text/fields.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::braille {

// NVDA: `FormatTagDelimiter`.
inline constexpr std::string_view kFormatTagStart = "⣋";
inline constexpr std::string_view kFormatTagEnd = "⣙";

// NVDA: `constants.TEXT_SEPARATOR`. One space, used everywhere braille joins
// two pieces of text. Spelled out because it is also the thing the buffer
// looks for when it wraps at a word boundary: a space in the TEXT becomes an
// empty CELL, and an empty cell is what `rindex` searches for.
inline constexpr std::string_view kTextSeparator = " ";

// NVDA: `constants.INPUT_START_IND` / `INPUT_END_IND`. What brackets
// untranslated braille input while the user is still typing a word.
inline constexpr std::string_view kInputStartIndicator = "⣏";
inline constexpr std::string_view kInputEndIndicator = " ⣹";

// NVDA: `FormattingMarker`.
struct FormattingMarker {
    // The format field key this marker reports ("bold", "invalid-spelling").
    std::string_view attribute;
    std::string_view start;
    std::string_view end;
};

// The six markers, in NVDA's order -- which is the order they appear in a tag.
const std::vector<FormattingMarker>& fontAttributeMarkers();

// NVDA: `FormattingMarker.shouldBeUsed`. The spelling and grammar markers are
// governed by their own setting, everything else by `reportFontAttributes`.
bool shouldUseMarker(std::string_view attribute, const Settings& settings);

// NVDA: `getParagraphStartMarker`. Empty when reading by paragraph is off.
std::string paragraphStartMarker(const Settings& settings);

// NVDA: `_getFormattingTags`. The tag for the attributes that CHANGED between
// `previous` and `field`, or nothing when none did. An attribute that turned
// on contributes its start marker, one that turned off its end marker, so a
// run of bold text produces exactly two tags however many format fields it
// spans.
std::optional<std::string> formattingTags(const text::FormatField& field,
                                          const text::FormatField* previous,
                                          const Settings& settings);

}  // namespace alus::braille
