// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.4 — Cached<T>: two get() calls in one cycle = 1 getter call;
//                   after invalidateAll() = 2 getter calls.
#include "alus/core/cached.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>

using alus::core::Cached;
using alus::core::CacheScope;
using alus::core::cacheScopeCount;
using alus::core::invalidateAllCaches;

TEST_CASE("T1.4 the getter runs once per cycle", "[core][cached]") {
    int calls = 0;
    CacheScope scope;
    Cached<std::string> name{scope, [&calls] {
                                 ++calls;
                                 return std::string{"button"};
                             }};

    REQUIRE(name.get() == "button");
    REQUIRE(name.get() == "button");
    REQUIRE(calls == 1);

    invalidateAllCaches();

    REQUIRE(name.get() == "button");
    REQUIRE(calls == 2);
}

TEST_CASE("one object, several properties — invalidate() clears all of them", "[core][cached]") {
    int nameCalls = 0;
    int roleCalls = 0;
    CacheScope scope;
    Cached<std::string> name{scope, [&nameCalls] {
                                 ++nameCalls;
                                 return std::string{"File"};
                             }};
    Cached<int> role{scope, [&roleCalls] {
                         ++roleCalls;
                         return 42;
                     }};

    (void)name.get();
    (void)role.get();
    (void)name.get();
    (void)role.get();
    REQUIRE(nameCalls == 1);
    REQUIRE(roleCalls == 1);

    scope.invalidate();

    (void)name.get();
    (void)role.get();
    REQUIRE(nameCalls == 2);
    REQUIRE(roleCalls == 2);
}

TEST_CASE("set() bypasses the getter — a property from a signal payload", "[core][cached]") {
    int calls = 0;
    CacheScope scope;
    Cached<int> states{scope, [&calls] {
                           ++calls;
                           return 1;
                       }};

    states.set(7);
    REQUIRE(states.cached());
    REQUIRE(states.get() == 7);
    REQUIRE(calls == 0);

    invalidateAllCaches();
    REQUIRE_FALSE(states.cached());
    REQUIRE(states.get() == 1);
    REQUIRE(calls == 1);
}

TEST_CASE("a throwing getter caches nothing", "[core][cached]") {
    int calls = 0;
    CacheScope scope;
    Cached<int> value{scope, [&calls]() -> int {
                          ++calls;
                          throw std::runtime_error("the object died");
                      }};

    REQUIRE_THROWS_AS(value.get(), std::runtime_error);
    REQUIRE_FALSE(value.cached());
    REQUIRE_THROWS_AS(value.get(), std::runtime_error);
    REQUIRE(calls == 2);
}

TEST_CASE("a scope deregisters itself when destroyed", "[core][cached]") {
    const std::size_t before = cacheScopeCount();
    {
        CacheScope first;
        CacheScope second;
        REQUIRE(cacheScopeCount() == before + 2);
        (void)first;
        (void)second;
    }
    REQUIRE(cacheScopeCount() == before);
}

TEST_CASE("a cache destroyed before its scope deregisters itself", "[core][cached]") {
    CacheScope scope;
    {
        Cached<int> temporary{scope, [] { return 1; }};
        REQUIRE(scope.size() == 1);
    }
    REQUIRE(scope.size() == 0);
    // Must not touch the freed object.
    invalidateAllCaches();
}

TEST_CASE("a cache outliving its scope never touches the freed vector", "[core][cached]") {
    auto scope = std::make_unique<CacheScope>();
    Cached<int> value{*scope, [] { return 5; }};
    REQUIRE(value.get() == 5);
    scope.reset();
    // The destructor of `value` must now be harmless, and so must the global pass.
    invalidateAllCaches();
    REQUIRE(value.get() == 5);
}
