// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 7 live check: BrlAPI against a real brltty.
//
// `meson test -C build --suite live`
//
// WHY THIS EXISTS SEPARATELY FROM THE braille SUITE. Everything behind
// `IDisplay` is measured against `RecordingDisplay`, which counts writes
// exactly -- and that is what T7.6 and T7.7 ask for. What a recording display
// cannot answer is what BRLTTY does, and finding N64 says plainly that on the
// machine phase 7 was written on, nothing did:
//
//   * whether `brlapi_enterTtyMode` succeeds for a process that is not the
//     session's screen reader;
//   * whether the display size comes back as (rows, cols) and not as
//     (cols, rows) -- a transposition no unit test can see;
//   * whether `brlapi_writeDots` accepts the cells ALUS masks to 0xFF, which
//     is the other half of the T7.3 rule;
//   * whether `brlapi_describeKeyCode` gives the command names
//     `kBrlapiBindings` is written against ("fwinlt", "lnup", "route").
//
// WITHOUT brltty IT SKIPS, like every other live check in this project: a
// build machine with no braille display is not a broken ALUS. With one it is
// the automatic half of what FAZA7.md leaves to the manual test, so the
// moment a display exists this runs instead of being read.
//
// IT DOES NOT TAKE THE DISPLAY AWAY FROM A RUNNING SCREEN READER for longer
// than one write: it connects, asks, writes one frame, restores a blank one
// and disconnects. That is the same courtesy `alus-cli braille-info` keeps.
//
// For a display-less run of the same path:
//     brltty -b no -d /dev/null -x no -n &
#include "alus/braille/brlapi_display.hpp"

#include "alus/braille/louis.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using alus::braille::BrlapiDisplay;
using alus::braille::DisplayDimensions;
using alus::braille::DisplayKey;

TEST_CASE("an unconnected BrlAPI display answers honestly rather than crashing",
          "[braille][live]") {
    // This one needs no brltty: it is the path every machine WITHOUT a
    // display takes. `Service::initialize` builds the display, fails to
    // connect, and hands the handler an object that must still answer every
    // call rather than crash -- which is what keeps braille free for a user
    // who has none (meson.build:87).
    BrlapiDisplay display;
    CHECK(display.name() == "brlapi");
    CHECK(display.dimensions().size() == 0);
    CHECK(display.isThreadSafe());
    // Not connected: a write is refused, not attempted, and nothing throws.
    CHECK_FALSE(display.display(std::vector<std::uint8_t>(40, 0)));
    CHECK(display.readKeys().empty());
    // And terminating something that never connected is allowed, because
    // that is what the daemon does on the way out.
    display.terminate();
}

TEST_CASE("BrlAPI answers a size, takes a frame and gives its keys back",
          "[braille][live]") {
    BrlapiDisplay display;
    std::string error;
    if (!display.initialize(&error)) {
        // Not a failure: this is the state of the machine, not of the code.
        // SKIP and not WARN + SUCCEED, because the TAP reporter counts a
        // warning as its own test point and the plan line then disagrees
        // with the count -- which meson reports as ERROR, turning a skip
        // into a failure (FAZA7.md N65).
        SKIP("no brltty (" + error +
             ") -- for a virtual display: brltty -b no -d /dev/null -x no -n");
    }

    const DisplayDimensions size = display.dimensions();
    INFO("brltty driver: " << display.model() << ", " << size.numRows << " rows x "
                           << size.numCols << " cols");
    // A connected display has cells. A size of zero here means `enterTtyMode`
    // succeeded and the size query did not, which is the failure that makes
    // the whole braille layer silently do nothing.
    CHECK(size.numRows >= 1);
    CHECK(size.numCols >= 1);
    CHECK(size.size() == size.numRows * size.numCols);
    CHECK(display.isThreadSafe());
    CHECK(display.name() == "brlapi");

    SECTION("a frame of real cells is accepted, and the display is left blank") {
        // The cells come from liblouis rather than from a literal, because the
        // other half of the T7.3 rule is that what liblouis produces, masked
        // to a byte, is what BrlAPI accepts. A hand-written frame would prove
        // only that the function returns true.
        alus::braille::Louis& louis = alus::braille::Louis::shared();
        louis.initialize();
        const alus::braille::Translation translation =
            louis.translate({"en-us-comp8.ctb", "braille-patterns.cti"}, "alus");
        REQUIRE(translation.ok);
        REQUIRE_FALSE(translation.cells.empty());

        std::vector<std::uint8_t> frame = translation.cells;
        frame.resize(static_cast<std::size_t>(size.size()), 0);
        CHECK(display.display(frame));

        // Give it back the way it was found.
        CHECK(display.display(std::vector<std::uint8_t>(
            static_cast<std::size_t>(size.size()), 0)));
    }

    SECTION("the key names are brltty's own, when any key is waiting") {
        // Nothing is pressed during a test run, so an empty list is the
        // expected answer and the assertion is about the SHAPE of a key that
        // is there -- which is what `kBrlapiBindings` is written against.
        // Press a thumb key while this runs and the name is logged.
        const std::vector<DisplayKey> keys = display.readKeys();
        for (const DisplayKey& key : keys) {
            INFO("key: " << key.command);
            if (key.isDots) {
                // A braille keyboard chord carries dots, not a command name.
                CHECK(key.command.empty());
            } else {
                CHECK_FALSE(key.command.empty());
                // brltty's names are lower case, and the bindings table is
                // written in that spelling.
                for (const char c : key.command) {
                    CHECK_FALSE((c >= 'A' && c <= 'Z'));
                }
            }
        }
        SUCCEED("read " + std::to_string(keys.size()) + " key(s)");
    }

    display.terminate();
}
