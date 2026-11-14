// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T5.7 — say all over 100 lines: continuous, with no pause per line, and the
//        caret following the sound to within one line.
// T5.8 — say all stops on a key press in under 50 ms.
#include "alus/nav/say_all.hpp"

#include "alus/a11y/events.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "nav/support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

using alus::nav::SayAll;
using alus::nav::SayAllCursor;
using alus::testing::DocumentTree;
using alus::testing::NavFixture;

namespace {

std::string hundredLines() {
    std::string text;
    for (int index = 1; index <= 100; ++index) {
        // Deliberately WITHOUT sentence-ending punctuation: that is the hard
        // case for the sentence buffer, and the one where a naive say-all
        // stutters once per line.
        text += "line number " + std::to_string(index) + "\n";
    }
    return text;
}

}  // namespace

namespace {

// Whether Memcheck is running, as its own test setup announces.
//
// WHY A LATENCY ASSERTION HAS TO ASK. 02-FAZE.md rule 2 says a latency budget
// is a test and not a note, and T5.8's budget is 50 ms from a key press to
// say-all stopping. Under Memcheck every instruction costs 20 to 50 times
// more, so the same code measures 90 ms and the assertion fails for a reason
// that has nothing to do with the code -- the build already says this about
// T2.11, in the comment on the `live` suite in meson.build.
//
// So under valgrind the number is REPORTED and not asserted. It is still
// measured, and it is still asserted in every other run, which is the one that
// resembles a user's machine.
bool underValgrind() {
    const char* value = std::getenv("ALUS_UNDER_VALGRIND");
    return value != nullptr && *value != '\0' && *value != '0';
}

void checkStopLatency(std::chrono::microseconds elapsed) {
    if (underValgrind()) {
        // SUCCEED and not WARN: Catch2's TAP reporter emits a line for a WARN
        // but leaves it out of the plan count, so `1..531` with 533 `ok` lines
        // makes meson report the whole executable as an ERROR. SUCCEED is a
        // counted passing assertion that still carries the number.
        SUCCEED("stop latency " << elapsed.count()
                                << " us; not asserted under valgrind, where T5.8's "
                                   "50 ms budget is meaningless");
        return;
    }
    CHECK(elapsed < std::chrono::milliseconds{50});
}

}  // namespace

TEST_CASE("T5.7 say all reads a hundred lines and the caret follows",
          "[nav][sayall][T5.7]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, hundredLines());
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setFocusObject(document));
    // Line by line, so that "one line" is a meaningful unit to measure
    // against (the default reading chunk is the paragraph).
    fixture.config().setString("documentNavigation", "sayAllReadingUnit", "line");
    fixture.nav().terminate();
    fixture.nav().initialize();
    REQUIRE(fixture.registry.api().setFocusObject(document));
    fixture.clearSpeech();
    fixture.backend.caretsSet.clear();

    REQUIRE(fixture.sayAll().readText(SayAllCursor::Caret));
    CHECK(fixture.sayAll().isRunning());

    for (int cycle = 0; cycle < 4000 && fixture.sayAll().isRunning(); ++cycle) {
        fixture.loop().iterateUntilIdle();
    }
    fixture.drainSpeech();

    // Every line was read.
    CHECK(fixture.sayAll().linesRead() >= 100);
    const std::string spoken = fixture.spokenJoined();
    CHECK(spoken.find("line number 1 ") != std::string::npos);
    CHECK(spoken.find("line number 100") != std::string::npos);

    // CONTINUITY: the sentence buffer held text until a sentence boundary, so
    // far FEWER utterances went to the synthesizer than there were lines.
    // One utterance per line is exactly the stutter §4.4 exists to prevent.
    const std::size_t utterances = fixture.synth().utterances().size();
    CHECK(utterances < 100);

    // THE CARET FOLLOWED THE SOUND. The caret is moved when a line STARTS
    // being spoken, so the offsets recorded by the platform are the line
    // starts, in order, and never more than one line apart.
    REQUIRE(fixture.backend.caretsSet.size() >= 99);
    int previous = -1;
    for (const auto& set : fixture.backend.caretsSet) {
        CHECK(set.offset >= previous);
        previous = set.offset;
    }
    // And it ended on the last line rather than at the end of the document.
    const std::string& text = fixture.backend.node(tree.document).text;
    const std::size_t lastLineStart = text.rfind('\n', text.size() - 2) + 1;
    CHECK(static_cast<std::size_t>(fixture.backend.caretsSet.back().offset) <=
          lastLineStart);
    CHECK(static_cast<std::size_t>(fixture.backend.caretsSet.back().offset) >=
          lastLineStart - std::string("line number 100\n").size());
}

TEST_CASE("T5.7 say all from the review cursor moves the review cursor",
          "[nav][sayall][T5.7]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, "alpha\nbeta\ngamma\n");
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setNavigatorObject(document));
    REQUIRE(fixture.review().position() != nullptr);
    fixture.backend.caretsSet.clear();

    REQUIRE(fixture.sayAll().readText(SayAllCursor::Review));
    for (int cycle = 0; cycle < 2000 && fixture.sayAll().isRunning(); ++cycle) {
        fixture.loop().iterateUntilIdle();
    }
    fixture.drainSpeech();

    CHECK(fixture.spokenJoined().find("gamma") != std::string::npos);
    // The REVIEW cursor moved, and the system caret was left alone: that is
    // the whole difference between numpadPlus and alus+downArrow.
    CHECK(fixture.backend.caretsSet.empty());
    alus::text::TextRange* position = fixture.review().position();
    REQUIRE(position != nullptr);
    alus::text::TextRangePtr line = position->copy();
    line->expand(alus::text::TextUnit::Line);
    CHECK(line->text() == "gamma\n");
}

TEST_CASE("T5.8 a key press stops say all at once", "[nav][sayall][T5.8]") {
    NavFixture fixture;
    DocumentTree tree(fixture.backend, hundredLines());
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setFocusObject(document));

    REQUIRE(fixture.sayAll().readText(SayAllCursor::Caret));
    // Let it get going, but not finish.
    for (int cycle = 0; cycle < 5; ++cycle) {
        fixture.loop().iterate(/*mayBlock=*/false);
    }
    REQUIRE(fixture.sayAll().isRunning());
    const std::size_t readBefore = fixture.sayAll().linesRead();

    const auto started = std::chrono::steady_clock::now();
    fixture.sayAll().stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);

    CHECK_FALSE(fixture.sayAll().isRunning());
    // Stopping is bookkeeping, not a round trip: far under the 50 ms budget.
    checkStopLatency(elapsed);

    // AND IT REALLY STOPS. The pipeline had lines buffered ahead of the
    // voice; none of them may be read after the stop. Without the generation
    // counter in SayAll, the callbacks still in flight would carry on.
    for (int cycle = 0; cycle < 200; ++cycle) {
        fixture.loop().iterateUntilIdle();
    }
    CHECK(fixture.sayAll().linesRead() == readBefore);
    CHECK_FALSE(fixture.sayAll().isRunning());
}

TEST_CASE("T5.8 the key event hook stops say all before the script runs",
          "[nav][sayall][T5.8]") {
    // The wiring, not only the stop: phase 3 left `isSayAllRunning` empty and
    // asked phase 5 to fill it (FAZA4.md handover point 5). This is a real
    // input service with a real GestureManager, fed a real key event.
    alus::testing::QuietLog quiet;
    alus::testing::NavFixture fixture;
    DocumentTree tree(fixture.backend, hundredLines());
    const auto document = fixture.registry.objectFor(tree.document);
    REQUIRE(fixture.registry.api().setFocusObject(document));

    alus::config::Manager config;
    alus::input::Service::registerDefaults(config);
    alus::input::GestureManager::Hooks hooks;
    alus::input::InputServiceOptions options;
    // `noInput` keeps the compositor out of it: the test feeds the key event
    // straight to the manager, which is exactly what the backend would do,
    // and nothing competes with the running screen reader for the keyboard
    // (finding N8).
    options.noInput = true;
    options.dataDir = std::filesystem::path{ALUS_TEST_DATA_DIR};
    alus::input::Service input{fixture.loop(), config, hooks, options};
    input.initialize();
    REQUIRE(input.manager() != nullptr);

    alus::nav::Service nav{fixture.loop(), fixture.config(),
                           [&fixture] { return &fixture.registry; }, &input,
                           [&fixture] { return &fixture.presenter(); }};
    nav.initialize();
    REQUIRE(nav.sayAll() != nullptr);
    REQUIRE(nav.sayAll()->readText(SayAllCursor::Caret));
    for (int cycle = 0; cycle < 5; ++cycle) {
        fixture.loop().iterate(/*mayBlock=*/false);
    }
    REQUIRE(nav.sayAll()->isRunning());

    alus::input::KeyEvent event;
    event.pressed = true;
    event.keycode = 38;   // `a` on a PC keyboard
    event.keysym = 'a';
    event.unichar = 'a';
    const auto started = std::chrono::steady_clock::now();
    input.manager()->onKeyEvent(event);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);

    CHECK_FALSE(nav.sayAll()->isRunning());
    checkStopLatency(elapsed);
    nav.terminate();
    input.terminate();
}

TEST_CASE("say all falls back to reading objects when there is no text",
          "[nav][sayall]") {
    NavFixture fixture;
    const alus::a11y::Ref application = fixture.backend.addApplication("Dialog", "gtk");
    const alus::a11y::Ref dialog =
        fixture.backend.add(application, alus::a11y::PlatformRole::Dialog, "Preferences");
    fixture.backend.addState(dialog, alus::a11y::PlatformState::Active);
    fixture.backend.add(dialog, alus::a11y::PlatformRole::Button, "Close");
    fixture.backend.add(dialog, alus::a11y::PlatformRole::CheckBox, "Remember me");

    const auto object = fixture.registry.objectFor(dialog);
    REQUIRE(fixture.registry.api().setFocusObject(object));
    // No text at all: readText refuses, which is what makes the command fall
    // back to the object reader.
    CHECK_FALSE(fixture.sayAll().readText(SayAllCursor::Caret));
    REQUIRE(fixture.sayAll().readObjects(object));

    for (int cycle = 0; cycle < 500 && fixture.sayAll().isRunning(); ++cycle) {
        fixture.loop().iterateUntilIdle();
        fixture.drainSpeech();
    }
    const std::string spoken = fixture.spokenJoined();
    CHECK(spoken.find("Close") != std::string::npos);
    CHECK(spoken.find("Remember me") != std::string::npos);
}

TEST_CASE("say all goes on into the next paragraph object, and the caret with it",
          "[nav][sayall]") {
    // FAZA10.md N127, LibreOffice Writer: every paragraph is an object of its
    // own, linked by FLOWS_TO, and say-all stopped after the first line.
    NavFixture fixture;
    auto& backend = fixture.backend;
    const alus::a11y::Ref application = backend.addApplication("soffice", "VCL");
    const alus::a11y::Ref window = backend.add(application, alus::a11y::PlatformRole::Frame, "Book - Writer");
    backend.addState(window, alus::a11y::PlatformState::Active);
    const alus::a11y::Ref page = backend.add(window, alus::a11y::PlatformRole::DocumentText, "Book");
    std::vector<alus::a11y::Ref> paragraphs;
    for (const char* const text : {"The first paragraph.", "", "The third paragraph.",
                                   "The last paragraph."}) {
        const alus::a11y::Ref paragraph = backend.add(page, alus::a11y::PlatformRole::Paragraph, {});
        alus::testing::MockNode& node = backend.node(paragraph);
        node.text = text;
        node.caret = 0;
        node.interfaces |= alus::a11y::Interface::Text;
        node.interfaces |= alus::a11y::Interface::EditableText;
        backend.addState(paragraph, alus::a11y::PlatformState::Editable);
        backend.addState(paragraph, alus::a11y::PlatformState::Focusable);
        paragraphs.push_back(paragraph);
    }
    // The empty second paragraph is linked like any other; say-all skips it.
    for (std::size_t i = 0; i + 1 < paragraphs.size(); ++i) {
        backend.node(paragraphs[i]).relations[alus::a11y::PlatformRelation::FlowsTo] = {paragraphs[i + 1]};
    }

    const auto first = fixture.registry.objectFor(paragraphs.front());
    REQUIRE(fixture.registry.api().setFocusObject(first));
    fixture.clearSpeech();
    fixture.backend.caretsSet.clear();

    REQUIRE(fixture.sayAll().readText(SayAllCursor::Caret));
    for (int cycle = 0; cycle < 2000 && fixture.sayAll().isRunning(); ++cycle) {
        fixture.loop().iterateUntilIdle();
    }
    fixture.drainSpeech();

    const std::string spoken = fixture.spokenJoined();
    CHECK(spoken.find("The first paragraph") != std::string::npos);
    CHECK(spoken.find("The third paragraph") != std::string::npos);
    CHECK(spoken.find("The last paragraph") != std::string::npos);
    // The caret went into the paragraphs as they were read, ending in the
    // last one -- where a key press would have left the user.
    REQUIRE_FALSE(fixture.backend.caretsSet.empty());
    CHECK(fixture.backend.caretsSet.back().ref == paragraphs.back());
}

TEST_CASE("the focus say-all moves through paragraphs is not announced", "[nav][sayall]") {
    // FAZA10.md N128, LibreOffice Writer: the focus follows the caret into
    // every paragraph, and announcing it read each paragraph twice, with
    // "paragraph, multi line, editable" before it.
    NavFixture fixture;
    auto& backend = fixture.backend;
    const alus::a11y::Ref application = backend.addApplication("soffice", "VCL");
    const alus::a11y::Ref window =
        backend.add(application, alus::a11y::PlatformRole::Frame, "Book - Writer");
    backend.addState(window, alus::a11y::PlatformState::Active);
    const alus::a11y::Ref page =
        backend.add(window, alus::a11y::PlatformRole::DocumentText, "Book");
    // Writer's document is editable, so browse mode stays out of it, as
    // measured (log 16:11:10, "is editable; not intercepting").
    backend.addState(page, alus::a11y::PlatformState::Editable);
    std::vector<alus::a11y::Ref> paragraphs;
    for (const char* const text : {"Alpha one.", "Bravo two.", "Charlie three."}) {
        const alus::a11y::Ref paragraph =
            backend.add(page, alus::a11y::PlatformRole::Paragraph, {});
        alus::testing::MockNode& node = backend.node(paragraph);
        node.text = text;
        node.caret = 0;
        node.interfaces |= alus::a11y::Interface::Text;
        node.interfaces |= alus::a11y::Interface::EditableText;
        backend.addState(paragraph, alus::a11y::PlatformState::Editable);
        backend.addState(paragraph, alus::a11y::PlatformState::Focusable);
        backend.addState(paragraph, alus::a11y::PlatformState::MultiLine);
        paragraphs.push_back(paragraph);
    }
    for (std::size_t i = 0; i + 1 < paragraphs.size(); ++i) {
        backend.node(paragraphs[i]).relations[alus::a11y::PlatformRelation::FlowsTo] = {
            paragraphs[i + 1]};
    }
    REQUIRE(fixture.registry.api().setFocusObject(
        fixture.registry.objectFor(paragraphs.front())));
    fixture.clearSpeech();
    backend.caretsSet.clear();

    // The voice takes its time, as a real one does: each index is reached
    // only when the test says so, so the focus events arrive WHILE say-all is
    // reading, which is when Writer sends them.
    fixture.synth().setAutoComplete(false);
    REQUIRE(fixture.sayAll().readText(SayAllCursor::Caret));
    std::size_t focusMovedTo = 0;
    for (int cycle = 0; cycle < 200; ++cycle) {
        fixture.loop().iterateUntilIdle();
        // Writer: the paragraph the caret entered takes the focus.
        if (!backend.caretsSet.empty() && focusMovedTo + 1 < paragraphs.size() &&
            backend.caretsSet.back().ref == paragraphs[focusMovedTo + 1]) {
            ++focusMovedTo;
            backend.addState(paragraphs[focusMovedTo], alus::a11y::PlatformState::Focused);
            backend.emit("object:state-changed:focused", paragraphs[focusMovedTo], 1);
            fixture.loop().iterateUntilIdle();
        }
        const std::vector<int> pending = fixture.synth().pendingIndexes();
        if (pending.empty()) {
            if (!fixture.sayAll().isRunning()) {
                break;
            }
            continue;
        }
        fixture.synth().completeIndex(pending.front(), pending.size() == 1);
    }
    fixture.synth().setAutoComplete(true);
    fixture.drainSpeech();

    REQUIRE(focusMovedTo == 2);
    // The focus did move: stopping leaves the user in the paragraph.
    CHECK(fixture.registry.api().focus()->ref() == paragraphs.back());
    const std::string spoken = fixture.spokenJoined();
    const auto count = [&spoken](const std::string& needle) {
        std::size_t found = 0;
        for (std::size_t at = spoken.find(needle); at != std::string::npos;
             at = spoken.find(needle, at + 1)) {
            ++found;
        }
        return found;
    };
    INFO(spoken);
    CHECK(count("Bravo two") == 1);
    CHECK(count("Charlie three") == 1);
    CHECK(spoken.find("paragraph") == std::string::npos);

    SECTION("after say-all, the focus moving into a paragraph is said again") {
        fixture.clearSpeech();
        backend.addState(paragraphs[0], alus::a11y::PlatformState::Focused);
        backend.emit("object:state-changed:focused", paragraphs[0], 1);
        fixture.loop().iterateUntilIdle();
        fixture.drainSpeech();
        const std::string again = fixture.spokenJoined();
        INFO(again);
        CHECK(again.find("Alpha one") != std::string::npos);
        // Only the text: no role, no states (N128).
        CHECK(again.find("paragraph") == std::string::npos);
        CHECK(again.find("multi") == std::string::npos);
        CHECK(again.find("editable") == std::string::npos);
    }
}
