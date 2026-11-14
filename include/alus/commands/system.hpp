// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The "System status" commands: what time is it, and how much battery is left.
//
//     alus+f12         once: the time. Twice: the date.
//     alus+shift+b     the battery: percentage, time left, and whether the
//                      machine is plugged in.
//
// WHY THEY ARE IN A LIBRARY OF THEIR OWN RATHER THAN IN A SUBSYSTEM. Neither
// of them belongs to speech, to the object model or to the text layer: they
// read the machine and say a sentence. They are here with the other commands
// that have no single owner (src/meson.build, libalus_commands).
//
// WHERE THE FACTS COME FROM, and the one deliberate difference from upstream.
// Upstream asks Windows (`GetTimeFormatEx`, `GetSystemPowerStatus`); there is
// no port of either, so:
//
//   * the time and the date are `strftime` in the locale ALUS set up
//     (i18n::initialize calls setlocale(LC_ALL, "")), and
//   * the battery is read from /sys/class/power_supply, which is the kernel's
//     own interface and needs no daemon, no D-Bus and no upower running.
//
// The SPOKEN STRINGS are upstream's, word for word, because they are already
// translated into Serbian in upstream's catalogue and a user's documentation
// says what they say (vision.md §7, tools/import-nvda-po.py).
#pragma once

#include "alus/script/scriptable.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace alus::script {
class Dispatcher;
}

namespace alus::commands {

// What the kernel says about the battery. Separated from the command so that
// it can be tested against a directory of files the test writes itself --
// which is the only way to assert on a laptop at 7% and on a desktop with no
// battery at all from one build machine.
struct BatteryStatus {
    bool present = false;
    // Whether anything could be read at all. False means /sys said nothing
    // usable, which is NOT the same as "no battery" and is not spoken as it.
    bool known = false;
    bool pluggedIn = false;
    int percent = 0;
    // Nothing when the kernel does not report a rate (it often does not while
    // the rate is changing, and never on some firmware).
    std::optional<int> minutesRemaining;
};

// Reads /sys/class/power_supply. `root` is the directory, injected for the
// tests; empty means the real one.
BatteryStatus readBatteryStatus(const std::filesystem::path& root = {});

// The sentence upstream would speak for this status, in upstream's order:
// percentage, then the time remaining, then plugged in or unplugged.
std::string describeBattery(const BatteryStatus& status);

class SystemCommands final : public script::ScriptableObject {
public:
    struct Context {
        // Repeat counting (the second press of alus+f12 says the date).
        // Asked for on every press, not held: the input service creates its
        // dispatcher when it starts -- AFTER the daemon builds this context -- and
        // again on every restart. A held pointer was null for the whole life of
        // the daemon, so every second press counted as a first (phase-10.md N122).
        std::function<const script::Dispatcher*()> dispatcher;
        // ui::message by default; injected so the tests can read what was said.
        std::function<void(const std::string&)> message;
        // The /sys directory to read the battery from. Empty means the real
        // one; the tests point it at a directory they wrote.
        std::filesystem::path powerSupplyRoot;
        // The clock, injected for the same reason: a test that asserted on
        // the real one would assert on the minute it happened to run in.
        std::function<std::time_t()> now;
    };

    SystemCommands(Context context, const script::Catalog* catalog);

private:
    void dateTime(input::Gesture& gesture);
    void say_battery_status(input::Gesture& gesture);

    void say(const std::string& text) const;
    int repeatCount() const;
    std::time_t clock() const;

    Context context_;
};

// The locale's time without seconds, and its long date. Public because the
// tests pin them to a fixed timestamp.
std::string formatTime(std::time_t when);
// The locale's time pattern (nl_langinfo(T_FMT)) with the seconds taken out.
// Public for the tests: sr_RS, en_US and most glibc locales say `%T` or `%r`,
// shorthands that HAVE seconds without a `%S` in sight (phase-10.md N122).
std::string timePatternWithoutSeconds(std::string pattern);
std::string formatDate(std::time_t when);

}  // namespace alus::commands
