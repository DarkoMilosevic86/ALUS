// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Event dispatch: a chain with an explicit continuation.
//
// Port of NVDA's `eventHandler.py:1-166`. See architecture.md §3.3 — the most
// valuable extensibility pattern in NVDA. Levels 1-3 receive `(event, next)` and
// DECIDE whether to continue; level 4 is terminal and gets no continuation:
//
//   1. every global plugin   → handler(event, next)
//   2. the object's app module → handler(event, next)
//   3. the tree interceptor    → handler(event, next)   [only if ready]
//   4. the object itself       → handler(event)          [terminal]
//
// Alongside the chain come the pending-event counters
// (`_pendingEventCountsBy{Name,Obj,NameAndObj}`) and the predicate
// `isPendingEvents()` — "is there any point doing this work, or will a newer
// event overwrite it anyway". The whole speech and braille layer leans on it, so
// it is ported WITH the counters, not without them.
//
// The core knows nothing about AT-SPI: an object is an opaque identity
// (`ObjectId`) plus a payload the platform layer attaches. Phase 4 supplies the
// level providers.
#pragma once

#include "alus/core/queue.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace alus::core {

class Loop;

// Identity of the object an event is about. The core compares and counts these;
// it never dereferences them.
using ObjectId = std::uintptr_t;

struct Event {
    std::string name;      // "gainFocus", "nameChange", "stateChange", ...
    ObjectId object = 0;   // 0 means "no object" (a global event)
    // Whatever the platform layer needs to carry along; the core only moves it.
    std::shared_ptr<void> payload;
};

// The four levels, in the order NVDA runs them.
enum class EventLevel {
    GlobalPlugin,
    AppModule,
    TreeInterceptor,
    Object,  // terminal: no continuation
};

class EventDispatcher {
public:
    // Call it to run the rest of the chain. A handler that does NOT call it
    // stops the chain — that is the whole point of the pattern.
    using NextHandler = std::function<void()>;

    // Levels 1-3.
    using ChainHandler = std::function<void(const Event&, const NextHandler&)>;
    // Level 4 (terminal).
    using TerminalHandler = std::function<void(const Event&)>;

    // A level contributes zero or more handlers for a given event. Phase 4
    // plugs the real sources in here (running plugins, app module, tree
    // interceptor, the object itself).
    using ChainProvider = std::function<std::vector<ChainHandler>(const Event&)>;
    using TerminalProvider = std::function<std::vector<TerminalHandler>(const Event&)>;

    // `loop` may be null, in which case only executeEvent() is usable.
    explicit EventDispatcher(Loop* loop = nullptr);

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    void setChainProvider(EventLevel level, ChainProvider provider);
    void setTerminalProvider(TerminalProvider provider);

    // NVDA: queueEvent. Callable from any thread. Bumps the pending counters,
    // then queues the execution; `gainFocus` goes with immediate=true, because
    // response time to focus matters most (§3.3).
    void queueEvent(Event event);

    // NVDA: executeEvent. Runs the chain now, on the calling thread.
    void executeEvent(const Event& event);

    // NVDA: isPendingEvents(eventName, obj) — all four combinations.
    bool isPendingEvents() const;
    bool isPendingEvents(const std::string& name) const;
    bool isPendingEvents(ObjectId object) const;
    bool isPendingEvents(const std::string& name, ObjectId object) const;

    // Event names dispatched with immediate=true. Defaults to {"gainFocus"}.
    void setImmediateEvents(std::vector<std::string> names);

    std::uint64_t executed() const;
    std::uint64_t queued() const;
    std::size_t pendingCount() const;

private:
    void incrementPending(const Event& event);
    void decrementPending(const Event& event);
    bool isImmediate(const std::string& name) const;

    Loop* loop_;

    ChainProvider globalPluginProvider_;
    ChainProvider appModuleProvider_;
    ChainProvider treeInterceptorProvider_;
    TerminalProvider objectProvider_;

    // Written from platform threads, read from the main thread.
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::size_t> byName_;
    std::unordered_map<ObjectId, std::size_t> byObject_;
    std::map<std::pair<std::string, ObjectId>, std::size_t> byNameAndObject_;
    std::vector<std::string> immediateEvents_{"gainFocus"};
    std::uint64_t executed_ = 0;
    std::uint64_t queued_ = 0;
};

}  // namespace alus::core
