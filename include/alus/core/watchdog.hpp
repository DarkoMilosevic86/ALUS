// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Freeze detection.
//
// Port of NVDA's `watchdog.py`. See architecture.md §3.7: when an application
// blocks on an accessibility call, ALUS must not block with it.
//
// `alive()` is called on EVERY pump cycle, EVERY queue item and EVERY coroutine
// step; `asleep()` at the end of a pump (an idle core may stay quiet forever).
// While the core is stuck, `isAttemptingRecovery()` is true, and the input layer
// uses it to let keystrokes pass straight through (`inputCore.py:548-552`).
//
// DELIBERATE DIVERGENCE FROM NVDA: NVDA waits up to NORMAL_CORE_ALIVE_TIMEOUT
// (10 s) before recovering, because its recovery (`CoCancelCall`) is disruptive
// and it has a cheap Windows-specific check for "the foreground thread really is
// frozen". ALUS has neither: recovery here only raises two flags — keystrokes
// pass through and in-flight platform calls are abandoned — which is cheap and
// is exactly what a user needs within a fraction of a second. So recovery starts
// after `minAliveTimeout`, and the long timeout survives only as the threshold
// for logging a hard freeze.
#pragma once

#include "alus/core/extension_points.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace alus::core {

struct WatchdogConfig {
    // Time without a heartbeat after which the core counts as stuck.
    // NVDA: MIN_CORE_ALIVE_TIMEOUT.
    std::chrono::milliseconds minAliveTimeout{500};
    // How often the supervisory thread looks. NVDA: RECOVER_ATTEMPT_INTERVAL.
    std::chrono::milliseconds pollInterval{50};
    // After this long in one freeze, log an error with the full picture.
    // NVDA: FROZEN_WARNING_TIMEOUT.
    std::chrono::milliseconds frozenWarningTimeout{15000};
};

class Watchdog {
public:
    explicit Watchdog(WatchdogConfig config = {});
    ~Watchdog();

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    void start();
    void stop();
    bool running() const noexcept;

    // ---- main thread, hot path ----

    // The core is working. Clears both recovery flags.
    void alive() noexcept;

    // The core has nothing to do; no deadline applies until the next alive().
    void asleep() noexcept;

    bool coreAsleep() const noexcept;

    // ---- any thread ----

    // True while the core is considered stuck. The input layer checks this
    // first and lets the keystroke through to the application.
    bool isAttemptingRecovery() const noexcept;

    // True while in-flight platform calls should be abandoned. The platform
    // layer (phase 3 onwards) checks this instead of NVDA's `CoCancelCall`.
    bool callCancelRequested() const noexcept;

    // How many freezes have been detected since start. For tests and `--stats`.
    std::uint64_t freezeCount() const noexcept;

    const WatchdogConfig& config() const noexcept { return config_; }

    // Notified ONCE per freeze, from the watchdog's own thread, as soon as the
    // freeze is detected and before anything else is attempted.
    //
    // Added in phase 3 for one reason, spelled out so it is not mistaken for a
    // generic hook: a wedged screen reader that is holding key grabs has taken
    // the user's keyboard away as well as their speech (finding N19, measured:
    // nine minutes, only SIGKILL ended it). The input backend hangs its
    // `emergencyRelease()` here, so the keyboard comes back even if the main
    // loop never runs again.
    //
    // A handler therefore runs on a NON-MAIN thread and must be thread safe
    // and must not block — the one thing it exists to undo is a block.
    Action<>& freezeDetected() noexcept { return freezeDetected_; }

private:
    void watcherLoop();
    std::chrono::steady_clock::time_point lastAlive() const noexcept;

    WatchdogConfig config_;

    // Heartbeat timestamp as a plain integer, so the hot path is one relaxed store.
    std::atomic<std::int64_t> lastAliveNs_{0};
    std::atomic<bool> coreAsleep_{true};
    std::atomic<bool> attemptingRecovery_{false};
    std::atomic<bool> cancelCalls_{false};
    std::atomic<std::uint64_t> freezes_{0};

    Action<> freezeDetected_;

    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread watcher_;
};

}  // namespace alus::core
