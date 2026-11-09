// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.3 — the flush is bounded: a function that re-queues itself must not wedge
//        the pump.
#include "alus/core/queue.hpp"

#include "alus/core/log.hpp"
#include "alus/core/watchdog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using alus::core::Log;
using alus::core::LogLevel;
using alus::core::Queue;
using alus::core::Watchdog;

namespace {

// Silences the expected error records of the "a throwing item" cases.
struct QuietLog {
    QuietLog() {
        previous = Log::instance().stderrEnabled();
        Log::instance().setStderrEnabled(false);
    }
    ~QuietLog() { Log::instance().setStderrEnabled(previous); }
    bool previous = true;
};

}  // namespace

TEST_CASE("T1.3 a self-requeueing function cannot wedge the flush", "[core][queue]") {
    Queue queue;
    int runs = 0;

    // NVDA: range(qsize() + 1) — with one item queued the bound is two items.
    Queue::Function self;
    self = [&] {
        ++runs;
        queue.push(self);
    };
    queue.push(self);

    queue.flush();  // must return

    REQUIRE(runs == 2);
    REQUIRE_FALSE(queue.empty());  // the work is still pending, the pump is not stuck
}

TEST_CASE("the queue is FIFO", "[core][queue]") {
    Queue queue;
    std::vector<int> order;

    for (int i = 0; i < 5; ++i) {
        queue.push([&order, i] { order.push_back(i); });
    }
    queue.flush();

    REQUIRE(order == std::vector<int>{0, 1, 2, 3, 4});
    REQUIRE(queue.empty());
    REQUIRE(queue.executed() == 5);
}

TEST_CASE("items queued during a flush run in the same flush, up to the bound",
          "[core][queue]") {
    Queue queue;
    int runs = 0;

    // Three items queued → bound is four; the item added by the first one fits.
    queue.push([&] {
        ++runs;
        queue.push([&] { ++runs; });
    });
    queue.push([&] { ++runs; });
    queue.push([&] { ++runs; });

    queue.flush();

    REQUIRE(runs == 4);
    REQUIRE(queue.empty());
}

TEST_CASE("a throwing item does not stop the rest of the queue", "[core][queue]") {
    QuietLog quiet;
    Queue queue;
    int after = 0;

    queue.push([] { throw std::runtime_error("boom"); }, "test-thrower");
    queue.push([&after] { ++after; });

    queue.flush();

    REQUIRE(after == 1);
    REQUIRE(queue.failed() == 1);
    REQUIRE(queue.executed() == 2);
}

TEST_CASE("push is safe from other threads", "[core][queue]") {
    Queue queue;
    std::atomic<int> pushed{0};

    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 50; ++i) {
                queue.push([] {});
                ++pushed;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(pushed.load() == 200);
    REQUIRE(queue.size() == 200);
    queue.flush();
    REQUIRE(queue.executed() == 200);
    REQUIRE(queue.empty());
}

TEST_CASE("the queue sends a heartbeat per item", "[core][queue]") {
    Watchdog watchdog{{.minAliveTimeout = std::chrono::milliseconds{50},
                       .pollInterval = std::chrono::milliseconds{5},
                       .frozenWarningTimeout = std::chrono::milliseconds{5000}}};
    Queue queue{&watchdog};
    watchdog.start();
    watchdog.alive();

    // Four items of 30 ms each: 120 ms of work with a 50 ms freeze timeout.
    // Without the per-item heartbeat this would be reported as a freeze.
    for (int i = 0; i < 4; ++i) {
        queue.push([] { std::this_thread::sleep_for(std::chrono::milliseconds{30}); });
    }
    queue.flush();

    REQUIRE(watchdog.freezeCount() == 0);
    watchdog.asleep();
    watchdog.stop();
}

TEST_CASE("an empty flush on an empty queue is a no-op", "[core][queue]") {
    Queue queue;
    queue.flush();
    REQUIRE(queue.executed() == 0);
    REQUIRE(queue.empty());
}
