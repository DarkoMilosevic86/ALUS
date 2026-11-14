// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T5.1 — the offset converter over emoji, CJK and a combining diacritic:
//        the byte <-> codepoint round trip must be exact in all three cases.
// T5.4 — unicodeNormalize: a decomposed "é" is recognised as ONE character
//        for spelling.
//
// architecture.md §7.3 calls this "the most likely source of silent bugs",
// which is why these tests are the first ones phase 5 has: a mistake here is
// invisible in English and breaks every other language.
#include "alus/text/offsets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using alus::text::OffsetConverter;

namespace {

// U+1F600 GRINNING FACE: four UTF-8 bytes, one character.
constexpr std::string_view kEmoji = "\xF0\x9F\x98\x80";
// U+4E2D U+6587 "中文": three bytes each.
constexpr std::string_view kCjk = "\xE4\xB8\xAD\xE6\x96\x87";
// "e" + U+0301 COMBINING ACUTE ACCENT: three bytes, two codepoints, one
// character as the user sees it.
constexpr std::string_view kDecomposedE = "e\xCC\x81";
// U+00E9: the composed form, two bytes, one codepoint.
constexpr std::string_view kComposedE = "\xC3\xA9";

}  // namespace

TEST_CASE("T5.1 the ASCII fast path is exact and builds no table", "[text][offsets]") {
    const OffsetConverter converter("hello");
    CHECK(converter.isAscii());
    CHECK(converter.byteLength() == 5);
    CHECK(converter.codepointLength() == 5);
    for (std::size_t i = 0; i <= 5; ++i) {
        CHECK(converter.toCodepoint(i) == i);
        CHECK(converter.toByte(i) == i);
    }
}

TEST_CASE("T5.1 emoji: one character, four bytes, round trip exact", "[text][offsets]") {
    const std::string text = std::string("a") + std::string(kEmoji) + "b";
    const OffsetConverter converter(text);

    CHECK_FALSE(converter.isAscii());
    CHECK(converter.byteLength() == 6);
    CHECK(converter.codepointLength() == 3);

    // The character index of each byte: 'a' is 0, every byte of the emoji is
    // 1, 'b' is 2. This is rule 1, and it is what makes one arrow key press
    // step over the whole emoji.
    CHECK(converter.toCodepoint(0) == 0);
    CHECK(converter.toCodepoint(1) == 1);
    CHECK(converter.toCodepoint(2) == 1);
    CHECK(converter.toCodepoint(3) == 1);
    CHECK(converter.toCodepoint(4) == 1);
    CHECK(converter.toCodepoint(5) == 2);
    CHECK(converter.toCodepoint(6) == 3);

    CHECK(converter.toByte(0) == 0);
    CHECK(converter.toByte(1) == 1);
    CHECK(converter.toByte(2) == 5);
    CHECK(converter.toByte(3) == 6);

    // Round trip: a byte offset inside the sequence comes back as the START of
    // that sequence, never as a split one.
    for (std::size_t byte = 0; byte <= converter.byteLength(); ++byte) {
        const std::size_t back = converter.toByte(converter.toCodepoint(byte));
        CHECK(back <= byte);
        CHECK(converter.toCodepoint(back) == converter.toCodepoint(byte));
    }

    // And the character range at any byte of the emoji is the whole emoji.
    for (std::size_t byte = 1; byte <= 4; ++byte) {
        const auto [start, end] = converter.codepointRangeAt(byte);
        CHECK(start == 1);
        CHECK(end == 5);
        CHECK(text.substr(start, end - start) == kEmoji);
    }
}

TEST_CASE("T5.1 CJK: three bytes per character", "[text][offsets]") {
    const OffsetConverter converter(kCjk);
    CHECK(converter.byteLength() == 6);
    CHECK(converter.codepointLength() == 2);
    CHECK(converter.toByte(1) == 3);
    CHECK(converter.toCodepoint(4) == 1);
    const auto [start, end] = converter.codepointRangeAt(1);
    CHECK(start == 0);
    CHECK(end == 3);
}

TEST_CASE("T5.1 combining diacritic: two codepoints, each addressable",
          "[text][offsets]") {
    const OffsetConverter converter(kDecomposedE);
    CHECK(converter.byteLength() == 3);
    // Two codepoints: the converter counts codepoints, not graphemes. The
    // grapheme question is T5.4's, and it is answered by normalisation.
    CHECK(converter.codepointLength() == 2);
    CHECK(converter.toByte(0) == 0);
    CHECK(converter.toByte(1) == 1);
    CHECK(converter.toCodepoint(2) == 1);
}

TEST_CASE("T5.1 out of range offsets are clamped, never an error",
          "[text][offsets]") {
    const OffsetConverter converter(kCjk);
    CHECK(converter.toCodepoint(999) == 2);
    CHECK(converter.toByte(999) == 6);
    const auto [start, end] = converter.codepointRangeAt(999);
    CHECK(start == 6);
    CHECK(end == 6);

    const OffsetConverter empty("");
    CHECK(empty.byteLength() == 0);
    CHECK(empty.codepointLength() == 0);
    CHECK(empty.toCodepoint(0) == 0);
    CHECK(empty.toByte(0) == 0);
}

TEST_CASE("T5.1 invalid UTF-8 stays traversable", "[text][offsets]") {
    // A stray continuation byte and a truncated two-byte sequence: a toolkit
    // handing over a half-written buffer must not cost the user the line.
    const std::string text = std::string("a\x80") + "b\xC3";
    const OffsetConverter converter(text);
    CHECK(converter.byteLength() == 4);
    CHECK(converter.codepointLength() == 4);
    CHECK(converter.toByte(3) == 3);
    CHECK(converter.toCodepoint(3) == 3);
}

TEST_CASE("T5.4 a decomposed character is one character for spelling",
          "[text][offsets]") {
    using alus::text::codepointCount;
    using alus::text::isSingleCharacter;
    using alus::text::normalizeNfc;

    // Two codepoints before normalisation...
    CHECK(codepointCount(kDecomposedE) == 2);
    // ...one after it, and therefore ONE character to spell. Without this the
    // "press twice to spell" shortcut stops working on every accented letter.
    CHECK(normalizeNfc(kDecomposedE) == kComposedE);
    CHECK(codepointCount(normalizeNfc(kDecomposedE)) == 1);
    CHECK(isSingleCharacter(kDecomposedE));
    CHECK(isSingleCharacter(kComposedE));
    CHECK(isSingleCharacter("a"));
    CHECK(isSingleCharacter(kEmoji));
    CHECK_FALSE(isSingleCharacter("ab"));
    CHECK_FALSE(isSingleCharacter(""));
    // A composed sequence that does NOT compose to one character stays two.
    CHECK_FALSE(isSingleCharacter(kCjk));
}

TEST_CASE("the Unicode category questions the word search asks",
          "[text][offsets]") {
    using alus::text::codepointAt;
    using alus::text::isSpaceCharacter;
    using alus::text::isUppercase;
    using alus::text::isWordCharacter;

    CHECK(isWordCharacter('a'));
    CHECK(isWordCharacter('7'));
    CHECK_FALSE(isWordCharacter('.'));
    CHECK_FALSE(isWordCharacter(' '));
    // A combining mark is part of a word (§11.1). This is the rule whose
    // absence breaks every language with diacritics.
    CHECK(isWordCharacter(codepointAt("\xCC\x81", 0)));
    // Serbian: č is a letter.
    CHECK(isWordCharacter(codepointAt("č", 0)));

    CHECK(isSpaceCharacter(' '));
    CHECK(isSpaceCharacter('\t'));
    CHECK(isSpaceCharacter('\n'));
    // A non-breaking space is a space, which is why the word search converts
    // it rather than tripping over it.
    CHECK(isSpaceCharacter(0x00A0));

    CHECK(isUppercase("A"));
    CHECK(isUppercase("Č"));
    CHECK_FALSE(isUppercase("a"));
    CHECK_FALSE(isUppercase("Ab"));
    CHECK_FALSE(isUppercase("."));
    CHECK_FALSE(isUppercase(""));
}

TEST_CASE("the base line search, with \\r\\n counted once", "[text][offsets]") {
    using alus::text::findEndOfLine;
    using alus::text::findStartOfLine;

    const std::string_view text = "one\ntwo\r\nthree";
    //                             0123 4567 8 9

    CHECK(findStartOfLine(text, 0) == 0);
    CHECK(findStartOfLine(text, 2) == 0);
    // Standing on the break itself still belongs to the line before it.
    CHECK(findStartOfLine(text, 3) == 0);
    CHECK(findStartOfLine(text, 4) == 4);
    CHECK(findStartOfLine(text, 6) == 4);
    // The \n of a \r\n pair: the line still starts at "two".
    CHECK(findStartOfLine(text, 8) == 4);
    CHECK(findStartOfLine(text, 9) == 9);
    CHECK(findStartOfLine(text, 99) == 9);

    CHECK(findEndOfLine(text, 0) == 4);
    CHECK(findEndOfLine(text, 3) == 4);
    // The \r\n pair goes with the line it ends, so the next line starts at 9.
    CHECK(findEndOfLine(text, 4) == 9);
    CHECK(findEndOfLine(text, 9) == text.size());

    CHECK(findStartOfLine("", 0) == 0);
    CHECK(findEndOfLine("", 0) == 0);
}

TEST_CASE("the base word search breaks on non-alphanumerics", "[text][offsets]") {
    using alus::text::findEndOfWord;
    using alus::text::findStartOfWord;

    const std::string_view text = "hello, world";
    //                             012345678

    CHECK(findStartOfWord(text, 0) == 0);
    CHECK(findStartOfWord(text, 3) == 0);
    CHECK(findStartOfWord(text, 5) == 5);  // the comma
    CHECK(findStartOfWord(text, 8) == 7);

    CHECK(findEndOfWord(text, 0) == 5);
    // From the comma: the punctuation plus the whitespace after it.
    CHECK(findEndOfWord(text, 5) == 7);
    CHECK(findEndOfWord(text, 7) == text.size());

    // A Serbian word with diacritics is ONE word, start to end.
    const std::string_view serbian = "čača radost";
    CHECK(findStartOfWord(serbian, 3) == 0);
    CHECK(findEndOfWord(serbian, 0) == 7);
}

TEST_CASE("indentation splits from content", "[text][offsets]") {
    using alus::text::splitIndentation;

    auto [indent, content] = splitIndentation("    text");
    CHECK(indent == "    ");
    CHECK(content == "text");

    auto [tabs, afterTabs] = splitIndentation("\t\tx");
    CHECK(tabs == "\t\t");
    CHECK(afterTabs == "x");

    // A line break is NOT indentation: an empty line has no indent and the
    // break is content, which is how "ignore blank lines" can work at all.
    auto [none, all] = splitIndentation("\nx");
    CHECK(none.empty());
    CHECK(all == "\nx");

    auto [blankIndent, blankContent] = splitIndentation("   ");
    CHECK(blankIndent == "   ");
    CHECK(blankContent.empty());
}
