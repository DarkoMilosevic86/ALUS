// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Segmentation: where does a character, a word or a sentence begin and end.
//
// Deliverable `alus::text::WordSeg` of phase-plan.md, under the name this ended
// up deserving: it segments characters and sentences as well as words, and
// calling that `WordSeg` would misname two thirds of it (see phase-5.md O1).
//
// architecture.md §7.3 is explicit: ALUS uses ICU AND ONLY ICU, and NVDA's
// `textUtils/uniscribe.py` is deleted rather than replaced. That is a better
// position than NVDA's, not a worse one:
//
//   * UAX#29 is the standard the rest of the world agrees on, so a word
//     boundary ALUS reports is the one the application's own double-click
//     selects;
//   * ICU carries the dictionaries that make Japanese, Thai and Chinese
//     segmentation possible at all -- there are no spaces to break on, and no
//     rule-based segmenter can do it (T5.3 asks for Japanese by name);
//   * grapheme clusters come from the same iterator, which is what makes "one
//     character" mean the same thing to review, to spelling and to braille.
//
// WHAT THE PLATFORM ALREADY DOES, and why this is still needed: AT-SPI's
// `get_string_at_offset` offers WORD and SENTENCE granularity, and
// `OffsetsTextRange` prefers it -- the toolkit knows its own text best. But
// `AtspiText` is optional per interface and the granularities are not
// uniformly implemented (GTK's word granularity differs from Qt's, and
// LibreOffice's sentence granularity is its own), so every unit needs an
// answer that does not depend on the application being helpful.
//
// THREADING: a Segmenter is a value, cheap to make and not shared. ICU break
// iterators are not thread safe and none is cached across threads.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace alus::text {

// A half-open byte range inside the text that was segmented.
struct Segment {
    std::size_t start = 0;
    std::size_t end = 0;

    bool empty() const noexcept { return end <= start; }
    std::size_t length() const noexcept { return end > start ? end - start : 0; }
    bool operator==(const Segment& other) const noexcept {
        return start == other.start && end == other.end;
    }
};

class Segmenter {
public:
    // NO LOCALE IS PASSED, and that is NVDA's own finding, kept because it is
    // right (`textUtils/icu.py`): word, character and sentence boundaries are
    // SCRIPT driven, not locale driven. ICU picks the Thai, Khmer and CJK
    // dictionaries from the script of the characters it is looking at, so
    // Japanese segments correctly with no language told to it (T5.3), and any
    // locale -- including an unrecognised one -- gives identical boundaries.
    // The one thing a locale would buy is abbreviation tailoring in sentence
    // breaking ("Dr." not ending a sentence), which NVDA does not apply
    // either; doing it here would make ALUS and NVDA disagree about where a
    // sentence ends, in a way no test could justify.
    //
    // Keeps no copy of the text (like OffsetConverter): the caller holds the
    // line or the paragraph it is already working with.
    explicit Segmenter(std::string_view text);
    ~Segmenter();

    Segmenter(const Segmenter&) = delete;
    Segmenter& operator=(const Segmenter&) = delete;
    Segmenter(Segmenter&&) noexcept;
    Segmenter& operator=(Segmenter&&) noexcept;

    // True when ICU answered. False means the rule-based fallback is in use,
    // which is correct for space-separated scripts and WRONG for Japanese --
    // so it is reported rather than hidden, and T5.3 asserts it.
    static bool icuAvailable();
    // The ICU version this build is linked against, or an empty string.
    static std::string icuVersion();

    // The grapheme cluster containing this byte offset. "One character" as the
    // user means it: a base letter with its combining marks, an emoji with its
    // variation selector and skin tone, a Devanagari conjunct.
    Segment characterAt(std::size_t byteOffset) const;
    // The word containing this byte offset, in NVDA's sense of the word unit:
    // THE WORD PLUS THE WHITESPACE AFTER IT. That is not UAX#29's word
    // boundary and it is deliberate -- NVDA's `findEndOfWord` does the same,
    // because a word unit that stops before the space makes "move by word"
    // need two presses per word.
    Segment wordAt(std::size_t byteOffset) const;
    // The sentence containing this byte offset.
    Segment sentenceAt(std::size_t byteOffset) const;

    // The boundary before / after this offset, for the units above. Used by
    // `move()`, which steps rather than expands.
    std::size_t nextWordStart(std::size_t byteOffset) const;
    std::size_t previousWordStart(std::size_t byteOffset) const;

    std::string_view text() const noexcept { return text_; }

private:
    class Impl;
    struct Boundaries;
    // The boundary list for one break kind, computed on first use: a line is
    // segmented once and then asked about several times (a word command asks
    // for the word, then for its text, then for its bounding rectangle).
    const Boundaries& graphemes() const;
    const Boundaries& words() const;
    const Boundaries& sentences() const;

    std::string_view text_;
    std::unique_ptr<Impl> impl_;
};

// A convenience for the common one-shot question, so that a caller that needs
// exactly one answer does not have to name a Segmenter.
Segment characterSegment(std::string_view text, std::size_t byteOffset);
Segment wordSegment(std::string_view text, std::size_t byteOffset);
Segment sentenceSegment(std::string_view text, std::size_t byteOffset);

}  // namespace alus::text
