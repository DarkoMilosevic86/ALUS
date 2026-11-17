// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Property cache with a lifetime of exactly one pump cycle.
//
// Port of NVDA's `AutoPropertyObject` (`baseObject.py:121-178`) plus the cache
// flush at the end of each cycle (`core.py:1035`). See architecture.md §3.5:
// this is the whole performance model. Speech, braille and review all ask the
// same object for `name`, `role`, `states` within one cycle — the platform is
// hit once.
//
// THREADING: main thread only. The cache introduces no locking (§13).
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace alus::core {

class CacheScope;

// Common base so that a `CacheScope` can flush caches of differing types.
class CachedBase {
public:
    CachedBase(const CachedBase&) = delete;
    CachedBase& operator=(const CachedBase&) = delete;

    virtual void invalidate() noexcept = 0;

protected:
    explicit CachedBase(CacheScope& scope);
    virtual ~CachedBase();

private:
    friend class CacheScope;

    CacheScope* scope_;
    std::size_t indexInScope_;
};

// The cache group of a single object. `invalidate()` is NVDA's
// `AutoPropertyObject.invalidateCache()`.
class CacheScope {
public:
    CacheScope();
    ~CacheScope();

    CacheScope(const CacheScope&) = delete;
    CacheScope& operator=(const CacheScope&) = delete;

    void invalidate() noexcept;

    std::size_t size() const noexcept { return entries_.size(); }

    // What keeps the scope's OWNER alive while its entries are flushed. An
    // object caches its children, a child caches its parent: flushing the
    // children can drop the last reference to the parent whose scope is
    // being walked, and the walk then runs on freed memory (N149, a pure
    // virtual call in a test). The owner pins itself for the duration.
    void setPin(std::function<std::shared_ptr<const void>()> pin) { pin_ = std::move(pin); }

private:
    friend class CachedBase;

    void add(CachedBase* entry);
    void remove(CachedBase* entry) noexcept;

    std::vector<CachedBase*> entries_;
    std::size_t indexInRegistry_;
    std::function<std::shared_ptr<const void>()> pin_;
};

// Called by `Loop::tick()` at the END of every cycle. The single point that
// defines the cache lifetime — do not move it without reading §3.1.
void invalidateAllCaches() noexcept;

// Number of live scopes; for tests and leak diagnostics.
std::size_t cacheScopeCount() noexcept;

// Global getter call count — a cache-miss meter, for tests and `--stats`.
std::size_t cacheGetterCalls() noexcept;
void resetCacheGetterCalls() noexcept;

template <class T>
class Cached final : public CachedBase {
public:
    using Getter = std::function<T()>;

    Cached(CacheScope& scope, Getter getter) : CachedBase(scope), getter_(std::move(getter)) {}

    // The getter runs at most once per cycle. If it throws, the value is NOT
    // cached and the exception reaches the caller (a dead object must not
    // become a cached lie).
    const T& get() const {
        if (!value_.has_value()) {
            noteGetterCall();
            value_ = getter_();
        }
        return *value_;
    }

    const T& operator*() const { return get(); }

    // A value the platform already delivered (for example a property carried in
    // an AT-SPI signal payload — §3.5 "the extra lever"). Saves a round trip.
    void set(T value) { value_ = std::move(value); }

    bool cached() const noexcept { return value_.has_value(); }

    void invalidate() noexcept override { value_.reset(); }

private:
    static void noteGetterCall() noexcept;

    Getter getter_;
    mutable std::optional<T> value_;
};

namespace detail {
void noteCacheGetterCall() noexcept;
}  // namespace detail

template <class T>
void Cached<T>::noteGetterCall() noexcept {
    detail::noteCacheGetterCall();
}

}  // namespace alus::core
