// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T7.1 — liblouis: sr-Cyrl.ctb translates Cyrillic correctly
// T7.2 — the typeform list is NOT modified in place (a copy is passed)
// T7.3 — dotsIO: the cells arrive with bit 0x8000 set, masked to 0xFF
// T7.4 — the custom table resolver finds a table from an add-on directory
//
// Four of the eight exit criteria of phase-plan.md's phase 7, and all four are
// about liblouis rather than about ALUS. That is deliberate: §14 lists exactly
// three details of `louisHelper.py` "that are easy to miss", and three of
// these tests are those three details. The fourth, T7.1, is the one that says
// the whole path works for the language this project is written for.
#include "alus/braille/louis.hpp"

// T7.3 compares ALUS's answer against the library's own raw output, so the
// test calls liblouis directly. That is the only honest way to assert "the bit
// really arrives": a test that only checked `cell <= 0xFF` would pass against
// an implementation that had never heard of dotsIO.
#include <liblouis/liblouis.h>
#undef italic
#undef underline
#undef bold

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>

using alus::braille::Louis;
using alus::braille::Table;
using alus::braille::TableRegistry;
using alus::braille::Translation;
using alus::braille::Typeform;
using alus::braille::formatCellsForLog;
using alus::braille::unicodeBrailleFor;

namespace {

Louis& louis() {
    Louis& shared = Louis::shared();
    shared.initialize();
    return shared;
}

std::vector<std::string> serbian() { return {"sr-Cyrl.ctb", "braille-patterns.cti"}; }

std::string unicodeOf(const std::vector<std::uint8_t>& cells) {
    std::string out;
    for (const std::uint8_t cell : cells) {
        out += unicodeBrailleFor(cell);
    }
    return out;
}

}  // namespace

TEST_CASE("T7.1 sr-Cyrl.ctb translates Cyrillic exactly", "[braille][louis][T7.1]") {
    Louis& shared = louis();
    REQUIRE_FALSE(shared.registry().resolve("sr-Cyrl.ctb").empty());

    SECTION("the letters of the Serbian alphabet round trip") {
        // Every Cyrillic letter Serbian has, including the six that only
        // Serbian has (ђ, ј, љ, њ, ћ, џ). If any of them were dropped or
        // mapped to the same cell as another, the round trip would not match.
        const std::string alphabet = "абвгдђежзијклљмнњопрстћуфхцчџш";
        const Translation translation = shared.translate(serbian(), alphabet);
        REQUIRE(translation.ok);
        CHECK(translation.cells.size() == 30);
        CHECK(shared.backTranslate(serbian(), translation.cells) == alphabet);
    }

    SECTION("a word is the cells a Serbian braille reader expects") {
        const Translation translation = shared.translate(serbian(), "Ћирилица");
        REQUIRE(translation.ok);
        // The capital sign (dots 4-6) and then ћ-и-р-и-л-и-ц-а. The expected
        // value is written as Unicode braille rather than as dot numbers,
        // because that is the form a braille reader can check by eye.
        CHECK(unicodeOf(translation.cells) == "⠨⠩⠊⠗⠊⠇⠊⠉⠁");
        CHECK(shared.backTranslate(serbian(), translation.cells) == "Ћирилица");
    }

    SECTION("a whole sentence round trips, spaces included") {
        const std::string sentence = "Приступ Линуксу говором";
        const Translation translation = shared.translate(serbian(), sentence);
        REQUIRE(translation.ok);
        CHECK(shared.backTranslate(serbian(), translation.cells) == sentence);
        // Two spaces in the text are two blank cells in the output, and the
        // buffer's word wrap depends on exactly that.
        std::size_t blanks = 0;
        for (const std::uint8_t cell : translation.cells) {
            if (cell == 0) {
                ++blanks;
            }
        }
        CHECK(blanks == 2);
    }

    SECTION("the table declares its language, so `auto` can find it") {
        CHECK(shared.tableLanguage("sr-Cyrl.ctb") == "sr-Cyrl");
        TableRegistry registry;
        registry.addKnownTables();
        CHECK(registry.defaultTableForLanguage("sr-Cyrl", false) == "sr-Cyrl.ctb");
        CHECK(registry.defaultTableForLanguage("sr", false) == "sr-Cyrl.ctb");
        // An unknown language falls back rather than failing: a screen reader
        // that refuses to show braille for Welsh is worse than one that shows
        // English braille and says so.
        CHECK(registry.defaultTableForLanguage("cy", false) == "en-ueb-g1.ctb");
    }

    SECTION("the position maps are the right length and monotonic") {
        const Translation translation = shared.translate(serbian(), "Ћирилица");
        REQUIRE(translation.ok);
        // One entry per codepoint of the input, one per cell of the output.
        CHECK(translation.rawToBraillePos.size() == 8);
        CHECK(translation.brailleToRawPos.size() == translation.cells.size());
        for (std::size_t index = 1; index < translation.rawToBraillePos.size(); ++index) {
            CHECK(translation.rawToBraillePos[index] >=
                  translation.rawToBraillePos[index - 1]);
        }
    }
}

TEST_CASE("T7.2 the typeform vector is not modified in place",
          "[braille][louis][T7.2]") {
    Louis& shared = louis();
    // §14: "liblouis MODIFIES the typeform list in place -> pass a copy". The
    // signature is `formtype*`, not `const formtype*`, and the library really
    // does write back into it -- NVDA passes a tuple so that Python copies.
    // This is the test that ALUS copies too.
    const std::string text = "hello world";
    std::vector<Typeform> typeform(text.size(), Typeform::PlainText);
    typeform[0] = Typeform::Bold;
    typeform[6] = Typeform::Italic;
    const std::vector<Typeform> before = typeform;

    const Translation translation =
        shared.translate({"en-ueb-g2.ctb", "braille-patterns.cti"}, text, typeform);
    REQUIRE(translation.ok);
    CHECK(typeform == before);

    SECTION("and a shorter typeform vector is padded rather than read past") {
        // The caller is allowed to be sloppy: liblouis reads `inlen` entries
        // whatever was handed over, so a short vector must be padded on ALUS's
        // side and not on liblouis's stack.
        std::vector<Typeform> shortForm{Typeform::Bold};
        const std::vector<Typeform> shortBefore = shortForm;
        const Translation second =
            shared.translate({"en-ueb-g2.ctb", "braille-patterns.cti"}, text, shortForm);
        CHECK(second.ok);
        CHECK(shortForm == shortBefore);
    }

    SECTION("a text that produces MORE cells than characters still fits the buffer") {
        // liblouis writes the OUTPUT typeform back into the array it was
        // given, one entry per CELL. Capitals in a grade 1 table each add a
        // capital sign, so the output is longer than the input -- and a
        // typeform buffer sized at the INPUT length is then written past its
        // end. Valgrind and ASAN see it; a plain run does not, which is why
        // this section exists at all rather than being left to chance.
        const std::string capitals = "ABC DEF";
        std::vector<Typeform> forms(capitals.size(), Typeform::Bold);
        const std::vector<Typeform> formsBefore = forms;
        const Translation wide =
            shared.translate({"en-ueb-g1.ctb", "braille-patterns.cti"}, capitals, forms);
        REQUIRE(wide.ok);
        CHECK(forms == formsBefore);
        CHECK(wide.cells.size() > capitals.size());
        // Every character still maps to a cell that exists.
        REQUIRE(wide.rawToBraillePos.size() == capitals.size());
        for (const int cell : wide.rawToBraillePos) {
            CHECK(cell >= 0);
            CHECK(cell <= static_cast<int>(wide.cells.size()));
        }
    }

    SECTION("bold really does change the cells, so the test is measuring something") {
        const Translation plain =
            shared.translate({"en-ueb-g2.ctb", "braille-patterns.cti"}, text);
        REQUIRE(plain.ok);
        CHECK(plain.cells != translation.cells);
    }
}

TEST_CASE("T7.3 dotsIO delivers cells with bit 0x8000, and ALUS masks them",
          "[braille][louis][T7.3]") {
    Louis& shared = louis();
    const std::string text = "braille";
    const std::string tableList = "en-us-comp8.ctb,braille-patterns.cti";

    // liblouis, called directly, in exactly the mode ALUS uses.
    std::vector<widechar> input;
    for (const char character : text) {
        input.push_back(static_cast<widechar>(static_cast<unsigned char>(character)));
    }
    int inLength = static_cast<int>(input.size());
    int outLength = 256;
    std::vector<widechar> output(static_cast<std::size_t>(outLength));
    REQUIRE(lou_translate(tableList.c_str(), input.data(), &inLength, output.data(),
                          &outLength, nullptr, nullptr, nullptr, nullptr, nullptr,
                          dotsIO) == 1);
    REQUIRE(outLength > 0);

    // THE BIT IS REALLY THERE. Every output character carries 0x8000, which is
    // what makes the mask necessary rather than decorative.
    for (int index = 0; index < outLength; ++index) {
        CHECK((output[static_cast<std::size_t>(index)] & 0x8000U) == 0x8000U);
    }

    // And ALUS's answer is exactly the low byte of each of them.
    const Translation translation =
        shared.translate({"en-us-comp8.ctb", "braille-patterns.cti"}, text);
    REQUIRE(translation.ok);
    REQUIRE(translation.cells.size() == static_cast<std::size_t>(outLength));
    for (int index = 0; index < outLength; ++index) {
        const std::size_t at = static_cast<std::size_t>(index);
        CHECK(translation.cells[at] == static_cast<std::uint8_t>(output[at] & 0xFF));
        // Nothing above dot 8 ever reaches a display. `cells` is a byte, so
        // the assertion is that the high half of the widechar was DROPPED and
        // not folded in.
        CHECK(static_cast<unsigned>(translation.cells[at]) !=
              static_cast<unsigned>(output[at]));
    }

    SECTION("the back translation sets the bit on the way in") {
        // The other direction of the same rule: cells handed BACK to liblouis
        // must carry 0x8000 or they are read as characters, not as dots.
        CHECK(shared.backTranslate({"en-us-comp8.ctb", "braille-patterns.cti"},
                                   translation.cells) == text);
    }
}

TEST_CASE("T7.4 the table resolver finds a table from an add-on directory",
          "[braille][louis][T7.4]") {
    Louis& shared = louis();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "alus-braille-table-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    // A minimal table of its own: four letters, one per cell, so a successful
    // translation can only have come from THIS file.
    const std::filesystem::path path = directory / "alus-addon.utb";
    {
        std::ofstream table{path};
        table << "space \\s 0\n";
        table << "punctuation . 256\n";
        table << "letter a 1\n";
        table << "letter b 12\n";
        table << "letter c 14\n";
        table << "letter d 145\n";
    }
    REQUIRE(std::filesystem::is_regular_file(path));

    SECTION("an unregistered name is not found in the system directory") {
        CHECK(shared.registry().resolve("alus-addon.utb").empty());
    }

    SECTION("registering the source makes the resolver find it") {
        shared.registry().addSource("alus-test-addon", directory);
        shared.registry().addTable(Table{"alus-addon.utb", "ALUS add-on test table",
                                         "alus-test-addon", false, true, true});
        CHECK(shared.registry().resolve("alus-addon.utb") == path);

        // And liblouis itself can then translate with it -- which is the real
        // assertion: the resolver is called BY liblouis, through the callback
        // registered in Louis::initialize(), not by ALUS.
        const Translation translation = shared.translate({"alus-addon.utb"}, "abcd");
        REQUIRE(translation.ok);
        REQUIRE(translation.cells.size() == 4);
        CHECK(formatCellsForLog(translation.cells) == "1 12 14 145");
    }

    SECTION("the add-on directory wins over the system one for the same name") {
        // How an add-on OVERRIDES a shipped table: same file name, registered
        // against a source, and the resolver puts that source first.
        const std::filesystem::path shadow = directory / "en-us-comp8.ctb";
        {
            std::ofstream table{shadow};
            table << "space \\s 0\n";
            table << "letter a 123456\n";
        }
        shared.registry().addSource("alus-test-addon", directory);
        shared.registry().addTable(Table{"en-us-comp8.ctb", "shadowed", "alus-test-addon",
                                         false, true, true});
        CHECK(shared.registry().resolve("en-us-comp8.ctb") == shadow);
        // Put the registry back, or the tests after this one translate with a
        // two-letter table.
        shared.registry().addTable(Table{"en-us-comp8.ctb",
                                         "English (US) 8 dot computer braille", {}, false,
                                         true, true});
        shared.terminate();
        shared.initialize();
    }

    SECTION("every table ALUS names by itself resolves to a file that exists") {
        // `addKnownTables()` is a hand-written list of twelve file names and
        // a name in it that liblouis does not ship is a SETTING THAT CANNOT
        // WORK: the resolver finds nothing, liblouis refuses the list, and
        // the display falls back to raw codepoints (§3.8). The list is data,
        // so a typo in it is invisible until someone picks that language --
        // this is the test that reads it. `hr-comp8.ctb` was such a typo:
        // liblouis ships `hr-comp8.utb` (phase-7.md N63).
        TableRegistry clean;
        clean.addKnownTables();
        REQUIRE_FALSE(clean.tables().empty());
        for (const Table& table : clean.tables()) {
            INFO("table: " << table.fileName);
            CHECK_FALSE(clean.resolve(table.fileName).empty());
        }
    }

    SECTION("$LOUIS_TABLEPATH is ignored, as NVDA's resolver ignores it") {
        // The whole reason for a custom resolver: a stray environment
        // variable must not be able to change what the display shows.
        //
        // The registry is a fresh one rather than the shared singleton,
        // because a section above registers this directory as a source --
        // and a test that cannot tell "found through the environment" from
        // "found because it was registered" measures nothing.
        CHECK(::setenv("LOUIS_TABLEPATH", directory.c_str(), 1) == 0);
        TableRegistry clean;
        clean.addKnownTables();
        CHECK(clean.resolve("alus-addon.utb").empty());
        CHECK(::unsetenv("LOUIS_TABLEPATH") == 0);
    }

    std::filesystem::remove_all(directory);
}

TEST_CASE("liblouis failure is a value, not a crash", "[braille][louis]") {
    Louis& shared = louis();
    SECTION("a table that does not exist answers not-ok") {
        const Translation translation = shared.translate({"no-such-table.ctb"}, "text");
        CHECK_FALSE(translation.ok);
        CHECK(translation.cells.empty());
    }
    SECTION("an empty table list answers not-ok") {
        CHECK_FALSE(shared.translate({}, "text").ok);
    }
    SECTION("empty text is not a failure") {
        const Translation translation = shared.translate(serbian(), "");
        CHECK(translation.ok);
        CHECK(translation.cells.empty());
    }
    SECTION("a NUL in the text is stripped rather than truncating the line") {
        const Translation translation =
            shared.translate({"en-us-comp8.ctb", "braille-patterns.cti"},
                             std::string{"ab\0cd", 5});
        REQUIRE(translation.ok);
        CHECK(translation.cells.size() == 4);
    }
}

TEST_CASE("the dot formatting helpers say what a reader expects", "[braille][louis]") {
    CHECK(alus::braille::formatDotNumbers(0) == "");
    CHECK(alus::braille::formatDotNumbers(0x01) == "1");
    CHECK(alus::braille::formatDotNumbers(0x13) == "1 2 5");
    CHECK(alus::braille::formatDotNumbers(0xC0) == "7 8");
    CHECK(formatCellsForLog({0x01, 0x00, 0x13}) == "1 - 125");
    CHECK(unicodeBrailleFor(0) == "⠀");
    CHECK(unicodeBrailleFor(0x01) == "⠁");
    CHECK(unicodeBrailleFor(0xFF) == "⣿");
}
