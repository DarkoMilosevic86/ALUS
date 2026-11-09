// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Extension points: the five shapes of pluggable behaviour.
//
// Port of NVDA's `extensionPoints/` (`__init__.py`, `util.py`). See
// 01-ARHITEKTURA.md §3.6. Five kinds, all built on one registrar:
//
//   Action               notify(args...)         call all; log and continue on error
//   Filter<T>            apply(value, args...)   a chain: one's output is the next one's input
//   Decider              decide(args...)         stop at the first false
//   AccumulatingDecider  decide(args...)         run ALL, return !default if any said so
//   Chain<T>             iter(args...)           concatenate what every handler yields
//
// Two lessons from NVDA that had to carry over:
//
//  1. THE CALLER KEEPS THE HANDLER ALIVE. NVDA holds weak references, so
//     lambdas and local closures silently vanish; NVDA therefore registers
//     methods of long-lived singletons. Here that contract is explicit:
//     `add()` returns a [[nodiscard]] handle that deregisters when it dies, and
//     `addWeak()` ties the handler to an owner's lifetime (an object that dies
//     deregisters silently, exactly as in NVDA).
//
//  2. ITERATION GOES OVER A SNAPSHOT (NVDA: `list(self._handlers.values())`),
//     because a handler may register or unregister handlers while being called.
#pragma once

#include "alus/core/log.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace alus::core {

// Returned by add(). Move-only: while it lives, the handler is registered.
// `release()` keeps the handler registered for the rest of the process, which
// is what a long-lived singleton wants.
class [[nodiscard]] HandlerHandle {
public:
    HandlerHandle() = default;
    explicit HandlerHandle(std::function<void()> remover) : remover_(std::move(remover)) {}

    HandlerHandle(const HandlerHandle&) = delete;
    HandlerHandle& operator=(const HandlerHandle&) = delete;

    HandlerHandle(HandlerHandle&& other) noexcept
        : remover_(std::exchange(other.remover_, nullptr)) {}

    HandlerHandle& operator=(HandlerHandle&& other) noexcept {
        if (this != &other) {
            reset();
            remover_ = std::exchange(other.remover_, nullptr);
        }
        return *this;
    }

    ~HandlerHandle() { reset(); }

    void reset() noexcept {
        if (remover_) {
            auto remover = std::exchange(remover_, nullptr);
            remover();
        }
    }

    void release() noexcept { remover_ = nullptr; }
    bool active() const noexcept { return static_cast<bool>(remover_); }

private:
    std::function<void()> remover_;
};

namespace detail {

// The shared handler list. Held by shared_ptr so a handle outliving its
// extension point is harmless.
template <class Signature>
class HandlerList : public std::enable_shared_from_this<HandlerList<Signature>> {
public:
    using Id = std::uint64_t;

    struct Entry {
        Id id = 0;
        std::function<Signature> handler;
        std::string name;
        bool weak = false;
        std::weak_ptr<void> owner;
    };

    Id add(std::function<Signature> handler, std::string name, bool weak,
           std::weak_ptr<void> owner) {
        const std::lock_guard lock{mutex_};
        const Id id = ++nextId_;
        entries_.push_back(Entry{id, std::move(handler), std::move(name), weak, std::move(owner)});
        return id;
    }

    bool remove(Id id) {
        const std::lock_guard lock{mutex_};
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == id) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }

    std::size_t size() const {
        const std::lock_guard lock{mutex_};
        return entries_.size();
    }

    void clear() {
        const std::lock_guard lock{mutex_};
        entries_.clear();
    }

    // A snapshot of the live handlers; handlers whose owner has died are
    // dropped from the list on the way out, silently (NVDA's weakref callback).
    std::vector<Entry> snapshot() {
        std::vector<Entry> live;
        std::vector<Id> dead;
        {
            const std::lock_guard lock{mutex_};
            live.reserve(entries_.size());
            for (const Entry& entry : entries_) {
                if (entry.weak && entry.owner.expired()) {
                    dead.push_back(entry.id);
                    continue;
                }
                live.push_back(entry);
            }
        }
        for (const Id id : dead) {
            remove(id);
        }
        return live;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    Id nextId_ = 0;
};

// Shared registration surface of all five extension point kinds.
template <class Signature>
class Registrar {
public:
    using Handler = std::function<Signature>;
    using Id = typename HandlerList<Signature>::Id;

    Registrar() : handlers_(std::make_shared<HandlerList<Signature>>()) {}

    // The returned handle keeps the registration alive.
    HandlerHandle add(Handler handler, std::string name = {}) {
        const Id id = handlers_->add(std::move(handler), std::move(name), false, {});
        return makeHandle(id);
    }

    // Tied to `owner`: when the owner dies, the handler is dropped silently.
    // This is how NVDA's weak references behave.
    HandlerHandle addWeak(const std::shared_ptr<void>& owner, Handler handler,
                          std::string name = {}) {
        const Id id = handlers_->add(std::move(handler), std::move(name), true, owner);
        return makeHandle(id);
    }

    std::size_t size() const { return handlers_->size(); }
    void clear() { handlers_->clear(); }

protected:
    std::vector<typename HandlerList<Signature>::Entry> snapshot() const {
        return handlers_->snapshot();
    }

    void logHandlerError(const std::string& name) const {
        Log::instance().exception(name.empty() ? "error running extension point handler"
                                               : "error running handler " + name);
    }

private:
    HandlerHandle makeHandle(Id id) {
        std::weak_ptr<HandlerList<Signature>> weak = handlers_;
        return HandlerHandle{[weak, id] {
            if (const auto list = weak.lock()) {
                list->remove(id);
            }
        }};
    }

    std::shared_ptr<HandlerList<Signature>> handlers_;
};

}  // namespace detail

// Call everybody; one handler's failure must not stop the others.
template <class... Args>
class Action : public detail::Registrar<void(Args...)> {
public:
    void notify(Args... args) const {
        for (const auto& entry : this->snapshot()) {
            try {
                entry.handler(args...);
            } catch (...) {
                this->logHandlerError(entry.name);
            }
        }
    }
};

// A chain over a value: each handler's return value feeds the next one.
template <class Value, class... Args>
class Filter : public detail::Registrar<Value(Value, Args...)> {
public:
    Value apply(Value value, Args... args) const {
        for (const auto& entry : this->snapshot()) {
            try {
                // The value is COPIED into the handler and only replaced once the
                // handler has returned. Moving it in would leave the value gutted
                // if the handler threw, and NVDA's semantics are that a broken
                // filter leaves the data untouched.
                Value filtered = entry.handler(value, args...);
                value = std::move(filtered);
            } catch (...) {
                this->logHandlerError(entry.name);
            }
        }
        return value;
    }
};

// Stop at the first false. No handlers, or all true → true.
template <class... Args>
class Decider : public detail::Registrar<bool(Args...)> {
public:
    bool decide(Args... args) const {
        for (const auto& entry : this->snapshot()) {
            bool decision = true;
            try {
                decision = entry.handler(args...);
            } catch (...) {
                this->logHandlerError(entry.name);
                continue;  // a broken handler does not get to decide
            }
            if (!decision) {
                return false;
            }
        }
        return true;
    }
};

// Run EVERY handler, then answer. Used where each handler must see the data
// even if an earlier one already decided (NVDA: unknown command line options).
template <class... Args>
class AccumulatingDecider : public detail::Registrar<bool(Args...)> {
public:
    explicit AccumulatingDecider(bool defaultDecision) : defaultDecision_(defaultDecision) {}

    bool decide(Args... args) const {
        bool sawOpposite = false;
        for (const auto& entry : this->snapshot()) {
            try {
                if (entry.handler(args...) == !defaultDecision_) {
                    sawOpposite = true;
                }
            } catch (...) {
                this->logHandlerError(entry.name);
                continue;
            }
        }
        return sawOpposite ? !defaultDecision_ : defaultDecision_;
    }

    bool defaultDecision() const noexcept { return defaultDecision_; }

private:
    bool defaultDecision_;
};

// Concatenate what every handler produces. NVDA's handlers are generators; the
// C++ equivalent is a handler that returns a vector, appended in order.
template <class Value, class... Args>
class Chain : public detail::Registrar<std::vector<Value>(Args...)> {
public:
    std::vector<Value> iter(Args... args) const {
        std::vector<Value> result;
        for (const auto& entry : this->snapshot()) {
            try {
                std::vector<Value> part = entry.handler(args...);
                result.insert(result.end(), std::make_move_iterator(part.begin()),
                              std::make_move_iterator(part.end()));
            } catch (...) {
                this->logHandlerError(entry.name);
            }
        }
        return result;
    }
};

}  // namespace alus::core
