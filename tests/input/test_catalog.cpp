// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The generated command table, against the real files in data/:
//
//   T3.10 — the Serbian locale map works: alus+š runs leftMouseClick in the
//           laptop layout
//
// plus the checks that keep the generator honest — the key assignments users
// already know have to come out the other end, or ALUS is not the screen
// reader they moved from (Z1).
#include "alus/script/catalog.hpp"

#include "alus/input/gesture.hpp"
#include "alus/input/key_names.hpp"
#include "alus/script/dispatch.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>

#ifndef ALUS_TEST_DATA_DIR
#define ALUS_TEST_DATA_DIR "data"
#endif

using alus::input::GestureMap;
using alus::input::KeyboardGesture;
using alus::input::Layout;
using alus::input::Modifier;
using alus::script::Catalog;
using alus::script::DispatchContext;
using alus::script::Dispatcher;
using alus::testing::TestScriptable;

namespace {

const std::filesystem::path kDataDir{ALUS_TEST_DATA_DIR};

std::shared_ptr<KeyboardGesture> gestureFor(std::uint32_t keysym, Layout layout,
                                            alus::input::Modifiers modifiers,
                                            std::uint32_t unichar = 0) {
    KeyboardGesture::Init init;
    init.event = alus::testing::keyDown(keysym, 0, 100, unichar);
    init.modifiers = modifiers;
    init.config.layout = layout;
    return std::make_shared<KeyboardGesture>(std::move(init));
}

}  // namespace

TEST_CASE("the generated catalogue loads") {
    Catalog catalog;
    REQUIRE(catalog.loadScripts(kDataDir / "scripts.csv"));
    // ~300 scripts was the plan's estimate; the extractor finds more because
    // browse mode's quick navigation is registered programmatically.
    CHECK(catalog.scriptCount() > 300);
    // v1.0 non-goals are deliberately NOT registered (00-VIZIJA.md §5).
    CHECK(catalog.deferredCount() > 0);
    CHECK(catalog.find("globalCommands.GlobalCommands", "activateMagnifierSettingsDialog") ==
          nullptr);

    SECTION("descriptions, categories and flags come through") {
        const auto* help = catalog.find("globalCommands.GlobalCommands", "toggleInputHelp");
        REQUIRE(help != nullptr);
        CHECK(help->category == "Input");
        CHECK(help->description.find("input help") != std::string::npos);
        CHECK(help->speakOnDemand);
        CHECK(help->isVisibleInGestureDialog());

        const auto* focus = catalog.find("globalCommands.GlobalCommands", "reportCurrentFocus");
        REQUIRE(focus != nullptr);
        CHECK(focus->category == "System focus");
        CHECK(focus->speakOnDemand);

        const auto* nextHeading = catalog.find("browseMode.BrowseModeTreeInterceptor",
                                               "nextHeading");
        REQUIRE(nextHeading != nullptr);
        CHECK(nextHeading->category == "Browse mode");
        CHECK(nextHeading->resumeSayAllMode == alus::script::SayAllCursor::Caret);
    }

    SECTION("a script with no description is hidden from the gesture dialog") {
        const auto* longDesc =
            catalog.find("browseMode.BrowseModeDocumentTreeInterceptor", "activateLongDesc");
        REQUIRE(longDesc != nullptr);
        CHECK_FALSE(longDesc->isVisibleInGestureDialog());
    }
}

TEST_CASE("the built-in gesture map carries the expected key assignments") {
    GestureMap builtin("built-in");
    REQUIRE(builtin.load(kDataDir / "gestures-builtin.ini"));
    CHECK_FALSE(builtin.lastUpdateContainedError());

    const auto boundTo = [&](const char* gesture, const char* script) {
        const auto* entries =
            builtin.entriesForNormalized(alus::input::normalizeGestureIdentifier(gesture));
        if (entries == nullptr) {
            return false;
        }
        for (const auto& entry : *entries) {
            if (entry.script == script) {
                return true;
            }
        }
        return false;
    };

    // The keys this phase's manual test uses.
    CHECK(boundTo("kb:ALUS+1", "toggleInputHelp"));
    CHECK(boundTo("kb:ALUS+2", "toggleSpeakTypedCharacters"));
    CHECK(boundTo("kb:ALUS+3", "toggleSpeakTypedWords"));
    CHECK(boundTo("kb:ALUS+4", "toggleSpeakCommandKeys"));
    CHECK(boundTo("kb:ALUS+f2", "passNextKeyThrough"));
    // Keys later phases depend on, checked now because the generator is what
    // produces them.
    CHECK(boundTo("kb:ALUS+tab", "reportCurrentFocus"));
    CHECK(boundTo("kb:ALUS+f7", "elementsList"));
    CHECK(boundTo("kb:ALUS+space", "toggleVirtualBufferPassThrough"));
    CHECK(boundTo("kb:h", "nextHeading"));
    CHECK(boundTo("kb:shift+h", "previousHeading"));
    CHECK(boundTo("kb:2", "nextHeading2"));
    CHECK(boundTo("kb:numpad8", "review_currentLine"));
    CHECK(boundTo("kb(desktop):ALUS+downArrow", "sayAll"));
    CHECK(boundTo("kb(laptop):ALUS+a", "sayAll"));

    SECTION("touch gestures are not in the map: touch is a v1.0 non-goal") {
        for (const auto& [gesture, entry] : builtin.allEntries()) {
            CHECK(gesture.rfind("ts", 0) != 0);
        }
    }
}

TEST_CASE("T3.10 the Serbian locale map works in the laptop layout") {
    Catalog catalog;
    REQUIRE(catalog.loadScripts(kDataDir / "scripts.csv"));
    REQUIRE(catalog.loadBuiltinGestures(kDataDir / "gestures-builtin.ini"));

    GestureMap locale("locale");
    REQUIRE(locale.load(kDataDir / "locale" / "sr" / "gestures.ini"));
    CHECK_FALSE(locale.lastUpdateContainedError());

    TestScriptable commands({"globalCommands.GlobalCommands"}, &catalog);
    commands.add("leftMouseClick");
    commands.add("rightMouseClick");
    commands.add("zoomOut");

    Dispatcher dispatcher(nullptr, &catalog);
    dispatcher.setGlobalMaps({&locale});
    dispatcher.setBuiltinMap(&catalog.builtinGestures());
    dispatcher.setContextProvider(
        [&](DispatchContext& context) { context.globalCommands = {&commands}; });

    const auto alus = static_cast<alus::input::Modifiers>(Modifier::Alus);

    SECTION("alus+š is leftMouseClick on a laptop") {
        // The key arrives as the legacy keysym XK_scaron with U+0161 as its
        // character; both paths have to name it "š".
        const auto gesture = gestureFor(0x1b9, Layout::Laptop, alus, 0x161);
        REQUIRE(gesture->mainKeyName() == "š");
        const auto found = dispatcher.findScript(*gesture);
        REQUIRE(found.has_value());
        CHECK(found->info->name == "leftMouseClick");
        // And the key really can be grabbed, which is what makes it work at
        // all: the name has to convert back to the legacy keysym.
        CHECK(alus::input::KeyNames::shared().keysym("š") == 0x1b9);
    }

    SECTION("the same key does nothing in the desktop layout") {
        const auto gesture = gestureFor(0x1b9, Layout::Desktop, alus, 0x161);
        CHECK_FALSE(dispatcher.findScript(*gesture).has_value());
    }

    SECTION("alus+đ is rightMouseClick on a laptop") {
        const auto gesture = gestureFor(0x1f0, Layout::Laptop, alus, 0x111);
        REQUIRE(gesture->mainKeyName() == "đ");
        const auto found = dispatcher.findScript(*gesture);
        REQUIRE(found.has_value());
        CHECK(found->info->name == "rightMouseClick");
    }

    SECTION("a locale binding also applies in the desktop layout when written kb:") {
        // zoomOut = kb:ALUS+shift+' — no layout, so both.
        const auto gesture = gestureFor('\'', Layout::Desktop,
                                        alus | static_cast<alus::input::Modifiers>(
                                                   Modifier::Shift),
                                        '\'');
        const auto found = dispatcher.findScript(*gesture);
        REQUIRE(found.has_value());
        CHECK(found->info->name == "zoomOut");
    }
}

TEST_CASE("the key labels are what input help says out loud") {
    alus::input::KeyNames names;
    REQUIRE(names.loadLabels(kDataDir / "key-labels.ini"));
    CHECK(names.labelCount() > 50);
    CHECK(names.label("capsLock") == "caps lock");
    CHECK(names.label("numpad7") == "numpad 7");
    CHECK(names.label("leftArrow") == "left arrow");
    CHECK(names.label("pageUp") == "page up");
    // A letter has no label, and needs none.
    CHECK(names.label("t") == "t");
}

TEST_CASE("a script implementation picks its metadata up from the catalogue") {
    Catalog catalog;
    REQUIRE(catalog.loadScripts(kDataDir / "scripts.csv"));
    TestScriptable commands({"globalCommands.GlobalCommands"}, &catalog);
    commands.add("toggleInputHelp");

    const auto* script = commands.scriptByName("toggleInputHelp");
    REQUIRE(script != nullptr);
    // Nothing was typed in at the binding site, and the command still has
    // the catalogue's description, category and flags (§5.4).
    CHECK(script->info->category == "Input");
    CHECK_FALSE(script->info->description.empty());
    CHECK(script->info->speakOnDemand);

    SECTION("an ALUS-specific script carries only what it declares") {
        commands.add("alusOnlyCommand",
                     alus::script::ScriptSpec{.description = "Something ALUS alone does.",
                                              .category = "Input"});
        const auto* own = commands.scriptByName("alusOnlyCommand");
        REQUIRE(own != nullptr);
        CHECK(own->info->description == "Something ALUS alone does.");
    }
}
