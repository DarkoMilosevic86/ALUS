// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T4.3 — the limiter: 1000 ChildrenChanged in 10 ms => at most 10 per
//        application per cycle.
// T4.4 — the limiter: 20 focus events => exactly 1 is emitted (the newest).
// T4.5 — Window:Activate erases a pending StateChanged:Focused for the same
//        object.
//
// These are phase 1's EventLimiter (01-ARHITEKTURA.md §3.4) doing its job with
// phase 4's event names in it. Both halves have to be right: the limiter was
// written and tested with platform-neutral names, and a wrong name here would
// leave the whole mechanism switched off while every test of it still passed.
#include "alus/a11y/events.hpp"

#include "alus/a11y/registry.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace alus::testing;
using namespace alus::a11y;

TEST_CASE("the AT-SPI event names ALUS subscribes to", "[a11y][events]") {
    const std::vector<std::string>& types = subscribedEventTypes();
    // §6.1's list, verified live against Orca's own subscriptions.
    CHECK(types.size() == 38);
    const auto has = [&types](const char* type) {
        return std::find(types.begin(), types.end(), type) != types.end();
    };
    CHECK(has("object:state-changed:focused"));
    CHECK(has("object:children-changed:add"));
    CHECK(has("object:text-caret-moved"));
    CHECK(has("window:activate"));
    CHECK(has("document:load-complete"));
    CHECK(has("object:announcement"));
    CHECK(has("mouse:button"));
}

TEST_CASE("AT-SPI names become NVDA names", "[a11y][events]") {
    CHECK(alusEventName("object:state-changed:focused", 1) == events::kGainFocus);
    // detail1 == 0 is "no longer focused", and ALUS does not use it: losing
    // the focus is decided by api::setFocusObject, never by an event, or two
    // applications disagreeing would leave ALUS with no focus at all.
    CHECK(alusEventName("object:state-changed:focused", 0).empty());
    CHECK(alusEventName("focus:", 0) == events::kGainFocus);
    CHECK(alusEventName("window:activate", 0) == events::kForeground);
    CHECK(alusEventName("object:property-change:accessible-name", 0) == events::kNameChange);
    CHECK(alusEventName("object:children-changed:add", 1) == events::kChildrenAdded);
    CHECK(alusEventName("object:children-changed:remove", 1) == events::kChildrenRemoved);
    CHECK(alusEventName("object:announcement", 0) == events::kLiveRegionChange);
    // An event ALUS never subscribed to.
    CHECK(alusEventName("object:bounds-changed", 0).empty());
}

TEST_CASE("the limiter is configured with the AT-SPI names", "[a11y][events]") {
    const alus::core::EventLimiterConfig config = limiterConfig();
    CHECK(config.focusName == std::string{events::kGainFocus});
    CHECK(config.foregroundName == std::string{events::kForeground});
    CHECK(config.maxEventsPerSource == 10);
    CHECK(config.maxFocusEvents == 4);
    // The menu list is empty on AT-SPI, and that is a decision, not an
    // omission: a menu announces itself through focus events on its items.
    CHECK(config.menuNames.empty());
    // The pairs that cancel each other while both are pending.
    REQUIRE(config.cancellingPairs.size() == 3);
    CHECK(config.cancellingPairs[0].first == std::string{events::kChildrenAdded});
    CHECK(config.cancellingPairs[0].second == std::string{events::kChildrenRemoved});
}

TEST_CASE("T4.3 a thousand children-changed events cost ten", "[a11y][events][limiter]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Web Browser", "chromium");
    const Ref document = backend.add(application, PlatformRole::DocumentWeb, "A page");

    // A page loading: a thousand different children appear, all in the same
    // application, inside one pump cycle.
    std::vector<Ref> children;
    children.reserve(1000);
    for (int index = 0; index < 1000; ++index) {
        children.push_back(backend.add(document, PlatformRole::Section));
    }
    for (const Ref child : children) {
        backend.emit("object:children-changed:add", child, 1);
    }

    fixture.pumpOnce();

    // At most ten per application per cycle. The user cannot hear a thousand,
    // and the screen reader cannot afford to look at them.
    CHECK(fixture.countDispatched(events::kChildrenAdded) <= 10);
    CHECK(fixture.countDispatched(events::kChildrenAdded) > 0);
    CHECK(fixture.registry.limiter().discarded() >= 990);

    SECTION("and every platform reference is handed back") {
        // The 990 discarded events each carried a reference. This is the leak
        // test: the limiter dropping an event must not drop its reference.
        fixture.registry.releaseCycleObjects();
        alus::core::invalidateAllCaches();
        fixture.registry.sweepObjectCache();
        CHECK(fixture.extraRefs() == 0);
    }
}

TEST_CASE("the flood of one application does not starve another", "[a11y][events][limiter]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref noisy = backend.addApplication("Web Browser", "chromium");
    const Ref quiet = backend.addApplication("Calculator", "gtk");
    const Ref quietButton = backend.add(quiet, PlatformRole::Button, "Equals");

    for (int index = 0; index < 500; ++index) {
        backend.emit("object:children-changed:add",
                     backend.add(noisy, PlatformRole::Section), 1);
    }
    backend.emit("object:property-change:accessible-name", quietButton);

    fixture.pumpOnce();

    // The cap is PER APPLICATION, so the one event from the quiet application
    // survives the other one's thousand.
    CHECK(fixture.wasDispatched(events::kNameChange, quietButton));
}

TEST_CASE("T4.4 twenty focus events become one", "[a11y][events][limiter]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Settings");
    const Ref window = backend.add(application, PlatformRole::Frame, "Settings");
    backend.addState(window, PlatformState::Active);

    // Holding down Tab: twenty controls take the focus in one cycle.
    std::vector<Ref> buttons;
    for (int index = 0; index < 20; ++index) {
        const Ref button =
            backend.add(window, PlatformRole::Button, "Button " + std::to_string(index));
        backend.addState(button, PlatformState::Focusable);
        buttons.push_back(button);
        backend.emit("object:state-changed:focused", button, 1);
    }

    fixture.pumpOnce();

    // Exactly one, and it is the last one: the others are not just unspoken,
    // they are not true any more.
    CHECK(fixture.countDispatched(events::kGainFocus) == 1);
    CHECK(fixture.wasDispatched(events::kGainFocus, buttons.back()));
    REQUIRE(fixture.registry.api().focus() != nullptr);
    CHECK(fixture.registry.api().focus()->name() == "Button 19");
    // And the user hears two things, not twenty-one: the window they just
    // entered, and the control they landed on. The window comes from the
    // focusEntered event for the one new ancestor, and it is announced by
    // name alone -- Role::Frame is in NVDA's silentRolesOnFocus, so nobody
    // hears the word "frame".
    REQUIRE(fixture.spoken.size() == 2);
    CHECK(fixture.spoken.front().reason == OutputReason::FocusEntered);
    CHECK(fixture.spoken.front().text == "Settings");
    CHECK(fixture.spoken.back().reason == OutputReason::Focus);
    CHECK(fixture.spoken.back().text == "Button 19 button");
}

TEST_CASE("a focus event for a dead object falls back to an older one",
          "[a11y][events][limiter]") {
    // The second stage of the limiter (NVDA: IAccessibleHandler.pumpAll):
    // try the newest focus event, and fall back until one is accepted. This
    // is what happens when a dialog closes just as its button took the focus.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref window = backend.add(application, PlatformRole::Frame, "Home");
    backend.addState(window, PlatformState::Active);
    const Ref older = backend.add(window, PlatformRole::Button, "Still here");
    const Ref newer = backend.add(window, PlatformRole::Button, "Gone already");
    backend.addState(older, PlatformState::Focusable);
    backend.addState(newer, PlatformState::Focusable);

    backend.emit("object:state-changed:focused", older, 1);
    backend.emit("object:state-changed:focused", newer, 1);
    // The newer object dies before the pump gets to it.
    backend.kill(newer);

    const QuietLog quiet;
    fixture.pumpOnce();

    CHECK(fixture.countDispatched(events::kGainFocus) == 1);
    CHECK(fixture.wasDispatched(events::kGainFocus, older));
    REQUIRE(fixture.registry.api().focus() != nullptr);
    CHECK(fixture.registry.api().focus()->name() == "Still here");
}

TEST_CASE("T4.5 window:activate erases a pending focus for the same object",
          "[a11y][events][limiter]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Text Editor");
    const Ref window = backend.add(application, PlatformRole::Frame, "Untitled");
    backend.addState(window, PlatformState::Active);
    backend.addState(window, PlatformState::Focusable);

    // What alt+tab actually produces: the window reports that it is focused,
    // and then that it is active. Announcing both would say the window twice.
    backend.emit("object:state-changed:focused", window, 1);
    backend.emit("window:activate", window);

    fixture.pumpOnce();

    CHECK(fixture.countDispatched(events::kForeground) >= 1);
    CHECK(fixture.countDispatched(events::kGainFocus) == 0);
}

TEST_CASE("children added cancels a pending children removed", "[a11y][events][limiter]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref list = backend.add(application, PlatformRole::List, "Files");

    // A list that rebuilds itself: the removal and the addition cancel out,
    // because nothing changed by the time the pump looks.
    backend.emit("object:children-changed:remove", list, 1);
    backend.emit("object:children-changed:add", list, 1);

    fixture.pumpOnce();

    CHECK(fixture.countDispatched(events::kChildrenRemoved) == 0);
    CHECK(fixture.countDispatched(events::kChildrenAdded) == 0);
}

TEST_CASE("the focused object is never capped", "[a11y][events][limiter]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Terminal");
    const Ref window = backend.add(application, PlatformRole::Frame, "Terminal");
    backend.addState(window, PlatformState::Active);
    const Ref terminal = backend.add(window, PlatformRole::Terminal, "bash");
    backend.addState(terminal, PlatformState::Focusable);
    backend.addState(terminal, PlatformState::Focused);
    backend.addInterface(terminal, Interface::Text);

    backend.emit("object:state-changed:focused", terminal, 1);
    fixture.pumpOnce();
    fixture.clearRecords();

    // A build running in the terminal: the terminal floods, and the user is
    // reading the terminal.
    for (int index = 0; index < 60; ++index) {
        backend.emit("object:text-changed:insert", terminal, index, 1);
        backend.emit("object:children-changed:add",
                     backend.add(window, PlatformRole::Section), 1);
    }
    fixture.pumpOnce();

    // The events for the focused object survive the cap; the others do not.
    CHECK(fixture.countDispatched(events::kTextChange) >= 1);
    CHECK(fixture.countDispatched(events::kChildrenAdded) <= 10);
}

TEST_CASE("an unsubscribed event type is dropped with its references",
          "[a11y][events]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref object = backend.add(application, PlatformRole::Panel);

    backend.emit("object:bounds-changed", object);
    fixture.pumpOnce();

    CHECK(fixture.dispatched.empty());
    fixture.registry.releaseCycleObjects();
    alus::core::invalidateAllCaches();
    fixture.registry.sweepObjectCache();
    CHECK(fixture.extraRefs() == 0);
}

TEST_CASE("an active descendant change becomes a focus event", "[a11y][events]") {
    // A GTK list box keeps the focus on itself and reports which row is
    // active. Without this the user hears the list once and nothing after.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref window = backend.add(application, PlatformRole::Frame, "Home");
    backend.addState(window, PlatformState::Active);
    const Ref list = backend.add(window, PlatformRole::List, "Places");
    backend.addState(list, PlatformState::Focusable);
    backend.addState(list, PlatformState::Focused);
    backend.addState(list, PlatformState::ManagesDescendants);
    const Ref row = backend.add(list, PlatformRole::ListItem, "Documents");

    backend.emit("object:state-changed:focused", list, 1);
    fixture.pumpOnce();
    fixture.clearRecords();

    backend.emit("object:active-descendant-changed", list, 0, 0, {}, row);
    fixture.pumpOnce();

    CHECK(fixture.wasDispatched(events::kGainFocus, row));
    REQUIRE(fixture.registry.api().focus() != nullptr);
    CHECK(fixture.registry.api().focus()->name() == "Documents");
    REQUIRE_FALSE(fixture.spoken.empty());
    CHECK(fixture.spoken.back().text == "Documents");
}

TEST_CASE("a name change on the focus is announced, elsewhere it is not",
          "[a11y][events]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Settings");
    const Ref window = backend.add(application, PlatformRole::Frame, "Settings");
    backend.addState(window, PlatformState::Active);
    const Ref button = backend.add(window, PlatformRole::Button, "Connect");
    backend.addState(button, PlatformState::Focusable);
    const Ref label = backend.add(window, PlatformRole::Label, "Not focused");

    backend.emit("object:state-changed:focused", button, 1);
    fixture.pumpOnce();
    fixture.clearRecords();

    backend.node(button).name = "Disconnect";
    backend.emit("object:property-change:accessible-name", button);
    backend.node(label).name = "Changed too";
    backend.emit("object:property-change:accessible-name", label);
    fixture.pumpOnce();

    // Both events are dispatched -- braille and the review cursor want them --
    // but only the focus is spoken.
    CHECK(fixture.wasDispatched(events::kNameChange, button));
    CHECK(fixture.wasDispatched(events::kNameChange, label));
    REQUIRE(fixture.changes.size() == 1);
    CHECK(fixture.changes.front().ref == button);
    CHECK(fixture.changes.front().text == "Disconnect");
}

TEST_CASE("a state change is announced on the focus and its ancestors",
          "[a11y][events]") {
    // NVDA #10890: a focused button inside a column header changes the
    // header's sort state, and the user needs to hear it.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref window = backend.add(application, PlatformRole::Frame, "Home");
    backend.addState(window, PlatformState::Active);
    const Ref header = backend.add(window, PlatformRole::ColumnHeader, "Name");
    const Ref button = backend.add(header, PlatformRole::Button, "Sort");
    backend.addState(button, PlatformState::Focusable);

    backend.emit("object:state-changed:focused", button, 1);
    fixture.pumpOnce();
    fixture.clearRecords();

    backend.node(header).attributes = {{"sort", "ascending"}};
    backend.emit("object:state-changed:selected", header, 1);
    fixture.pumpOnce();

    REQUIRE(fixture.changes.size() == 1);
    CHECK(fixture.changes.front().ref == header);
}

TEST_CASE("an event asks for a pump cycle", "[a11y][events]") {
    // Invariant 1 of §3.1. Without it an event waits in the limiter until
    // something else happens to pump the loop, and a focus change made with
    // the mouse is announced late or never. It is invisible in every other
    // test, because the tests pump by hand.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref window = backend.add(application, PlatformRole::Frame, "Home");
    backend.addState(window, PlatformState::Active);
    const Ref button = backend.add(window, PlatformRole::Button, "Open");
    backend.addState(button, PlatformState::Focusable);

    fixture.drain();
    REQUIRE(fixture.loop().pendingRequest() == alus::core::PumpRequest::None);

    SECTION("a focus event asks for it immediately") {
        backend.emit("object:state-changed:focused", button, 1);
        CHECK(fixture.loop().pendingRequest() == alus::core::PumpRequest::Immediate);
    }

    SECTION("anything else asks for the grouped one") {
        backend.emit("object:children-changed:add", window, 1);
        CHECK(fixture.loop().pendingRequest() == alus::core::PumpRequest::Delayed);
    }

    SECTION("an event ALUS has no use for asks for nothing") {
        backend.emit("object:bounds-changed", window);
        CHECK(fixture.loop().pendingRequest() == alus::core::PumpRequest::None);
    }

    SECTION("and the cycle that follows announces it") {
        backend.emit("object:state-changed:focused", button, 1);
        // The real loop, not pumpOnce(): this is the whole path from the
        // platform callback to the spoken word.
        CHECK(fixture.pumpUntil([&fixture] { return !fixture.spoken.empty(); },
                                std::chrono::milliseconds{500}));
        CHECK(fixture.spoken.back().text == "Open button");
    }
}

TEST_CASE("a live region announcement is spoken as a message", "[a11y][events]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Web Browser", "chromium");
    const Ref region = backend.add(application, PlatformRole::Section, "Status");

    backend.emit("object:announcement", region, 0, 0, "Your message was sent");
    fixture.pumpOnce();

    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "Your message was sent");
}

TEST_CASE("a GTK 4 combo box: the shown item is its value, and a new one is said",
          "[a11y][events]") {
    // FAZA10.md N124, the measured shape of AdwComboRow (GTK 4.18): no Value,
    // no Selection; the choice is a label in the one list item, and the arrow
    // keys rename that label.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("alus-settings");
    const Ref window = backend.add(application, PlatformRole::Frame, "Settings");
    backend.addState(window, PlatformState::Active);
    const Ref combo = backend.add(window, PlatformRole::ComboBox, "Voice");
    backend.addState(combo, PlatformState::Focusable);
    const Ref outer = backend.add(combo, PlatformRole::Panel, "");
    const Ref titles = backend.add(outer, PlatformRole::Panel, "");
    backend.add(titles, PlatformRole::Label, "Voice");
    backend.add(titles, PlatformRole::Label, "The voices of the output module above.");
    const Ref box = backend.add(outer, PlatformRole::Panel, "");
    const Ref list = backend.add(box, PlatformRole::List, "");
    const Ref item = backend.add(list, PlatformRole::ListItem, "");
    const Ref itemPanel = backend.add(item, PlatformRole::Panel, "");
    const Ref shown = backend.add(itemPanel, PlatformRole::Label, "Marija (hr)");

    const ObjectPtr object = fixture.registry.objectFor(combo);
    REQUIRE(object != nullptr);
    // Not the title or the subtitle label: the list item's.
    CHECK(object->value() == "Marija (hr)");

    backend.emit("object:state-changed:focused", combo, 1);
    fixture.pumpOnce();
    REQUIRE_FALSE(fixture.spoken.empty());
    CHECK(fixture.lastSpoken().find("Marija (hr)") != std::string::npos);

    fixture.clearRecords();
    backend.node(shown).name = "Ana (sr)";
    backend.emit("object:property-change:accessible-name", shown);
    fixture.pumpOnce();
    REQUIRE(fixture.changes.size() == 1);
    CHECK(fixture.changes.front().ref == combo);
    CHECK(fixture.changes.front().text.find("Ana (sr)") != std::string::npos);

    SECTION("a label renamed outside the focused combo box is not its value") {
        const Ref elsewhere = backend.add(window, PlatformRole::Label, "Status");
        fixture.clearRecords();
        backend.node(elsewhere).name = "Ready";
        backend.emit("object:property-change:accessible-name", elsewhere);
        fixture.pumpOnce();
        CHECK(fixture.changes.empty());
    }
}
