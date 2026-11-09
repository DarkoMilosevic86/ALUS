// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T2.9 — the symbol levels (nvda+p) change how punctuation is spoken.
//
// Also the `.dic` format itself, because that format IS NVDA's locale data:
// ALUS has to read what the locale files already contain, escapes, complex
// symbols, display names and all (01-ARHITEKTURA.md §11.1).
#include "alus/text/symbols.hpp"

#include "../support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using alus::testing::QuietLog;
using alus::text::SpeechSymbol;
using alus::text::SymbolDictionary;
using alus::text::SymbolLevel;
using alus::text::SymbolPreserve;
using alus::text::SymbolProcessor;
using alus::text::SymbolRegistry;

namespace {

// A built-in dictionary with everything the format can express.
constexpr std::string_view kBuiltin =
    "complexSymbols:\n"
    "sentence end\t(?<=[^\\s.])\\.(?=\\s|$)\n"
    "decimal point\t(?<=\\d)\\.(?=\\d)\n"
    "range\t(?<=\\d)-(?=\\d)\n"
    "\n"
    "symbols:\n"
    "sentence end\tdot\tall\talways\n"
    "decimal point\t\tnone\talways\n"
    "range\tto\tsome\tnever\n"
    ".\tdot\tall\n"
    ",\tcomma\tall\talways\n"
    "!\tbang\tall\n"
    "(\tleft paren\tmost\n"
    ")\tright paren\tmost\n"
    "%\tpercent\tsome\n"
    "$\tdollar\tall\tnorep\n"
    "-\tdash\tmost\n"
    "...\tdot dot dot\tall\talways\t# ellipsis typed as three dots\n"
    "\\t\ttab\tchar\n"
    "\\#\tnumber\tsome\n";

SymbolProcessor makeProcessor(const SymbolDictionary& user, const SymbolDictionary& builtin) {
    SymbolProcessor processor;
    REQUIRE(processor.build({&user, &builtin}, 1));
    return processor;
}

}  // namespace

TEST_CASE("the dic format is parsed as NVDA writes it", "[text][symbols]") {
    SymbolDictionary dictionary;
    dictionary.loadFromString(kBuiltin);

    REQUIRE(dictionary.complexSymbols().size() == 3);
    CHECK(dictionary.complexSymbols()[0].first == "sentence end");
    CHECK(dictionary.complexSymbols()[0].second == "(?<=[^\\s.])\\.(?=\\s|$)");

    const SpeechSymbol* comma = dictionary.find(",");
    REQUIRE(comma != nullptr);
    CHECK(comma->replacement.value() == "comma");
    CHECK(comma->level.value() == SymbolLevel::All);
    CHECK(comma->preserve.value() == SymbolPreserve::Always);

    // A trailing comment is the display name, whatever the field count.
    const SpeechSymbol* ellipsis = dictionary.find("...");
    REQUIRE(ellipsis != nullptr);
    CHECK(ellipsis->displayName.value() == "ellipsis typed as three dots");

    // "-" means "not specified here, inherit it".
    const SpeechSymbol* decimal = dictionary.find("decimal point");
    REQUIRE(decimal != nullptr);
    CHECK(decimal->replacement.value().empty());
    CHECK(decimal->level.value() == SymbolLevel::None);

    // Identifier escapes.
    REQUIRE(dictionary.find("\t") != nullptr);
    CHECK(dictionary.find("\t")->replacement.value() == "tab");
    REQUIRE(dictionary.find("#") != nullptr);
    CHECK(dictionary.find("#")->replacement.value() == "number");

    // Fields that were not given stay unset, so a lower priority source can
    // supply them.
    const SpeechSymbol* dot = dictionary.find(".");
    REQUIRE(dot != nullptr);
    CHECK_FALSE(dot->preserve.has_value());
}

TEST_CASE("T2.9 the symbol level decides what punctuation is spoken", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString(kBuiltin);
    const SymbolDictionary user;
    const SymbolProcessor processor = makeProcessor(user, builtin);

    const std::string text = "Total (50%) is $7, right!";

    // none: nothing is named, but symbols marked `always` are still passed on
    // so the synthesizer keeps its phrasing.
    const std::string none = processor.processText(text, SymbolLevel::None);
    CHECK(none.find("percent") == std::string::npos);
    CHECK(none.find("comma") == std::string::npos);
    CHECK(none.find(",") != std::string::npos);

    // some: the low-level symbols appear.
    const std::string some = processor.processText(text, SymbolLevel::Some);
    CHECK(some.find("percent") != std::string::npos);
    CHECK(some.find("left paren") == std::string::npos);

    // most: brackets too.
    const std::string most = processor.processText(text, SymbolLevel::Most);
    CHECK(most.find("percent") != std::string::npos);
    CHECK(most.find("left paren") != std::string::npos);
    CHECK(most.find("comma") == std::string::npos);

    // all: everything, including the comma and the full stop.
    const std::string all = processor.processText(text, SymbolLevel::All);
    CHECK(all.find("percent") != std::string::npos);
    CHECK(all.find("left paren") != std::string::npos);
    CHECK(all.find("comma") != std::string::npos);
    CHECK(all.find("bang") != std::string::npos);
    CHECK(all.find("dollar") != std::string::npos);

    // nvda+p cycles through exactly these four.
    CHECK(alus::text::nextSymbolLevel(SymbolLevel::None) == SymbolLevel::Some);
    CHECK(alus::text::nextSymbolLevel(SymbolLevel::Some) == SymbolLevel::Most);
    CHECK(alus::text::nextSymbolLevel(SymbolLevel::Most) == SymbolLevel::All);
    CHECK(alus::text::nextSymbolLevel(SymbolLevel::All) == SymbolLevel::None);
}

TEST_CASE("preserve decides whether the symbol itself is passed on", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString(kBuiltin);
    const SymbolDictionary user;
    const SymbolProcessor processor = makeProcessor(user, builtin);

    // always: the character survives at every level, because the synthesizer
    // needs it for prosody.
    CHECK(processor.processText("a, b", SymbolLevel::None).find(',') != std::string::npos);
    CHECK(processor.processText("a, b", SymbolLevel::All).find(',') != std::string::npos);

    // never (the default): the character is replaced by its name, or by a
    // space when the level is too low.
    const std::string most = processor.processText("a ( b", SymbolLevel::Most);
    CHECK(most.find("left paren") != std::string::npos);
    CHECK(most.find('(') == std::string::npos);

    // norep: kept only while its name is NOT being spoken.
    CHECK(processor.processText("$5", SymbolLevel::Some).find('$') != std::string::npos);
    CHECK(processor.processText("$5", SymbolLevel::All).find('$') == std::string::npos);
}

TEST_CASE("a complex symbol beats the plain character", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString(kBuiltin);
    const SymbolDictionary user;
    const SymbolProcessor processor = makeProcessor(user, builtin);

    // A full stop inside a number is not punctuation: it is silent at every
    // level, and the digits stay together.
    const std::string price = processor.processText("3.50", SymbolLevel::All);
    CHECK(price.find("dot") == std::string::npos);
    CHECK(price.find("3.50") != std::string::npos);

    // The same character at the end of a sentence IS punctuation.
    CHECK(processor.processText("Done. Next", SymbolLevel::All).find("dot") !=
          std::string::npos);

    // A hyphen between digits is a range, not a dash.
    const std::string range = processor.processText("5-7", SymbolLevel::Some);
    CHECK(range.find("to") != std::string::npos);
    CHECK(range.find("dash") == std::string::npos);
}

TEST_CASE("a run of the same character is counted, not repeated", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString(kBuiltin);
    const SymbolDictionary user;
    const SymbolProcessor processor = makeProcessor(user, builtin);

    // Four or more: "5 dot" rather than five dots, which is the difference
    // between a usable table of contents and an unusable one.
    const std::string many = processor.processText("a.....b", SymbolLevel::All);
    CHECK(many.find("5 dot") != std::string::npos);
    // Three of them is the multi-character symbol instead.
    CHECK(processor.processText("a...b", SymbolLevel::All).find("dot dot dot") !=
          std::string::npos);
}

TEST_CASE("the longest simple symbol wins", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString(kBuiltin);
    const SymbolDictionary user;
    const SymbolProcessor processor = makeProcessor(user, builtin);
    const std::string out = processor.processText("wait...", SymbolLevel::All);
    CHECK(out.find("dot dot dot") != std::string::npos);
}

TEST_CASE("the user's dictionary overrides the built-in one", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString(kBuiltin);
    SymbolDictionary user;
    // The user renames one symbol and lowers its level.
    user.loadFromString("symbols:\n%\tpostotak\tnone\n");

    const SymbolProcessor processor = makeProcessor(user, builtin);
    const std::string out = processor.processText("50%", SymbolLevel::None);
    CHECK(out.find("postotak") != std::string::npos);
    CHECK(out.find("percent") == std::string::npos);

    CHECK(processor.isBuiltin("("));
    // A symbol the user introduced is not built in, and the GUI must be able
    // to tell the difference (it may only delete the user's own).
    SymbolDictionary userOnly;
    userOnly.loadFromString("symbols:\n\u00a7\tsection\tsome\n");
    SymbolProcessor second;
    REQUIRE(second.build({&userOnly, &builtin}, 1));
    CHECK_FALSE(second.isBuiltin("\u00a7"));
    CHECK(second.isBuiltin("%"));
}

TEST_CASE("only a built-in dictionary may define complex symbols", "[text][symbols]") {
    QuietLog quiet;
    SymbolDictionary user;
    // This is the security property from §11.1: a user dictionary is edited
    // through a dialog and synchronised between machines, so a regular
    // expression there would be both a hang risk and a way to rewrite
    // arbitrary speech.
    user.loadFromString(
        "complexSymbols:\n"
        "evil\t(.*)\n"
        "\n"
        "symbols:\n"
        "evil\tpwned\tall\n",
        false);
    CHECK(user.complexSymbols().empty());
    // The simple section is still honoured.
    CHECK(user.find("evil") != nullptr);
}

TEST_CASE("a symbol with no replacement anywhere is dropped", "[text][symbols]") {
    QuietLog quiet;
    SymbolDictionary builtin;
    builtin.loadFromString("symbols:\n@\t-\tsome\n");
    const SymbolDictionary user;
    const SymbolProcessor processor = makeProcessor(user, builtin);
    // Dropped rather than spoken as nothing, and the text is left alone.
    CHECK(processor.computed("@") == nullptr);
    CHECK(processor.processText("a@b", SymbolLevel::All) == "a@b");
}

TEST_CASE("fields are inherited from lower priority sources", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString("symbols:\n&\tand\tsome\talways\t# ampersand\n");
    SymbolDictionary user;
    // Only the replacement is given; level, preserve and display name come
    // from the built-in entry.
    user.loadFromString("symbols:\n&\ti\n");

    const SymbolProcessor processor = makeProcessor(user, builtin);
    const SpeechSymbol* symbol = processor.computed("&");
    REQUIRE(symbol != nullptr);
    CHECK(symbol->replacement.value() == "i");
    CHECK(symbol->level.value() == SymbolLevel::Some);
    CHECK(symbol->preserve.value() == SymbolPreserve::Always);
    CHECK(symbol->displayName.value() == "ampersand");
}

TEST_CASE("a single symbol can be looked up on its own", "[text][symbols]") {
    SymbolDictionary builtin;
    builtin.loadFromString(kBuiltin);
    const SymbolDictionary user;
    const SymbolProcessor processor = makeProcessor(user, builtin);
    CHECK(processor.processSymbol("%") == "percent");
    // Something that is not a symbol comes back unchanged.
    CHECK(processor.processSymbol("x") == "x");
}

TEST_CASE("a dictionary survives a save and load round trip", "[text][symbols]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "alus-test-symbols.dic";
    std::filesystem::remove(path);

    SymbolDictionary original;
    original.loadFromString(kBuiltin);
    std::string error;
    REQUIRE(original.save(path, &error));

    SymbolDictionary reloaded;
    REQUIRE(reloaded.load(path, true, &error));
    CHECK(reloaded.complexSymbols().size() == original.complexSymbols().size());
    CHECK(reloaded.symbols().size() == original.symbols().size());
    REQUIRE(reloaded.find("\t") != nullptr);
    CHECK(reloaded.find("\t")->replacement.value() == "tab");
    REQUIRE(reloaded.find(",") != nullptr);
    CHECK(reloaded.find(",")->preserve.value() == SymbolPreserve::Always);
    std::filesystem::remove(path);
}

TEST_CASE("the registry falls back through the locale and to English",
          "[text][symbols][registry]") {
    SymbolRegistry registry{std::filesystem::path{ALUS_TEST_DATA_DIR} / "symbols"};
    // The data ALUS actually ships.
    REQUIRE(registry.processorFor("sr") != nullptr);
    // A dialect falls back to its language, and an unknown language to English.
    CHECK(registry.processorFor("sr_RS") == registry.processorFor("sr"));
    CHECK(registry.processorFor("ja") == registry.processorFor("en"));

    // Serbian punctuation is Serbian, and English is the base underneath it:
    // a symbol only English defines still works in Serbian.
    const std::string serbian = registry.processText("sr", "Да, свете!", SymbolLevel::All);
    CHECK(serbian.find("запета") != std::string::npos);
    CHECK(serbian.find("узвичник") != std::string::npos);
    const std::string inherited = registry.processText("sr", "50 \u00a9", SymbolLevel::Some);
    CHECK(inherited.find("ауторско право") != std::string::npos);
}

TEST_CASE("Serbian numbers use a comma for decimals", "[text][symbols][registry]") {
    SymbolRegistry registry{std::filesystem::path{ALUS_TEST_DATA_DIR} / "symbols"};
    // 3,50 is three and a half: the comma must be silent, not "comma".
    const std::string out = registry.processText("sr", "Цена је 3,50 евра.", SymbolLevel::All);
    CHECK(out.find("3,50") != std::string::npos);
    CHECK(out.find("запета,50") == std::string::npos);
    // And the dot inside 1.200 is a thousands separator, not a full stop.
    const std::string thousands = registry.processText("sr", "1.200 комада", SymbolLevel::All);
    CHECK(thousands.find("1.200") != std::string::npos);
    CHECK(thousands.find("тачка") == std::string::npos);
}

TEST_CASE("the compiled-in English data works with no files at all",
          "[text][symbols][registry]") {
    QuietLog quiet;
    // An installation with no data directory still pronounces punctuation:
    // silence is the one failure a blind user cannot diagnose.
    SymbolRegistry registry{std::filesystem::path{"/nonexistent"}};
    SymbolProcessor* processor = registry.processorFor("en");
    REQUIRE(processor != nullptr);
    CHECK(processor->processText("50%", SymbolLevel::Some).find("percent") !=
          std::string::npos);
}
