// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.1 — the pump coalescer: 1000 requestPump() calls in one cycle → at most
//        one pump.
// T1.2 — Immediate upgrades a pending Delayed pump.
#include "alus/core/loop.hpp"

#include "alus/core/cached.hpp"
#include "alus/core/log.hpp"
#include "alus/core/watchdog.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glib.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using alus::core::Cached;
using alus::core::CacheScope;
using alus::core::Log;
using alus::core::Loop;
using alus::core::LoopConfig;
using alus::core::nextCycle;
using alus::core::PumpRequest;
using alus::core::PumpStage;
using alus::core::Task;
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

// A Loop on its own GLib context, so tests never disturb the default context
// (which in the daemon belongs to libatspi).
class LoopFixture {
public:
    explicit LoopFixture(std::chrono::milliseconds pumpMaxDelay = 50ms)
        : context_(g_main_context_new()),
          loop_(nullptr, LoopConfig{.pumpMaxDelay = pumpMaxDelay, .context = context_}) {}

    ~LoopFixture() { g_main_context_unref(context_); }

    Loop& loop() noexcept { return loop_; }

    // Iterates without blocking until `predicate` holds or the deadline passes.
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
        return true;
    }

    // Lets every source that is ready right now run, nothing more.
    void drain() { loop_.iterateUntilIdle(); }

private:
    GMainContext* context_;
    Loop loop_;
};

}  // namespace

TEST_CASE("T1.1 a thousand pump requests in one cycle cause one pump", "[core][loop]") {
    LoopFixture fixture;
    Loop& loop = fixture.loop();
    int handlerRuns = 0;

    loop.addStageHandler(PumpStage::PlatformEvents, "flooder", [&] {
        ++handlerRuns;
        if (handlerRuns == 1) {
            for (int i = 0; i < 1000; ++i) {
                loop.requestPump(false);
            }
        }
    });

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 1; }));

    // The thousand requests coalesced into exactly one further pump.
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 2; }));
    std::this_thread::sleep_for(150ms);
    fixture.drain();
    REQUIRE(loop.tickCount() == 2);
    REQUIRE(handlerRuns == 2);
    REQUIRE(loop.pendingRequest() == PumpRequest::None);
}

TEST_CASE("T1.2 Immediate upgrades a pending Delayed pump", "[core][loop]") {
    LoopFixture fixture{500ms};  // a delay long enough that a timer-driven pump is unmistakable
    Loop& loop = fixture.loop();

    loop.requestPump(false);
    REQUIRE(loop.pendingRequest() == PumpRequest::Delayed);

    // Nothing is ready yet: the delayed pump is 500 ms away.
    fixture.drain();
    REQUIRE(loop.tickCount() == 0);

    loop.requestPump(true);
    REQUIRE(loop.pendingRequest() == PumpRequest::Immediate);

    const auto start = std::chrono::steady_clock::now();
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 1; }));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < 400ms);  // it did not wait for the delayed timer

    // The cancelled delayed timer must not fire a second pump.
    std::this_thread::sleep_for(600ms);
    fixture.drain();
    REQUIRE(loop.tickCount() == 1);
}

TEST_CASE("Delayed does not downgrade a pending Immediate pump", "[core][loop]") {
    LoopFixture fixture{500ms};
    Loop& loop = fixture.loop();

    loop.requestPump(true);
    loop.requestPump(false);
    REQUIRE(loop.pendingRequest() == PumpRequest::Immediate);

    const auto start = std::chrono::steady_clock::now();
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 1; }));
    REQUIRE(std::chrono::steady_clock::now() - start < 400ms);
}

TEST_CASE("the cycle keeps the order from the architecture", "[core][loop]") {
    LoopFixture fixture;
    Loop& loop = fixture.loop();
    std::vector<std::string> order;

    loop.addStageHandler(PumpStage::Vision, "vision", [&] { order.emplace_back("vision"); });
    loop.addStageHandler(PumpStage::Braille, "braille", [&] { order.emplace_back("braille"); });
    loop.addStageHandler(PumpStage::Mouse, "mouse", [&] { order.emplace_back("mouse"); });
    loop.addStageHandler(PumpStage::Core, "core-extra", [&] { order.emplace_back("core-extra"); });
    // The platform stage queues work, exactly as the AT-SPI layer will.
    loop.addStageHandler(PumpStage::PlatformEvents, "platform", [&] {
        order.emplace_back("platform");
        loop.queue().push([&] { order.emplace_back("queued-work"); });
    });

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 1; }));

    // Platform events are drained first, so the work they queue runs in the
    // SAME cycle (invariant 1), before the later stages.
    REQUIRE(order == std::vector<std::string>{"platform", "queued-work", "core-extra", "mouse",
                                              "braille", "vision"});
}

TEST_CASE("the cache lives exactly one cycle", "[core][loop]") {
    LoopFixture fixture;
    Loop& loop = fixture.loop();

    CacheScope scope;
    int getterCalls = 0;
    Cached<int> role{scope, [&getterCalls] {
                         ++getterCalls;
                         return 7;
                     }};

    loop.addStageHandler(PumpStage::PlatformEvents, "reader", [&] {
        // Three readers in one cycle, as speech, braille and review would be.
        REQUIRE(role.get() == 7);
        REQUIRE(role.get() == 7);
        REQUIRE(role.get() == 7);
    });

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 1; }));
    REQUIRE(getterCalls == 1);

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 2; }));
    REQUIRE(getterCalls == 2);  // invalidated at the end of the first cycle
}

TEST_CASE("a live task keeps the pump ticking by itself", "[core][loop]") {
    LoopFixture fixture{5ms};
    Loop& loop = fixture.loop();
    int steps = 0;

    loop.tasks().add([](int& counter) -> Task {
        for (int i = 0; i < 4; ++i) {
            ++counter;
            co_yield nextCycle;
        }
    }(steps));
    loop.requestPump(false);

    REQUIRE(fixture.pumpUntil([&] { return steps == 4 && loop.tasks().size() == 0; }));
    REQUIRE(loop.tickCount() >= 5);  // four steps plus the cycle that finishes it

    // With no tasks left the pump stops asking for itself.
    const std::uint64_t settled = loop.tickCount();
    std::this_thread::sleep_for(60ms);
    fixture.drain();
    REQUIRE(loop.tickCount() == settled);
}

TEST_CASE("the pump is not re-entrant", "[core][loop]") {
    LoopFixture fixture{5ms};
    Loop& loop = fixture.loop();
    int nested = 0;

    loop.addStageHandler(PumpStage::PlatformEvents, "re-enterer", [&] {
        const std::uint64_t before = loop.tickCount();
        REQUIRE(loop.isPumping());
        // Asking for an immediate pump from inside a pump must not recurse.
        loop.requestPump(true);
        REQUIRE(loop.tickCount() == before);
        ++nested;
    });

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() >= 3; }));
    REQUIRE(nested >= 3);
}

TEST_CASE("queueFunction from another thread reaches the main thread",
          "[core][loop]") {
    LoopFixture fixture{5ms};
    Loop& loop = fixture.loop();
    std::thread::id ranOn;
    bool ran = false;

    std::thread worker{[&] {
        loop.queueFunction(
            [&] {
                ranOn = std::this_thread::get_id();
                ran = true;
            },
            true, "from-worker");
    }};
    worker.join();

    REQUIRE(fixture.pumpUntil([&] { return ran; }));
    REQUIRE(ranOn == std::this_thread::get_id());
}

TEST_CASE("callLater runs the function inside a pump cycle", "[core][loop]") {
    LoopFixture fixture{5ms};
    Loop& loop = fixture.loop();
    bool ran = false;
    bool insidePump = false;

    loop.callLater(10ms, [&] {
        ran = true;
        insidePump = loop.isPumping();
    }, "later");

    REQUIRE(fixture.pumpUntil([&] { return ran; }));
    REQUIRE(insidePump);
}

TEST_CASE("cancelLater stops a pending call", "[core][loop]") {
    LoopFixture fixture{5ms};
    Loop& loop = fixture.loop();
    bool ran = false;

    const Loop::TimerId id = loop.callLater(20ms, [&] { ran = true; }, "doomed");
    REQUIRE(loop.cancelLater(id));
    REQUIRE_FALSE(loop.cancelLater(id));  // cancelling twice is not an error

    std::this_thread::sleep_for(60ms);
    fixture.drain();
    REQUIRE_FALSE(ran);
}

TEST_CASE("a throwing stage handler does not starve the later stages",
          "[core][loop]") {
    QuietLog quiet;
    LoopFixture fixture;
    Loop& loop = fixture.loop();
    bool queueFlushed = false;
    bool brailleRan = false;

    loop.addStageHandler(PumpStage::PlatformEvents, "thrower", [&] {
        loop.queue().push([&] { queueFlushed = true; });
        throw std::runtime_error("platform layer blew up");
    });
    loop.addStageHandler(PumpStage::Braille, "braille", [&] { brailleRan = true; });

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 1; }));

    REQUIRE(queueFlushed);
    REQUIRE(brailleRan);
}

TEST_CASE("a removed stage handler stops running", "[core][loop]") {
    LoopFixture fixture;
    Loop& loop = fixture.loop();
    int runs = 0;

    const Loop::HandlerId id =
        loop.addStageHandler(PumpStage::Mouse, "counter", [&] { ++runs; });

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 1; }));
    REQUIRE(runs == 1);

    REQUIRE(loop.removeStageHandler(id));
    REQUIRE_FALSE(loop.removeStageHandler(id));

    loop.requestPump(true);
    REQUIRE(fixture.pumpUntil([&] { return loop.tickCount() == 2; }));
    REQUIRE(runs == 1);
}

TEST_CASE("the watchdog sees a heartbeat per cycle and sleeps after it",
          "[core][loop]") {
    alus::core::Watchdog watchdog{{.minAliveTimeout = 100ms,
                                   .pollInterval = 10ms,
                                   .frozenWarningTimeout = 5000ms}};
    GMainContext* context = g_main_context_new();
    {
        Loop loop{&watchdog, LoopConfig{.pumpMaxDelay = 5ms, .context = context}};
        watchdog.start();

        loop.requestPump(true);
        const auto deadline = std::chrono::steady_clock::now() + 2000ms;
        while (loop.tickCount() == 0 && std::chrono::steady_clock::now() < deadline) {
            loop.iterate(false);
        }
        REQUIRE(loop.tickCount() == 1);
        // After a cycle the core is asleep, so idling is never a freeze.
        REQUIRE(watchdog.coreAsleep());
        std::this_thread::sleep_for(250ms);
        REQUIRE(watchdog.freezeCount() == 0);
        watchdog.stop();
    }
    g_main_context_unref(context);
}
