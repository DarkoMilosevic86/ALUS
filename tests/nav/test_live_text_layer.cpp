// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 5 live check: the text layer against the real toolkits.
//
// The mock platform can count calls and make a toolkit lie on command. What
// it cannot tell is WHICH GRANULARITIES THE REAL TOOLKITS ANSWER, what their
// text attributes are really called, and what flat screen review costs on a
// real window -- and those three answers are what §7.2 and §10 are built on.
//
// It reads and measures only: no caret is moved, no selection changed, no
// window activated, no key taken. Without an accessibility bus it SKIPS
// rather than fails.
//
//     meson test -C build --suite live
#include "alus/a11y/atspi_backend.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_range.hpp"
#include "alus/core/cached.hpp"
#include "alus/core/loop.hpp"
#include "alus/nav/screen_review.hpp"
#include "alus/text/segment.hpp"
#include "support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

using namespace alus::a11y;
using namespace alus::testing;
using Clock = std::chrono::steady_clock;

namespace {

struct LiveTextFixture {
    LiveTextFixture() {
        AtspiBackend::Options options;
        options.mainContext = loop.loop().context();
        options.callTimeout = std::chrono::milliseconds{500};
        backend = std::make_unique<AtspiBackend>(options);
        available = backend->initialize(&error);
        if (available) {
            registry = std::make_unique<Registry>(loop.loop(), *backend);
            started = registry->start(&error);
        }
    }

    ~LiveTextFixture() {
        if (registry != nullptr) {
            registry->stop();
        }
        alus::core::invalidateAllCaches();
    }

    // The first object in the desktop tree with text in it, which is the only
    // honest way to find one: what is on this desktop is not ours to choose.
    ObjectPtr findTextObject(const ObjectPtr& from, int depth = 0,
                             int budget = 400) const {
        if (from == nullptr || depth > 6 || budget <= 0) {
            return nullptr;
        }
        if (from->hasInterface(Interface::Text) && from->characterCount() > 0) {
            return from;
        }
        int remaining = budget - 1;
        for (const ObjectPtr& child : from->children()) {
            if (const ObjectPtr found = findTextObject(child, depth + 1, remaining)) {
                return found;
            }
            remaining -= 8;
            if (remaining <= 0) {
                break;
            }
        }
        return nullptr;
    }

    LoopFixture loop;
    std::unique_ptr<AtspiBackend> backend;
    std::unique_ptr<Registry> registry;
    bool available = false;
    bool started = false;
    std::string error;
};

}  // namespace

TEST_CASE("live: the segmenter in this build", "[live][text]") {
    if (alus::text::Segmenter::icuAvailable()) {
        std::printf("# ICU %s\n", alus::text::Segmenter::icuVersion().c_str());
        SUCCEED("ICU is linked in");
    } else {
        std::printf("# ICU IS NOT LINKED IN: Japanese and Thai word navigation "
                    "are wrong in this build (T5.3)\n");
        SUCCEED("the rule-based fallback is in use");
    }
}

TEST_CASE("live: which granularities the real toolkits answer", "[live][text]") {
    LiveTextFixture fixture;
    if (!fixture.available || !fixture.started) {
        std::printf("# skipped: %s\n", fixture.error.c_str());
        SUCCEED("no accessibility bus");
        return;
    }
    const ObjectPtr desktop = fixture.registry->desktop();
    REQUIRE(desktop != nullptr);

    struct Row {
        const char* name;
        TextGranularity value;
    };
    static const Row kRows[] = {
        {"char", TextGranularity::Character},
        {"word", TextGranularity::Word},
        {"sentence", TextGranularity::Sentence},
        {"line", TextGranularity::Line},
        {"paragraph", TextGranularity::Paragraph},
    };

    std::size_t examined = 0;
    for (const ObjectPtr& application : desktop->children()) {
        if (application == nullptr) {
            continue;
        }
        const ObjectPtr holder = fixture.findTextObject(application);
        if (holder == nullptr) {
            continue;
        }
        ++examined;
        std::string answers;
        for (const Row& row : kRows) {
            const bool answered =
                fixture.backend->stringAtOffset(holder->ref(), 0, row.value).has_value();
            answers += " ";
            answers += row.name;
            answers += answered ? "=yes" : "=no";
        }
        std::printf("# %-24s %-10s%s\n", application->name().c_str(),
                    application->appInfo().toolkitName.c_str(), answers.c_str());
        // WHATEVER THE TOOLKIT ANSWERS, the text layer must produce a line.
        // That is the half of T5.2 that matters: the platform's answer and the
        // ported search must not disagree about whether there IS one.
        const alus::text::TextRangePtr range =
            holder->makeTextRange(alus::text::Position::First);
        REQUIRE(range != nullptr);
        CHECK(range->expand(alus::text::TextUnit::Line));
        CHECK(range->expand(alus::text::TextUnit::Word));
        CHECK(range->expand(alus::text::TextUnit::Character));
    }
    std::printf("# %zu application(s) with text examined\n", examined);
    SUCCEED("the granularity table was measured");
}

TEST_CASE("live: the text attributes the toolkits really use", "[live][text]") {
    LiveTextFixture fixture;
    if (!fixture.available || !fixture.started) {
        SUCCEED("no accessibility bus");
        return;
    }
    const ObjectPtr desktop = fixture.registry->desktop();
    REQUIRE(desktop != nullptr);
    std::size_t normalised = 0;
    for (const ObjectPtr& application : desktop->children()) {
        const ObjectPtr holder = fixture.findTextObject(application);
        if (holder == nullptr) {
            continue;
        }
        const AttributeRun run = fixture.backend->attributeRun(holder->ref(), 0, true);
        const alus::text::FormatField field = normaliseTextAttributes(run.attributes);
        normalised += field.values().size();
        std::printf("# %-24s %zu raw -> %zu normalised, run [%d,%d)\n",
                    application->name().c_str(), run.attributes.size(),
                    field.values().size(), run.start, run.end);
        // The RANGE is what the format reporting walks: an empty run would
        // make getTextWithFields step one byte at a time.
        if (!run.attributes.empty()) {
            CHECK(run.end >= run.start);
        }
    }
    std::printf("# %zu normalised attribute(s) in total\n", normalised);
    SUCCEED("the normaliser was measured");
}

TEST_CASE("live: flat screen review of the active window, and what it costs",
          "[live][text]") {
    LiveTextFixture fixture;
    if (!fixture.available || !fixture.started) {
        SUCCEED("no accessibility bus");
        return;
    }
    const ObjectPtr window = fixture.registry->activeWindow();
    if (window == nullptr) {
        std::printf("# no active window found\n");
        SUCCEED("nothing to flatten");
        return;
    }

    const std::uint64_t before = fixture.backend->callCount();
    const auto started = Clock::now();
    auto screen = alus::nav::ScreenReviewRange::make(window, alus::text::Position::First);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started);
    const std::uint64_t calls = fixture.backend->callCount() - before;

    if (screen == nullptr) {
        std::printf("# \"%s\" has no screen content\n", window->name().c_str());
        SUCCEED("no content");
        return;
    }
    std::printf("# \"%s\": %zu pieces, %d rows, %zu objects, %llu calls, %lld us%s\n",
                window->name().c_str(), screen->pieces().size(),
                screen->lineCount().value_or(0), screen->visitedObjects(),
                static_cast<unsigned long long>(calls),
                static_cast<long long>(elapsed.count()),
                screen->hitLimit() ? " (BUDGET REACHED)" : "");

    // The spatial order really is top to bottom, left to right. This is T5.9
    // against a real window instead of a built one.
    const std::vector<alus::nav::ScreenReviewRange::Piece>& pieces = screen->pieces();
    for (std::size_t index = 1; index < pieces.size(); ++index) {
        const alus::core::Rect& previous = pieces[index - 1].rect;
        const alus::core::Rect& current = pieces[index].rect;
        const bool sameRow = current.y + current.height / 2 < previous.bottom() &&
                             previous.y + previous.height / 2 < current.bottom();
        if (sameRow) {
            CHECK(current.x >= previous.x);
        } else {
            CHECK(current.y >= previous.y);
        }
    }

    // THE SNAPSHOT IS SHARED: copying the range must cost no platform calls at
    // all, which is what makes the review commands cheap (they copy on every
    // move).
    const std::uint64_t beforeCopy = fixture.backend->callCount();
    const alus::text::TextRangePtr copy = screen->copy();
    CHECK(fixture.backend->callCount() == beforeCopy);
    REQUIRE(copy != nullptr);
    CHECK(copy->expand(alus::text::TextUnit::Line));
}

TEST_CASE("live: the focused object's units, if anything has the focus",
          "[live][text]") {
    LiveTextFixture fixture;
    if (!fixture.available || !fixture.started) {
        SUCCEED("no accessibility bus");
        return;
    }
    const ObjectPtr focus = fixture.registry->api().focus();
    if (focus == nullptr) {
        std::printf("# nothing has the focus\n");
        SUCCEED("no focus");
        return;
    }
    std::printf("# focus: %s (%s), navigable text: %s\n", focus->name().c_str(),
                roleName(focus->role()), focus->hasNavigableText() ? "yes" : "no");
    const alus::text::TextRangePtr caret =
        focus->makeTextRange(alus::text::Position::Caret);
    if (caret == nullptr) {
        SUCCEED("the focused object has no text interface");
        return;
    }
    // Every unit the text layer claims to support has to answer on a real
    // object, and the answers have to nest: a character is inside its word,
    // which is inside its line.
    alus::text::TextRangePtr character = caret->copy();
    alus::text::TextRangePtr word = caret->copy();
    alus::text::TextRangePtr line = caret->copy();
    REQUIRE(character->expand(alus::text::TextUnit::Character));
    REQUIRE(word->expand(alus::text::TextUnit::Word));
    REQUIRE(line->expand(alus::text::TextUnit::Line));
    CHECK(word->compareEndPoints(*character, alus::text::EndPointPair::StartToStart) <= 0);
    CHECK(line->compareEndPoints(*word, alus::text::EndPointPair::StartToStart) <= 0);
    std::printf("# character \"%s\", word \"%s\", line %zu bytes\n",
                character->text().c_str(), word->text().c_str(), line->text().size());

    // And the number the whole caching design exists for: reading the same
    // property again inside one cycle costs NOTHING. Each one is read once
    // first, because the FIRST read is the one that pays (T4.2 measured this
    // for the object model; the point here is that the text layer did not
    // break it).
    (void)focus->characterCount();
    (void)focus->name();
    (void)focus->role();
    const std::uint64_t before = fixture.backend->callCount();
    for (int repeat = 0; repeat < 20; ++repeat) {
        (void)focus->characterCount();
        (void)focus->name();
        (void)focus->role();
    }
    CHECK(fixture.backend->callCount() == before);
}
