// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The translation layer, and the three ways localization rots silently.
//
// Every check here exists because its failure produces NO error anywhere: a
// string missing from POTFILES.in builds, links, passes msgfmt and is simply
// never offered to a translator; a `_` macro in a public header compiles until
// the day someone names a variable `_`; a description in scripts.csv with no
// N_() mark in data_strings.cpp is a command whose description is English
// forever. None of these would ever fail a build, so they fail a test.

#include "alus/i18n/data_strings.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// The source tree, found from the build directory the test runs in. The tests
// read DATA, so they need it; ALUS_SOURCE_ROOT is set by tests/meson.build.
std::filesystem::path sourceRoot() { return std::filesystem::path{ALUS_SOURCE_ROOT}; }

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream{path};
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

// --- helpers for the script check (T9.4) ---------------------------------
//
// The text is UTF-8 and the question is per LETTER, so these work on bytes
// and know only two things: an ASCII letter is Latin, and a two-byte sequence
// starting 0xD0/0xD1 is Cyrillic. Nothing else in a Serbian catalogue is a
// letter.
bool isLatinByte(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

// Splits on whitespace and on the quoting, and drops gettext placeholders
// (`%s`, `%1$s`, `{name}`) and the escapes, which are Latin by nature.
std::vector<std::string> wordsOf(const std::string& line) {
    const std::size_t quote = line.find('"');
    if (quote == std::string::npos || line.size() < quote + 2) {
        return {};
    }
    std::string value = line.substr(quote + 1, line.size() - quote - 2);
    // Blank out what is not language.
    for (std::size_t i = 0; i + 1 < value.size(); ++i) {
        if (value[i] == '\\') {
            value[i] = ' ';
            value[i + 1] = ' ';
            ++i;
        }
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%' && value[i] != '{') {
            continue;
        }
        const char close = value[i] == '{' ? '}' : '\0';
        std::size_t j = i;
        while (j < value.size() && value[j] != ' ') {
            const bool done = close == '}' ? value[j] == '}'
                                           : (j > i && isLatinByte(
                                                  static_cast<unsigned char>(value[j])));
            value[j] = ' ';
            ++j;
            if (done) {
                break;
            }
        }
        i = j - 1;
    }
    std::vector<std::string> words;
    std::string current;
    for (const char ch : value) {
        if (ch == ' ' || ch == '\t') {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

// The text inside the first pair of quotes on a po line.
std::string quoted(const std::string& line) {
    const std::size_t open = line.find('"');
    if (open == std::string::npos || line.size() < open + 2) {
        return {};
    }
    return line.substr(open + 1, line.size() - open - 2);
}

// Whether a translation has letters but not one Cyrillic letter: the shape of
// the defect -- a Serbian word that was translated and never transliterated.
// Placeholders and escapes are Latin by nature and do not count.
bool isWhollyLatin(const std::string& value) {
    bool letters = false;
    for (const std::string& word : wordsOf("x \"" + value + "\"")) {
        for (std::size_t i = 0; i < word.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(word[i]);
            if (ch == 0xD0 || ch == 0xD1) {
                return false;  // Cyrillic
            }
            if (isLatinByte(ch)) {
                letters = true;
            }
        }
    }
    return letters;
}

// Whether a Latin letter appears AFTER a Cyrillic one in the same word: the
// signature of a transliteration that ran over a name it did not know.
bool latinAfterCyrillic(const std::string& word) {
    bool seenCyrillic = false;
    for (std::size_t i = 0; i < word.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(word[i]);
        if (ch == 0xD0 || ch == 0xD1) {
            seenCyrillic = true;
            ++i;  // the second byte of the pair
            continue;
        }
        if (seenCyrillic && isLatinByte(ch)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream stream{text};
    std::string line;
    while (std::getline(stream, line)) {
        out.push_back(line);
    }
    return out;
}

}  // namespace

TEST_CASE("translate falls back to the msgid", "[i18n]") {
    // Before initialize(), and after it when there is no catalogue. Both must
    // give correct English rather than an empty string, because the speaking
    // path has no way to report "untranslated" to the user.
    CHECK(std::string{alus::i18n::translate("check box")} == "check box");

    // The EMPTY msgid addresses the catalogue's own metadata header in
    // gettext. If that ever reached the user, a blind user would hear
    // "Project-Id-Version: alus 0.1.0 Report-Msgid-Bugs-To..." in place of a
    // state label -- two states carry an empty label on purpose.
    CHECK(std::string{alus::i18n::translate("")}.empty());
    CHECK(std::string{alus::i18n::translate(nullptr)}.empty());
}

TEST_CASE("substitute keeps one and two placeholders", "[i18n]") {
    using alus::i18n::substitute;
    CHECK(substitute("row %s", "3") == "row 3");
    CHECK(substitute("%s of %s", "3", "7") == "3 of 7");
    CHECK(substitute("linija %s od %s", "3", "7") == "linija 3 od 7");

    // A translation that LOST its placeholder must not throw and must not
    // drop the value: wrong word order is a bug report, a crash in the screen
    // reader is a user who cannot use their computer.
    CHECK(substitute("row", "3") == "row 3");
    CHECK(substitute("rows", "3", "7") == "rows 3 7");

    // The VALUE may contain "%s": an object's name is user data. Substituting
    // into it would be a format-string bug with user input as the format.
    CHECK(substitute("%s of %s", "%s", "7") == "%s of 7");
    CHECK(substitute("name: %s", "100%s") == "name: 100%s");
}

TEST_CASE("every translatable source is in POTFILES.in", "[i18n]") {
    // The generator's own --check, run as a test. See
    // tools/update-potfiles.py for why a missing file is invisible otherwise.
    const std::filesystem::path potfiles = sourceRoot() / "po" / "POTFILES.in";
    REQUIRE(std::filesystem::exists(potfiles));

    std::set<std::string> listed;
    for (const std::string& line : lines(readFile(potfiles))) {
        if (!line.empty() && line[0] != '#') {
            listed.insert(line);
        }
    }
    REQUIRE_FALSE(listed.empty());

    // Every listed file must exist: a stale entry makes xgettext fail the
    // build, which is noisy and fine, but a test that says which one is
    // kinder.
    for (const std::string& name : listed) {
        CHECK(std::filesystem::exists(sourceRoot() / name));
    }

    // And every source that uses a macro must be listed. The scan is the same
    // one the generator does, minus comments and string literals.
    std::set<std::string> uses;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator{sourceRoot() / "src"}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        if (path.extension() != ".cpp" && path.extension() != ".hpp") {
            continue;
        }
        const std::string text = readFile(path);
        // A cheap, deliberately over-eager scan: `_("`, `N_("`, `P_("`. Over-
        // eager is the safe direction -- it can only ask for a file to be
        // LISTED, and listing a file with no strings costs nothing.
        bool found = false;
        for (const char* needle : {"_(\"", "N_(\"", "C_(\"", "P_(\""}) {
            if (text.find(needle) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }
        uses.insert(std::filesystem::relative(path, sourceRoot()).generic_string());
    }

    for (const std::string& name : uses) {
        INFO("not in po/POTFILES.in: " << name
                                       << " -- run python3 tools/update-potfiles.py");
        CHECK(listed.count(name) == 1);
    }
}

TEST_CASE("no installed header defines the gettext macros", "[i18n]") {
    // include/alus/i18n/i18n.hpp is the exception and is not installed; the
    // one header ALUS DOES install is alus-plugin.h, which §12.3 promises is
    // self-contained C. A `_` macro leaking into it would break any add-on
    // that uses `_` as an identifier -- and `_` is a common one.
    const std::filesystem::path plugin =
        sourceRoot() / "include" / "alus" / "alus-plugin.h";
    REQUIRE(std::filesystem::exists(plugin));
    const std::string text = readFile(plugin);
    CHECK(text.find("#define _(") == std::string::npos);
    CHECK(text.find("i18n/i18n.hpp") == std::string::npos);

    // And no public header may include the macro header, which would put the
    // macros into every translation unit that includes ALUS.
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator{sourceRoot() / "include"}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".hpp") {
            continue;
        }
        if (entry.path().filename() == "i18n.hpp") {
            continue;  // the macro header itself
        }
        INFO(entry.path().string() << " includes the macro header");
        CHECK(readFile(entry.path()).find("alus/i18n/i18n.hpp") == std::string::npos);
    }
}

TEST_CASE("the generated data strings match the generated data", "[i18n]") {
    // src/i18n/data_strings.cpp and data/scripts.csv are two generated copies
    // of one truth (see tools/extract-commands.py, write_data_strings). Two
    // copies is acceptable; two copies that drift is not, and drift here means
    // a command description that can never be translated.
    std::set<std::string> marked;
    for (const char* text : alus::i18n::scriptDescriptionStrings()) {
        marked.insert(text);
    }
    std::set<std::string> markedCategories;
    for (const char* text : alus::i18n::scriptCategoryStrings()) {
        markedCategories.insert(text);
    }
    REQUIRE(marked.size() > 300);
    REQUIRE(markedCategories.size() > 10);

    const std::filesystem::path csv = sourceRoot() / "data" / "scripts.csv";
    REQUIRE(std::filesystem::exists(csv));
    const std::vector<std::string> rows = lines(readFile(csv));
    REQUIRE(rows.size() > 300);

    // A minimal CSV reader: the columns are known and the only quoted field
    // that matters is `description`.
    const std::vector<std::string> header = [&] {
        std::vector<std::string> out;
        std::istringstream stream{rows.front()};
        std::string field;
        while (std::getline(stream, field, ',')) {
            out.push_back(field);
        }
        return out;
    }();
    const auto columnOf = [&](const std::string& name) {
        return static_cast<std::size_t>(
            std::find(header.begin(), header.end(), name) - header.begin());
    };
    const std::size_t descriptionColumn = columnOf("description");
    const std::size_t categoryColumn = columnOf("category");
    const std::size_t scopeColumn = columnOf("scope");
    REQUIRE(descriptionColumn < header.size());
    REQUIRE(scopeColumn < header.size());

    std::size_t checked = 0;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        // Fields, honouring quotes.
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        for (std::size_t at = 0; at < rows[i].size(); ++at) {
            const char ch = rows[i][at];
            if (quoted) {
                if (ch == '"' && at + 1 < rows[i].size() && rows[i][at + 1] == '"') {
                    field.push_back('"');
                    ++at;
                } else if (ch == '"') {
                    quoted = false;
                } else {
                    field.push_back(ch);
                }
            } else if (ch == '"') {
                quoted = true;
            } else if (ch == ',') {
                fields.push_back(field);
                field.clear();
            } else {
                field.push_back(ch);
            }
        }
        fields.push_back(field);
        if (fields.size() <= scopeColumn || fields[scopeColumn] != "v1") {
            continue;
        }
        const std::string& description = fields[descriptionColumn];
        if (!description.empty()) {
            INFO("no N_() mark for the description of row " << i << ": " << description);
            CHECK(marked.count(description) == 1);
            ++checked;
        }
        const std::string& category = fields[categoryColumn];
        if (!category.empty()) {
            INFO("no N_() mark for the category: " << category);
            CHECK(markedCategories.count(category) == 1);
        }
    }
    CHECK(checked > 300);
}

TEST_CASE("every key label has a msgid", "[i18n]") {
    // Same risk as the descriptions: data/key-labels.ini is generated data,
    // and a label with no mark is a key that is spoken in English forever.
    std::set<std::string> marked;
    for (const char* text : alus::i18n::keyLabelStrings()) {
        marked.insert(text);
    }
    REQUIRE(marked.size() > 50);

    const std::filesystem::path ini = sourceRoot() / "data" / "key-labels.ini";
    REQUIRE(std::filesystem::exists(ini));
    std::size_t checked = 0;
    for (std::string line : lines(readFile(ini))) {
        const auto equals = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' ||
            equals == std::string::npos) {
            continue;
        }
        std::string value = line.substr(equals + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) {
            value.pop_back();
        }
        if (value.empty()) {
            continue;
        }
        INFO("no N_() mark for the key label: " << value);
        CHECK(marked.count(value) == 1);
        ++checked;
    }
    CHECK(checked > 50);
}

TEST_CASE("T9.4 — the Serbian catalogue covers every message", "[i18n][T9.4]") {
    // The exit criterion is "LANG=sr_RS.UTF-8: every announcement in Serbian".
    // What a test can check without a sound card is that no msgid is left
    // without a msgstr: an untranslated entry is precisely an announcement
    // that comes out in English.
    //
    // po/sr.po is GENERATED (tools/import-nvda-po.py), so a gap here means
    // either a new string nobody translated or a msgid whose English changed.
    for (const char* language : {"sr", "sr@latin"}) {
        const std::filesystem::path po =
            sourceRoot() / "po" / (std::string{language} + ".po");
        INFO("catalogue: " << po.string());
        REQUIRE(std::filesystem::exists(po));

        const std::vector<std::string> text = lines(readFile(po));
        std::size_t total = 0;
        std::size_t untranslated = 0;
        std::string lastMsgid;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i].rfind("msgid \"", 0) != 0 || text[i] == "msgid \"\"") {
                continue;
            }
            lastMsgid = text[i];
            ++total;
            // The msgstr (or msgstr[0]) that follows. A plural entry has three
            // and all three must be filled.
            for (std::size_t j = i + 1; j < text.size() && !text[j].empty(); ++j) {
                if (text[j] == "msgstr \"\"" || text[j] == "msgstr[0] \"\"" ||
                    text[j] == "msgstr[1] \"\"" || text[j] == "msgstr[2] \"\"") {
                    INFO("untranslated: " << lastMsgid);
                    ++untranslated;
                    break;
                }
            }
        }
        CHECK(total > 700);
        CHECK(untranslated == 0);
    }
}

TEST_CASE("T9.4 — the Serbian catalogue is in the script it claims",
          "[i18n][T9.4]") {
    // "Every announcement in Serbian" also means IN THE RIGHT SCRIPT.
    // po/sr.po is Cyrillic and po/sr@latin.po is Latin, both produced from one
    // hand-written Latin source by tools/import-nvda-po.py.
    //
    // WHY THIS EXISTS. The coverage test above passed while 76 ordinary words
    // sat in LATIN inside the CYRILLIC catalogue -- prozor, dugme, meni,
    // lista, naslov, tabela, slika, link, the words a screen reader says most
    // often -- because they WERE translated, just not transliterated. Five
    // more came out in neither script: "wиндоwс", "Wаyланд", "рхвоице"
    // (docs/history/phase-9.md N106).
    //
    // Two checks, and neither restates the importer's rule: both read the
    // shipped text and one short declared list.
    const std::filesystem::path po = sourceRoot() / "po" / "sr.po";
    REQUIRE(std::filesystem::exists(po));
    const std::vector<std::string> text = lines(readFile(po));

    SECTION("only names and braille abbreviations are left in Latin") {
        // Keyed by MSGID, which is stable, and not by the translation, which
        // is what the test is checking. Two kinds:
        //
        //   * the braille display's abbreviations, where the unit is the CELL
        //     and a Cyrillic word would be no shorter and no clearer;
        //   * names spelled the same in every script.
        //
        // The list is the contract. A new wholly-Latin entry is either a name
        // -- say so here -- or a Serbian word that lost its transliteration,
        // which is the defect.
        static const std::set<std::string> latinOnPurpose = {
            // src/braille/labels.cpp and the abbreviations of properties.cpp
            "app", "art", "bkmk", "bnnr", "bqt", "bsyind", "btn", "c%s",
            "cap", "cbo", "chk", "cinf", "clk", "cmnt", "cmpl", "del", "dlg",
            "doc", "drbtn", "edt", "embedded", "enote", "fig", "fnote",
            "frml", "gra", "grp", "h%s", "hlght", "hlp", "ins", "invalid",
            "ldesc", "lmk", "lnk", "lst", "main", "mln", "mnu", "mnubar",
            "mnubtn", "mnuitem", "msel", "mslst", "navi", "nsel", "pnl",
            "prgbar", "pwdedt", "r%s", "rbtn", "req", "rgn", "ro", "scrlbar",
            "sect", "sel", "sggstn", "smp", "sorted asc", "sorted desc",
            "splbtn", "spnbtn", "srch", "stbar", "submnu", "swtch", "tabctl",
            "tbl", "term", "tgbtn", "tlbar", "tltip", "tv", "tvbtn",
            "tvitem", "vlnk", "wnd",
            // names, and one configuration value that is an identifier
            "ALUS", "brltty (BrlAPI)", "speech-dispatcher", "windows",
            "changedContext",
        };

        std::size_t checked = 0;
        std::string msgid;
        for (const std::string& line : text) {
            if (line.rfind("msgid \"", 0) == 0) {
                msgid = quoted(line);
                continue;
            }
            if (line.rfind("msgstr", 0) != 0 || msgid.empty()) {
                continue;
            }
            const std::string value = quoted(line);
            if (value.empty() || !isWhollyLatin(value)) {
                continue;
            }
            ++checked;
            INFO("po/sr.po is the CYRILLIC catalogue and msgid \""
                 << msgid << "\" is translated \"" << value
                 << "\", which has no Cyrillic in it at all. Either "
                    "tools/import-nvda-po.py stopped transliterating it, or it is a "
                    "name or a braille abbreviation and belongs in the list in this "
                    "test.");
            CHECK(latinOnPurpose.count(msgid) == 1);
        }
        // The braille abbreviations alone are more than fifty, so a run that
        // checked almost nothing is a broken test and not a clean catalogue.
        CHECK(checked > 50);
    }

    SECTION("no word is left in neither script") {
        // A Latin letter AFTER a Cyrillic one in the same word is not a word
        // in either language -- it is a transliteration that ran over a name
        // it did not recognise. `speech-dispatcher-а` and `BrlAPI-ја` are
        // fine and common: a Latin name with a Cyrillic ending, which never
        // goes back to Latin.
        std::size_t mixed = 0;
        std::string msgid;
        for (const std::string& line : text) {
            if (line.rfind("msgid \"", 0) == 0) {
                msgid = quoted(line);
                continue;
            }
            if (line.rfind("msgstr", 0) != 0) {
                continue;
            }
            for (const std::string& word : wordsOf(line)) {
                if (!latinAfterCyrillic(word)) {
                    continue;
                }
                INFO("neither Latin nor Cyrillic: \"" << word << "\" in msgid \""
                     << msgid
                     << "\" -- the transliteration ran over a name it does not know. "
                        "Add it to KEEP in tools/import-nvda-po.py, or give it one of "
                        "the letters the q/w/x/y rule there already protects.");
                CHECK(false);
                ++mixed;
            }
        }
        CHECK(mixed == 0);
    }
}

TEST_CASE("every translation keeps its placeholders", "[i18n][T9.4]") {
    // What `msgfmt --check-format` checks, in C++ over the same files.
    //
    // In C++ and not by calling msgfmt, because the valgrind run names its
    // suites explicitly and would then be running Memcheck over GNU gettext's
    // own binary -- whose leaks are not ALUS's. msgfmt is still run, in the
    // `external` suite.
    //
    // Why it matters: i18n::substitute appends a value whose placeholder went
    // missing rather than throwing, so a translation that dropped its %s reads
    // in the wrong order and nothing ever reports it. A translation that GAINED
    // one shows a literal "%s" to the user.
    for (const char* language : {"sr", "sr@latin"}) {
        const std::filesystem::path po =
            sourceRoot() / "po" / (std::string{language} + ".po");
        REQUIRE(std::filesystem::exists(po));
        const std::vector<std::string> text = lines(readFile(po));

        const auto placeholders = [](const std::string& line) {
            std::size_t count = 0;
            for (std::size_t at = line.find("%s"); at != std::string::npos;
                 at = line.find("%s", at + 2)) {
                ++count;
            }
            return count;
        };

        std::size_t checked = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i].rfind("msgid \"", 0) != 0 || text[i] == "msgid \"\"") {
                continue;
            }
            const std::size_t wanted = placeholders(text[i]);
            for (std::size_t j = i + 1; j < text.size() && !text[j].empty(); ++j) {
                if (text[j].rfind("msgstr", 0) != 0) {
                    continue;
                }
                INFO(po.filename().string() << ": " << text[i] << "  ->  " << text[j]);
                CHECK(placeholders(text[j]) == wanted);
                ++checked;
            }
        }
        CHECK(checked > 700);
    }
}

TEST_CASE("the Serbian catalogue never carries the other product's name",
          "[i18n][T9.4]") {
    // tools/import-nvda-po.py looks translations up by upstream's msgid and
    // rewrites the product name in the RESULT. If that rewrite ever stops
    // working, ALUS would speak a competitor's name to its own users, which is
    // both wrong and confusing.
    for (const char* language : {"sr", "sr@latin"}) {
        const std::filesystem::path po =
            sourceRoot() / "po" / (std::string{language} + ".po");
        for (const std::string& line : lines(readFile(po))) {
            if (line.rfind("msgstr", 0) != 0 && line.rfind("\"", 0) != 0) {
                continue;
            }
            INFO(po.filename().string() << ": " << line);
            CHECK(line.find("NVDA") == std::string::npos);
        }
    }
}
