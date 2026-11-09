// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T5.5 — caret movement detection works in GTK4, Qt6 and LibreOffice; three
//        separate tests, and a different timeout for each is allowed.
//
// 01-ARHITEKTURA.md §8.3: the three strategies are transferred, not
// simplified, because the toolkits lie in DIFFERENT ways. Each test below
// makes one of them lie the way that toolkit really does, and checks that the
// right strategy caught it.
#include "alus/nav/editable_text.hpp"

#include "alus/script/catalog.hpp"
#include "nav/support.hpp"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include <thread>

using alus::nav::CaretDetectionSettings;
using alus::nav::CaretMoveResult;
using alus::nav::CaretWaiter;
using alus::nav::caretTuningFor;
using alus::testing::DocumentTree;
using alus::testing::NavFixture;
using alus::text::Position;
using alus::text::TextUnit;

namespace {

// Drives the loop until the waiter has answered, or until the test gives up.
// A live task makes the loop pump again (core/task.hpp), so this is what the
// daemon does -- not a sleep with a check at the end.
CaretMoveResult waitFor(NavFixture& fixture, CaretWaiter& waiter,
                        std::chrono::milliseconds limit = std::chrono::milliseconds{2000}) {
    CaretMoveResult captured;
    bool done = false;
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done && std::chrono::steady_clock::now() < deadline) {
        fixture.loop().iterateUntilIdle();
        if (!waiter.waiting()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    (void)captured;
    (void)done;
    return captured;
}

}  // namespace

TEST_CASE("T5.5 the per-toolkit tuning differs, and says why", "[nav][caret][T5.5]") {
    // GTK4 emits a prompt caret event: believed, short timeout.
    const CaretDetectionSettings gtk = caretTuningFor("gtk");
    CHECK(gtk.detectionUsesEvents);
    CHECK(gtk.timeout == std::chrono::milliseconds{80});

    // The toolkit name is NOT canonically spelled (finding N41): `GTK` and
    // `gtk` are the same toolkit and must get the same tuning.
    CHECK(caretTuningFor("GTK").timeout == gtk.timeout);

    // Qt6 emits it late: believed, but a longer overall timeout.
    const CaretDetectionSettings qt = caretTuningFor("qt");
    CHECK(qt.detectionUsesEvents);
    CHECK(qt.timeout > gtk.timeout);

    // LibreOffice: events NOT believed, and the multiplier NVDA's own
    // LibreOffice support uses.
    const CaretDetectionSettings office = caretTuningFor("libreoffice");
    CHECK_FALSE(office.detectionUsesEvents);
    CHECK(office.timeoutMultiplier == 2.0);
    CHECK(office.minWordTimeout > gtk.minWordTimeout);

    // Chromium posts them from the renderer, out of order.
    CHECK_FALSE(caretTuningFor("Chromium").detectionUsesEvents);

    // An unknown toolkit gets NVDA's defaults rather than nothing.
    const CaretDetectionSettings unknown = caretTuningFor("some-new-toolkit");
    CHECK(unknown.detectionUsesEvents);
    CHECK(unknown.timeout == std::chrono::milliseconds{100});
}

TEST_CASE("T5.5 GTK4: the caret event is believed when it is prompt",
          "[nav][caret][T5.5]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "first line\nsecond line", "gtk");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr before = document->makeTextRange(Position::Caret);
    REQUIRE(before != nullptr);

    alus::nav::CaretDetectionSettings settings = caretTuningFor("gtk");
    // A GENEROUS window, so that the test measures the RULE and not the wall
    // clock: under valgrind the first task step can land well past 60 ms, and
    // the bookmark would then answer first for the wrong reason.
    settings.useEventsMaxTimeout = std::chrono::seconds{10};
    CaretWaiter waiter(fixture.loop(), settings);
    CaretMoveResult result;
    waiter.start(document, before->bookmark(), std::nullopt,
                 [&result](CaretMoveResult value) { result = std::move(value); });

    // THE CARET IS NOT MOVED. Only the event says it did -- so if the event is
    // believed, this is detected, and if it is not, nothing is. That makes the
    // test about strategy 1 alone, with no timing in it.
    waiter.noteCaretEvent();

    waitFor(fixture, waiter);
    CHECK(result.moved);
    CHECK(result.strategy == "event");
    REQUIRE(result.position != nullptr);
    result.position->expand(TextUnit::Line);
    CHECK(result.position->text() == "first line\n");
}

TEST_CASE("T5.5 GTK4: with events turned off the same event proves nothing",
          "[nav][caret][T5.5]") {
    // The other half of the rule: `caretMovementDetectionUsesEvents` is what
    // the per-toolkit table sets, and turning it off has to really turn it off
    // -- that is what makes the Chromium and LibreOffice entries mean
    // anything.
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "first line\nsecond line", "gtk");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr before = document->makeTextRange(Position::Caret);
    REQUIRE(before != nullptr);

    alus::nav::CaretDetectionSettings settings = caretTuningFor("gtk");
    settings.detectionUsesEvents = false;
    settings.timeout = std::chrono::milliseconds{20};
    CaretWaiter waiter(fixture.loop(), settings);
    CaretMoveResult result;
    waiter.start(document, before->bookmark(), std::nullopt,
                 [&result](CaretMoveResult value) { result = std::move(value); });
    waiter.noteCaretEvent();

    waitFor(fixture, waiter);
    CHECK_FALSE(result.moved);
    CHECK(result.strategy == "timeout");
}

TEST_CASE("T5.5 Qt6: a late event is ignored and the bookmark catches it",
          "[nav][caret][T5.5]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "first line\nsecond line", "qt");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr before = document->makeTextRange(Position::Caret);
    REQUIRE(before != nullptr);

    CaretDetectionSettings settings = caretTuningFor("qt");
    // Make the window tiny so the test does not have to wait 60 ms for it to
    // close: the RULE is what is being tested, not the number.
    settings.useEventsMaxTimeout = std::chrono::milliseconds{0};
    CaretWaiter waiter(fixture.loop(), settings);
    CaretMoveResult result;
    waiter.start(document, before->bookmark(), std::nullopt,
                 [&result](CaretMoveResult value) { result = std::move(value); });

    // The event arrives too late to be about this key press...
    waiter.noteCaretEvent();
    // ...but the caret really did move, so the bookmark finds it.
    fixture.backend.node(tree.document).caret = 11;
    alus::core::invalidateAllCaches();

    waitFor(fixture, waiter);
    CHECK(result.moved);
    CHECK(result.strategy == "bookmark");
}

TEST_CASE("T5.5 LibreOffice: events are not believed at all", "[nav][caret][T5.5]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "first line\nsecond line", "libreoffice");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr before = document->makeTextRange(Position::Caret);
    REQUIRE(before != nullptr);

    CaretWaiter waiter(fixture.loop(), caretTuningFor("libreoffice"));
    CaretMoveResult result;
    waiter.start(document, before->bookmark(), std::nullopt,
                 [&result](CaretMoveResult value) { result = std::move(value); });

    // An event arrives, and is worth nothing here: LibreOffice emits it
    // before the text has settled.
    waiter.noteCaretEvent();
    fixture.loop().iterateUntilIdle();
    // Nothing concluded yet, because the event is not evidence in this toolkit.
    CHECK(waiter.waiting());

    fixture.backend.node(tree.document).caret = 11;
    alus::core::invalidateAllCaches();
    waitFor(fixture, waiter);
    CHECK(result.moved);
    CHECK(result.strategy == "bookmark");
}

TEST_CASE("T5.5 the word under the caret catches a deletion", "[nav][caret][T5.5]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "alpha beta");
    fixture.backend.node(tree.document).caret = 0;
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr before = document->makeTextRange(Position::Caret);
    REQUIRE(before != nullptr);
    alus::text::TextRangePtr word = before->copy();
    word->expand(TextUnit::Word);
    const std::string originalWord = word->text();
    CHECK(originalWord == "alpha ");

    CaretDetectionSettings settings = caretTuningFor("gtk");
    settings.detectionUsesEvents = false;
    settings.minWordTimeout = std::chrono::milliseconds{0};
    CaretWaiter waiter(fixture.loop(), settings);
    CaretMoveResult result;
    waiter.start(document, before->bookmark(), originalWord,
                 [&result](CaretMoveResult value) { result = std::move(value); });

    // Delete: the caret does NOT move, but the text under it changes. Without
    // strategy 3 the deletion would be announced as nothing at all.
    fixture.backend.node(tree.document).text = "lpha beta";
    alus::core::invalidateAllCaches();

    waitFor(fixture, waiter);
    CHECK(result.moved);
    CHECK(result.strategy == "word");
}

TEST_CASE("T5.5 a frozen application costs the timeout and nothing else",
          "[nav][caret][T5.5]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "nothing moves here");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr before = document->makeTextRange(Position::Caret);
    REQUIRE(before != nullptr);

    CaretDetectionSettings settings = caretTuningFor("gtk");
    settings.timeout = std::chrono::milliseconds{30};
    CaretWaiter waiter(fixture.loop(), settings);
    CaretMoveResult result;
    bool called = false;
    waiter.start(document, before->bookmark(), std::nullopt,
                 [&](CaretMoveResult value) {
                     result = std::move(value);
                     called = true;
                 });

    const auto started = std::chrono::steady_clock::now();
    waitFor(fixture, waiter);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    // THE CALLBACK ALWAYS RUNS, however the wait ends: a caller that never
    // hears back would leave the user with no announcement at all.
    CHECK(called);
    CHECK_FALSE(result.moved);
    CHECK(result.strategy == "timeout");
    // And the wait did not block the loop: it took about the timeout, not the
    // minutes a frozen application can hang for.
    CHECK(elapsed < std::chrono::milliseconds{1000});
}

TEST_CASE("a second key press abandons the first wait", "[nav][caret]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "one two three");
    const auto document = fixture.registry.objectFor(tree.document);
    alus::text::TextRangePtr before = document->makeTextRange(Position::Caret);
    REQUIRE(before != nullptr);

    CaretWaiter waiter(fixture.loop(), caretTuningFor("gtk"));
    int firstCalls = 0;
    int secondCalls = 0;
    waiter.start(document, before->bookmark(), std::nullopt,
                 [&firstCalls](CaretMoveResult) { ++firstCalls; });
    // The user pressed another key before the first answer came back. NVDA's
    // `isScriptWaiting` has the same effect: without it, tabbing quickly
    // through a dialog announces every line twice.
    waiter.start(document, before->bookmark(), std::nullopt,
                 [&secondCalls](CaretMoveResult) { ++secondCalls; });

    fixture.backend.node(tree.document).caret = 4;
    alus::core::invalidateAllCaches();
    waitFor(fixture, waiter);
    CHECK(firstCalls == 0);
    CHECK(secondCalls == 1);
}

TEST_CASE("the caret commands are observed, not consumed", "[nav][caret]") {
    // The Wayland rule (editable_text.hpp, point A): a caret key must reach
    // the application, so the script that watches it is marked as
    // pass-through and the input layer does not grab it. Without this the
    // caret would never move at all.
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "one two three");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(document != nullptr);
    // The behaviour chain gave the document the editable-text behaviour, and
    // with it the caret scripts.
    CHECK(document->hasBehavior("editableText.EditableText"));
    const alus::script::Script* script = document->scriptByName("caret_moveByLine");
    REQUIRE(script != nullptr);
    REQUIRE(script->info != nullptr);
    CHECK(script->info->needsKeyPassThrough);
}

TEST_CASE("the generated command table carries the caret and review keys",
          "[nav][caret]") {
    // §5.4: the command table is GENERATED from NVDA, so phase 5 retypes no
    // description and no key assignment. This is the check that the rows the
    // caret and review commands need are really in it.
    alus::script::Catalog catalog;
    REQUIRE(catalog.loadFromDataDirectory(std::filesystem::path{ALUS_TEST_DATA_DIR}));

    const alus::script::ScriptInfo* line =
        catalog.find("editableText.EditableText", "caret_moveByLine");
    REQUIRE(line != nullptr);
    CHECK_FALSE(line->gestures.empty());

    const alus::script::ScriptInfo* review =
        catalog.find("globalCommands.GlobalCommands", "review_nextLine");
    REQUIRE(review != nullptr);
    CHECK_FALSE(review->gestures.empty());
    CHECK_FALSE(review->description.empty());
    // numpad9, which is what the manual test of 02-FAZE.md presses.
    bool hasNumpad9 = false;
    for (const std::string& gesture : review->gestures) {
        if (gesture.find("numpad9") != std::string::npos) {
            hasNumpad9 = true;
        }
    }
    CHECK(hasNumpad9);

    const alus::script::ScriptInfo* sayAll =
        catalog.find("globalCommands.GlobalCommands", "sayAll");
    REQUIRE(sayAll != nullptr);
    CHECK(sayAll->speakOnDemand);
}
