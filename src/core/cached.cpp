// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/cached.hpp"

namespace alus::core {
namespace {

std::vector<CacheScope*>& registry() {
    static std::vector<CacheScope*> scopes;
    return scopes;
}

std::size_t& getterCalls() {
    static std::size_t calls = 0;
    return calls;
}

constexpr std::size_t kNotRegistered = static_cast<std::size_t>(-1);

}  // namespace

namespace detail {
void noteCacheGetterCall() noexcept {
    ++getterCalls();
}
}  // namespace detail

CachedBase::CachedBase(CacheScope& scope) : scope_(&scope), indexInScope_(kNotRegistered) {
    scope.add(this);
}

CachedBase::~CachedBase() {
    if (scope_ != nullptr) {
        scope_->remove(this);
    }
}

CacheScope::CacheScope() : indexInRegistry_(registry().size()) {
    registry().push_back(this);
}

CacheScope::~CacheScope() {
    // Caches may outlive their scope (member destruction order); in that case
    // the link is severed so they never touch the freed vector.
    for (CachedBase* entry : entries_) {
        entry->scope_ = nullptr;
        entry->indexInScope_ = kNotRegistered;
    }
    entries_.clear();

    auto& scopes = registry();
    const std::size_t index = indexInRegistry_;
    scopes[index] = scopes.back();
    scopes[index]->indexInRegistry_ = index;
    scopes.pop_back();
}

void CacheScope::add(CachedBase* entry) {
    entry->indexInScope_ = entries_.size();
    entries_.push_back(entry);
}

void CacheScope::remove(CachedBase* entry) noexcept {
    const std::size_t index = entry->indexInScope_;
    if (index == kNotRegistered || index >= entries_.size() || entries_[index] != entry) {
        return;
    }
    entries_[index] = entries_.back();
    entries_[index]->indexInScope_ = index;
    entries_.pop_back();
    entry->indexInScope_ = kNotRegistered;
    entry->scope_ = nullptr;
}

void CacheScope::invalidate() noexcept {
    // Held until the loop is over; released last, so that if this was the
    // last reference the scope dies after its walk and not inside it.
    const std::shared_ptr<const void> pinned = pin_ ? pin_() : nullptr;
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        entries_[index]->invalidate();
    }
}

void invalidateAllCaches() noexcept {
    // Iterated BY INDEX, re-reading the vector each step, because an
    // invalidation can destroy a scope. Phase 1 assumed it could not and said
    // so here; phase 4 found the case that makes it possible, and it is not an
    // abuse: `a11y::Object` caches its parent and its children as shared_ptrs
    // to other objects, each of which owns a scope. Dropping the last
    // reference to one of those destroys a scope from inside this loop.
    //
    // ~CacheScope swaps the last scope into the hole it leaves, so staying at
    // the same index visits that swapped-in scope next. Nothing is skipped and
    // nothing is visited twice.
    //
    // The object model ALSO keeps every object it touched alive for the whole
    // cycle (a11y::Registry::releaseCycleObjects), so in the daemon this
    // normally never happens. This loop is what makes it safe when it does --
    // at shutdown, in a test, or when a toolkit reports an object as its own
    // ancestor.
    for (std::size_t index = 0; index < registry().size();) {
        const std::size_t before = registry().size();
        registry()[index]->invalidate();
        if (registry().size() >= before) {
            ++index;
        }
    }
}

std::size_t cacheScopeCount() noexcept {
    return registry().size();
}

std::size_t cacheGetterCalls() noexcept {
    return getterCalls();
}

void resetCacheGetterCalls() noexcept {
    getterCalls() = 0;
}

}  // namespace alus::core
