// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T5.3 — UNIT_WORD through ICU: correct for Serbian, English and Japanese.
//
// 01-ARHITEKTURA.md §7.3 says ICU and only ICU, and the reason is the third
// language in that list: Japanese has no spaces to break on, so no rule-based
// segmenter can do it and only a dictionary can. The first two work either
// way, and the test says which answer this build is giving.
#include "alus/text/segment.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using alus::text::Segment;
using alus::text::Segmenter;

namespace {

// The pieces of text the segmenter is asked about, with their byte offsets
// written out so the expectations are readable.
constexpr std::string_view kEnglish = "the quick brown fox";
//                                     0   4     10    16
constexpr std::string_view kSerbian = "čaj je gotov";
//    bytes:                           0,1 2 3 4 5 6 7 8
// "日本語を読む" -- "reading Japanese". Three words, no spaces: 日本語 / を / 読む.
constexpr std::string_view kJapanese =
    "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x82\x92\xE8\xAA\xAD\xE3\x82\x80";

std::string textOf(std::string_view text, const Segment& segment) {
    return std::string(text.substr(segment.start, segment.length()));
}

}  // namespace

TEST_CASE("T5.3 the build says which segmenter it is using", "[text][segment][T5.3]") {
    // Not a tautology: `Segmenter::icuAvailable()` is what the phase 5 report
    // and tools/phase5-text-check.sh print, and a build that lost ICU must
    // say so rather than quietly segmenting Japanese wrongly.
    if (Segmenter::icuAvailable()) {
        CHECK_FALSE(Segmenter::icuVersion().empty());
        SUCCEED("ICU " << Segmenter::icuVersion() << " in use");
    } else {
        CHECK(Segmenter::icuVersion().empty());
        SUCCEED("ICU IS NOT LINKED INTO THIS BUILD. Serbian and English word "
                "segmentation are correct (the rule-based fallback handles "
                "space-separated scripts); Japanese is NOT. Install libicu-dev "
                "and rebuild: 01-ARHITEKTURA.md 7.3 requires ICU.");
    }
}

TEST_CASE("T5.3 English words, with the whitespace that follows them",
          "[text][segment][T5.3]") {
    const Segmenter segmenter(kEnglish);

    // NVDA's word unit: the word PLUS the space after it, so moving by word
    // takes one press per word and not two.
    CHECK(textOf(kEnglish, segmenter.wordAt(0)) == "the ");
    CHECK(textOf(kEnglish, segmenter.wordAt(2)) == "the ");
    // Standing ON the space still belongs to the word before it.
    CHECK(textOf(kEnglish, segmenter.wordAt(3)) == "the ");
    CHECK(textOf(kEnglish, segmenter.wordAt(4)) == "quick ");
    CHECK(textOf(kEnglish, segmenter.wordAt(10)) == "brown ");
    // The last word has no trailing space to take.
    CHECK(textOf(kEnglish, segmenter.wordAt(16)) == "fox");
}

TEST_CASE("T5.3 Serbian words: a diacritic is part of its word",
          "[text][segment][T5.3]") {
    const Segmenter segmenter(kSerbian);
    // "čaj" starts with a two-byte letter, and every byte of it belongs to the
    // same word. A segmenter that breaks inside the letter is the bug §11.1
    // warns about for every language with diacritics.
    CHECK(textOf(kSerbian, segmenter.wordAt(0)) == "čaj ");
    CHECK(textOf(kSerbian, segmenter.wordAt(1)) == "čaj ");
    CHECK(textOf(kSerbian, segmenter.wordAt(2)) == "čaj ");
    // The space after it belongs to it, as everywhere else.
    CHECK(textOf(kSerbian, segmenter.wordAt(4)) == "čaj ");
    CHECK(textOf(kSerbian, segmenter.wordAt(5)) == "je ");
    CHECK(textOf(kSerbian, segmenter.wordAt(8)) == "gotov");
}

TEST_CASE("T5.3 Japanese words: this is what ICU is for",
          "[text][segment][T5.3]") {
    const Segmenter segmenter(kJapanese);
    const Segment first = segmenter.wordAt(0);

    if (Segmenter::icuAvailable()) {
        // 日本語 / を / 読む: three words and not one, decided by ICU's
        // dictionary from the SCRIPT of the characters -- no language is told
        // to it (see segment.hpp).
        CHECK(textOf(kJapanese, first) == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        const Segment second = segmenter.wordAt(first.end);
        CHECK(textOf(kJapanese, second) == "\xE3\x82\x92");
        const Segment third = segmenter.wordAt(second.end);
        CHECK(textOf(kJapanese, third) == "\xE8\xAA\xAD\xE3\x82\x80");
    } else {
        // THE MEASURED CONSEQUENCE of having no ICU, written down rather than
        // hidden: with no spaces to break on, the rule-based fallback sees one
        // word where there are three, so `control+rightArrow` crosses the
        // whole phrase. Everything else in phase 5 works; this does not.
        CHECK(first.start == 0);
        CHECK(first.end == static_cast<std::size_t>(kJapanese.size()));
        SUCCEED("Japanese word segmentation needs ICU (see T5.3 above)");
    }
}

TEST_CASE("sentences are segmented over the text they are given",
          "[text][segment]") {
    constexpr std::string_view text = "First one. Second one! Third?";
    const Segmenter segmenter(text);
    CHECK(textOf(text, segmenter.sentenceAt(0)) == "First one. ");
    CHECK(textOf(text, segmenter.sentenceAt(11)) == "Second one! ");
    CHECK(textOf(text, segmenter.sentenceAt(23)) == "Third?");
}

TEST_CASE("a character is a grapheme cluster where ICU can say so",
          "[text][segment]") {
    // "e" + combining acute: ONE character to move over, two codepoints.
    constexpr std::string_view text = "e\xCC\x81x";
    const Segmenter segmenter(text);
    const Segment first = segmenter.characterAt(0);
    if (Segmenter::icuAvailable()) {
        CHECK(first.end == 3);
        CHECK(textOf(text, segmenter.characterAt(3)) == "x");
    } else {
        // Without ICU a character is one CODEPOINT, so the combining mark
        // steps on its own. That is the second visible cost of no ICU.
        CHECK(first.end == 1);
    }
}

TEST_CASE("an empty text and offsets past the end answer without crashing",
          "[text][segment]") {
    const Segmenter empty("");
    CHECK(empty.characterAt(0).start == 0);
    CHECK(empty.wordAt(0).start == 0);

    const Segmenter segmenter(kEnglish);
    // Past the end: NVDA's own answer is one offset wide, so that `move` can
    // reach the insertion point after the last character.
    const Segment past = segmenter.wordAt(kEnglish.size() + 5);
    CHECK(past.start == kEnglish.size() + 5);
    CHECK(past.end == kEnglish.size() + 6);
}
