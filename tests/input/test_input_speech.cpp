// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Input against the real speech subsystem — the half of T3.9 that a mock
// cannot answer: a key press has to actually stop speech that is already
// playing, through the pipeline phase 2 built.
//
// This is also the test that proves the two subsystems compose: the input
// library has no dependency on the speech library (see src/meson.build), so
// everything below goes through GestureManager::Hooks, exactly as the daemon
// wires it.
#include "../support/loop_fixture.hpp"
#include "alus/input/manager.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/dictionaries.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/speech/synth.hpp"
#include "alus/text/symbols.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using alus::input::GestureManager;
using alus::input::GestureMap;
using alus::input::NullInputBackend;
using alus::script::Dispatcher;
using alus::speech::NullSynth;
using alus::speech::Presenter;
using alus::speech::SpeechManager;
using alus::testing::keyDown;
using alus::testing::TestScriptable;
namespace keysym = alus::input::keysym;

TEST_CASE("T3.9 a key press interrupts speech that is already playing") {
    alus::testing::LoopFixture fixture;
    NullSynth synth{true};
    SpeechManager speechManager(fixture.loop());
    speechManager.setSynth(&synth);
    alus::text::SymbolRegistry symbols;
    alus::speech::Dictionaries dictionaries;
    Presenter presenter(speechManager, symbols, dictionaries);

    Dispatcher dispatcher(&fixture.loop());
    GestureMap builtin("built-in");
    TestScriptable commands({"globalCommands.GlobalCommands"});
    commands.add("reportSomething");
    builtin.add("kb:alus+t", "globalCommands.GlobalCommands", "reportSomething");
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider([&](alus::script::DispatchContext& context) {
        context.globalCommands = {&commands};
    });

    // The wiring the daemon does: the input layer reaches speech only here.
    GestureManager::Hooks hooks;
    hooks.cancelSpeech = [&]() { presenter.cancelSpeech(); };
    hooks.pauseSpeech = [&](bool pause) { presenter.pauseSpeech(pause); };
    hooks.speakText = [&](const std::string& text, bool) { presenter.speakMessage(text); };
    hooks.isSpeechPaused = [&]() { return presenter.isPaused(); };
    GestureManager manager(fixture.loop(), dispatcher, hooks);
    manager.setBuiltinMap(&builtin);
    NullInputBackend backend;
    backend.start(nullptr);
    manager.setBackend(&backend);

    // Something long is being said.
    presenter.speakMessage(
        "Ovo je dugačka poruka koja traje dovoljno dugo da je pritisak tastera prekine.");
    fixture.drain();
    REQUIRE_FALSE(synth.utterances().empty());
    const std::size_t cancelsBefore = synth.cancelCount();

    // A key press — any key, bound or not.
    manager.onKeyEvent(keyDown('q', 0, 24, 'q'));
    fixture.drain();
    CHECK(synth.cancelCount() > cancelsBefore);

    SECTION("a bound key cancels and then runs its script") {
        manager.onKeyEvent(keyDown(keysym::kInsert, 0, 118));
        manager.onKeyEvent(keyDown('t', 0, 28));
        fixture.drain();
        CHECK(commands.ran == std::vector<std::string>{"reportSomething"});
    }

    SECTION("shift pauses the synthesizer instead of cancelling it") {
        presenter.speakMessage("Još jedna poruka.");
        fixture.drain();
        manager.onKeyEvent(keyDown(keysym::kShiftL, 0, 50));
        // Pausing is queued without the immediate flag, as in NVDA.
        CHECK(fixture.pumpUntil([&] { return presenter.isPaused(); }));
        // And again to resume: the gesture asks the manager what the state is.
        manager.onKeyEvent(keyDown(keysym::kShiftL, 0, 50));
        CHECK(fixture.pumpUntil([&] { return !presenter.isPaused(); }));
    }
}
