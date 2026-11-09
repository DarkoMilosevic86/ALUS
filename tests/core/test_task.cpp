// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.5 — a coroutine task: N co_yield → N cycles, no more and no fewer.
#include "alus/core/task.hpp"

#include "alus/core/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using alus::core::Log;
using alus::core::nextCycle;
using alus::core::Task;
using alus::core::TaskRegistry;

namespace {

struct QuietLog {
    QuietLog() {
        previous = Log::instance().stderrEnabled();
        Log::instance().setStderrEnabled(false);
    }
    ~QuietLog() { Log::instance().setStderrEnabled(previous); }
    bool previous = true;
};

Task counting(int yields, int& steps) {
    for (int i = 0; i < yields; ++i) {
        ++steps;
        co_yield nextCycle;
    }
}

Task awaiting(int& steps) {
    ++steps;
    co_await nextCycle;
    ++steps;
    co_await nextCycle;
    ++steps;
}

}  // namespace

TEST_CASE("T1.5 N yields give N cycles of work, then one cycle to finish",
          "[core][task]") {
    constexpr int kYields = 5;
    int steps = 0;
    TaskRegistry registry;

    const TaskRegistry::Id id = registry.add(counting(kYields, steps));

    // Nothing runs before the first pump: a task is a generator, not a call.
    REQUIRE(steps == 0);
    REQUIRE(registry.has(id));

    for (int cycle = 1; cycle <= kYields; ++cycle) {
        const bool alive = registry.pumpAll();
        REQUIRE(steps == cycle);  // exactly one step per cycle, never two
        REQUIRE(alive);
        REQUIRE(registry.has(id));
    }

    // The cycle after the last yield runs the body to its end and drops the task.
    REQUIRE_FALSE(registry.pumpAll());
    REQUIRE(steps == kYields);
    REQUIRE_FALSE(registry.has(id));
    REQUIRE(registry.completed() == 1);
}

TEST_CASE("co_await nextCycle behaves like co_yield nextCycle", "[core][task]") {
    int steps = 0;
    TaskRegistry registry;
    registry.add(awaiting(steps));

    REQUIRE(registry.pumpAll());
    REQUIRE(steps == 1);
    REQUIRE(registry.pumpAll());
    REQUIRE(steps == 2);
    REQUIRE_FALSE(registry.pumpAll());
    REQUIRE(steps == 3);
    REQUIRE(registry.size() == 0);
}

TEST_CASE("tasks advance in parallel, one step each per cycle", "[core][task]") {
    int first = 0;
    int second = 0;
    TaskRegistry registry;
    registry.add(counting(3, first));
    registry.add(counting(3, second));

    registry.pumpAll();
    REQUIRE(first == 1);
    REQUIRE(second == 1);
    registry.pumpAll();
    REQUIRE(first == 2);
    REQUIRE(second == 2);
    REQUIRE(registry.size() == 2);
}

TEST_CASE("a cancelled task takes no further step", "[core][task]") {
    int steps = 0;
    TaskRegistry registry;
    const TaskRegistry::Id id = registry.add(counting(10, steps));

    registry.pumpAll();
    REQUIRE(steps == 1);

    REQUIRE(registry.cancel(id));
    REQUIRE_FALSE(registry.cancel(id));  // cancelling twice is not an error

    REQUIRE_FALSE(registry.pumpAll());
    REQUIRE(steps == 1);
    REQUIRE(registry.size() == 0);
}

TEST_CASE("a task may cancel itself from inside its own step", "[core][task]") {
    TaskRegistry registry;
    TaskRegistry::Id self = 0;
    int steps = 0;

    auto body = [&registry, &self, &steps]() -> Task {
        ++steps;
        co_yield nextCycle;
        ++steps;
        registry.cancel(self);  // the frame must survive until this step returns
        co_yield nextCycle;
        ++steps;  // unreachable
    };

    self = registry.add(body());
    registry.pumpAll();
    REQUIRE(steps == 1);
    registry.pumpAll();
    REQUIRE(steps == 2);
    REQUIRE(registry.size() == 0);

    registry.pumpAll();
    REQUIRE(steps == 2);
}

TEST_CASE("a task registered during a pump starts in the next cycle", "[core][task]") {
    TaskRegistry registry;
    int outer = 0;
    int inner = 0;

    auto spawner = [&registry, &outer, &inner]() -> Task {
        ++outer;
        registry.add([](int& counter) -> Task {
            ++counter;
            co_yield nextCycle;
            ++counter;
        }(inner));
        co_yield nextCycle;
        ++outer;
    };

    registry.add(spawner());
    registry.pumpAll();
    REQUIRE(outer == 1);
    REQUIRE(inner == 0);  // snapshot iteration: the newcomer waits for the next cycle

    registry.pumpAll();
    REQUIRE(outer == 2);
    REQUIRE(inner == 1);
}

TEST_CASE("a throwing task is logged and dropped, the others keep running",
          "[core][task]") {
    QuietLog quiet;
    TaskRegistry registry;
    int healthy = 0;

    registry.add([]() -> Task {
        co_yield nextCycle;
        throw std::runtime_error("task blew up");
    }(), "exploder");
    registry.add(counting(5, healthy));

    registry.pumpAll();
    REQUIRE(registry.size() == 2);
    REQUIRE(healthy == 1);

    registry.pumpAll();  // the thrower dies here
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.failed() == 1);
    REQUIRE(healthy == 2);
}

TEST_CASE("pumpAll reports whether any task is still alive", "[core][task]") {
    int steps = 0;
    TaskRegistry registry;
    REQUIRE_FALSE(registry.pumpAll());  // nothing registered

    registry.add(counting(1, steps));
    REQUIRE(registry.pumpAll());
    REQUIRE_FALSE(registry.pumpAll());
}

TEST_CASE("cancelAll empties the registry", "[core][task]") {
    int a = 0;
    int b = 0;
    TaskRegistry registry;
    registry.add(counting(5, a));
    registry.add(counting(5, b));
    registry.cancelAll();
    REQUIRE(registry.size() == 0);
    REQUIRE_FALSE(registry.pumpAll());
    REQUIRE(a == 0);
    REQUIRE(b == 0);
}
