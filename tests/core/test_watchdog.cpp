// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.8 — a blocked callback → isAttemptingRecovery() true in under 1 s.
#include "alus/core/watchdog.hpp"

#include "alus/core/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using alus::core::Log;
using alus::core::Watchdog;
using alus::core::WatchdogConfig;
using namespace std::chrono_literals;

namespace {

struct QuietLog {
    QuietLog() {
        previous = Log::instance().stderrEnabled();
        Log::instance().setStderrEnabled(false);
    }
    ~QuietLog() { Log::instance().setStderrEnabled(previous); }
    bool previous = true;
};

// Waits for a condition, polling, up to `limit`. Returns how long it took.
template <class Predicate>
std::chrono::milliseconds waitFor(Predicate predicate, std::chrono::milliseconds limit) {
    const auto start = std::chrono::steady_clock::now();
    while (!predicate()) {
        if (std::chrono::steady_clock::now() - start > limit) {
            break;
        }
        std::this_thread::sleep_for(2ms);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
}

WatchdogConfig fastConfig() {
    return WatchdogConfig{
        .minAliveTimeout = 100ms,
        .pollInterval = 10ms,
        .frozenWarningTimeout = 5000ms,
    };
}

}  // namespace

TEST_CASE("T1.8 a blocked core raises isAttemptingRecovery well within a second",
          "[core][watchdog]") {
    QuietLog quiet;
    Watchdog watchdog{fastConfig()};
    watchdog.start();

    watchdog.alive();  // the core enters a cycle and then blocks
    const auto elapsed = waitFor([&] { return watchdog.isAttemptingRecovery(); }, 1000ms);

    REQUIRE(watchdog.isAttemptingRecovery());
    REQUIRE(elapsed < 1000ms);
    REQUIRE(watchdog.freezeCount() == 1);
    // While stuck, in-flight platform calls are to be abandoned.
    REQUIRE(watchdog.callCancelRequested());

    // The core breathes again: both flags clear.
    watchdog.alive();
    REQUIRE_FALSE(watchdog.callCancelRequested());
    waitFor([&] { return !watchdog.isAttemptingRecovery(); }, 1000ms);
    REQUIRE_FALSE(watchdog.isAttemptingRecovery());

    watchdog.asleep();
    watchdog.stop();
}

TEST_CASE("a heartbeat keeps a long but progressing cycle out of recovery",
          "[core][watchdog]") {
    QuietLog quiet;
    Watchdog watchdog{fastConfig()};
    watchdog.start();

    // 400 ms of work in 40 ms steps, each reporting alive.
    for (int i = 0; i < 10; ++i) {
        watchdog.alive();
        std::this_thread::sleep_for(40ms);
    }

    REQUIRE_FALSE(watchdog.isAttemptingRecovery());
    REQUIRE(watchdog.freezeCount() == 0);

    watchdog.asleep();
    watchdog.stop();
}

TEST_CASE("an idle core may stay quiet forever", "[core][watchdog]") {
    QuietLog quiet;
    Watchdog watchdog{fastConfig()};
    watchdog.start();

    watchdog.asleep();
    REQUIRE(watchdog.coreAsleep());
    std::this_thread::sleep_for(300ms);

    REQUIRE_FALSE(watchdog.isAttemptingRecovery());
    REQUIRE(watchdog.freezeCount() == 0);

    watchdog.stop();
}

TEST_CASE("alive() after asleep() restarts the deadline", "[core][watchdog]") {
    QuietLog quiet;
    Watchdog watchdog{fastConfig()};
    watchdog.start();

    watchdog.asleep();
    std::this_thread::sleep_for(150ms);
    REQUIRE(watchdog.freezeCount() == 0);

    watchdog.alive();  // a cycle begins and blocks
    REQUIRE_FALSE(watchdog.coreAsleep());
    waitFor([&] { return watchdog.isAttemptingRecovery(); }, 1000ms);
    REQUIRE(watchdog.freezeCount() == 1);

    watchdog.asleep();
    watchdog.stop();
}

TEST_CASE("a stopped watchdog leaves no bypass flag behind", "[core][watchdog]") {
    QuietLog quiet;
    Watchdog watchdog{fastConfig()};
    watchdog.start();
    watchdog.alive();
    waitFor([&] { return watchdog.isAttemptingRecovery(); }, 1000ms);
    REQUIRE(watchdog.isAttemptingRecovery());

    watchdog.stop();

    REQUIRE_FALSE(watchdog.running());
    REQUIRE_FALSE(watchdog.isAttemptingRecovery());
    REQUIRE_FALSE(watchdog.callCancelRequested());
}

TEST_CASE("start and stop are idempotent", "[core][watchdog]") {
    Watchdog watchdog{fastConfig()};
    watchdog.stop();  // never started
    watchdog.start();
    watchdog.start();
    REQUIRE(watchdog.running());
    watchdog.asleep();
    watchdog.stop();
    watchdog.stop();
    REQUIRE_FALSE(watchdog.running());
}

TEST_CASE("a watchdog that is never started never reports recovery",
          "[core][watchdog]") {
    Watchdog watchdog{fastConfig()};
    watchdog.alive();
    std::this_thread::sleep_for(200ms);
    REQUIRE_FALSE(watchdog.isAttemptingRecovery());
    REQUIRE(watchdog.freezeCount() == 0);
}
