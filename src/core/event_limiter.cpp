// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/event_limiter.hpp"

#include "alus/core/log.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace alus::core {

EventLimiter::EventLimiter(EventLimiterConfig config) : config_(std::move(config)) {}

EventLimiter::Key EventLimiter::keyOf(const RawEvent& event) {
    return Key{event.name, event.object, event.source};
}

bool EventLimiter::isFocusLike(const std::string& name) const {
    return name == config_.focusName || name == config_.foregroundName;
}

bool EventLimiter::isMenu(const std::string& name) const {
    return std::find(config_.menuNames.begin(), config_.menuNames.end(), name) !=
           config_.menuNames.end();
}

std::optional<std::string> EventLimiter::cancelledBy(const std::string& name) const {
    for (const auto& [first, second] : config_.cancellingPairs) {
        if (name == first) {
            return second;
        }
        if (name == second) {
            return first;
        }
    }
    return std::nullopt;
}

bool EventLimiter::addEvent(RawEvent event) {
    const std::lock_guard lock{mutex_};

    // A duplicate does not get a second place in the queue; it gets a NEW
    // sequence number, which moves it forward in time. That is the difference
    // between "deduped" and "coalesced into the past".
    const auto stamp = [this](std::map<Key, Entry>& cache, RawEvent&& raw) {
        const Key key = keyOf(raw);
        cache[key] = Entry{++sequence_, std::move(raw)};
    };

    if (event.name == config_.focusName) {
        stamp(focusCache_, std::move(event));
        return true;
    }

    if (event.name == config_.foregroundName) {
        // A window coming to the foreground makes a pending focus event for the
        // same object pointless: the foreground event will announce it.
        focusCache_.erase(Key{config_.focusName, event.object, event.source});
        stamp(focusCache_, std::move(event));
        return true;
    }

    if (isMenu(event.name)) {
        // Only the last menu event of a cycle says anything about where the
        // menu ended up.
        lastMenuEvent_ = Entry{++sequence_, std::move(event)};
        return true;
    }

    if (const auto opposite = cancelledBy(event.name)) {
        const Key key{*opposite, event.object, event.source};
        if (genericCache_.erase(key) > 0) {
            // Added then removed (or shown then hidden) within one cycle: both
            // are noise, and the tree is as it was.
            ++discarded_;
            return false;
        }
    }

    stamp(genericCache_, std::move(event));
    return true;
}

std::vector<RawEvent> EventLimiter::flushEvents(const std::vector<ObjectId>& alwaysAllowed) {
    std::map<Key, Entry> generic;
    std::map<Key, Entry> focus;
    std::optional<Entry> menu;
    std::size_t maxPerSource = 0;
    std::size_t maxFocus = 0;

    {
        const std::lock_guard lock{mutex_};
        generic.swap(genericCache_);
        focus.swap(focusCache_);
        menu = std::move(lastMenuEvent_);
        lastMenuEvent_.reset();
        maxPerSource = config_.maxEventsPerSource;
        maxFocus = config_.maxFocusEvents;
    }

    std::vector<Entry> kept;
    kept.reserve(generic.size() + focus.size() + 1);
    if (menu.has_value()) {
        kept.push_back(std::move(*menu));
    }

    // Generic events: newest first, so that when an application's budget runs
    // out it is the OLDEST events that are dropped.
    std::vector<Entry> byRecency;
    byRecency.reserve(generic.size());
    for (auto& [key, entry] : generic) {
        byRecency.push_back(std::move(entry));
    }
    std::sort(byRecency.begin(), byRecency.end(),
              [](const Entry& a, const Entry& b) { return a.sequence > b.sequence; });

    std::unordered_map<std::uintptr_t, std::size_t> perSource;
    std::uint64_t dropped = 0;
    for (Entry& entry : byRecency) {
        const std::size_t count = perSource[entry.event.source]++;
        const bool exempt = std::find(alwaysAllowed.begin(), alwaysAllowed.end(),
                                      entry.event.object) != alwaysAllowed.end();
        if (count >= maxPerSource && !exempt) {
            if (count == maxPerSource) {
                ALUS_DEBUG("event budget for source {} spent in this cycle", entry.event.source);
            }
            ++dropped;
            continue;
        }
        kept.push_back(std::move(entry));
    }

    // Focus events: keep the last `maxFocusEvents`. A user cannot hear five
    // focus changes anyway, and only the last one is still true.
    std::vector<Entry> focusByAge;
    focusByAge.reserve(focus.size());
    for (auto& [key, entry] : focus) {
        focusByAge.push_back(std::move(entry));
    }
    std::sort(focusByAge.begin(), focusByAge.end(),
              [](const Entry& a, const Entry& b) { return a.sequence < b.sequence; });
    if (focusByAge.size() > maxFocus) {
        dropped += focusByAge.size() - maxFocus;
        focusByAge.erase(focusByAge.begin(),
                         focusByAge.end() - static_cast<std::ptrdiff_t>(maxFocus));
    }
    for (Entry& entry : focusByAge) {
        kept.push_back(std::move(entry));
    }

    // Arrival order is restored: the limiter changes WHICH events survive, not
    // the order they happened in.
    std::sort(kept.begin(), kept.end(),
              [](const Entry& a, const Entry& b) { return a.sequence < b.sequence; });

    std::vector<RawEvent> result;
    result.reserve(kept.size());
    for (Entry& entry : kept) {
        result.push_back(std::move(entry.event));
    }

    if (dropped > 0) {
        const std::lock_guard lock{mutex_};
        discarded_ += dropped;
    }
    return result;
}

std::size_t EventLimiter::pumpAll(const Emitter& emit,
                                  const std::vector<ObjectId>& alwaysAllowed) {
    const std::vector<RawEvent> events = flushEvents(alwaysAllowed);
    std::size_t emitted = 0;
    std::vector<const RawEvent*> pendingFocus;

    // NVDA: only ONE focus event reaches the core per cycle, but it must be the
    // most recent one that works. Try newest first and fall back.
    const auto drainFocus = [&] {
        if (pendingFocus.empty()) {
            return;
        }
        bool accepted = false;
        for (auto it = pendingFocus.rbegin(); it != pendingFocus.rend(); ++it) {
            if (emit(**it)) {
                accepted = true;
                ++emitted;
                break;
            }
        }
        {
            const std::lock_guard lock{mutex_};
            discarded_ += pendingFocus.size() - (accepted ? 1 : 0);
        }
        pendingFocus.clear();
    };

    for (const RawEvent& event : events) {
        if (isFocusLike(event.name)) {
            pendingFocus.push_back(&event);
            continue;
        }
        // A non-focus event means the focus run is over: settle it first, so the
        // focus change is announced before whatever happened after it.
        drainFocus();
        if (emit(event)) {
            ++emitted;
        }
    }
    drainFocus();

    return emitted;
}

std::size_t EventLimiter::pending() const {
    const std::lock_guard lock{mutex_};
    return focusCache_.size() + genericCache_.size() + (lastMenuEvent_.has_value() ? 1 : 0);
}

std::uint64_t EventLimiter::discarded() const {
    const std::lock_guard lock{mutex_};
    return discarded_;
}

}  // namespace alus::core
