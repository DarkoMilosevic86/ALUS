// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Script dispatch — the phase 3 exit criteria that live in the lookup:
//
//   T3.2  — kb(laptop) beats kb: when the layout is laptop
//   T3.3  — user beats locale beats braille driver beats built-in
//   T3.4  — "None = kb:h" unbinds h
//   T3.7  — lastScriptRepeatCount is 0/1/2/3, and resets on an unbound gesture
//   T3.8  — a tree interceptor with passThrough on is SKIPPED, except for
//           scripts marked ignoreTreeInterceptorPassThrough
//   T3.9  — isScriptWaiting() is true while a script is queued
#include "alus/script/dispatch.hpp"

#include "../support/loop_fixture.hpp"
#include "alus/input/gesture.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>

using alus::input::GestureMap;
using alus::input::KeyboardGesture;
using alus::input::Layout;
using alus::input::Modifier;
using alus::script::DispatchContext;
using alus::script::Dispatcher;
using alus::script::ScriptSpec;
using alus::testing::TestScriptable;

namespace {

std::shared_ptr<KeyboardGesture> gestureFor(std::uint32_t keysym, Layout layout = Layout::Desktop,
                                            alus::input::Modifiers modifiers = 0) {
    KeyboardGesture::Init init;
    init.event = alus::testing::keyDown(keysym);
    init.modifiers = modifiers;
    init.config.layout = layout;
    return std::make_shared<KeyboardGesture>(std::move(init));
}

}  // namespace

TEST_CASE("T3.2 a laptop binding beats the layout-agnostic one") {
    Dispatcher dispatcher;
    TestScriptable commands({"globalCommands.GlobalCommands"});
    commands.add("laptopScript");
    commands.add("agnosticScript");

    GestureMap builtin("built-in");
    builtin.add("kb(laptop):alus+a", "globalCommands.GlobalCommands", "laptopScript");
    builtin.add("kb:alus+a", "globalCommands.GlobalCommands", "agnosticScript");
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider(
        [&](DispatchContext& context) { context.globalCommands = {&commands}; });

    const auto laptop = gestureFor('a', Layout::Laptop, static_cast<alus::input::Modifiers>(
                                                            Modifier::Alus));
    auto found = dispatcher.findScript(*laptop);
    REQUIRE(found.has_value());
    CHECK(found->info->name == "laptopScript");

    // In the desktop layout the laptop identifier is not even generated.
    const auto desktop = gestureFor('a', Layout::Desktop, static_cast<alus::input::Modifiers>(
                                                              Modifier::Alus));
    found = dispatcher.findScript(*desktop);
    REQUIRE(found.has_value());
    CHECK(found->info->name == "agnosticScript");
}

TEST_CASE("T3.3 map priority: user, then locale, then braille driver, then built-in") {
    Dispatcher dispatcher;
    TestScriptable commands({"globalCommands.GlobalCommands"});
    commands.add("fromUser");
    commands.add("fromLocale");
    commands.add("fromDriver");
    commands.add("fromBuiltin");

    GestureMap user("user");
    GestureMap locale("locale");
    GestureMap driver("driver");
    GestureMap builtin("built-in");
    builtin.add("kb:alus+t", "globalCommands.GlobalCommands", "fromBuiltin");
    dispatcher.setGlobalMaps({&user, &locale, &driver});
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider(
        [&](DispatchContext& context) { context.globalCommands = {&commands}; });

    const auto gesture = gestureFor('t', Layout::Desktop,
                                    static_cast<alus::input::Modifiers>(Modifier::Alus));

    CHECK(dispatcher.findScript(*gesture)->info->name == "fromBuiltin");

    driver.add("kb:alus+t", "globalCommands.GlobalCommands", "fromDriver");
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromDriver");

    locale.add("kb:alus+t", "globalCommands.GlobalCommands", "fromLocale");
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromLocale");

    user.add("kb:alus+t", "globalCommands.GlobalCommands", "fromUser");
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromUser");
    CHECK(dispatcher.lastTrace().fromGlobalMap);
}

TEST_CASE("T3.4 None in a map unbinds the gesture for that class") {
    Dispatcher dispatcher;
    TestScriptable buffer({"virtualBuffers.VirtualBuffer"});
    buffer.add("nextHeading");

    GestureMap user("user");
    GestureMap builtin("built-in");
    builtin.add("kb:h", "virtualBuffers.VirtualBuffer", "nextHeading");
    dispatcher.setGlobalMaps({&user});
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider([&](DispatchContext& context) { context.focus = &buffer; });

    const auto gesture = gestureFor('h');
    REQUIRE(dispatcher.findScript(*gesture).has_value());

    // "None = kb:h" for this class.
    user.add("kb:h", "virtualBuffers.VirtualBuffer", "");
    CHECK_FALSE(dispatcher.findScript(*gesture).has_value());
    CHECK(dispatcher.lastTrace().unboundByMap);

    // The unbind is per class: another object still gets its script.
    TestScriptable other({"other.Other"});
    other.add("somethingElse");
    builtin.add("kb:h", "other.Other", "somethingElse");
    dispatcher.setContextProvider([&](DispatchContext& context) {
        context.focus = &buffer;
        context.globalCommands = {&other};
    });
    const auto found = dispatcher.findScript(*gesture);
    REQUIRE(found.has_value());
    CHECK(found->info->name == "somethingElse");
}

TEST_CASE("T3.8 a tree interceptor in pass-through mode is skipped") {
    Dispatcher dispatcher;
    TestScriptable interceptor({"browseMode.BrowseModeTreeInterceptor"});
    interceptor.add("nextHeading");
    interceptor.add("disablePassThrough", ScriptSpec{.ignoreTreeInterceptorPassThrough = true});
    TestScriptable commands({"globalCommands.GlobalCommands"});
    commands.add("globalFallback");

    GestureMap builtin("built-in");
    builtin.add("kb:h", "browseMode.BrowseModeTreeInterceptor", "nextHeading");
    builtin.add("kb:h", "globalCommands.GlobalCommands", "globalFallback");
    builtin.add("kb:escape", "browseMode.BrowseModeTreeInterceptor", "disablePassThrough");
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider([&](DispatchContext& context) {
        context.treeInterceptor = &interceptor;
        context.treeInterceptorReady = true;
        context.globalCommands = {&commands};
    });

    const auto h = gestureFor('h');

    SECTION("browse mode: the interceptor's script runs") {
        interceptor.setPassThrough(false);
        const auto found = dispatcher.findScript(*h);
        REQUIRE(found.has_value());
        CHECK(found->info->name == "nextHeading");
        CHECK(dispatcher.lastTrace().step == 6);
    }

    SECTION("focus mode: single-letter navigation falls through") {
        interceptor.setPassThrough(true);
        const auto found = dispatcher.findScript(*h);
        REQUIRE(found.has_value());
        CHECK(found->info->name == "globalFallback");
        CHECK(dispatcher.lastTrace().step == 10);
    }

    SECTION("focus mode: a script that ignores pass-through still runs") {
        interceptor.setPassThrough(true);
        const auto escape = gestureFor(alus::input::keysym::kEscape);
        const auto found = dispatcher.findScript(*escape);
        REQUIRE(found.has_value());
        CHECK(found->info->name == "disablePassThrough");
        CHECK(dispatcher.lastTrace().step == 6);
    }

    SECTION("an interceptor that is not ready is not consulted at all") {
        interceptor.setPassThrough(false);
        dispatcher.setContextProvider([&](DispatchContext& context) {
            context.treeInterceptor = &interceptor;
            context.treeInterceptorReady = false;
            context.globalCommands = {&commands};
        });
        const auto found = dispatcher.findScript(*h);
        REQUIRE(found.has_value());
        CHECK(found->info->name == "globalFallback");
    }
}

TEST_CASE("the ten steps are consulted in order") {
    Dispatcher dispatcher;
    TestScriptable plugin({"plugin.Plugin"});
    TestScriptable appModule({"appModules.app.AppModule"});
    TestScriptable focus({"objects.Focus"});
    TestScriptable ancestor({"objects.Ancestor"});
    TestScriptable commands({"globalCommands.GlobalCommands"});
    plugin.add("fromPlugin");
    appModule.add("fromAppModule");
    focus.add("fromFocus");
    // Only a script that may propagate is honoured on an ancestor.
    ancestor.add("fromAncestor", ScriptSpec{.canPropagate = true});
    ancestor.add("notPropagating");
    commands.add("fromCommands");

    GestureMap builtin("built-in");
    builtin.add("kb:alus+t", "plugin.Plugin", "fromPlugin");
    builtin.add("kb:alus+t", "appModules.app.AppModule", "fromAppModule");
    builtin.add("kb:alus+t", "objects.Focus", "fromFocus");
    builtin.add("kb:alus+t", "objects.Ancestor", "fromAncestor");
    builtin.add("kb:alus+t", "globalCommands.GlobalCommands", "fromCommands");
    builtin.add("kb:alus+u", "objects.Ancestor", "notPropagating");
    dispatcher.setBuiltinMap(&builtin);

    DispatchContext full;
    full.globalPlugins = {&plugin};
    full.appModule = &appModule;
    full.focus = &focus;
    full.focusAncestors = {&ancestor};
    full.globalCommands = {&commands};

    const auto gesture = gestureFor('t', Layout::Desktop,
                                    static_cast<alus::input::Modifiers>(Modifier::Alus));

    dispatcher.setContextProvider([&](DispatchContext& context) { context = full; });
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromPlugin");
    CHECK(dispatcher.lastTrace().step == 2);

    full.globalPlugins.clear();
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromAppModule");
    full.appModule = nullptr;
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromFocus");
    full.focus = nullptr;
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromAncestor");
    CHECK(dispatcher.lastTrace().step == 8);
    full.focusAncestors.clear();
    CHECK(dispatcher.findScript(*gesture)->info->name == "fromCommands");
    CHECK(dispatcher.lastTrace().step == 10);

    SECTION("a script that may not propagate is ignored on an ancestor") {
        DispatchContext ancestorsOnly;
        ancestorsOnly.focusAncestors = {&ancestor};
        dispatcher.setContextProvider(
            [&](DispatchContext& context) { context = ancestorsOnly; });
        const auto u = gestureFor('u', Layout::Desktop,
                                  static_cast<alus::input::Modifiers>(Modifier::Alus));
        CHECK_FALSE(dispatcher.findScript(*u).has_value());
    }
}

TEST_CASE("sleep mode silences everything except what asked for it") {
    Dispatcher dispatcher;
    TestScriptable commands({"globalCommands.GlobalCommands"});
    commands.add("ordinary");
    commands.add("evenAsleep", ScriptSpec{.allowInSleepMode = true});
    GestureMap builtin("built-in");
    builtin.add("kb:alus+t", "globalCommands.GlobalCommands", "ordinary");
    builtin.add("kb:alus+u", "globalCommands.GlobalCommands", "evenAsleep");
    dispatcher.setBuiltinMap(&builtin);

    DispatchContext::Sleep sleep = DispatchContext::Sleep::App;
    dispatcher.setContextProvider([&](DispatchContext& context) {
        context.globalCommands = {&commands};
        context.sleepMode = sleep;
    });
    const auto modifiers = static_cast<alus::input::Modifiers>(Modifier::Alus);
    CHECK_FALSE(dispatcher.findScript(*gestureFor('t', Layout::Desktop, modifiers)).has_value());
    CHECK(dispatcher.findScript(*gestureFor('u', Layout::Desktop, modifiers)).has_value());

    sleep = DispatchContext::Sleep::Full;
    CHECK_FALSE(dispatcher.findScript(*gestureFor('u', Layout::Desktop, modifiers)).has_value());
}

TEST_CASE("T3.7 repeat counting is per script, not per key") {
    alus::testing::LoopFixture fixture;
    Dispatcher dispatcher(&fixture.loop());
    dispatcher.setMultiPressTimeout(std::chrono::milliseconds(500));
    TestScriptable commands({"globalCommands.GlobalCommands"});
    std::vector<int> seen;
    commands.bindScript("report", [&](alus::input::Gesture&) {
        seen.push_back(dispatcher.lastScriptRepeatCount());
    });
    commands.add("other");

    GestureMap builtin("built-in");
    // TWO different keys for the SAME script: a repeat is a repeat of the
    // script, which is why NVDA counts the script and not the key.
    builtin.add("kb:alus+tab", "globalCommands.GlobalCommands", "report");
    builtin.add("kb:alus+t", "globalCommands.GlobalCommands", "report");
    builtin.add("kb:alus+u", "globalCommands.GlobalCommands", "other");
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider(
        [&](DispatchContext& context) { context.globalCommands = {&commands}; });

    const auto modifiers = static_cast<alus::input::Modifiers>(Modifier::Alus);
    const auto press = [&](std::uint32_t keysym) {
        const auto gesture = gestureFor(keysym, Layout::Desktop, modifiers);
        const auto script = dispatcher.findScript(*gesture);
        REQUIRE(script.has_value());
        dispatcher.queueScript(*script, gesture);
        fixture.drain();
    };

    press(alus::input::keysym::kTab);
    press(alus::input::keysym::kTab);
    press('t');  // the other key bound to the same script
    press(alus::input::keysym::kTab);
    CHECK(seen == std::vector<int>{0, 1, 2, 3});

    SECTION("a different script resets the count") {
        press('u');
        press(alus::input::keysym::kTab);
        CHECK(seen.back() == 0);
    }

    SECTION("an unbound gesture in between resets the count") {
        dispatcher.clearLastScript();  // what the manager queues for an unbound gesture
        press(alus::input::keysym::kTab);
        CHECK(seen.back() == 0);
    }

    SECTION("the count expires with the multi-press timeout") {
        dispatcher.setMultiPressTimeout(std::chrono::milliseconds(20));
        press(alus::input::keysym::kTab);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        press(alus::input::keysym::kTab);
        CHECK(seen.back() == 0);
    }
}

TEST_CASE("T3.9 isScriptWaiting is true while a script is queued") {
    alus::testing::LoopFixture fixture;
    Dispatcher dispatcher(&fixture.loop());
    TestScriptable commands({"globalCommands.GlobalCommands"});
    bool sawWaitingInsideScript = true;
    commands.bindScript("slow", [&](alus::input::Gesture&) {
        // Inside the script it is no longer waiting: it is running.
        sawWaitingInsideScript = dispatcher.isScriptWaiting();
    });
    GestureMap builtin("built-in");
    builtin.add("kb:alus+t", "globalCommands.GlobalCommands", "slow");
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider(
        [&](DispatchContext& context) { context.globalCommands = {&commands}; });

    const auto gesture =
        gestureFor('t', Layout::Desktop, static_cast<alus::input::Modifiers>(Modifier::Alus));
    const auto script = dispatcher.findScript(*gesture);
    REQUIRE(script.has_value());

    CHECK_FALSE(dispatcher.isScriptWaiting());
    dispatcher.queueScript(*script, gesture);
    // Queued but not yet pumped: this is the window in which a long utterance
    // has to give up (the speech side of T3.9 is in test_input_speech.cpp).
    CHECK(dispatcher.isScriptWaiting());
    fixture.drain();
    CHECK_FALSE(dispatcher.isScriptWaiting());
    CHECK_FALSE(sawWaitingInsideScript);
    CHECK(dispatcher.executedCount() == 1);
}

TEST_CASE("a kb: entry in a map emulates a key instead of running a script") {
    Dispatcher dispatcher;
    TestScriptable commands({"globalCommands.GlobalCommands"});
    GestureMap user("user");
    user.add("kb:alus+j", "globalCommands.GlobalCommands", "kb:downArrow");
    dispatcher.setGlobalMaps({&user});
    dispatcher.setContextProvider(
        [&](DispatchContext& context) { context.globalCommands = {&commands}; });

    std::string emulated;
    bool emulatorAnswer = true;
    dispatcher.setKeyEmulator([&](std::string_view keyName, alus::input::Gesture&) {
        emulated = std::string(keyName);
        return emulatorAnswer;
    });

    const auto gesture =
        gestureFor('j', Layout::Desktop, static_cast<alus::input::Modifiers>(Modifier::Alus));
    const auto script = dispatcher.findScript(*gesture);
    REQUIRE(script.has_value());
    CHECK(script->info->name == "kb:downArrow");
    CHECK(script->info->description.find("Emulates pressing") != std::string::npos);

    dispatcher.executeScript(*script, *gesture);
    CHECK(emulated == "downArrow");

    SECTION("a platform that cannot inject keys does not fail loudly") {
        // N18: on Wayland this is always the case. It must degrade quietly —
        // an error sound on every emulated key would be unusable (rule 5).
        alus::testing::QuietLog quiet;
        emulatorAnswer = false;
        dispatcher.executeScript(*script, *gesture);
        CHECK(dispatcher.failedCount() == 0);
    }

    SECTION("the script identity is stable, so repeats still count") {
        const auto again = dispatcher.findScript(*gesture);
        REQUIRE(again.has_value());
        CHECK(again->identity() == script->identity());
    }
}

TEST_CASE("a script that throws is logged, not fatal") {
    alus::testing::QuietLog quiet;
    alus::testing::LoopFixture fixture;
    Dispatcher dispatcher(&fixture.loop());
    TestScriptable commands({"globalCommands.GlobalCommands"});
    commands.bindScript("boom",
                        [](alus::input::Gesture&) { throw std::runtime_error("boom"); });
    GestureMap builtin("built-in");
    builtin.add("kb:alus+t", "globalCommands.GlobalCommands", "boom");
    dispatcher.setBuiltinMap(&builtin);
    dispatcher.setContextProvider(
        [&](DispatchContext& context) { context.globalCommands = {&commands}; });

    const auto gesture =
        gestureFor('t', Layout::Desktop, static_cast<alus::input::Modifiers>(Modifier::Alus));
    const auto script = dispatcher.findScript(*gesture);
    REQUIRE(script.has_value());
    dispatcher.queueScript(*script, gesture);
    fixture.drain();
    CHECK(dispatcher.failedCount() == 1);
    CHECK_FALSE(dispatcher.isScriptWaiting());
}
