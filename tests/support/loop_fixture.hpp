// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Shared test fixtures: a core loop on its own GLib context, and a quiet log.
//
// Every phase 2 subsystem is driven by the loop (notifications from the
// synthesizer are marshalled onto it), so the tests have to pump it. They must
// never pump the DEFAULT context: in the daemon that one belongs to libatspi,
// and a test that iterates it would run whatever the environment happens to
// have attached.
#pragma once

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <glib.h>

#include <chrono>
#include <thread>

namespace alus::testing {

using namespace std::chrono_literals;

// Silences the log for the duration of a test that deliberately provokes
// warnings — a lost index mark, an invalid dictionary entry.
class QuietLog {
public:
    QuietLog() : previous_(core::Log::instance().stderrEnabled()) {
        core::Log::instance().setStderrEnabled(false);
    }
    ~QuietLog() { core::Log::instance().setStderrEnabled(previous_); }

    QuietLog(const QuietLog&) = delete;
    QuietLog& operator=(const QuietLog&) = delete;

private:
    bool previous_;
};

class LoopFixture {
public:
    explicit LoopFixture(std::chrono::milliseconds pumpMaxDelay = 5ms)
        : context_(g_main_context_new()),
          loop_(nullptr, core::LoopConfig{.pumpMaxDelay = pumpMaxDelay, .context = context_}) {}

    ~LoopFixture() { g_main_context_unref(context_); }

    LoopFixture(const LoopFixture&) = delete;
    LoopFixture& operator=(const LoopFixture&) = delete;

    core::Loop& loop() noexcept { return loop_; }

    // Everything that is ready right now runs; nothing waits on the clock.
    void drain() { loop_.iterateUntilIdle(); }

    template <class Predicate>
    bool pumpUntil(Predicate predicate, std::chrono::milliseconds limit = 2000ms) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!predicate()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            if (!loop_.iterate(false)) {
                std::this_thread::sleep_for(1ms);
            }
        }
        // Let whatever the predicate was waiting for finish its cycle.
        drain();
        return true;
    }

private:
    GMainContext* context_;
    core::Loop loop_;
};

}  // namespace alus::testing
