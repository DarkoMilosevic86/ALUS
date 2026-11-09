// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T4.8 — getFocusDifferenceLevel works out the change in the ancestor chain.
// T4.9 — setFocusObject returns false for a dead object, and the caller looks.
//
// The focus ancestor chain is the least visible and most load-bearing piece of
// the object model (01-ARHITEKTURA.md §6.4). Everything hangs off it: which
// ancestors are announced on a focus change, which are not announced again,
// where the script dispatch looks for a binding, and what the foreground
// window is.
#include "alus/a11y/api.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace alus::testing;
using namespace alus::a11y;

namespace {

// A window with two groups, each holding two controls: deep enough for the
// ancestor chain to have something to say.
struct DialogTree {
    explicit DialogTree(MockBackend& backend) {
        application = backend.addApplication("Settings");
        window = backend.add(application, PlatformRole::Frame, "Settings");
        backend.addState(window, PlatformState::Active);
        left = backend.add(window, PlatformRole::Panel, "Appearance");
        right = backend.add(window, PlatformRole::Panel, "Privacy");
        leftFirst = backend.add(left, PlatformRole::CheckBox, "Dark mode");
        leftSecond = backend.add(left, PlatformRole::CheckBox, "Large text");
        rightFirst = backend.add(right, PlatformRole::CheckBox, "Location");
        for (const Ref control : {leftFirst, leftSecond, rightFirst}) {
            backend.addState(control, PlatformState::Focusable);
        }
    }

    Ref application = 0;
    Ref window = 0;
    Ref left = 0;
    Ref right = 0;
    Ref leftFirst = 0;
    Ref leftSecond = 0;
    Ref rightFirst = 0;
};

}  // namespace

TEST_CASE("the focus ancestor chain", "[a11y][api]") {
    A11yFixture fixture;
    const DialogTree tree{fixture.backend};
    Api& api = fixture.registry.api();

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftFirst)));
    REQUIRE(api.focus() != nullptr);
    CHECK(api.focus()->name() == "Dark mode");

    // Outermost first, and the focus itself is NOT in the chain:
    // desktop, application, window, panel.
    const std::vector<ObjectPtr>& ancestors = api.focusAncestors();
    REQUIRE(ancestors.size() == 4);
    CHECK(ancestors[0]->id() == fixture.backend.desktopRef());
    CHECK(ancestors[1]->id() == tree.application);
    CHECK(ancestors[2]->id() == tree.window);
    CHECK(ancestors[3]->id() == tree.left);
    // Nothing was shared with the empty previous chain.
    CHECK(api.focusDifferenceLevel() == 0);
}

TEST_CASE("T4.8 the focus difference level", "[a11y][api]") {
    A11yFixture fixture;
    const DialogTree tree{fixture.backend};
    Api& api = fixture.registry.api();

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftFirst)));
    CHECK(api.focusDifferenceLevel() == 0);

    SECTION("tab to a sibling: the whole chain is shared") {
        REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftSecond)));
        // desktop, application, window, panel -- all four still common, so
        // nothing is announced again.
        CHECK(api.focusDifferenceLevel() == 4);
        CHECK(api.focusAncestors().size() == 4);
    }

    SECTION("tab into the other group: the group is new, the window is not") {
        REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.rightFirst)));
        // desktop, application, window are common; the panel is not.
        CHECK(api.focusDifferenceLevel() == 3);
        REQUIRE(api.focusAncestors().size() == 4);
        CHECK(api.focusAncestors()[3]->name() == "Privacy");
    }

    SECTION("the focus moves to an ancestor of itself") {
        REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.window)));
        // The level counts the match in the OLD line (desktop, application,
        // window, panel, the old focus), and the window matched at index 2 --
        // so the level is 3 while the new chain holds two ancestors. That
        // looks inconsistent and is NVDA's own arithmetic: the level is used
        // to index the NEW chain for focusEntered, and 3 on a two-element
        // chain correctly means "nothing here is new".
        CHECK(api.focusDifferenceLevel() == 3);
        CHECK(api.focusAncestors().size() == 2);
    }

    SECTION("to another application: only the desktop is shared") {
        MockBackend& backend = fixture.backend;
        const Ref other = backend.addApplication("Calculator");
        const Ref otherWindow = backend.add(other, PlatformRole::Frame, "Calculator");
        const Ref otherButton = backend.add(otherWindow, PlatformRole::Button, "Equals");
        backend.addState(otherButton, PlatformState::Focusable);

        REQUIRE(api.setFocusObject(fixture.registry.objectFor(otherButton)));
        CHECK(api.focusDifferenceLevel() == 1);
    }
}

TEST_CASE("the ancestor objects themselves are reused", "[a11y][api]") {
    // Not an optimisation detail: if the shared ancestors were rebuilt on
    // every keystroke, their property caches would be rebuilt too, and
    // tabbing through a dialog would re-read the whole chain from the
    // application on every press.
    A11yFixture fixture;
    const DialogTree tree{fixture.backend};
    Api& api = fixture.registry.api();

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftFirst)));
    const Object* window = api.focusAncestors()[2].get();

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftSecond)));
    CHECK(api.focusAncestors()[2].get() == window);
}

TEST_CASE("focusEntered is fired for the new ancestors only", "[a11y][api]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const DialogTree tree{backend};

    backend.emit("object:state-changed:focused", tree.leftFirst, 1);
    fixture.pumpOnce();
    // The window and the group are new; the desktop and the application are
    // layout objects and say nothing (see Object::presentationType).
    CHECK(fixture.wasDispatched(events::kFocusEntered, tree.window));
    CHECK(fixture.wasDispatched(events::kFocusEntered, tree.left));
    CHECK(fixture.countDispatched(events::kGainFocus) == 1);

    fixture.clearRecords();
    backend.emit("object:state-changed:focused", tree.leftSecond, 1);
    fixture.pumpOnce();

    // Nothing is new, so nothing is entered -- and the user hears only the
    // control they moved to.
    CHECK(fixture.countDispatched(events::kFocusEntered) == 0);
    REQUIRE(fixture.spoken.size() == 1);
    CHECK(fixture.spoken.front().text == "Large text check box not checked");
}

TEST_CASE("focusExited is fired for the ancestors that are gone", "[a11y][api]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const DialogTree tree{backend};
    Api& api = fixture.registry.api();

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftFirst)));
    fixture.clearRecords();
    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.rightFirst)));

    CHECK(fixture.wasDispatched(events::kFocusExited, tree.left));
    CHECK_FALSE(fixture.wasDispatched(events::kFocusExited, tree.window));
    // And the old focus is told it lost the focus.
    CHECK(fixture.wasDispatched(events::kLoseFocus, tree.leftFirst));
}

TEST_CASE("T4.9 setFocusObject refuses a dead object", "[a11y][api]") {
    A11yFixture fixture;
    const DialogTree tree{fixture.backend};
    Api& api = fixture.registry.api();

    const ObjectPtr first = fixture.registry.objectFor(tree.leftFirst);
    REQUIRE(api.setFocusObject(first));

    const ObjectPtr dying = fixture.registry.objectFor(tree.rightFirst);
    fixture.backend.kill(tree.rightFirst);
    alus::core::invalidateAllCaches();

    const QuietLog quiet;
    // The refusal is the point: a caller that ignores it would leave ALUS
    // believing the focus is on an object that no longer exists, and every
    // later event about it would be answered with silence.
    CHECK_FALSE(api.setFocusObject(dying));
    // And the previous focus is untouched.
    REQUIRE(api.focus() != nullptr);
    CHECK(api.focus()->id() == tree.leftFirst);

    SECTION("a null object is refused too") {
        CHECK_FALSE(api.setFocusObject(nullptr));
        CHECK(api.focus()->id() == tree.leftFirst);
    }

    SECTION("and so are the other setters") {
        CHECK_FALSE(api.setForegroundObject(dying));
        CHECK_FALSE(api.setNavigatorObject(dying));
        CHECK_FALSE(api.setMouseObject(dying));
    }
}

TEST_CASE("the foreground window follows a focus change at the top",
          "[a11y][api]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const DialogTree tree{backend};

    backend.emit("object:state-changed:focused", tree.leftFirst, 1);
    fixture.pumpOnce();

    REQUIRE(fixture.registry.api().foreground() != nullptr);
    // The ACTIVE window, found through the desktop -- not the focus's
    // grandparent, which is only the fallback.
    CHECK(fixture.registry.api().foreground()->id() == tree.window);
    CHECK(fixture.wasDispatched(events::kForeground, tree.window));

    SECTION("and not again when the focus moves inside it") {
        fixture.clearRecords();
        backend.emit("object:state-changed:focused", tree.leftSecond, 1);
        fixture.pumpOnce();
        CHECK(fixture.countDispatched(events::kForeground) == 0);
    }
}

TEST_CASE("the navigator object follows the focus, and can leave it",
          "[a11y][api]") {
    A11yFixture fixture;
    const DialogTree tree{fixture.backend};
    Api& api = fixture.registry.api();

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftFirst)));
    // reviewCursor/followFocus is true by default, as in NVDA.
    REQUIRE(api.navigator() != nullptr);
    CHECK(api.navigator()->id() == tree.leftFirst);

    // Object navigation moves it without moving the focus, which is the whole
    // point of having two cursors.
    REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.right)));
    CHECK(api.navigator()->id() == tree.right);
    CHECK(api.focus()->id() == tree.leftFirst);

    SECTION("with followFocus off, the navigator stays put") {
        fixture.registry.settings().reviewCursorFollowFocus = false;
        REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.leftSecond)));
        CHECK(api.navigator()->id() == tree.right);
    }

    SECTION("becomeNavigatorObject is fired, and says whether it was the focus") {
        fixture.clearRecords();
        REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.leftSecond), true));
        CHECK(fixture.wasDispatched(events::kBecomeNavigatorObject, tree.leftSecond));
    }
}

TEST_CASE("a never-ending ancestry is survived", "[a11y][api]") {
    // An object that is its own ancestor exists in the wild. Without the
    // depth limit the screen reader walks it until the watchdog fires.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Broken");
    const Ref first = backend.add(application, PlatformRole::Panel, "One");
    const Ref second = backend.add(first, PlatformRole::Button, "Two");
    backend.addState(second, PlatformState::Focusable);
    // The loop: the panel claims the button as its parent.
    backend.node(first).parent = second;

    const QuietLog quiet;
    CHECK(fixture.registry.api().setFocusObject(fixture.registry.objectFor(second)));
    // It stopped, and it stopped at a bounded size.
    CHECK(fixture.registry.api().focusAncestors().size() <= 100);
}

TEST_CASE("isTypingProtected and isFocusEditable", "[a11y][api]") {
    // The two answers phase 3 has been waiting for (its handover, item 3).
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Browser");
    const Ref window = backend.add(application, PlatformRole::Frame, "Sign in");
    backend.addState(window, PlatformState::Active);
    const Ref entry = backend.add(window, PlatformRole::Entry, "User name");
    const Ref password = backend.add(window, PlatformRole::PasswordText, "Password");
    const Ref button = backend.add(window, PlatformRole::Button, "Sign in");
    const Ref readOnly = backend.add(window, PlatformRole::Text, "Terms");
    backend.addState(readOnly, PlatformState::ReadOnly);
    for (const Ref control : {entry, password, button, readOnly}) {
        backend.addState(control, PlatformState::Focusable);
    }
    Api& api = fixture.registry.api();

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(entry)));
    CHECK(api.isFocusEditable());
    CHECK_FALSE(api.isTypingProtected());

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(password)));
    CHECK(api.isFocusEditable());
    // This is what keeps the character echo from reading a password out loud.
    CHECK(api.isTypingProtected());

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(button)));
    CHECK_FALSE(api.isFocusEditable());

    REQUIRE(api.setFocusObject(fixture.registry.objectFor(readOnly)));
    CHECK_FALSE(api.isFocusEditable());
}

TEST_CASE("the initial focus is found without an event", "[a11y][api]") {
    // AT-SPI has no "who has the focus" call, so at start-up the tree is
    // walked. Without it the first thing a user does -- alus+tab -- answers
    // nothing.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref window = backend.add(application, PlatformRole::Frame, "Home");
    backend.addState(window, PlatformState::Active);
    const Ref panel = backend.add(window, PlatformRole::Panel);
    const Ref entry = backend.add(panel, PlatformRole::Entry, "Search");
    backend.addState(entry, PlatformState::Focusable);
    backend.addState(entry, PlatformState::Focused);

    const ObjectPtr found = fixture.registry.findInitialFocus();
    REQUIRE(found != nullptr);
    CHECK(found->name() == "Search");
    CHECK(fixture.registry.activeWindow()->id() == window);

    SECTION("and the active window is the answer when nothing is focused") {
        backend.removeState(entry, PlatformState::Focused);
        alus::core::invalidateAllCaches();
        const ObjectPtr fallback = fixture.registry.findInitialFocus();
        REQUIRE(fallback != nullptr);
        CHECK(fallback->id() == window);
    }

    SECTION("with no active window there is no answer, and that is not a crash") {
        backend.removeState(window, PlatformState::Active);
        alus::core::invalidateAllCaches();
        CHECK(fixture.registry.findInitialFocus() == nullptr);
        CHECK(fixture.registry.activeWindow() == nullptr);
    }
}

TEST_CASE("the focus-changed hook fires, for the grabs", "[a11y][api]") {
    // Phase 3's handover, item 1: the grab set depends on which scripts could
    // run, which depends on the focus (O6). Without this the object
    // navigation keys exist and do nothing.
    A11yFixture fixture;
    const DialogTree tree{fixture.backend};
    int refreshes = 0;
    fixture.registry.setFocusChangedHook([&refreshes] { ++refreshes; });

    fixture.backend.emit("object:state-changed:focused", tree.leftFirst, 1);
    fixture.pumpOnce();
    CHECK(refreshes == 1);

    fixture.backend.emit("object:state-changed:focused", tree.leftSecond, 1);
    fixture.pumpOnce();
    CHECK(refreshes == 2);
}
