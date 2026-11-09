// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/event_dispatcher.hpp"

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace alus::core {
namespace {

// Decrements a counter, erasing the entry at zero so that a membership test
// answers "is one pending" — NVDA relies on `in` for exactly this.
template <class Map, class Key>
void decrementEntry(Map& map, const Key& key) {
    const auto it = map.find(key);
    if (it == map.end()) {
        return;
    }
    if (it->second > 1) {
        --it->second;
    } else {
        map.erase(it);
    }
}

}  // namespace

EventDispatcher::EventDispatcher(Loop* loop) : loop_(loop) {}

void EventDispatcher::setChainProvider(EventLevel level, ChainProvider provider) {
    switch (level) {
        case EventLevel::GlobalPlugin:
            globalPluginProvider_ = std::move(provider);
            return;
        case EventLevel::AppModule:
            appModuleProvider_ = std::move(provider);
            return;
        case EventLevel::TreeInterceptor:
            treeInterceptorProvider_ = std::move(provider);
            return;
        case EventLevel::Object:
            ALUS_ERROR("the object level is terminal — use setTerminalProvider()");
            return;
    }
}

void EventDispatcher::setTerminalProvider(TerminalProvider provider) {
    objectProvider_ = std::move(provider);
}

void EventDispatcher::setImmediateEvents(std::vector<std::string> names) {
    const std::lock_guard lock{mutex_};
    immediateEvents_ = std::move(names);
}

bool EventDispatcher::isImmediate(const std::string& name) const {
    const std::lock_guard lock{mutex_};
    return std::find(immediateEvents_.begin(), immediateEvents_.end(), name) !=
           immediateEvents_.end();
}

void EventDispatcher::incrementPending(const Event& event) {
    const std::lock_guard lock{mutex_};
    ++byName_[event.name];
    ++byObject_[event.object];
    ++byNameAndObject_[{event.name, event.object}];
    ++queued_;
}

void EventDispatcher::decrementPending(const Event& event) {
    const std::lock_guard lock{mutex_};
    decrementEntry(byName_, event.name);
    decrementEntry(byObject_, event.object);
    decrementEntry(byNameAndObject_, std::pair{event.name, event.object});
}

// NVDA: queueEvent (eventHandler.py:47-67).
void EventDispatcher::queueEvent(Event event) {
    if (loop_ == nullptr) {
        ALUS_ERROR("queueEvent without a loop; event {} dropped", event.name);
        return;
    }
    incrementPending(event);

    const bool immediate = isImmediate(event.name);
    std::string name = event.name;
    loop_->queueFunction(
        [this, event = std::move(event)] {
            // NVDA: _queueEventCallback — the counters drop BEFORE execution, so
            // a handler asking isPendingEvents() learns about events still to
            // come, not about itself.
            decrementPending(event);
            executeEvent(event);
        },
        immediate, std::format("event:{}", name));
}

// NVDA: executeEvent + _EventExecuter (eventHandler.py:108-166).
void EventDispatcher::executeEvent(const Event& event) {
    {
        const std::lock_guard lock{mutex_};
        ++executed_;
    }

    // One flat chain: levels 1-3 receive a continuation, level 4 does not.
    // A terminal handler therefore ends the chain simply by existing.
    std::vector<std::function<void(const NextHandler&)>> chain;

    const auto appendChainLevel = [&chain, &event](const ChainProvider& provider) {
        if (!provider) {
            return;
        }
        for (ChainHandler& handler : provider(event)) {
            chain.emplace_back([handler = std::move(handler), &event](const NextHandler& next) {
                handler(event, next);
            });
        }
    };

    appendChainLevel(globalPluginProvider_);
    appendChainLevel(appModuleProvider_);
    appendChainLevel(treeInterceptorProvider_);

    if (objectProvider_) {
        for (TerminalHandler& handler : objectProvider_(event)) {
            chain.emplace_back([handler = std::move(handler), &event](const NextHandler&) {
                handler(event);
            });
        }
    }

    if (chain.empty()) {
        return;
    }

    std::size_t index = 0;
    NextHandler next;
    next = [&chain, &index, &next] {
        if (index >= chain.size()) {
            return;
        }
        const auto& step = chain[index++];
        step(next);
    };

    // FAITHFUL TO NVDA: an exception aborts the REST of the chain. The levels
    // are ordered by authority, and a plugin that threw may have left the state
    // half-changed, so running the lower levels on top of it is worse than
    // dropping the event and logging it.
    try {
        next();
    } catch (...) {
        Log::instance().exception(std::format("error dispatching event {}", event.name));
    }
}

bool EventDispatcher::isPendingEvents() const {
    const std::lock_guard lock{mutex_};
    return !byName_.empty();
}

bool EventDispatcher::isPendingEvents(const std::string& name) const {
    const std::lock_guard lock{mutex_};
    return byName_.find(name) != byName_.end();
}

bool EventDispatcher::isPendingEvents(ObjectId object) const {
    const std::lock_guard lock{mutex_};
    return byObject_.find(object) != byObject_.end();
}

bool EventDispatcher::isPendingEvents(const std::string& name, ObjectId object) const {
    const std::lock_guard lock{mutex_};
    return byNameAndObject_.find({name, object}) != byNameAndObject_.end();
}

std::uint64_t EventDispatcher::executed() const {
    const std::lock_guard lock{mutex_};
    return executed_;
}

std::uint64_t EventDispatcher::queued() const {
    const std::lock_guard lock{mutex_};
    return queued_;
}

std::size_t EventDispatcher::pendingCount() const {
    const std::lock_guard lock{mutex_};
    std::size_t total = 0;
    for (const auto& [name, count] : byName_) {
        total += count;
    }
    return total;
}

}  // namespace alus::core
