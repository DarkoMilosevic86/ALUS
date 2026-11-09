// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The three paragraph styles of 01-ARHITEKTURA.md §11.2, and the
// `(passKey, moved)` convention the plan says to keep.
#include "alus/nav/paragraph_nav.hpp"

#include "alus/text/regex.hpp"

#include "nav/support.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::nav::ParagraphMoveResult;
using alus::nav::moveToParagraph;
using alus::nav::nextParagraphStyle;
using alus::nav::paragraphTextAt;
using alus::nav::splitParagraphIntoChunks;
using alus::testing::DocumentTree;
using alus::testing::NavFixture;
using alus::text::ParagraphStyle;
using alus::text::Position;
using alus::text::TextUnit;

namespace {

// Two paragraphs, Markdown style: a blank line between them, and each one
// wrapped over several lines.
constexpr const char* kMarkdown =
    "The first paragraph starts here\n"
    "and carries on to a second line.\n"
    "\n"
    "The second paragraph is below\n"
    "the blank line.\n";

}  // namespace

TEST_CASE("the application style defers to the application", "[nav][paragraph]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, kMarkdown);
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    const ParagraphMoveResult result =
        moveToParagraph(*range, ParagraphStyle::Application, true, true);
    // `passKey` is the whole convention §11.2 asks to keep: ALUS declines and
    // the application's own paragraph movement stands. On Wayland it is
    // already happening, because the application got the key in parallel.
    CHECK(result.passKey);
    CHECK_FALSE(result.moved);
}

TEST_CASE("the multi line break style treats a blank line as the boundary",
          "[nav][paragraph]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, kMarkdown);
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    ParagraphMoveResult result =
        moveToParagraph(*range, ParagraphStyle::MultiLineBreak, true, true);
    CHECK_FALSE(result.passKey);
    CHECK(result.moved);
    // The whole paragraph is assembled, over as many line units as it takes:
    // reading only the first line would be the bug this style exists to avoid.
    CHECK(result.text.find("The second paragraph is below") != std::string::npos);
    CHECK(result.text.find("the blank line") != std::string::npos);

    // And back again, landing at the TOP of the first paragraph rather than
    // its last line -- NVDA's own exception for backwards movement.
    result = moveToParagraph(*range, ParagraphStyle::MultiLineBreak, false, true);
    CHECK(result.moved);
    CHECK(result.text.find("The first paragraph starts here") != std::string::npos);

    // There is nothing before it: refused, with something to say about it.
    result = moveToParagraph(*range, ParagraphStyle::MultiLineBreak, false, true);
    CHECK_FALSE(result.moved);
    CHECK(result.message == "No previous paragraph");
}

TEST_CASE("the single line break style treats every break as a boundary",
          "[nav][paragraph]") {
    NavFixture fixture;
    // Each line ends a paragraph in this style, which is what a code editor
    // or a chat log wants.
    DocumentTree tree(fixture.backend, "alpha\nbeta\ngamma\n");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);

    ParagraphMoveResult result =
        moveToParagraph(*range, ParagraphStyle::SingleLineBreak, true, true);
    CHECK(result.moved);
    CHECK(result.text == "beta");

    result = moveToParagraph(*range, ParagraphStyle::SingleLineBreak, true, true);
    CHECK(result.moved);
    CHECK(result.text == "gamma");
}

TEST_CASE("a blank paragraph is reported as blank", "[nav][paragraph]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "alpha\n\n\nbeta\n");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr range = document->makeTextRange(Position::First);
    REQUIRE(range != nullptr);
    // Standing on the blank run: the paragraph text is empty, and silence
    // would be indistinguishable from a broken screen reader.
    range->move(TextUnit::Line, 1);
    CHECK(paragraphTextAt(*range, ParagraphStyle::MultiLineBreak).empty());
    const ParagraphMoveResult result =
        moveToParagraph(*range, ParagraphStyle::MultiLineBreak, true, true);
    CHECK((result.text == "blank" || result.text.find("beta") != std::string::npos));
}

TEST_CASE("a long paragraph is split at sentence ends, never mid sentence",
          "[nav][paragraph]") {
    // A synthesizer handed half a sentence reads it with the wrong prosody,
    // so the chunks end where the sentences do.
    std::string paragraph;
    for (int index = 0; index < 80; ++index) {
        paragraph += "This is sentence number " + std::to_string(index) + ". ";
    }
    REQUIRE(paragraph.size() > alus::nav::kPreferredChunkSize);

    const std::vector<std::string> chunks = splitParagraphIntoChunks(paragraph);
    CHECK(chunks.size() > 1);
    std::string rejoined;
    for (const std::string& chunk : chunks) {
        rejoined += chunk;
        // Every chunk but the last ends after a full stop (with its space).
        if (&chunk != &chunks.back()) {
            const std::string trimmedChunk =
                chunk.substr(0, chunk.find_last_not_of(" ") + 1);
            CHECK(trimmedChunk.back() == '.');
        }
    }
    // And nothing was lost or duplicated.
    CHECK(rejoined == paragraph);

    // A short paragraph is one chunk and is not touched at all.
    const std::vector<std::string> one = splitParagraphIntoChunks("Short.");
    REQUIRE(one.size() == 1);
    CHECK(one.front() == "Short.");
}

TEST_CASE("the paragraph style cycles through all three", "[nav][paragraph]") {
    CHECK(nextParagraphStyle(ParagraphStyle::Application) ==
          ParagraphStyle::SingleLineBreak);
    CHECK(nextParagraphStyle(ParagraphStyle::SingleLineBreak) ==
          ParagraphStyle::MultiLineBreak);
    CHECK(nextParagraphStyle(ParagraphStyle::MultiLineBreak) ==
          ParagraphStyle::Application);
}

TEST_CASE("cycling the paragraph style writes it back and says it",
          "[nav][paragraph]") {
    NavFixture fixture;
    fixture.nav().cycleParagraphStyle();
    CHECK(fixture.config().getString("documentNavigation", "paragraphStyle", "") ==
          "singleLineBreak");
    REQUIRE_FALSE(fixture.messages.empty());
    CHECK(fixture.messages.back().find("Single line break") != std::string::npos);
}

// ---------------------------------------------------------------------------
// `p` / `shift+p`: which paragraphs count as TEXT paragraphs.
//
// This is the one quick-navigation type that is decided by reading the text
// rather than by matching a role, so the expression that decides it IS the
// command. The cases below are the ones a user meets on a news page: the
// article is prose, and the menu, the breadcrumb and the link row are not.
// ---------------------------------------------------------------------------

TEST_CASE("the text-paragraph expression tells prose from navigation",
          "[nav][paragraphs]") {
    alus::text::Regex regex;
    std::string error;
    REQUIRE(regex.compile(alus::nav::kTextParagraphPattern, alus::text::RegexOption::None,
                          &error));
    INFO("pattern error: " << error);

    SECTION("prose matches") {
        CHECK(regex.matches("This is a sentence of prose."));
        CHECK(regex.matches("Dve rečenice. I još jedna."));
        CHECK(regex.matches("Je li ovo pitanje?"));
        CHECK(regex.matches("Kakav dan!"));
        CHECK(regex.matches("Ellipsis is prose too…"));
        // A closing quote on either side of the full stop, which is what
        // upstream's template allows by repeating the piece.
        CHECK(regex.matches("Citat se zatvara ovako.”"));
        CHECK(regex.matches("Citat se zatvara ovako”."));
        // Wikipedia's footnote markers after the stop.
        CHECK(regex.matches("Fusnota je ovde.[1][2]"));
        // The CJK full stops, which are their own characters.
        CHECK(regex.matches("这是一个句子！"));
    }

    SECTION("navigation furniture does not match") {
        CHECK_FALSE(regex.matches("Naslov poglavlja"));
        CHECK_FALSE(regex.matches("Home"));
        CHECK_FALSE(regex.matches("Kontakt | O nama | Pomoć"));
        CHECK_FALSE(regex.matches(""));
        CHECK_FALSE(regex.matches("   "));
        // A numbered list item is NOT a sentence: that is what the
        // `(?<!\d)` lookbehind is for, and it is the difference between `p`
        // being useful on a documentation page and landing on every list item.
        CHECK_FALSE(regex.matches("3. novembar"));
        CHECK_FALSE(regex.matches("1."));
        // A file name is not a sentence either, because a full stop inside a
        // word has no space after it.
        CHECK_FALSE(regex.matches("alus.ini is the file"));
    }
}
