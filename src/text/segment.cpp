// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// ICU segmentation. See include/alus/text/segment.hpp.
//
// TWO IMPLEMENTATION DECISIONS WORTH THE WORDS:
//
//  1. ICU IS ASKED IN UTF-8, THROUGH UText. ICU's C API is UTF-16 by default,
//     and every offset it returns would then be a UTF-16 index that has to be
//     mapped back to the byte offsets AT-SPI speaks -- two conversions per
//     keystroke and two places to get it wrong (§7.3). `utext_openUTF8` makes
//     ICU iterate the UTF-8 bytes directly, so the boundaries it reports ARE
//     the offsets the platform wants, with no mapping at all.
//  2. THE BOUNDARIES ARE COMPUTED ONCE PER TEXT, NOT PER QUESTION. A word
//     command asks for the word, then its text, then its bounding rectangle,
//     and `move()` asks for several in a row; the iterator is walked once and
//     the boundary list answers the rest by binary search.
//
// WITHOUT ICU the fallback is the base word search of `offsets.cpp` (NVDA's
// own `findStartOfWord`/`findEndOfWord`) plus a terminator rule for sentences.
// That is correct for space-separated scripts and WRONG for Japanese, so it is
// never silent: `icuAvailable()` says so and T5.3 asserts it.

#include "alus/text/segment.hpp"

#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"

#ifdef ALUS_HAVE_ICU
#include <unicode/ubrk.h>
#include <unicode/utext.h>
#include <unicode/uversion.h>
#endif

#include <algorithm>
#include <vector>

namespace alus::text {

// A sorted list of boundary byte offsets, always containing 0 and the length.
struct Segmenter::Boundaries {
    std::vector<std::size_t> offsets;
    bool computed = false;
};

class Segmenter::Impl {
public:
    Boundaries graphemes;
    Boundaries words;
    Boundaries sentences;
};

namespace {

enum class BreakKind { Character, Word, Sentence };

bool isAllSpace(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    const OffsetConverter converter(text);
    for (std::size_t index = 0; index < converter.codepointLength(); ++index) {
        if (!isSpaceCharacter(codepointAt(text, converter.toByte(index)))) {
            return false;
        }
    }
    return true;
}

#ifdef ALUS_HAVE_ICU

UBreakIteratorType icuKind(BreakKind kind) {
    switch (kind) {
        case BreakKind::Character:
            return UBRK_CHARACTER;
        case BreakKind::Word:
            return UBRK_WORD;
        case BreakKind::Sentence:
            return UBRK_SENTENCE;
    }
    return UBRK_CHARACTER;
}

// The root locale, unconditionally. See the header: boundaries are script
// driven and NVDA passes the root locale for exactly this reason.
constexpr const char* kRootLocale = "";

bool icuBoundaries(std::string_view text, BreakKind kind, std::vector<std::size_t>& out) {
    UErrorCode status = U_ZERO_ERROR;
    UText* utext = utext_openUTF8(nullptr, text.data(), static_cast<int64_t>(text.size()),
                                  &status);
    if (U_FAILURE(status) || utext == nullptr) {
        ALUS_DEBUG_WARNING("utext_openUTF8 failed: {}", u_errorName(status));
        if (utext != nullptr) {
            utext_close(utext);
        }
        return false;
    }
    UBreakIterator* iterator = ubrk_open(icuKind(kind), kRootLocale, nullptr, 0, &status);
    if (U_FAILURE(status) || iterator == nullptr) {
        ALUS_DEBUG_WARNING("ubrk_open failed: {}", u_errorName(status));
        utext_close(utext);
        return false;
    }
    ubrk_setUText(iterator, utext, &status);
    if (U_FAILURE(status)) {
        ALUS_DEBUG_WARNING("ubrk_setUText failed: {}", u_errorName(status));
        ubrk_close(iterator);
        utext_close(utext);
        return false;
    }

    out.clear();
    for (std::int32_t boundary = ubrk_first(iterator); boundary != UBRK_DONE;
         boundary = ubrk_next(iterator)) {
        out.push_back(static_cast<std::size_t>(boundary));
    }
    ubrk_close(iterator);
    utext_close(utext);

    if (out.empty() || out.front() != 0) {
        out.insert(out.begin(), 0);
    }
    if (out.back() != text.size()) {
        out.push_back(text.size());
    }
    return true;
}

#endif  // ALUS_HAVE_ICU

// The fallback boundary lists. Each is built from the rules of offsets.cpp, so
// that the shape of the answer is the same with and without ICU and only its
// quality differs.
void fallbackBoundaries(std::string_view text, BreakKind kind,
                        std::vector<std::size_t>& out) {
    out.clear();
    out.push_back(0);
    if (text.empty()) {
        return;
    }
    switch (kind) {
        case BreakKind::Character: {
            // One codepoint per segment: not a grapheme cluster. A combining
            // mark therefore steps on its own, which is the visible cost of
            // having no ICU.
            const OffsetConverter converter(text);
            for (std::size_t index = 1; index <= converter.codepointLength(); ++index) {
                out.push_back(converter.toByte(index));
            }
            break;
        }
        case BreakKind::Word: {
            std::size_t offset = 0;
            while (offset < text.size()) {
                const std::size_t end = findEndOfWord(text, offset);
                if (end <= offset) {
                    break;
                }
                out.push_back(std::min(end, text.size()));
                offset = end;
            }
            break;
        }
        case BreakKind::Sentence: {
            // A terminator followed by whitespace, with closing quotes and
            // brackets allowed in between. The same shape as the sentence
            // regular expression phase 2's SentenceBuffer already uses.
            const OffsetConverter converter(text);
            const std::size_t count = converter.codepointLength();
            for (std::size_t index = 0; index < count; ++index) {
                const std::size_t byteOffset = converter.toByte(index);
                const std::uint32_t value = codepointAt(text, byteOffset);
                if (value != '.' && value != '!' && value != '?' && value != 0x3002U) {
                    continue;
                }
                std::size_t next = index + 1;
                while (next < count) {
                    const std::uint32_t after = codepointAt(text, converter.toByte(next));
                    if (after == '"' || after == '\'' || after == ')' || after == ']' ||
                        after == 0x201DU) {
                        ++next;
                        continue;
                    }
                    break;
                }
                while (next < count &&
                       isSpaceCharacter(codepointAt(text, converter.toByte(next)))) {
                    ++next;
                }
                const std::size_t boundary = converter.toByte(next);
                if (boundary > out.back() && boundary < text.size()) {
                    out.push_back(boundary);
                }
            }
            break;
        }
    }
    if (out.back() != text.size()) {
        out.push_back(text.size());
    }
}

void computeBoundaries(std::string_view text, BreakKind kind,
                       std::vector<std::size_t>& out) {
#ifdef ALUS_HAVE_ICU
    if (icuBoundaries(text, kind, out)) {
        return;
    }
#endif
    fallbackBoundaries(text, kind, out);
}

// The segment of a boundary list containing this offset.
Segment segmentFor(const std::vector<std::size_t>& boundaries, std::size_t byteOffset,
                   std::size_t length) {
    if (byteOffset >= length) {
        // NVDA's own answer past the end: one offset wide, so that `move()`
        // can reach the insertion point after the last character.
        return Segment{byteOffset, byteOffset + 1};
    }
    const auto it = std::upper_bound(boundaries.begin(), boundaries.end(), byteOffset);
    if (it == boundaries.begin() || it == boundaries.end()) {
        return Segment{0, length};
    }
    return Segment{*(it - 1), *it};
}

}  // namespace

Segmenter::Segmenter(std::string_view text)
    : text_(text), impl_(std::make_unique<Impl>()) {}

Segmenter::~Segmenter() = default;
Segmenter::Segmenter(Segmenter&&) noexcept = default;
Segmenter& Segmenter::operator=(Segmenter&&) noexcept = default;

bool Segmenter::icuAvailable() {
#ifdef ALUS_HAVE_ICU
    return true;
#else
    return false;
#endif
}

std::string Segmenter::icuVersion() {
#ifdef ALUS_HAVE_ICU
    return U_ICU_VERSION;
#else
    return {};
#endif
}

const Segmenter::Boundaries& Segmenter::graphemes() const {
    Boundaries& boundaries = impl_->graphemes;
    if (!boundaries.computed) {
        computeBoundaries(text_, BreakKind::Character, boundaries.offsets);
        boundaries.computed = true;
    }
    return boundaries;
}

const Segmenter::Boundaries& Segmenter::words() const {
    Boundaries& boundaries = impl_->words;
    if (!boundaries.computed) {
        computeBoundaries(text_, BreakKind::Word, boundaries.offsets);
        boundaries.computed = true;
    }
    return boundaries;
}

const Segmenter::Boundaries& Segmenter::sentences() const {
    Boundaries& boundaries = impl_->sentences;
    if (!boundaries.computed) {
        computeBoundaries(text_, BreakKind::Sentence, boundaries.offsets);
        boundaries.computed = true;
    }
    return boundaries;
}

Segment Segmenter::characterAt(std::size_t byteOffset) const {
    return segmentFor(graphemes().offsets, byteOffset, text_.size());
}

Segment Segmenter::sentenceAt(std::size_t byteOffset) const {
    return segmentFor(sentences().offsets, byteOffset, text_.size());
}

Segment Segmenter::wordAt(std::size_t byteOffset) const {
    if (byteOffset >= text_.size()) {
        return Segment{byteOffset, byteOffset + 1};
    }
    const std::vector<std::size_t>& boundaries = words().offsets;
    Segment segment = segmentFor(boundaries, byteOffset, text_.size());

    // THE WHITESPACE RULE, ported from NVDA's `calculateWordOffsets`. ICU puts
    // a boundary on both sides of a whitespace run, so a line reads
    // "|Multiple| |boundaries|" and a single tab is a word of its own. NVDA's
    // word unit instead means "the word plus the whitespace after it", because
    // otherwise moving by word takes two presses per word. So:
    //
    //   * while the segment is nothing but whitespace, extend it BACKWARDS to
    //     the previous boundary -- standing in the gap belongs to the word
    //     before it;
    //   * then extend it FORWARDS over any following whitespace-only segments,
    //     stopping before the next real word.
    std::size_t prefixEnd = segment.end;
    while (segment.start > 0 &&
           isAllSpace(text_.substr(segment.start, prefixEnd - segment.start))) {
        const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), segment.start);
        if (it == boundaries.begin()) {
            segment.start = 0;
            break;
        }
        prefixEnd = segment.start;
        segment.start = *(it - 1);
    }
    while (segment.end < text_.size()) {
        const auto it = std::upper_bound(boundaries.begin(), boundaries.end(), segment.end);
        if (it == boundaries.end()) {
            break;
        }
        const std::size_t nextEnd = *it;
        if (!isAllSpace(text_.substr(segment.end, nextEnd - segment.end))) {
            break;
        }
        segment.end = nextEnd;
    }
    return segment;
}

std::size_t Segmenter::nextWordStart(std::size_t byteOffset) const {
    if (byteOffset >= text_.size()) {
        return text_.size();
    }
    return wordAt(byteOffset).end;
}

std::size_t Segmenter::previousWordStart(std::size_t byteOffset) const {
    if (byteOffset == 0) {
        return 0;
    }
    const Segment current = wordAt(byteOffset);
    if (current.start > 0 && current.start < byteOffset) {
        return current.start;
    }
    return wordAt(current.start > 0 ? current.start - 1 : 0).start;
}

Segment characterSegment(std::string_view text, std::size_t byteOffset) {
    return Segmenter(text).characterAt(byteOffset);
}

Segment wordSegment(std::string_view text, std::size_t byteOffset) {
    return Segmenter(text).wordAt(byteOffset);
}

Segment sentenceSegment(std::string_view text, std::size_t byteOffset) {
    return Segmenter(text).sentenceAt(byteOffset);
}

}  // namespace alus::text
