// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The event limiter: why a screen reader stays responsive under a flood.
//
// Port of NVDA's `OrderedWinEventLimiter`
// (`IAccessibleHandler/orderedWinEventLimiter.py`) plus the second stage from
// `IAccessibleHandler.pumpAll`. See architecture.md §3.4.
//
// It sits BETWEEN the platform callback threads and the pump: callbacks add raw
// events from whatever thread delivers them, and the pump drains what is left
// after the rules below. AT-SPI floods exactly as winEvents do — Chromium
// loading a page, LibreOffice scrolling a table — so this belongs to phase 1,
// not to a later round of optimisation.
//
// First stage (addEvent):
//   * duplicates are DEDUPED WITH A SHIFT IN TIME — the duplicate takes a new
//     sequence number instead of a second place in the queue;
//   * a "children added" event cancels a pending "children removed" for the
//     same object, and the other way round;
//   * a foreground event ERASES a pending focus event for the same object;
//   * of the menu events only the LAST one is kept;
//   * at most `maxEventsPerSource` events per originating application per
//     cycle, except for objects named as always allowed (the focused one);
//   * of the focus events only the last `maxFocusEvents` are kept.
//
// Second stage (pumpAll): emit only ONE focus/foreground event — try the newest
// and fall back to older ones until one is accepted.
//
// The event NAMES are configurable because the core knows nothing about AT-SPI;
// phase 4 fills them in with the AT-SPI spellings (§3.4).
#pragma once

#include "alus/core/event_dispatcher.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace alus::core {

// A platform event as it arrives, before it becomes an `Event`.
struct RawEvent {
    std::string name;
    ObjectId object = 0;
    // The application the event came from. NVDA keys its per-application cap on
    // the Windows threadID; on AT-SPI this is the application's bus name
    // identity, supplied by phase 4.
    std::uintptr_t source = 0;
    std::shared_ptr<void> payload;
    // Never dropped by the per-source budget. For the rare event that says
    // where the user IS when the focus did not move (a newly selected item);
    // set by the platform layer, which is the one that can tell.
    bool priority = false;
};

struct EventLimiterConfig {
    // NVDA: MAX_WINEVENTS_PER_THREAD.
    std::size_t maxEventsPerSource = 10;
    // Events that come by the hundred when a container is rebuilt. They get a
    // budget of their own, `maxEventsPerSource` again, so that a rebuild
    // cannot starve the name and state changes that arrive with it.
    std::vector<std::string> bulkNames = {"childrenAdded", "childrenRemoved"};
    // NVDA: maxFocusItems.
    std::size_t maxFocusEvents = 4;

    // Platform-neutral defaults; phase 4 replaces them with AT-SPI names.
    std::string focusName = "gainFocus";
    std::string foregroundName = "foreground";
    // Events of which only the last one per cycle is worth anything.
    std::vector<std::string> menuNames = {"menuStart", "menuEnd", "menuPopupStart",
                                          "menuPopupEnd"};
    // Pairs that cancel each other out while both are still pending.
    std::vector<std::pair<std::string, std::string>> cancellingPairs = {
        {"childrenAdded", "childrenRemoved"},
        {"show", "hide"},
    };
};

class EventLimiter {
public:
    explicit EventLimiter(EventLimiterConfig config = {});

    EventLimiter(const EventLimiter&) = delete;
    EventLimiter& operator=(const EventLimiter&) = delete;

    // Callable from ANY thread: this is what a platform callback calls.
    // Returns false if the event was discarded outright.
    bool addEvent(RawEvent event);

    // Main thread. Returns what survived, in arrival order. Events for objects
    // in `alwaysAllowed` ignore the per-application cap — the focused object's
    // events are never dropped.
    std::vector<RawEvent> flushEvents(const std::vector<ObjectId>& alwaysAllowed = {});

    // Returns true if this focus event was accepted. A rejected one makes the
    // limiter fall back to an older focus event, as NVDA does when the newest
    // focus event turns out to be for a dead or useless object.
    using Emitter = std::function<bool(const RawEvent&)>;

    // Second stage (NVDA: IAccessibleHandler.pumpAll). Returns how many events
    // were emitted.
    std::size_t pumpAll(const Emitter& emit,
                        const std::vector<ObjectId>& alwaysAllowed = {});

    std::size_t pending() const;
    std::uint64_t discarded() const;
    const EventLimiterConfig& config() const noexcept { return config_; }

private:
    // (name, object, source) — the identity a duplicate collapses onto.
    using Key = std::tuple<std::string, ObjectId, std::uintptr_t>;

    struct Entry {
        std::uint64_t sequence = 0;
        RawEvent event;
    };

    bool isFocusLike(const std::string& name) const;
    bool isMenu(const std::string& name) const;
    std::optional<std::string> cancelledBy(const std::string& name) const;
    static Key keyOf(const RawEvent& event);

    EventLimiterConfig config_;

    mutable std::mutex mutex_;
    std::map<Key, Entry> focusCache_;
    std::map<Key, Entry> genericCache_;
    std::optional<Entry> lastMenuEvent_;
    std::uint64_t sequence_ = 0;
    std::uint64_t discarded_ = 0;
};

}  // namespace alus::core
