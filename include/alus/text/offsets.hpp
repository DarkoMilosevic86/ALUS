// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Offset encoding: the most likely source of silent bugs in the whole project.
//
// Port of NVDA's `textUtils.OffsetConverter` family, and specifically of
// `UTF8OffsetConverter` (`textUtils/__init__.py:320`). 01-ARHITEKTURA.md §7.3
// states the problem: THREE OFFSET SYSTEMS ARE IN PLAY.
//
//   * AT-SPI2 counts in UTF-8 BYTES. Every offset that crosses the platform
//     boundary -- `Text::get_text`, `get_caret_offset`, `set_caret_offset`,
//     `get_string_at_offset`, `get_selection` -- is a byte offset.
//   * NVDA assumes UTF-16 code units, because that is what Windows speaks.
//     ALUS inherits none of that: `WideStringOffsetConverter` has no reason to
//     exist here and is not ported.
//   * Everything ABOVE the platform boundary wants to count CHARACTERS, as a
//     user does: "move one character right", "this line is 12 characters".
//
// ALUS resolves it the way §7.3 prescribes: the text layer keeps AT-SPI's own
// byte offsets (so `OffsetsTextRange::encoding` is UTF-8, and no conversion
// happens on the hot path at all), and this class converts to and from
// CODEPOINT indices wherever a human-facing count or a per-character step is
// needed.
//
// THE RULES THIS CLASS KEEPS, each one a test in tests/text/test_offsets.cpp:
//
//  1. A byte offset in the MIDDLE of a UTF-8 sequence belongs to the character
//     that sequence encodes. It converts to that character's index, and
//     converting back gives the SEQUENCE START -- never a split sequence.
//     This is what makes an emoji one step of the arrow key rather than four.
//  2. Out-of-range offsets are CLAMPED, not an error. A screen reader asks
//     about a caret in a document that is being edited underneath it; the
//     honest answer is "the end", not an exception. NVDA's `raiseOnError` is
//     therefore not ported: nothing in NVDA passes it as true either.
//  3. An empty or invalid byte is never produced. Text from a toolkit may be
//     invalid UTF-8 (a half-written buffer, a `char` array with a stray 0x80);
//     such a byte is treated as one character of its own, so a damaged string
//     is read aloud imperfectly instead of silently truncating the line.
//
// The ASCII fast path is a measurement, not an optimisation reflex: every line
// of a terminal and nearly every widget label is ASCII, where byte offset and
// character index are the same number and the index table is never built.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace alus::text {

class OffsetConverter {
public:
    // Keeps no copy of the text: the caller must keep it alive. Every user in
    // ALUS converts offsets for a string it is already holding (a line, a
    // story), so copying it would double the cost of the one thing this class
    // exists to make cheap.
    explicit OffsetConverter(std::string_view text);

    std::string_view text() const noexcept { return text_; }

    // The number of bytes: AT-SPI's own character count for this string.
    std::size_t byteLength() const noexcept { return text_.size(); }
    // The number of codepoints: what a user would call the length.
    std::size_t codepointLength() const noexcept { return count_; }
    // True when the two are the same number and no conversion is needed.
    bool isAscii() const noexcept { return starts_.empty(); }

    // Byte offset -> codepoint index. Clamped; an offset inside a sequence
    // gives the index of the character that sequence encodes (rule 1).
    std::size_t toCodepoint(std::size_t byteOffset) const;
    std::pair<std::size_t, std::size_t> toCodepoints(std::size_t byteStart,
                                                     std::size_t byteEnd) const;

    // Codepoint index -> byte offset. Clamped to the end of the string; the
    // result always sits on a sequence boundary (rule 1).
    std::size_t toByte(std::size_t codepointIndex) const;
    std::pair<std::size_t, std::size_t> toBytes(std::size_t start, std::size_t end) const;

    // The byte range of the single codepoint at this byte offset, which is the
    // answer `_getCharacterOffsets` needs when no segmenter is available: the
    // whole sequence, never a part of one. An offset at or past the end gives
    // an empty range at the end.
    std::pair<std::size_t, std::size_t> codepointRangeAt(std::size_t byteOffset) const;

    // The byte offset of the start of the sequence containing this offset.
    std::size_t alignToCodepoint(std::size_t byteOffset) const;

private:
    std::string_view text_;
    std::size_t count_ = 0;
    // Byte offset of each codepoint, or empty for pure ASCII (where the
    // identity mapping is exact).
    std::vector<std::uint32_t> starts_;
};

// ---------------------------------------------------------------------------
// The Unicode questions the text layer asks
// ---------------------------------------------------------------------------

// NFC. The same answer `speech::Presenter::unicodeNormalize` gives, and
// deliberately the same code: §7.3's closing note is that the review layer and
// the speech layer must agree on "is this one character", or a decomposed "é"
// is spelled by one of them and not the other (T5.4).
std::string normalizeNfc(std::string_view text);

// The number of codepoints, without building a converter.
std::size_t codepointCount(std::string_view text);

// True when the text is ONE character as a user sees it: one codepoint, or
// several that compose to one under NFC. This is NVDA's
// `len(text) == 1 or len(unicodeNormalize(text)) == 1`, which decides whether
// a caret movement spells the character instead of speaking it (T5.4).
bool isSingleCharacter(std::string_view text);

// The codepoint at a byte offset, 0 past the end. Invalid bytes answer with
// the byte's own value, so a damaged string still moves forward.
std::uint32_t codepointAt(std::string_view text, std::size_t byteOffset);

// NVDA's `unicodedata.category(ch)[0] in "LMN"` -- a letter, a mark or a
// number, i.e. "part of a word" for the fallback word segmentation of
// `findStartOfWord`. Spelled out because the NVDA test is a MAJOR category
// test, and a combining mark being in it is the whole point (§11.1: a `\w`
// that excludes combining marks breaks every language with diacritics).
bool isWordCharacter(std::uint32_t codepoint);
bool isSpaceCharacter(std::uint32_t codepoint);
// NVDA's `in "LMNZ"`: a word character or a separator.
bool isWordOrSeparatorCharacter(std::uint32_t codepoint);
bool isUppercase(std::string_view text);

// NVDA: `findStartOfLine` / `findEndOfLine` (`textInfos/offsets.py:88-142`),
// the base implementation used when the platform cannot give line offsets.
// Byte offsets in, byte offsets out; `\r\n` counts as one break.
std::size_t findStartOfLine(std::string_view text, std::size_t offset);
std::size_t findEndOfLine(std::string_view text, std::size_t offset);
// NVDA: `findStartOfWord` / `findEndOfWord` (same file, lines 144-187). The
// fallback that breaks only on non-alphanumerics; the real answer comes from
// the platform or from `Segmenter` (ICU).
std::size_t findStartOfWord(std::string_view text, std::size_t offset);
std::size_t findEndOfWord(std::string_view text, std::size_t offset);

// NVDA: `speech.splitTextIndentation`. The leading whitespace and the rest.
std::pair<std::string_view, std::string_view> splitIndentation(std::string_view text);

}  // namespace alus::text
