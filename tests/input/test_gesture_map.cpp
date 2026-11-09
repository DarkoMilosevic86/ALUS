// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The gesture map file: NVDA's `gestures.ini`, parsed, queried and written.
// The hard requirement being tested is Z3 — a user's own NVDA map loads
// unchanged, including the `None =` unbind entry and the `kb:` emulation form.
#include "alus/input/gesture_map.hpp"

#include "../support/loop_fixture.hpp"
#include "alus/input/gesture.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::input::GestureMap;
using alus::input::normalizeGestureIdentifier;

TEST_CASE("an imported gesture map loads unchanged") {
    // 00-VIZIJA.md Z3: a user arrives with their own map, written for the
    // upstream screen reader, and it has to work WITHOUT EDITS — including its
    // spelling of the modifier key, which ALUS canonicalises to `alus` on the
    // way in and never writes back.
    GestureMap map("test");
    const std::string text =
        "; a comment\n"
        "# another comment\n"
        "[globalCommands.GlobalCommands]\n"
        "\tzoomOut = kb:NVDA+shift+'\n"
        "\tleftMouseClick = kb(laptop):NVDA+š\n"
        "\tnavigatorObject_next = kb:NVDA+numpad6, kb(laptop):NVDA+shift+rightArrow\n"
        "\n"
        "[virtualBuffers.VirtualBuffer]\n"
        "\tnextHeading = kb:a\n"
        "\tNone = kb:h, kb:control+h\n";
    REQUIRE(map.loadFromString(text));
    CHECK_FALSE(map.lastUpdateContainedError());

    SECTION("entries are found by their normalized identifier") {
        // Looked up by ALUS's own spelling: the import was canonicalised.
        const auto* entries =
            map.entriesForNormalized(normalizeGestureIdentifier("kb:shift+alus+'"));
        REQUIRE(entries != nullptr);
        REQUIRE(entries->size() == 1);
        CHECK((*entries)[0].location == "globalCommands.GlobalCommands");
        CHECK((*entries)[0].script == "zoomOut");
    }

    SECTION("one script may have several gestures") {
        // Stored sorted, so the lookup key is the normalized spelling.
        CHECK(map.entriesForNormalized("kb:alus+numpad6") != nullptr);
        CHECK(map.entriesForNormalized("kb(laptop):alus+rightarrow+shift") != nullptr);
    }

    SECTION("None unbinds, and is kept as an entry with no script") {
        const auto* entries = map.entriesForNormalized("kb:h");
        REQUIRE(entries != nullptr);
        REQUIRE(entries->size() == 1);
        CHECK((*entries)[0].location == "virtualBuffers.VirtualBuffer");
        CHECK((*entries)[0].script.empty());
        CHECK(map.entriesForNormalized("kb:control+h") != nullptr);
    }

    SECTION("a non-ASCII key name survives") {
        CHECK(map.entriesForNormalized("kb(laptop):alus+š") != nullptr);
    }
}

TEST_CASE("a malformed line is skipped, not fatal") {
    alus::testing::QuietLog quiet;
    GestureMap map("broken");
    const std::string text =
        "nextHeading = kb:a\n"            // an entry before any section
        "[globalCommands.GlobalCommands]\n"
        "this line has no equals sign\n"
        "\tzoomIn = alus+shift+plus\n"    // no source prefix
        "\tzoomOut = kb:alus+shift+'\n";  // this one is fine
    REQUIRE(map.loadFromString(text));
    CHECK(map.lastUpdateContainedError());
    // The rest of the file is still the user's configuration.
    CHECK(map.entriesForNormalized("kb:'+alus+shift") != nullptr);
    CHECK(map.entriesForNormalized("kb:alus+plus+shift") == nullptr);
}

TEST_CASE("a UTF-8 BOM is not an error") {
    // NVDA ships locale maps with one (de/gestures.ini).
    GestureMap map;
    REQUIRE(map.loadFromString("\xEF\xBB\xBF[globalCommands.GlobalCommands]\n\tzoomIn = kb:a\n"));
    CHECK_FALSE(map.lastUpdateContainedError());
    CHECK(map.entriesForNormalized("kb:a") != nullptr);
}

TEST_CASE("a gesture map round-trips through serialization") {
    GestureMap map;
    map.add("kb:alus+shift+t", "globalCommands.GlobalCommands", "reportStuff");
    map.add("kb(laptop):alus+a", "globalCommands.GlobalCommands", "reportStuff");
    map.add("kb:h", "virtualBuffers.VirtualBuffer", "");
    map.add("kb:j", "braille.BrailleDisplay", "kb:downArrow");

    GestureMap reloaded;
    REQUIRE(reloaded.loadFromString(map.serialize()));
    CHECK_FALSE(reloaded.lastUpdateContainedError());
    CHECK(reloaded.size() == map.size());
    REQUIRE(reloaded.entriesForNormalized("kb:h") != nullptr);
    CHECK(reloaded.entriesForNormalized("kb:h")->front().script.empty());
    REQUIRE(reloaded.entriesForNormalized("kb:j") != nullptr);
    // The emulation form is a script NAME, not a gesture, and must survive.
    CHECK(reloaded.entriesForNormalized("kb:j")->front().script == "kb:downArrow");
}

TEST_CASE("adding, replacing and removing bindings") {
    GestureMap map;
    map.add("kb:alus+t", "a.A", "first");
    map.add("kb:alus+t", "b.B", "second");
    REQUIRE(map.entriesForNormalized("kb:alus+t")->size() == 2);

    map.add("kb:alus+t", "c.C", "third", /*replace=*/true);
    REQUIRE(map.entriesForNormalized("kb:alus+t")->size() == 1);

    CHECK(map.remove("kb:ALUS+T", "c.C", "third"));
    CHECK(map.entriesForNormalized("kb:alus+t") == nullptr);
    CHECK_FALSE(map.remove("kb:nvda+t", "c.C", "third"));
}
