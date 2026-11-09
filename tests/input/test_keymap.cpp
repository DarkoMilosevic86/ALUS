// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The keyboard layout lookup. What is being checked is the thing NVDA gets for
// free from virtual key codes and ALUS has to reconstruct: the identity of a
// physical key, independent of which shift level it was pressed at.
#include "alus/input/keymap.hpp"

#include "alus/input/key_names.hpp"
#include "alus/input/keysyms.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using alus::input::KeyMap;
using alus::input::KeyMapOptions;
namespace keysym = alus::input::keysym;

TEST_CASE("a keymap compiles for an explicit layout") {
    KeyMap keymap;
    std::string error;
    REQUIRE(keymap.load(KeyMapOptions{.layout = "us"}, &error));
    CHECK(keymap.loaded());
    CHECK(keymap.layout() == "us");
    CHECK(keymap.keyCount() > 50);

    SECTION("a letter key is identified by its unshifted keysym") {
        // Keycode 28 is `t` on a US layout (evdev 20 + 8), and xkbcommon uses
        // the same numbering the compositor reports.
        const auto base = keymap.baseKeysym(28);
        REQUIRE(base.has_value());
        CHECK(*base == static_cast<std::uint32_t>('t'));
    }

    SECTION("the shifted level of the same key is known") {
        // This is what a grab for `nvda+shift+'` needs: the compositor may
        // report either level, and ALUS registers both rather than guess.
        const auto apostrophe = keymap.shiftedKeysymFor(static_cast<std::uint32_t>('\''));
        REQUIRE(apostrophe.has_value());
        CHECK(*apostrophe == static_cast<std::uint32_t>('"'));
        CHECK(keymap.shiftedKeysymFor(static_cast<std::uint32_t>('2')) ==
              static_cast<std::uint32_t>('@'));
    }

    SECTION("an unknown keycode is answered with nothing, not a guess") {
        CHECK_FALSE(keymap.baseKeysym(9999).has_value());
    }
}

TEST_CASE("a Serbian layout names its own letters") {
    KeyMap keymap;
    // The Latin variant is the one the laptop bindings in NVDA's sr locale map
    // assume (`nvda+š`, `nvda+đ`).
    if (!keymap.load(KeyMapOptions{.layout = "rs", .variant = "latin"})) {
        SKIP("xkeyboard-config has no rs(latin) layout on this machine");
    }
    bool foundScaron = false;
    bool foundDstroke = false;
    for (std::uint32_t keycode = 8; keycode < 130; ++keycode) {
        const auto base = keymap.baseKeysym(keycode);
        if (!base.has_value()) {
            continue;
        }
        foundScaron = foundScaron || *base == 0x1b9;   // š
        foundDstroke = foundDstroke || *base == 0x1f0;  // đ
    }
    CHECK(foundScaron);
    CHECK(foundDstroke);
    // And the name comes back as the letter, which is how the gesture matches.
    CHECK(alus::input::KeyNames::shared().name(0x1b9) == "š");
}

TEST_CASE("a nonsense layout fails without taking the process with it") {
    KeyMap keymap;
    std::string error;
    // Best effort by design: the caller falls back to the keysym the
    // compositor sent, because a screen reader that refuses to start over a
    // keymap is worse than one with imperfect punctuation bindings.
    const bool loaded = keymap.load(KeyMapOptions{.layout = "no-such-layout-exists"}, &error);
    if (!loaded) {
        CHECK_FALSE(error.empty());
        CHECK_FALSE(keymap.loaded());
        CHECK_FALSE(keymap.baseKeysym(28).has_value());
    }
}

TEST_CASE("ALUS_XKB_LAYOUT overrides the session's layout") {
    setenv("ALUS_XKB_LAYOUT", "de", 1);
    KeyMap keymap;
    const bool loaded = keymap.load({}, nullptr);
    unsetenv("ALUS_XKB_LAYOUT");
    REQUIRE(loaded);
    CHECK(keymap.layout().rfind("de", 0) == 0);
}
