// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The event limiter rules from 01-ARHITEKTURA.md §3.4.
#include "alus/core/event_limiter.hpp"

#include "alus/core/event_dispatcher.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glib.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using alus::core::Event;
using alus::core::EventDispatcher;
using alus::core::EventLimiter;
using alus::core::EventLimiterConfig;
using alus::core::Loop;
using alus::core::LoopConfig;
using alus::core::ObjectId;
using alus::core::PumpStage;
using alus::core::RawEvent;
using namespace std::chrono_literals;

namespace {

constexpr std::uintptr_t kFirefox = 1;
constexpr std::uintptr_t kNautilus = 2;

RawEvent raw(std::string name, ObjectId object, std::uintptr_t source = kFirefox) {
    return RawEvent{std::move(name), object, source, {}};
}

std::vector<std::string> names(const std::vector<RawEvent>& events) {
    std::vector<std::string> result;
    result.reserve(events.size());
    for (const RawEvent& event : events) {
        result.push_back(event.name);
    }
    return result;
}

std::vector<ObjectId> objects(const std::vector<RawEvent>& events) {
    std::vector<ObjectId> result;
    result.reserve(events.size());
    for (const RawEvent& event : events) {
        result.push_back(event.object);
    }
    return result;
}

}  // namespace

TEST_CASE("a duplicate is deduped and moved forward in time", "[core][eventlimiter]") {
    EventLimiter limiter;

    REQUIRE(limiter.addEvent(raw("nameChange", 1)));
    REQUIRE(limiter.addEvent(raw("nameChange", 2)));
    REQUIRE(limiter.addEvent(raw("nameChange", 1)));  // the duplicate

    const std::vector<RawEvent> flushed = limiter.flushEvents();
    // One entry per object, and the duplicate takes a NEW place in time rather
    // than a second place in the queue.
    REQUIRE(objects(flushed) == std::vector<ObjectId>{2, 1});
}

TEST_CASE("the same event from two applications is not a duplicate",
          "[core][eventlimiter]") {
    EventLimiter limiter;
    REQUIRE(limiter.addEvent(raw("nameChange", 1, kFirefox)));
    REQUIRE(limiter.addEvent(raw("nameChange", 1, kNautilus)));
    REQUIRE(limiter.flushEvents().size() == 2);
}

TEST_CASE("children added cancels a pending children removed, and the other way round",
          "[core][eventlimiter]") {
    EventLimiter limiter;

    REQUIRE(limiter.addEvent(raw("childrenRemoved", 7)));
    REQUIRE_FALSE(limiter.addEvent(raw("childrenAdded", 7)));  // both are noise
    REQUIRE(limiter.flushEvents().empty());

    REQUIRE(limiter.addEvent(raw("childrenAdded", 8)));
    REQUIRE_FALSE(limiter.addEvent(raw("childrenRemoved", 8)));
    REQUIRE(limiter.flushEvents().empty());

    // A different object is untouched by the cancellation.
    REQUIRE(limiter.addEvent(raw("childrenAdded", 9)));
    REQUIRE(limiter.addEvent(raw("childrenRemoved", 10)));
    REQUIRE(limiter.flushEvents().size() == 2);
}

TEST_CASE("a foreground event erases a pending focus event for the same object",
          "[core][eventlimiter]") {
    EventLimiter limiter;

    REQUIRE(limiter.addEvent(raw("gainFocus", 5)));
    REQUIRE(limiter.addEvent(raw("foreground", 5)));

    const std::vector<RawEvent> flushed = limiter.flushEvents();
    REQUIRE(names(flushed) == std::vector<std::string>{"foreground"});

    // A focus event for a DIFFERENT object survives a foreground event.
    REQUIRE(limiter.addEvent(raw("gainFocus", 6)));
    REQUIRE(limiter.addEvent(raw("foreground", 7)));
    REQUIRE(limiter.flushEvents().size() == 2);
}

TEST_CASE("only the last menu event of a cycle is kept", "[core][eventlimiter]") {
    EventLimiter limiter;

    REQUIRE(limiter.addEvent(raw("menuStart", 1)));
    REQUIRE(limiter.addEvent(raw("menuPopupStart", 2)));
    REQUIRE(limiter.addEvent(raw("menuPopupEnd", 2)));
    REQUIRE(limiter.addEvent(raw("menuEnd", 1)));

    const std::vector<RawEvent> flushed = limiter.flushEvents();
    REQUIRE(names(flushed) == std::vector<std::string>{"menuEnd"});
}

TEST_CASE("an application gets at most ten events per cycle", "[core][eventlimiter]") {
    EventLimiter limiter;

    // Chromium loading a page: fifteen events from one application.
    for (ObjectId object = 1; object <= 15; ++object) {
        REQUIRE(limiter.addEvent(raw("nameChange", object)));
    }
    // Another application is unaffected by its neighbour's flood.
    REQUIRE(limiter.addEvent(raw("nameChange", 100, kNautilus)));

    const std::vector<RawEvent> flushed = limiter.flushEvents();
    REQUIRE(flushed.size() == 11);
    REQUIRE(limiter.discarded() == 5);

    // The NEWEST ten survive: the oldest are the ones no longer worth saying.
    const std::vector<ObjectId> survivors = objects(flushed);
    REQUIRE(std::find(survivors.begin(), survivors.end(), 15) != survivors.end());
    REQUIRE(std::find(survivors.begin(), survivors.end(), 1) == survivors.end());
    REQUIRE(std::find(survivors.begin(), survivors.end(), 100) != survivors.end());
}

TEST_CASE("the focused object is never cut by the per-application cap",
          "[core][eventlimiter]") {
    EventLimiter limiter;
    constexpr ObjectId kFocused = 1;

    REQUIRE(limiter.addEvent(raw("stateChange", kFocused)));
    for (ObjectId object = 2; object <= 15; ++object) {
        REQUIRE(limiter.addEvent(raw("nameChange", object)));
    }

    const std::vector<RawEvent> flushed = limiter.flushEvents({kFocused});
    const std::vector<ObjectId> survivors = objects(flushed);
    // Oldest of all, and still there, because it is the focused object.
    REQUIRE(std::find(survivors.begin(), survivors.end(), kFocused) != survivors.end());
}

TEST_CASE("only the last four focus events are kept", "[core][eventlimiter]") {
    EventLimiter limiter;

    for (ObjectId object = 1; object <= 7; ++object) {
        REQUIRE(limiter.addEvent(raw("gainFocus", object)));
    }

    const std::vector<RawEvent> flushed = limiter.flushEvents();
    REQUIRE(objects(flushed) == std::vector<ObjectId>{4, 5, 6, 7});
    REQUIRE(limiter.discarded() == 3);
}

TEST_CASE("arrival order survives the limiting", "[core][eventlimiter]") {
    EventLimiter limiter;

    REQUIRE(limiter.addEvent(raw("nameChange", 1)));
    REQUIRE(limiter.addEvent(raw("gainFocus", 2)));
    REQUIRE(limiter.addEvent(raw("stateChange", 3)));
    REQUIRE(limiter.addEvent(raw("menuStart", 4)));
    REQUIRE(limiter.addEvent(raw("valueChange", 5)));

    REQUIRE(names(limiter.flushEvents()) ==
            std::vector<std::string>{"nameChange", "gainFocus", "stateChange", "menuStart",
                                     "valueChange"});
}

TEST_CASE("a flush empties the limiter", "[core][eventlimiter]") {
    EventLimiter limiter;
    REQUIRE(limiter.addEvent(raw("nameChange", 1)));
    REQUIRE(limiter.addEvent(raw("gainFocus", 2)));
    REQUIRE(limiter.addEvent(raw("menuStart", 3)));
    REQUIRE(limiter.pending() == 3);

    REQUIRE(limiter.flushEvents().size() == 3);
    REQUIRE(limiter.pending() == 0);
    REQUIRE(limiter.flushEvents().empty());
}

TEST_CASE("the second stage emits only one focus event", "[core][eventlimiter]") {
    EventLimiter limiter;
    std::vector<ObjectId> emitted;

    for (ObjectId object = 1; object <= 4; ++object) {
        REQUIRE(limiter.addEvent(raw("gainFocus", object)));
    }

    const std::size_t count = limiter.pumpAll([&emitted](const RawEvent& event) {
        emitted.push_back(event.object);
        return true;
    });

    // The newest one, and nothing older.
    REQUIRE(count == 1);
    REQUIRE(emitted == std::vector<ObjectId>{4});
}

TEST_CASE("a rejected focus event falls back to an older one",
          "[core][eventlimiter]") {
    EventLimiter limiter;
    std::vector<ObjectId> tried;

    for (ObjectId object = 1; object <= 4; ++object) {
        REQUIRE(limiter.addEvent(raw("gainFocus", object)));
    }

    // Objects 4 and 3 are dead by the time the pump runs; 2 still works.
    const std::size_t count = limiter.pumpAll([&tried](const RawEvent& event) {
        tried.push_back(event.object);
        return event.object == 2;
    });

    REQUIRE(count == 1);
    REQUIRE(tried == std::vector<ObjectId>{4, 3, 2});
}

TEST_CASE("the focus change is settled before the events that followed it",
          "[core][eventlimiter]") {
    EventLimiter limiter;
    std::vector<std::string> emitted;

    REQUIRE(limiter.addEvent(raw("gainFocus", 1)));
    REQUIRE(limiter.addEvent(raw("gainFocus", 2)));
    REQUIRE(limiter.addEvent(raw("nameChange", 3)));
    REQUIRE(limiter.addEvent(raw("gainFocus", 4)));

    limiter.pumpAll([&emitted](const RawEvent& event) {
        emitted.push_back(event.name + ":" + std::to_string(event.object));
        return true;
    });

    // One focus event from the run before nameChange, then the later one.
    REQUIRE(emitted ==
            std::vector<std::string>{"gainFocus:2", "nameChange:3", "gainFocus:4"});
}

TEST_CASE("addEvent is safe from platform callback threads", "[core][eventlimiter]") {
    EventLimiter limiter{EventLimiterConfig{.maxEventsPerSource = 1000}};

    std::vector<std::thread> threads;
    threads.reserve(4);
    for (std::uintptr_t source = 1; source <= 4; ++source) {
        threads.emplace_back([&limiter, source] {
            for (ObjectId object = 1; object <= 50; ++object) {
                limiter.addEvent(raw("nameChange", object, source));
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    REQUIRE(limiter.flushEvents().size() == 200);
}

TEST_CASE("the limiter sits between the platform and the pump",
          "[core][eventlimiter][integration]") {
    GMainContext* context = g_main_context_new();
    {
        Loop loop{nullptr, LoopConfig{.pumpMaxDelay = 5ms, .context = context}};
        EventDispatcher dispatcher{&loop};
        EventLimiter limiter;
        std::vector<std::string> announced;

        dispatcher.setTerminalProvider([&announced](const Event&) {
            return std::vector<EventDispatcher::TerminalHandler>{
                [&announced](const Event& event) {
                    announced.push_back(event.name + ":" + std::to_string(event.object));
                }};
        });

        // This is what the AT-SPI layer will register in phase 4.
        loop.addStageHandler(PumpStage::PlatformEvents, "limiter", [&] {
            limiter.pumpAll([&dispatcher](const RawEvent& event) {
                dispatcher.queueEvent(Event{event.name, event.object, event.payload});
                return true;
            });
        });

        // A flood arrives from a callback thread between two pumps.
        for (ObjectId object = 1; object <= 6; ++object) {
            limiter.addEvent(raw("gainFocus", object));
        }
        limiter.addEvent(raw("nameChange", 42));

        loop.requestPump(true);
        const auto deadline = std::chrono::steady_clock::now() + 2000ms;
        while (announced.size() < 2 && std::chrono::steady_clock::now() < deadline) {
            loop.iterate(false);
        }

        // Six focus events in, one focus announcement out — plus the name change.
        REQUIRE(announced == std::vector<std::string>{"gainFocus:6", "nameChange:42"});
    }
    g_main_context_unref(context);
}
