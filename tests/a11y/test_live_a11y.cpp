// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The live check: the real libatspi, against the session's own accessibility
// bus.
//
// What the mock backend cannot test is exactly what this does: that libatspi
// initialises, that the subscription is accepted, that the desktop tree can be
// read, that the toolkit says what the mapping tables assume, and WHAT IT ALL
// COSTS -- the number the whole caching design exists for.
//
// It reads and measures only. It never takes the keyboard, never activates a
// window and never performs an action, so it is safe to run while the user's
// own screen reader is running. Without an accessibility bus it SKIPS rather
// than fails: a build machine with no desktop is not a broken ALUS.
//
//     meson test -C build --suite live
#include "alus/a11y/atspi_backend.hpp"

#include "alus/a11y/object.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/cached.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <string>
#include <utility>
#include <vector>

using namespace alus::a11y;
using namespace alus::testing;
using Clock = std::chrono::steady_clock;

namespace {

struct LiveFixture {
    LiveFixture() {
        AtspiBackend::Options options;
        options.mainContext = loop.loop().context();
        options.callTimeout = std::chrono::milliseconds{500};
        backend = std::make_unique<AtspiBackend>(options);
        available = backend->initialize(&error);
    }

    LoopFixture loop;
    std::unique_ptr<AtspiBackend> backend;
    bool available = false;
    std::string error;
};

std::size_t countSubtree(Object& object, int depth, std::size_t budget) {
    if (depth <= 0 || budget == 0) {
        return 0;
    }
    std::size_t seen = 1;
    for (const ObjectPtr& child : object.children()) {
        if (child == nullptr || seen >= budget) {
            break;
        }
        seen += countSubtree(*child, depth - 1, budget - seen);
    }
    return seen;
}

}  // namespace

TEST_CASE("libatspi starts and the desktop can be read", "[live][a11y]") {
    LiveFixture fixture;
    if (!fixture.available) {
        // SKIP and not WARN + SUCCEED: the TAP reporter counts a warning as
        // its own test point, the plan line then disagrees with the count,
        // and meson reports the whole executable as ERROR -- turning the
        // graceful skip this file promises into a failure (FAZA7.md N65).
        SKIP("no accessibility bus: " + fixture.error);
    }
    std::printf("AT-SPI %s\n", AtspiBackend::platformVersion().c_str());

    Registry registry{fixture.loop.loop(), *fixture.backend};
    std::string error;
    REQUIRE(registry.start(&error));

    const ObjectPtr desktop = registry.desktop();
    REQUIRE(desktop != nullptr);
    CHECK(desktop->isAlive());
    std::printf("desktop: %s, role %s, %d applications\n", desktop->name().c_str(),
                roleName(desktop->role()), desktop->childCount());
    // A session with no applications at all would mean the bus is there and
    // nothing is talking to it, which is worth seeing in the log rather than
    // failing over.
    if (desktop->childCount() == 0) {
        WARN("the accessibility bus has no applications on it");
        return;
    }

    SECTION("every application answers for its toolkit") {
        for (const ObjectPtr& application : desktop->children()) {
            if (application == nullptr) {
                continue;
            }
            const AppInfo info = application->appInfo();
            std::printf("  %-28s toolkit %-12s pid %d\n", application->name().c_str(),
                        info.toolkitName.c_str(), info.processId);
            // The toolkit name is the primary key for app modules and overlay
            // behaviours (§6.2). An application that does not say it is a
            // finding, not a failure.
            if (info.toolkitName.empty()) {
                WARN("no toolkit name for " << application->name());
            }
        }
    }

    SECTION("the role of every object in reach maps to something") {
        std::size_t objects = 0;
        std::size_t unknown = 0;
        for (const ObjectPtr& application : desktop->children()) {
            if (application == nullptr) {
                continue;
            }
            for (const ObjectPtr& window : application->children()) {
                if (window == nullptr) {
                    continue;
                }
                ++objects;
                if (window->role() == Role::Unknown) {
                    ++unknown;
                    std::printf("  unknown role: %s in %s\n", window->name().c_str(),
                                application->name().c_str());
                }
            }
        }
        std::printf("%zu top-level windows, %zu with an unknown role\n", objects, unknown);
        // Not an assertion about the desktop's contents: a toolkit is allowed
        // to say ATSPI_ROLE_UNKNOWN. What matters is that it is rare.
        CHECK(objects > 0);
    }

    SECTION("the cost of reading a window, with and without the cache") {
        ObjectPtr window;
        for (const ObjectPtr& application : desktop->children()) {
            if (application != nullptr && application->childCount() > 0) {
                window = application->firstChild();
                break;
            }
        }
        if (window == nullptr) {
            WARN("no window to measure");
            return;
        }

        // Cold: one cycle, every property once.
        alus::core::invalidateAllCaches();
        fixture.backend->resetCounters();
        const auto coldStart = Clock::now();
        (void)window->name();
        (void)window->role();
        (void)window->states();
        (void)window->description();
        (void)window->location();
        (void)window->value();
        const auto coldEnd = Clock::now();
        const std::uint64_t coldCalls = fixture.backend->callCount();
        const auto coldMicros =
            std::chrono::duration_cast<std::chrono::microseconds>(coldEnd - coldStart).count();

        // Warm: the same reads, ten times over, in the same cycle.
        fixture.backend->resetCounters();
        const auto warmStart = Clock::now();
        for (int round = 0; round < 10; ++round) {
            (void)window->name();
            (void)window->role();
            (void)window->states();
            (void)window->description();
            (void)window->location();
            (void)window->value();
        }
        const auto warmEnd = Clock::now();
        const std::uint64_t warmCalls = fixture.backend->callCount();
        const auto warmMicros =
            std::chrono::duration_cast<std::chrono::microseconds>(warmEnd - warmStart).count();

        std::printf("six properties: cold %llu calls in %lld us; "
                    "sixty reads warm: %llu calls in %lld us\n",
                    static_cast<unsigned long long>(coldCalls),
                    static_cast<long long>(coldMicros),
                    static_cast<unsigned long long>(warmCalls),
                    static_cast<long long>(warmMicros));
        // THE POINT OF THE WHOLE CACHING DESIGN: sixty reads in one cycle
        // cost nothing on the bus.
        CHECK(warmCalls == 0);
        CHECK(coldCalls > 0);
    }

    SECTION("how long a bounded tree walk takes") {
        const auto start = Clock::now();
        const std::size_t seen = countSubtree(*desktop, 4, 400);
        const auto micros =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
        std::printf("%zu objects four levels deep in %lld us (%lld us each), %llu calls\n", seen,
                    static_cast<long long>(micros),
                    static_cast<long long>(seen > 0 ? micros / static_cast<long long>(seen) : 0),
                    static_cast<unsigned long long>(fixture.backend->callCount()));
        CHECK(seen > 0);
    }

    SECTION("the event subscription is accepted") {
        // start() already subscribed; what this records is how many of the 38
        // types the registry accepted, which is the number that decides
        // whether ALUS hears anything at all.
        CHECK(registry.isRunning());
        // Pump for a few seconds: on a live desktop something is almost
        // always happening (a terminal printing, a clock, a blinking caret),
        // and anything that arrives proves the whole path -- the a11y bus, the
        // subscription, OUR main context, the translation of the event name,
        // the limiter, and the dispatcher. If this is zero, ALUS is deaf and
        // every criterion above it is meaningless.
        //
        // The duration is settable because the right answer depends on the
        // desktop: ALUS_LIVE_EVENT_MS=10000 while typing somewhere is how to
        // see the path work on a quiet machine.
        int windowMs = 3000;
        if (const char* fromEnvironment = std::getenv("ALUS_LIVE_EVENT_MS")) {
            windowMs = std::atoi(fromEnvironment);
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds{windowMs};
        while (Clock::now() < deadline) {
            if (!fixture.loop.loop().iterate(false)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            registry.pumpEvents();
        }
        std::printf("%llu events dispatched in %d ms (%llu reached the limiter)\n",
                    static_cast<unsigned long long>(registry.dispatchedCount()), windowMs,
                    static_cast<unsigned long long>(registry.dispatchedCount() +
                                                    registry.limiter().discarded()));
        if (registry.dispatchedCount() == 0 && registry.limiter().discarded() == 0) {
            // Not a failure: a headless or completely idle desktop sends
            // nothing. It IS worth a warning, because the same silence is
            // what a broken event path looks like.
            WARN("no AT-SPI events arrived in " << windowMs
                                                << " ms; run with ALUS_LIVE_EVENT_MS=10000 "
                                                   "and type somewhere to see the path work");
        }
        SUCCEED("the subscription was accepted");
    }

    registry.stop();
}

// ---------------------------------------------------------------------------
// T6.7 -- quick-nav latency, against the browser that is actually running.
//
// WHY THIS TEST EXISTS AND WHY IT IS HERE AND NOT IN test_browse_mode.cpp.
// The hermetic T6.7 test counts D-Bus calls against the MOCK backend, and the
// mock implements `matches`/`matchesFrom` directly. The translation from
// ALUS's MatchRule to an AtspiMatchRule -- the layer the mock REPLACES -- is
// where three separate defects lived at once, and all three made quick
// navigation find NOTHING on every real web page while all 397 browse-mode
// subtests passed:
//
//   1. `traverse` was FALSE, so libatspi considered only the document root's
//      immediate children;
//   2. a MatchType the caller left unset became MATCH_INVALID, which no
//      provider implements as "criterion unused" -- Chrome matched nothing;
//   3. `GetMatches` with sortBy=FLOW returns an empty array in Chrome, where
//      `GetMatchesFrom` honours FLOW.
//
// A mock cannot catch any of them. Only a real provider can, so T6.7 is
// measured here, and it SKIPS rather than fails when no browser is open.
// ---------------------------------------------------------------------------

// Count a role by WALKING the tree, with no Collection call anywhere. This is
// the independent oracle the T6.7 test needs: if it says the page has links
// and `matches()` says it has none, that is a DEFECT and not an empty tab.
// Deciding "is there a page open" with the code under test is what let three
// defects hide behind a graceful skip.
std::size_t walkCount(Backend& backend, Ref node, PlatformRole role, int depth,
                      std::size_t* budget) {
    if (node == 0 || depth <= 0 || *budget == 0) {
        return 0;
    }
    --*budget;
    std::size_t total = backend.role(node) == role ? 1 : 0;
    const int children = std::min(backend.childCount(node), 100);
    for (int i = 0; i < children; ++i) {
        const Ref child = backend.childAt(node, i);
        if (child == 0) {
            continue;
        }
        total += walkCount(backend, child, role, depth - 1, budget);
        backend.release(child);
    }
    return total;
}

TEST_CASE("T6.7 quick navigation finds elements and answers fast", "[live][a11y][T6.7]") {
    LiveFixture fixture;
    if (!fixture.available) {
        SKIP("no accessibility bus: " + fixture.error);
    }

    // Find every web document on this desktop. Breadth first and budgeted: a
    // desktop with a dozen applications must not turn this into a tree walk.
    std::vector<Ref> documents;
    std::vector<Ref> queue{fixture.backend->desktop()};
    for (std::size_t head = 0; head < queue.size() && head < 4000; ++head) {
        const Ref node = queue[head];
        if (node == 0) {
            continue;
        }
        if (fixture.backend->role(node) == PlatformRole::DocumentWeb) {
            // Its subtree is the page; nothing below it is another document.
            documents.push_back(node);
            continue;
        }
        const int children = std::min(fixture.backend->childCount(node), 60);
        for (int i = 0; i < children; ++i) {
            if (const Ref child = fixture.backend->childAt(node, i); child != 0) {
                queue.push_back(child);
            }
        }
    }
    if (documents.empty()) {
        SKIP("no web document on this desktop: open a page in a browser and run again");
    }

    // Measure against the BIGGEST page open, not the first one found. A chat
    // widget in another tab satisfies "has links" and would make the p95 look
    // better than a real article does, which is the opposite of the point.
    // Chosen BY THE WALK, so that a Collection that answers nothing cannot
    // disguise itself as an empty desktop.
    Ref document = 0;
    std::size_t walkedLinks = 0;
    for (const Ref candidate : documents) {
        std::size_t budget = 6000;
        const std::size_t count =
            walkCount(*fixture.backend, candidate, PlatformRole::Link, 20, &budget);
        if (count > walkedLinks) {
            walkedLinks = count;
            document = candidate;
        }
    }
    const std::string name = document == 0 ? std::string{} : fixture.backend->name(document);

    // A document with almost no links is a blank tab, not a measurement. This
    // is the ONLY skip, and it is decided by the walk.
    if (walkedLinks < 5) {
        SKIP("the biggest document open has " + std::to_string(walkedLinks) +
             " links by tree walk: open a content page (a Wikipedia article) and run again");
    }
    std::printf("T6.7 document: \"%s\", %zu links by tree walk (%zu documents open)\n",
                name.c_str(), walkedLinks, documents.size());

    SECTION("the elements list is not empty") {
        // This is defect 1 and 3 above: both made this list empty, and an
        // empty list is indistinguishable from "this page has no links"
        // unless something asserts otherwise against a real page.
        MatchRule rule;
        rule.roles = {PlatformRole::Link};
        rule.roleMatch = MatchType::Any;
        // BOTH orders, because they are two different paths now:
        //   Canonical is what QuickNav::all actually asks for (N90), and
        //   Flow is what exercises the FLOW -> CANONICAL fallback inside
        //   AtspiBackend::matches, which is dead code the day it stops being
        //   tested.
        for (const auto& [label, order] :
             {std::pair{"Canonical", SortOrder::Canonical}, std::pair{"Flow", SortOrder::Flow}}) {
            const std::vector<Ref> found = fixture.backend->matches(document, rule, order, 0);
            // The walk already proved the links are there. Not an exact
            // equality: the walk is depth- and breadth-budgeted and Collection
            // is not, so Collection may legitimately see MORE. Seeing none, or
            // a small fraction, is the defect.
            std::printf("T6.7 elements list (%s): %zu links by Collection, %zu by walk\n", label,
                        found.size(), walkedLinks);
            CHECK(found.size() >= walkedLinks / 2);
            for (const Ref ref : found) {
                fixture.backend->release(ref);
            }
        }
    }

    SECTION("a keypress answers in under 20 ms at p95") {
        // The criterion itself. `count=1` and `from` the document root is
        // exactly what one press of `k` does.
        MatchRule rule;
        rule.roles = {PlatformRole::Link};
        rule.roleMatch = MatchType::Any;
        std::vector<double> milliseconds;
        std::size_t found = 0;
        for (int i = 0; i < 20; ++i) {
            const auto start = Clock::now();
            const std::vector<Ref> result = fixture.backend->matchesFrom(
                document, document, rule, SortOrder::Flow, TreeTraversal::InOrder, 1, true);
            milliseconds.push_back(
                std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            found = result.size();
            for (const Ref ref : result) {
                fixture.backend->release(ref);
            }
        }
        // Defect 2 made this zero on every real page.
        CHECK(found == 1);
        std::sort(milliseconds.begin(), milliseconds.end());
        const double p95 = milliseconds[static_cast<std::size_t>(
            0.95 * static_cast<double>(milliseconds.size() - 1))];
        std::printf("T6.7 p95 %.2f ms over %zu presses (min %.2f, max %.2f)\n", p95,
                    milliseconds.size(), milliseconds.front(), milliseconds.back());
        CHECK(p95 < 20.0);
    }

    fixture.backend->release(document);
}
