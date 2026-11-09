// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T2.10 — a dictionary entry with a diacritic and a word boundary works.
//
// This is the trap from 01-ARHITEKTURA.md §11.1 made executable. With a `\w`
// that excludes combining marks, an entry for a word containing a diacritic
// either fails to match or matches inside a longer word. Serbian is ALUS's
// first locale, so this is not a corner case.
#include "alus/speech/dictionaries.hpp"

#include "../support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using alus::speech::DictEntry;
using alus::speech::Dictionaries;
using alus::speech::DictionaryType;
using alus::speech::EntryType;
using alus::speech::SpeechDict;
using alus::testing::QuietLog;

namespace {

std::string apply(const std::string& pattern, const std::string& replacement,
                  const std::string& text, EntryType type, bool caseSensitive = true) {
    DictEntry entry{pattern, replacement, {}, caseSensitive, type};
    std::string error;
    REQUIRE(entry.compile(&error));
    return entry.sub(text);
}

}  // namespace

TEST_CASE("T2.10 a whole-word entry with diacritics matches only whole words",
          "[speech][dictionaries]") {
    // Composed form: U+0107 LATIN SMALL LETTER C WITH ACUTE.
    CHECK(apply("Milošević", "Милошевић", "Darko Milošević piše", EntryType::Word) ==
          "Darko Милошевић piše");
    // The word must not match inside a longer word.
    CHECK(apply("šef", "chief", "šefica je tu", EntryType::Word) == "šefica je tu");
    CHECK(apply("šef", "chief", "šef je tu", EntryType::Word) == "chief je tu");

    // DECOMPOSED form: "c" + U+0301 COMBINING ACUTE ACCENT. This is the case
    // that breaks with a word character class that excludes marks: the mark
    // would count as a non-word character, so a boundary would be found in the
    // MIDDLE of the word and "Milos<ć>evic" would match inside "Miloševicu".
    const std::string decomposed = "Milosevic\u0301";
    CHECK(apply(decomposed, "X", decomposed + "u", EntryType::Word) == decomposed + "u");
    CHECK(apply(decomposed, "X", "Darko " + decomposed + " piše", EntryType::Word) ==
          "Darko X piše");

    // Cyrillic, which has no ASCII in it at all.
    CHECK(apply("приступачност", "a11y", "Тема је приступачност данас", EntryType::Word) ==
          "Тема је a11y данас");
    CHECK(apply("приступачност", "a11y", "приступачности", EntryType::Word) ==
          "приступачности");
}

TEST_CASE("every entry type matches where it should", "[speech][dictionaries]") {
    // Anywhere: inside a word too.
    CHECK(apply("cat", "dog", "concatenate", EntryType::Anywhere) == "condogenate");
    // And a pattern that looks like a regular expression is a literal.
    CHECK(apply("a.c", "X", "abc a.c", EntryType::Anywhere) == "abc X");

    // Regexp: group references are honoured.
    CHECK(apply("(\\d+)-(\\d+)", "\\1 to \\2", "pages 10-20", EntryType::Regexp) ==
          "pages 10 to 20");

    // Word: boundaries on both sides.
    CHECK(apply("read", "red", "read reader", EntryType::Word) == "red reader");

    // PartOfWord: preceded OR followed by a word character.
    CHECK(apply("ing", "ING", "singing ing", EntryType::PartOfWord) == "sINGING ing");

    // StartOfWord: a boundary before, a word character after.
    CHECK(apply("pre", "PRE", "prefix pre", EntryType::StartOfWord) == "PREfix pre");

    // EndOfWord: a word character before, a boundary after.
    CHECK(apply("ing", "ING", "singing ingot", EntryType::EndOfWord) == "singING ingot");

    // Unix wildcards. `*` is GREEDY and crosses spaces, exactly as NVDA's
    // fnmatch translation does — "f*o" against "foo and fao" is one match, not
    // two. Pinned here because it surprises people who expect shell globbing
    // of file names.
    CHECK(apply("f*o", "X", "foo and fao", EntryType::Unix) == "X");
    CHECK(apply("f?o", "X", "foo fo", EntryType::Unix) == "X fo");
    CHECK(apply("f[ao]o", "X", "foo fao fio", EntryType::Unix) == "X X fio");
}

TEST_CASE("case sensitivity is honoured", "[speech][dictionaries]") {
    CHECK(apply("nvda", "NVDA", "Nvda and nvda", EntryType::Word, true) == "Nvda and NVDA");
    CHECK(apply("nvda", "NVDA", "Nvda and nvda", EntryType::Word, false) == "NVDA and NVDA");
    // Serbian case folding, not just ASCII.
    CHECK(apply("милош", "Милош", "МИЛОШ", EntryType::Word, false) == "Милош");
}

TEST_CASE("a backslash in a non-regex replacement is literal", "[speech][dictionaries]") {
    // Only a regular expression entry may use group references; everywhere
    // else a backslash is a backslash.
    CHECK(apply("path", "C:\\x", "the path", EntryType::Anywhere) == "the C:\\x");
}

TEST_CASE("the dic format is parsed and written back", "[speech][dictionaries]") {
    SpeechDict dictionary;
    dictionary.loadFromString(
        "#a comment about the next entry\n"
        "nvda\tNVDA\t1\t2\n"
        "\n"
        "\\#hash\tnumber sign\t0\t0\n"
        "(\\d+)\tnumber \\1\t1\t1\n");
    REQUIRE(dictionary.size() == 3);
    CHECK(dictionary.entries()[0].pattern() == "nvda");
    CHECK(dictionary.entries()[0].comment() == "a comment about the next entry");
    CHECK(dictionary.entries()[0].type() == EntryType::Word);
    CHECK(dictionary.entries()[0].caseSensitive());
    // \# in the file is a literal #; the escape exists because # starts a
    // comment.
    CHECK(dictionary.entries()[1].pattern() == "#hash");
    CHECK_FALSE(dictionary.entries()[1].caseSensitive());
    CHECK(dictionary.entries()[2].type() == EntryType::Regexp);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "alus-test-speechdict.dic";
    std::filesystem::remove(path);
    std::string error;
    REQUIRE(dictionary.save(path, &error));
    SpeechDict reloaded;
    REQUIRE(reloaded.load(path, &error));
    REQUIRE(reloaded.size() == 3);
    CHECK(reloaded.entries()[1].pattern() == "#hash");
    CHECK(reloaded.entries()[0].comment() == "a comment about the next entry");
    CHECK(reloaded.sub("nvda rules") == "NVDA rules");
    std::filesystem::remove(path);
}

TEST_CASE("an unparsable line is skipped, not fatal", "[speech][dictionaries]") {
    QuietLog quiet;
    SpeechDict dictionary;
    dictionary.loadFromString(
        "good\tfine\t1\t0\n"
        "this line has too few fields\n"
        "bad\tx\tnotanumber\t0\n"
        "alsogood\talso fine\t1\t0\n");
    // The user's file is allowed to be wrong; the entries that work still do.
    REQUIRE(dictionary.size() == 2);
    CHECK(dictionary.sub("good") == "fine");
}

TEST_CASE("an invalid regular expression is rejected at load", "[speech][dictionaries]") {
    QuietLog quiet;
    SpeechDict dictionary;
    dictionary.loadFromString("(unclosed\tx\t1\t1\nvalid\ty\t1\t0\n");
    REQUIRE(dictionary.size() == 1);
    CHECK(dictionary.entries()[0].pattern() == "valid");
}

TEST_CASE("entries are applied in file order", "[speech][dictionaries]") {
    SpeechDict dictionary;
    dictionary.loadFromString(
        "one\ttwo\t1\t0\n"
        "two\tthree\t1\t0\n");
    // The second entry sees the output of the first, as in NVDA.
    CHECK(dictionary.sub("one") == "three");
}

TEST_CASE("the four dictionaries apply in the documented order",
          "[speech][dictionaries][registry]") {
    Dictionaries dictionaries;
    dictionaries.dictionary(DictionaryType::Builtin)
        .add(DictEntry{"a11y", "accessibility", {}, true, EntryType::Word});
    dictionaries.dictionary(DictionaryType::Default)
        .add(DictEntry{"accessibility", "приступачност", {}, true, EntryType::Word});
    dictionaries.dictionary(DictionaryType::Voice)
        .add(DictEntry{"приступачност", "pristupačnost", {}, true, EntryType::Word});
    dictionaries.dictionary(DictionaryType::Temp)
        .add(DictEntry{"pristupačnost", "DONE", {}, true, EntryType::Word});

    // builtin → default → voice → temp, each seeing the previous result. The
    // order is user visible, so it is pinned by a test.
    CHECK(dictionaries.processText("a11y") == "DONE");

    dictionaries.setEnabled(DictionaryType::Voice, false);
    CHECK(dictionaries.processText("a11y") == "приступачност");

    dictionaries.setProcessingEnabled(false);
    CHECK(dictionaries.processText("a11y") == "a11y");
}

TEST_CASE("a voice dictionary file name is made safe", "[speech][dictionaries][registry]") {
    const std::filesystem::path userDir =
        std::filesystem::temp_directory_path() / "alus-test-dicts";
    std::filesystem::remove_all(userDir);
    std::filesystem::create_directories(userDir / "dictionaries");
    Dictionaries dictionaries{{}, userDir};
    // A voice name may contain a slash; the file name must not.
    dictionaries.loadVoiceDict("ssip", "espeak-ng/sr");
    const std::filesystem::path expected =
        userDir / "dictionaries" / "ssip-espeak-ng_sr.dic";
    CHECK(dictionaries.dictionary(DictionaryType::Voice).fileName() == expected);
    // And saving it creates the directory rather than failing.
    dictionaries.dictionary(DictionaryType::Voice)
        .add(DictEntry{"x", "y", {}, true, EntryType::Word});
    std::string error;
    CHECK(dictionaries.dictionary(DictionaryType::Voice).save({}, &error));
    CHECK(std::filesystem::exists(expected));
    std::filesystem::remove_all(userDir);
}
