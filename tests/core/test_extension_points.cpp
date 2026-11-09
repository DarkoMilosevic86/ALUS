// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.9 — an extension point handler whose owner dies is deregistered silently.
#include "alus/core/extension_points.hpp"

#include "alus/core/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using alus::core::AccumulatingDecider;
using alus::core::Action;
using alus::core::Chain;
using alus::core::Decider;
using alus::core::Filter;
using alus::core::HandlerHandle;
using alus::core::Log;
using alus::core::LogLevel;

namespace {

struct QuietLog {
    QuietLog() {
        previous = Log::instance().stderrEnabled();
        Log::instance().setStderrEnabled(false);
        Log::instance().resetCounts();
    }
    ~QuietLog() { Log::instance().setStderrEnabled(previous); }
    bool previous = true;
};

// Stands in for a long-lived subsystem that registers its own method.
class Listener {
public:
    void onEvent(int value) { seen.push_back(value); }
    std::vector<int> seen;
};

}  // namespace

TEST_CASE("T1.9 a handler whose owner died is dropped without a word",
          "[core][extensionpoints]") {
    QuietLog quiet;
    Action<int> somethingHappened;

    auto listener = std::make_shared<Listener>();
    HandlerHandle handle = somethingHappened.addWeak(
        listener, [weak = std::weak_ptr<Listener>{listener}](int value) {
            if (const auto owner = weak.lock()) {
                owner->onEvent(value);
            }
        },
        "listener");

    somethingHappened.notify(1);
    REQUIRE(listener->seen == std::vector<int>{1});
    REQUIRE(somethingHappened.size() == 1);

    listener.reset();  // the subsystem goes away

    somethingHappened.notify(2);
    REQUIRE(somethingHappened.size() == 0);  // pruned on the way out
    // Silently: a dead handler is not an error, so nothing was logged.
    REQUIRE(Log::instance().countFor(LogLevel::Error) == 0);
}

TEST_CASE("the handle governs the registration", "[core][extensionpoints]") {
    Action<> action;
    int runs = 0;

    {
        HandlerHandle handle = action.add([&runs] { ++runs; });
        REQUIRE(action.size() == 1);
        action.notify();
        REQUIRE(runs == 1);
    }

    // The handle died, so the registration did too: the caller keeps the
    // handler alive, as in NVDA.
    REQUIRE(action.size() == 0);
    action.notify();
    REQUIRE(runs == 1);
}

TEST_CASE("release() keeps a handler for the life of the process",
          "[core][extensionpoints]") {
    Action<> action;
    int runs = 0;
    static int staticRuns = 0;
    staticRuns = 0;

    {
        HandlerHandle handle = action.add([&staticRuns = staticRuns] { ++staticRuns; });
        handle.release();
    }
    REQUIRE(action.size() == 1);
    action.notify();
    REQUIRE(staticRuns == 1);
    REQUIRE(runs == 0);
}

TEST_CASE("Action calls everybody and survives a thrower", "[core][extensionpoints]") {
    QuietLog quiet;
    Action<std::string> action;
    std::vector<std::string> seen;

    HandlerHandle first = action.add([&](const std::string&) {
        throw std::runtime_error("handler is broken");
    }, "thrower");
    HandlerHandle second = action.add([&](const std::string& value) { seen.push_back(value); });

    action.notify("hello");

    REQUIRE(seen == std::vector<std::string>{"hello"});
    REQUIRE(Log::instance().countFor(LogLevel::Error) == 1);
}

TEST_CASE("a handler may register handlers while being called",
          "[core][extensionpoints]") {
    Action<> action;
    int outer = 0;
    int inner = 0;
    HandlerHandle innerHandle;

    HandlerHandle outerHandle = action.add([&] {
        ++outer;
        if (!innerHandle.active()) {
            innerHandle = action.add([&inner] { ++inner; });
        }
    });

    // Snapshot iteration: the newcomer is not called in this round.
    action.notify();
    REQUIRE(outer == 1);
    REQUIRE(inner == 0);

    action.notify();
    REQUIRE(outer == 2);
    REQUIRE(inner == 1);
}

TEST_CASE("Filter chains output into input", "[core][extensionpoints]") {
    Filter<std::string> messageFilter;

    HandlerHandle first = messageFilter.add([](std::string value) { return value + " one"; });
    HandlerHandle second = messageFilter.add([](std::string value) { return value + " two"; });

    REQUIRE(messageFilter.apply("start") == "start one two");
}

TEST_CASE("Filter passes extra arguments to every handler",
          "[core][extensionpoints]") {
    Filter<int, int> scaled;
    HandlerHandle handle = scaled.add([](int value, int factor) { return value * factor; });
    REQUIRE(scaled.apply(3, 5) == 15);
}

TEST_CASE("a throwing Filter handler leaves the value untouched",
          "[core][extensionpoints]") {
    QuietLog quiet;
    Filter<std::string> messageFilter;

    HandlerHandle first = messageFilter.add([](std::string value) { return value + " one"; });
    HandlerHandle second = messageFilter.add([](std::string) -> std::string {
        throw std::runtime_error("nope");
    });
    HandlerHandle third = messageFilter.add([](std::string value) { return value + " three"; });

    REQUIRE(messageFilter.apply("start") == "start one three");
}

TEST_CASE("Decider stops at the first false", "[core][extensionpoints]") {
    Decider<> doSomething;
    int secondRuns = 0;

    REQUIRE(doSomething.decide());  // no handlers → true

    HandlerHandle first = doSomething.add([] { return false; });
    HandlerHandle second = doSomething.add([&secondRuns] {
        ++secondRuns;
        return true;
    });

    REQUIRE_FALSE(doSomething.decide());
    REQUIRE(secondRuns == 0);  // never reached
}

TEST_CASE("a throwing Decider handler does not get to decide",
          "[core][extensionpoints]") {
    QuietLog quiet;
    Decider<int> doSomething;

    HandlerHandle first = doSomething.add([](int) -> bool { throw std::runtime_error("nope"); });
    REQUIRE(doSomething.decide(1));  // the exception is not a "no"
}

TEST_CASE("AccumulatingDecider runs every handler", "[core][extensionpoints]") {
    AccumulatingDecider<std::string> handled{true};
    std::vector<std::string> seen;

    HandlerHandle first = handled.add([&seen](const std::string& arg) {
        seen.push_back("first:" + arg);
        return false;
    });
    HandlerHandle second = handled.add([&seen](const std::string& arg) {
        seen.push_back("second:" + arg);
        return true;
    });

    REQUIRE_FALSE(handled.decide("--odd-option"));
    // Unlike Decider, the second handler still saw the data.
    REQUIRE(seen == std::vector<std::string>{"first:--odd-option", "second:--odd-option"});
}

TEST_CASE("AccumulatingDecider returns its default when nobody objects",
          "[core][extensionpoints]") {
    AccumulatingDecider<> handled{false};
    REQUIRE_FALSE(handled.decide());

    HandlerHandle keeper = handled.add([] { return false; });
    REQUIRE_FALSE(handled.decide());

    HandlerHandle objector = handled.add([] { return true; });
    REQUIRE(handled.decide());
}

TEST_CASE("Chain concatenates in registration order", "[core][extensionpoints]") {
    Chain<int> numbers;

    HandlerHandle first = numbers.add([] { return std::vector<int>{1, 2, 3}; });
    HandlerHandle second = numbers.add([] { return std::vector<int>{4, 5}; });

    REQUIRE(numbers.iter() == std::vector<int>{1, 2, 3, 4, 5});
}

TEST_CASE("a throwing Chain handler contributes nothing", "[core][extensionpoints]") {
    QuietLog quiet;
    Chain<int> numbers;

    HandlerHandle first = numbers.add([] { return std::vector<int>{1}; });
    HandlerHandle second = numbers.add([]() -> std::vector<int> {
        throw std::runtime_error("nope");
    });
    HandlerHandle third = numbers.add([] { return std::vector<int>{3}; });

    REQUIRE(numbers.iter() == std::vector<int>{1, 3});
}

TEST_CASE("a handle outliving its extension point is harmless",
          "[core][extensionpoints]") {
    HandlerHandle handle;
    {
        Action<> action;
        handle = action.add([] {});
        REQUIRE(action.size() == 1);
    }
    handle.reset();  // must not touch the destroyed registrar
    REQUIRE_FALSE(handle.active());
}
