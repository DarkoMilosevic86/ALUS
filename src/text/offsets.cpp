// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/text/offsets.hpp for why this file exists at all.

#include "alus/text/offsets.hpp"

#include <glib.h>

#include <algorithm>
#include <cstring>

namespace alus::text {
namespace {

// The length in bytes of the UTF-8 sequence starting with this byte, or 1 for
// a byte that cannot start one. Returning 1 for an invalid byte is rule 3: a
// damaged string is still traversable, one byte at a time.
std::size_t sequenceLength(unsigned char lead) {
    if (lead < 0x80U) {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;
}

bool isContinuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

}  // namespace

OffsetConverter::OffsetConverter(std::string_view text) : text_(text) {
    const unsigned char* data = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t size = text.size();

    // The ASCII fast path: one pass of plain byte comparisons, and if it holds
    // no table is built and every conversion is the identity.
    bool ascii = true;
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] >= 0x80U) {
            ascii = false;
            break;
        }
    }
    if (ascii) {
        count_ = size;
        return;
    }

    starts_.reserve(size);
    std::size_t offset = 0;
    while (offset < size) {
        starts_.push_back(static_cast<std::uint32_t>(offset));
        std::size_t length = sequenceLength(data[offset]);
        // A truncated or malformed sequence must not swallow the bytes after
        // it: only continuation bytes that are really there belong to it.
        std::size_t step = 1;
        while (step < length && offset + step < size && isContinuation(data[offset + step])) {
            ++step;
        }
        offset += step;
    }
    count_ = starts_.size();
}

std::size_t OffsetConverter::toCodepoint(std::size_t byteOffset) const {
    if (byteOffset >= text_.size()) {
        return count_;
    }
    if (isAscii()) {
        return byteOffset;
    }
    // The largest sequence start at or before this offset: the character the
    // offset belongs to, even when it points into the middle of one (rule 1).
    const auto it = std::upper_bound(starts_.begin(), starts_.end(),
                                     static_cast<std::uint32_t>(byteOffset));
    return static_cast<std::size_t>(it - starts_.begin()) - 1;
}

std::pair<std::size_t, std::size_t> OffsetConverter::toCodepoints(std::size_t byteStart,
                                                                  std::size_t byteEnd) const {
    const std::size_t start = toCodepoint(byteStart);
    std::size_t end = toCodepoint(byteEnd);
    if (end < start) {
        end = start;
    }
    return {start, end};
}

std::size_t OffsetConverter::toByte(std::size_t codepointIndex) const {
    if (codepointIndex >= count_) {
        return text_.size();
    }
    if (isAscii()) {
        return codepointIndex;
    }
    return starts_[codepointIndex];
}

std::pair<std::size_t, std::size_t> OffsetConverter::toBytes(std::size_t start,
                                                             std::size_t end) const {
    const std::size_t byteStart = toByte(start);
    std::size_t byteEnd = toByte(end);
    if (byteEnd < byteStart) {
        byteEnd = byteStart;
    }
    return {byteStart, byteEnd};
}

std::size_t OffsetConverter::alignToCodepoint(std::size_t byteOffset) const {
    if (byteOffset >= text_.size()) {
        return text_.size();
    }
    if (isAscii()) {
        return byteOffset;
    }
    return starts_[toCodepoint(byteOffset)];
}

std::pair<std::size_t, std::size_t> OffsetConverter::codepointRangeAt(
    std::size_t byteOffset) const {
    if (byteOffset >= text_.size()) {
        return {text_.size(), text_.size()};
    }
    if (isAscii()) {
        return {byteOffset, byteOffset + 1};
    }
    const std::size_t index = toCodepoint(byteOffset);
    const std::size_t start = starts_[index];
    const std::size_t end = index + 1 < count_ ? starts_[index + 1] : text_.size();
    return {start, end};
}

// ---------------------------------------------------------------------------

std::string normalizeNfc(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    gchar* normalized = g_utf8_normalize(text.data(), static_cast<gssize>(text.size()),
                                         G_NORMALIZE_NFC);
    if (normalized == nullptr) {
        // Invalid UTF-8. Returning the text unchanged is the only honest
        // answer: refusing to normalise must never lose the user's line.
        return std::string(text);
    }
    std::string result(normalized);
    g_free(normalized);
    return result;
}

std::size_t codepointCount(std::string_view text) {
    return OffsetConverter(text).codepointLength();
}

bool isSingleCharacter(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    if (codepointCount(text) == 1) {
        return true;
    }
    return codepointCount(normalizeNfc(text)) == 1;
}

std::uint32_t codepointAt(std::string_view text, std::size_t byteOffset) {
    if (byteOffset >= text.size()) {
        return 0;
    }
    const char* start = text.data() + byteOffset;
    const gunichar value = g_utf8_get_char_validated(
        start, static_cast<gssize>(text.size() - byteOffset));
    if (value == static_cast<gunichar>(-1) || value == static_cast<gunichar>(-2)) {
        // Rule 3: an invalid byte answers with its own value, which is never a
        // word character and never a space, so segmentation treats it as
        // punctuation and keeps going.
        return static_cast<std::uint32_t>(static_cast<unsigned char>(*start));
    }
    return static_cast<std::uint32_t>(value);
}

bool isWordCharacter(std::uint32_t codepoint) {
    switch (g_unichar_type(codepoint)) {
        // L*: letters.
        case G_UNICODE_LOWERCASE_LETTER:
        case G_UNICODE_MODIFIER_LETTER:
        case G_UNICODE_OTHER_LETTER:
        case G_UNICODE_TITLECASE_LETTER:
        case G_UNICODE_UPPERCASE_LETTER:
        // M*: marks. A combining mark IS part of a word; §11.1 says what
        // happens when it is not.
        case G_UNICODE_SPACING_MARK:
        case G_UNICODE_ENCLOSING_MARK:
        case G_UNICODE_NON_SPACING_MARK:
        // N*: numbers.
        case G_UNICODE_DECIMAL_NUMBER:
        case G_UNICODE_LETTER_NUMBER:
        case G_UNICODE_OTHER_NUMBER:
            return true;
        default:
            return false;
    }
}

bool isSpaceCharacter(std::uint32_t codepoint) {
    // NVDA uses `str.isspace()`, which is true for the ASCII control
    // whitespace as well as the Unicode separators; g_unichar_isspace matches
    // it, except that it says nothing about \v and \f.
    return g_unichar_isspace(codepoint) != FALSE || codepoint == 0x0BU || codepoint == 0x0CU;
}

bool isWordOrSeparatorCharacter(std::uint32_t codepoint) {
    if (isWordCharacter(codepoint)) {
        return true;
    }
    switch (g_unichar_type(codepoint)) {
        // Z*: separators.
        case G_UNICODE_LINE_SEPARATOR:
        case G_UNICODE_PARAGRAPH_SEPARATOR:
        case G_UNICODE_SPACE_SEPARATOR:
            return true;
        default:
            return false;
    }
}

bool isUppercase(std::string_view text) {
    // NVDA: `str.isupper()` -- true when there is at least one cased character
    // and every cased character is upper case.
    bool sawCased = false;
    OffsetConverter converter(text);
    for (std::size_t index = 0; index < converter.codepointLength(); ++index) {
        const std::uint32_t value = codepointAt(text, converter.toByte(index));
        if (g_unichar_islower(value) != FALSE) {
            return false;
        }
        if (g_unichar_isupper(value) != FALSE || g_unichar_istitle(value) != FALSE) {
            sawCased = true;
        }
    }
    return sawCased;
}

// ---------------------------------------------------------------------------
// The base line and word search (NVDA textInfos/offsets.py:88-187)
// ---------------------------------------------------------------------------

std::size_t findStartOfLine(std::string_view text, std::size_t offset) {
    if (text.empty() || offset == 0) {
        return 0;
    }
    if (offset >= text.size()) {
        offset = text.size() - 1;
    }
    // NVDA: standing on the \n of a \r\n pair, look before the \r.
    if (text[offset] == '\n' && offset > 0 && text[offset - 1] == '\r') {
        --offset;
    }
    if (offset == 0) {
        return 0;
    }
    // NVDA searches the half-open range [0, offset), so the break the caret
    // stands on does not count as the end of the previous line.
    std::size_t start = text.rfind('\n', offset - 1);
    if (start == std::string_view::npos) {
        start = text.rfind('\r', offset - 1);
    }
    if (start == std::string_view::npos) {
        return 0;
    }
    return start + 1;
}

std::size_t findEndOfLine(std::string_view text, std::size_t offset) {
    if (text.empty()) {
        return 0;
    }
    if (offset >= text.size()) {
        offset = text.size() - 1;
    }
    std::size_t end = offset;
    if (text[end] != '\n') {
        end = text.find('\n', offset);
        if (end == std::string_view::npos && text[offset] != '\r') {
            end = text.find('\r', offset);
        }
    }
    if (end == std::string_view::npos) {
        end = text.size() - 1;
    }
    return end + 1;
}

std::size_t findStartOfWord(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) {
        return offset;
    }
    OffsetConverter converter(text);
    std::size_t index = converter.toCodepoint(offset);
    const auto charAt = [&](std::size_t i) {
        return codepointAt(text, converter.toByte(i));
    };
    while (index > 0 && isSpaceCharacter(charAt(index))) {
        --index;
    }
    if (!isWordCharacter(charAt(index))) {
        return converter.toByte(index);
    }
    while (index > 0 && isWordCharacter(charAt(index - 1))) {
        --index;
    }
    return converter.toByte(index);
}

std::size_t findEndOfWord(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) {
        // NVDA returns one past the offset here, and `move()` relies on it to
        // reach the insertion point at the end of the text.
        return offset + 1;
    }
    OffsetConverter converter(text);
    const std::size_t count = converter.codepointLength();
    std::size_t index = converter.toCodepoint(offset);
    const auto charAt = [&](std::size_t i) {
        return i < count ? codepointAt(text, converter.toByte(i)) : 0U;
    };
    if (isWordCharacter(charAt(index))) {
        while (index < count && isWordCharacter(charAt(index))) {
            ++index;
        }
    } else if (!isWordOrSeparatorCharacter(charAt(index))) {
        ++index;
    }
    while (index < count && isSpaceCharacter(charAt(index))) {
        ++index;
    }
    return converter.toByte(index);
}

std::pair<std::string_view, std::string_view> splitIndentation(std::string_view text) {
    // NVDA: r"^([^\S\r\n\f\v]*)(.*)$" -- whitespace that is NOT a line break.
    const OffsetConverter converter(text);
    const std::size_t count = converter.codepointLength();
    std::size_t index = 0;
    for (; index < count; ++index) {
        const std::size_t byteOffset = converter.toByte(index);
        const char byte = text[byteOffset];
        if (byte == '\r' || byte == '\n' || byte == '\f' || byte == '\v') {
            break;
        }
        if (!isSpaceCharacter(codepointAt(text, byteOffset))) {
            break;
        }
    }
    const std::size_t split = converter.toByte(index);
    return {text.substr(0, split), text.substr(split)};
}

}  // namespace alus::text
