// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.6 — a handler that does not call next() stops the chain.
// T1.7 — isPendingEvents() correctly reports a newer event for the same object.
#include "alus/core/event_dispatcher.hpp"

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glib.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using alus::core::Event;
using alus::core::EventDispatcher;
using alus::core::EventLevel;
using alus::core::Log;
using alus::core::Loop;
using alus::core::LoopConfig;
using alus::core::ObjectId;
using namespace std::chrono_literals;

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

class LoopFixture {
public:
    LoopFixture()
        : context_(g_main_context_new()),
          loop_(nullptr, LoopConfig{.pumpMaxDelay = 5ms, .context = context_}) {}
    ~LoopFixture() { g_main_context_unref(context_); }

    Loop& loop() noexcept { return loop_; }

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

private:
    GMainContext* context_;
    Loop loop_;
};

constexpr ObjectId kButton = 0x1001;
constexpr ObjectId kEdit = 0x1002;

}  // namespace

TEST_CASE("T1.6 a handler that does not continue stops the chain",
          "[core][eventdispatcher]") {
    EventDispatcher dispatcher;
    std::vector<std::string> seen;

    dispatcher.setChainProvider(EventLevel::GlobalPlugin, [&seen](const Event&) {
        return std::vector<EventDispatcher::ChainHandler>{
            [&seen](const Event&, const EventDispatcher::NextHandler& next) {
                seen.emplace_back("plugin");
                next();  // this one continues
            }};
    });
    dispatcher.setChainProvider(EventLevel::AppModule, [&seen](const Event&) {
        return std::vector<EventDispatcher::ChainHandler>{
            [&seen](const Event&, const EventDispatcher::NextHandler&) {
                seen.emplace_back("appModule");
                // Does NOT call next(): the app module handles this event itself.
            }};
    });
    dispatcher.setChainProvider(EventLevel::TreeInterceptor, [&seen](const Event&) {
        return std::vector<EventDispatcher::ChainHandler>{
            [&seen](const Event&, const EventDispatcher::NextHandler& next) {
                seen.emplace_back("treeInterceptor");
                next();
            }};
    });
    dispatcher.setTerminalProvider([&seen](const Event&) {
        return std::vector<EventDispatcher::TerminalHandler>{
            [&seen](const Event&) { seen.emplace_back("object"); }};
    });

    dispatcher.executeEvent(Event{.name = "gainFocus", .object = kButton, .payload = {}});

    REQUIRE(seen == std::vector<std::string>{"plugin", "appModule"});
}

TEST_CASE("the full chain runs in level order when everybody continues",
          "[core][eventdispatcher]") {
    EventDispatcher dispatcher;
    std::vector<std::string> seen;

    const auto passing = [&seen](std::string label) {
        return [&seen, label](const Event&) {
            return std::vector<EventDispatcher::ChainHandler>{
                [&seen, label](const Event&, const EventDispatcher::NextHandler& next) {
                    seen.push_back(label);
                    next();
                }};
        };
    };

    dispatcher.setChainProvider(EventLevel::GlobalPlugin, passing("plugin"));
    dispatcher.setChainProvider(EventLevel::AppModule, passing("appModule"));
    dispatcher.setChainProvider(EventLevel::TreeInterceptor, passing("treeInterceptor"));
    dispatcher.setTerminalProvider([&seen](const Event&) {
        return std::vector<EventDispatcher::TerminalHandler>{
            [&seen](const Event&) { seen.emplace_back("object"); }};
    });

    dispatcher.executeEvent(Event{.name = "nameChange", .object = kEdit, .payload = {}});

    REQUIRE(seen == std::vector<std::string>{"plugin", "appModule", "treeInterceptor", "object"});
    REQUIRE(dispatcher.executed() == 1);
}

TEST_CASE("several global plugins each get a turn", "[core][eventdispatcher]") {
    EventDispatcher dispatcher;
    std::vector<std::string> seen;

    dispatcher.setChainProvider(EventLevel::GlobalPlugin, [&seen](const Event&) {
        std::vector<EventDispatcher::ChainHandler> handlers;
        for (const std::string name : {"pluginA", "pluginB", "pluginC"}) {
            handlers.emplace_back(
                [&seen, name](const Event&, const EventDispatcher::NextHandler& next) {
                    seen.push_back(name);
                    next();
                });
        }
        return handlers;
    });

    dispatcher.executeEvent(Event{.name = "stateChange", .object = kButton, .payload = {}});
    REQUIRE(seen == std::vector<std::string>{"pluginA", "pluginB", "pluginC"});
}

TEST_CASE("an event nobody handles is not an error", "[core][eventdispatcher]") {
    QuietLog quiet;
    EventDispatcher dispatcher;
    dispatcher.executeEvent(Event{.name = "nameChange", .object = kButton, .payload = {}});
    REQUIRE(dispatcher.executed() == 1);
    REQUIRE(Log::instance().countFor(Log::instance().level()) == 0);
}

TEST_CASE("T1.7 a pending newer event for the same object is visible",
          "[core][eventdispatcher]") {
    LoopFixture fixture;
    EventDispatcher dispatcher{&fixture.loop()};

    std::vector<bool> newerPendingWhileHandling;
    int handled = 0;

    dispatcher.setTerminalProvider([&](const Event& event) {
        return std::vector<EventDispatcher::TerminalHandler>{[&](const Event& inner) {
            ++handled;
            // The question the whole speech layer asks: is there any point
            // doing this work, or will a newer event overwrite it?
            newerPendingWhileHandling.push_back(
                dispatcher.isPendingEvents(inner.name, inner.object));
            (void)event;
        }};
    });

    // Two focus events for the same object, as a flood of AT-SPI events gives.
    dispatcher.queueEvent(Event{.name = "gainFocus", .object = kEdit, .payload = {}});
    dispatcher.queueEvent(Event{.name = "gainFocus", .object = kEdit, .payload = {}});

    REQUIRE(dispatcher.isPendingEvents());
    REQUIRE(dispatcher.isPendingEvents("gainFocus"));
    REQUIRE(dispatcher.isPendingEvents(kEdit));
    REQUIRE(dispatcher.isPendingEvents("gainFocus", kEdit));
    REQUIRE_FALSE(dispatcher.isPendingEvents("gainFocus", kButton));
    REQUIRE_FALSE(dispatcher.isPendingEvents("nameChange"));
    REQUIRE(dispatcher.pendingCount() == 2);

    REQUIRE(fixture.pumpUntil([&] { return handled == 2; }));

    // While handling the first one a newer one was pending; while handling the
    // last one, nothing was.
    REQUIRE(newerPendingWhileHandling == std::vector<bool>{true, false});
    REQUIRE_FALSE(dispatcher.isPendingEvents());
    REQUIRE(dispatcher.pendingCount() == 0);
}

TEST_CASE("counters separate events by name and by object",
          "[core][eventdispatcher]") {
    LoopFixture fixture;
    EventDispatcher dispatcher{&fixture.loop()};

    dispatcher.queueEvent(Event{.name = "gainFocus", .object = kEdit, .payload = {}});
    dispatcher.queueEvent(Event{.name = "nameChange", .object = kButton, .payload = {}});

    REQUIRE(dispatcher.isPendingEvents("gainFocus", kEdit));
    REQUIRE(dispatcher.isPendingEvents("nameChange", kButton));
    REQUIRE_FALSE(dispatcher.isPendingEvents("gainFocus", kButton));
    REQUIRE_FALSE(dispatcher.isPendingEvents("nameChange", kEdit));

    REQUIRE(fixture.pumpUntil([&] { return dispatcher.executed() == 2; }));
    REQUIRE_FALSE(dispatcher.isPendingEvents());
}

TEST_CASE("gainFocus is queued as immediate, other events are delayed",
          "[core][eventdispatcher]") {
    LoopFixture fixture;
    Loop& loop = fixture.loop();
    EventDispatcher dispatcher{&loop};

    dispatcher.queueEvent(Event{.name = "nameChange", .object = kButton, .payload = {}});
    REQUIRE(loop.pendingRequest() == alus::core::PumpRequest::Delayed);

    dispatcher.queueEvent(Event{.name = "gainFocus", .object = kEdit, .payload = {}});
    REQUIRE(loop.pendingRequest() == alus::core::PumpRequest::Immediate);

    REQUIRE(fixture.pumpUntil([&] { return dispatcher.executed() == 2; }));
}

TEST_CASE("the immediate event list is configurable", "[core][eventdispatcher]") {
    LoopFixture fixture;
    Loop& loop = fixture.loop();
    EventDispatcher dispatcher{&loop};
    dispatcher.setImmediateEvents({"gesture"});

    dispatcher.queueEvent(Event{.name = "gainFocus", .object = kEdit, .payload = {}});
    REQUIRE(loop.pendingRequest() == alus::core::PumpRequest::Delayed);
    dispatcher.queueEvent(Event{.name = "gesture", .object = kEdit, .payload = {}});
    REQUIRE(loop.pendingRequest() == alus::core::PumpRequest::Immediate);

    REQUIRE(fixture.pumpUntil([&] { return dispatcher.executed() == 2; }));
}

TEST_CASE("a throwing handler aborts that event and is logged",
          "[core][eventdispatcher]") {
    QuietLog quiet;
    EventDispatcher dispatcher;
    bool terminalRan = false;

    dispatcher.setChainProvider(EventLevel::GlobalPlugin, [](const Event&) {
        return std::vector<EventDispatcher::ChainHandler>{
            [](const Event&, const EventDispatcher::NextHandler&) {
                throw std::runtime_error("plugin blew up");
            }};
    });
    dispatcher.setTerminalProvider([&terminalRan](const Event&) {
        return std::vector<EventDispatcher::TerminalHandler>{
            [&terminalRan](const Event&) { terminalRan = true; }};
    });

    dispatcher.executeEvent(Event{.name = "gainFocus", .object = kButton, .payload = {}});

    // Faithful to NVDA: the rest of the chain does not run on top of a
    // half-finished handler.
    REQUIRE_FALSE(terminalRan);
    REQUIRE(Log::instance().countFor(alus::core::LogLevel::Error) == 1);
}

TEST_CASE("the event payload reaches the handlers", "[core][eventdispatcher]") {
    EventDispatcher dispatcher;
    auto payload = std::make_shared<std::string>("Нови документ");
    std::string seen;

    dispatcher.setTerminalProvider([&seen](const Event&) {
        return std::vector<EventDispatcher::TerminalHandler>{[&seen](const Event& event) {
            seen = *std::static_pointer_cast<std::string>(event.payload);
        }};
    });

    dispatcher.executeEvent(Event{.name = "nameChange", .object = kEdit, .payload = payload});
    REQUIRE(seen == "Нови документ");
}

TEST_CASE("queueEvent without a loop is reported, not silently dropped",
          "[core][eventdispatcher]") {
    QuietLog quiet;
    EventDispatcher dispatcher;
    dispatcher.queueEvent(Event{.name = "gainFocus", .object = kEdit, .payload = {}});
    REQUIRE(Log::instance().countFor(alus::core::LogLevel::Error) == 1);
    REQUIRE(dispatcher.queued() == 0);
}
