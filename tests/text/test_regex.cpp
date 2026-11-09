// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The regular expression engine, and the one property the whole text layer
// rests on (01-ARHITEKTURA.md §11.1): A WORD CHARACTER CLASS THAT INCLUDES
// COMBINING MARKS. Python's standard `re` excludes them, which is why NVDA had
// to move its word-boundary dictionary entries to the `regex` module, and why
// ALUS writes the class out instead of trusting any engine's \w.
#include "alus/text/regex.hpp"

#include "../support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>

using alus::text::expandTemplate;
using alus::text::Match;
using alus::text::Regex;
using alus::text::RegexOption;

TEST_CASE("the word character class includes combining marks", "[text][regex]") {
    Regex regex;
    // "e" followed by U+0301 COMBINING ACUTE ACCENT: one grapheme, two code
    // points. Decomposed text like this is what a word boundary has to cope
    // with.
    REQUIRE(regex.compile(std::format("^{}+$", alus::text::kWordCharacterClass)));
    CHECK(regex.matches("café"));
    CHECK(regex.matches("Приступачност"));
    CHECK(regex.matches("Milošević"));
    CHECK_FALSE(regex.matches("two words"));
    CHECK_FALSE(regex.matches("end."));
}

TEST_CASE("named groups and the Python spelling of them", "[text][regex]") {
    Regex regex;
    // (?P<name>...) is the spelling NVDA's locale data uses. PCRE2 accepts it
    // verbatim, which is the reason this wrapper is GRegex and not ICU.
    REQUIRE(regex.compile("(?P<first>\\d+)x(?P<second>\\d+)"));
    REQUIRE(regex.groupNames().size() == 2);
    CHECK(regex.groupNames()[0] == "first");
    CHECK(regex.groupNumber("first") == 1);
    CHECK(regex.groupNumber("second") == 2);
    CHECK(regex.groupNumber("missing") == -1);

    bool matched = false;
    regex.search("size 3x4 here", [&](const Match& match) {
        matched = true;
        CHECK(match.group(0) == "3x4");
        CHECK(match.group("first") == "3");
        CHECK(match.group("second") == "4");
        CHECK(match.participated("first"));
        CHECK(match.start(0) == 5);
        CHECK(match.lastGroupName() == "second");
    });
    CHECK(matched);
}

TEST_CASE("a named backreference works", "[text][regex]") {
    Regex regex;
    // (?P=name) is the other Python spelling the symbol processor relies on,
    // for runs of the same character.
    REQUIRE(regex.compile("(?P<repeated>(?P<char>[.])(?P=char){3,})"));
    CHECK(regex.matches("....."));
    CHECK_FALSE(regex.matches("..."));
}

TEST_CASE("lookbehind is not mistaken for a named group", "[text][regex]") {
    Regex regex;
    REQUIRE(regex.compile("(?<=\\d)[.](?=\\d)"));
    CHECK(regex.groupNames().empty());
    CHECK(regex.matches("3.50"));
    CHECK_FALSE(regex.matches("end. next"));
}

TEST_CASE("an alternative that did not match did not participate", "[text][regex]") {
    Regex regex;
    REQUIRE(regex.compile("(?P<a>aa)|(?P<b>bb)"));
    regex.search("bb", [](const Match& match) {
        CHECK_FALSE(match.participated("a"));
        CHECK(match.participated("b"));
        CHECK(match.lastGroupName() == "b");
    });
}

TEST_CASE("replace with a callback", "[text][regex]") {
    Regex regex;
    REQUIRE(regex.compile("\\d+"));
    const std::string out = regex.replace("a1b22c333", [](const Match& match) {
        return std::format("<{}>", match.group(0).size());
    });
    CHECK(out == "a<1>b<2>c<3>");
}

TEST_CASE("a pattern that can match nothing does not loop for ever", "[text][regex]") {
    Regex regex;
    REQUIRE(regex.compile("x*"));
    // Every position matches the empty string; the replacer must still finish
    // and must not lose characters.
    const std::string out = regex.replace("abc", [](const Match&) { return std::string{"-"}; });
    CHECK(out == "-a-b-c-");
}

TEST_CASE("a replacement template expands group references", "[text][regex]") {
    const alus::testing::QuietLog quiet;  // the invalid escapes below are logged
    Regex regex;
    REQUIRE(regex.compile("(\\d+)-(\\d+)"));
    CHECK(regex.replaceWithTemplate("7-9", "\\2 to \\1") == "9 to 7");
    // A literal backslash is written doubled, which is what the dictionary
    // writes for non-regex entries.
    CHECK(regex.replaceWithTemplate("7-9", "a\\\\b") == "a\\b");

    bool checked = false;
    regex.search("7-9", [&](const Match& match) {
        checked = true;
        // An invalid escape is an error the caller reports, not a crash.
        CHECK_FALSE(expandTemplate(match, "\\q", 0).has_value());
        CHECK_FALSE(expandTemplate(match, "trailing\\", 0).has_value());
        // groupBase shifts the numbering, which is how a complex symbol's own
        // subgroups are addressed (characterProcessing's m.lastindex trick).
        CHECK(expandTemplate(match, "\\1", 0).value() == "7");
    });
    CHECK(checked);
}

TEST_CASE("case insensitivity is honoured for non-ASCII letters", "[text][regex]") {
    Regex regex;
    REQUIRE(regex.compile("милош", RegexOption::CaseInsensitive));
    CHECK(regex.matches("МИЛОШ"));
    CHECK(regex.matches("Милош"));
}

TEST_CASE("dot-all makes a pattern span lines", "[text][regex]") {
    Regex withDotAll;
    REQUIRE(withDotAll.compile("a.b", RegexOption::DotAll));
    CHECK(withDotAll.matches("a\nb"));

    Regex without;
    REQUIRE(without.compile("a.b"));
    CHECK_FALSE(without.matches("a\nb"));
}

TEST_CASE("escaping makes a literal out of anything", "[text][regex]") {
    Regex regex;
    REQUIRE(regex.compile(Regex::escape("a.b[c]")));
    CHECK(regex.matches("a.b[c]"));
    CHECK_FALSE(regex.matches("axbc"));
}

TEST_CASE("an invalid pattern is reported, not fatal", "[text][regex]") {
    Regex regex;
    std::string error;
    // A user's dictionary is allowed to contain this.
    CHECK_FALSE(regex.compile("(unclosed", RegexOption::None, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(regex.valid());
    // And an invalid regex changes nothing.
    CHECK(regex.replace("abc", [](const Match&) { return std::string{"x"}; }) == "abc");
}

TEST_CASE("UTF-8 offsets are byte offsets into the subject", "[text][regex]") {
    Regex regex;
    REQUIRE(regex.compile("свете"));
    regex.search("Здраво свете", [](const Match& match) {
        // "Здраво " is 13 bytes in UTF-8.
        CHECK(match.start(0) == 13);
        CHECK(match.group(0) == "свете");
    });
}
