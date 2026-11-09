// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Overlay behaviours: NVDA's overlay classes, by composition (§6.2, §6.3).
//
// Two things are under test here. One is each behaviour's own job. The other
// is the composition contract itself -- ordered, first one wins, a later stage
// may remove what an earlier one proposed -- because that contract is what
// makes app modules and plugins possible in phase 8, and it is easier to get
// right now than to retrofit.
#include "alus/a11y/behaviors.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/overlay_registry.hpp"
#include "alus/a11y/registry.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace alus::testing;
using namespace alus::a11y;

namespace {

// A behaviour that answers everything, to prove that the first one wins.
class LoudBehavior final : public Behavior {
public:
    std::string_view className() const override { return "test.Loud"; }
    std::optional<std::string> name(const Object&) const override { return "from the overlay"; }
    std::optional<Role> role(const Object&) const override { return Role::Dial; }
    std::optional<std::string> value(const Object&) const override { return "loud"; }
    bool handleEvent(Object&, const alus::core::Event&) override {
        ++handled;
        return true;
    }
    static int handled;
};

int LoudBehavior::handled = 0;

class QuietBehavior final : public Behavior {
public:
    std::string_view className() const override { return "test.Quiet"; }
};

}  // namespace

TEST_CASE("a behaviour overrides a property, and the first one wins",
          "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref button = backend.add(backend.desktopRef(), PlatformRole::Button, "Save");

    fixture.registry.overlays()
        .add(OverlayRegistry::Stage::AppModule,
             [](Object&, BehaviorList& list) {
                 list.push_back(behaviorEntry<QuietBehavior>("test.Quiet"));
                 list.push_back(behaviorEntry<LoudBehavior>("test.Loud"));
             },
             "test")
        .release();

    const ObjectPtr object = fixture.registry.objectFor(button);
    REQUIRE(object->behaviors().size() == 2);
    // The quiet one declines, so the loud one answers.
    CHECK(object->name() == "from the overlay");
    CHECK(object->role() == Role::Dial);
    CHECK(object->value() == "loud");
    CHECK(object->hasBehavior("test.Loud"));

    SECTION("and the behaviours join the script class chain") {
        // A gesture map section written for an NVDA behaviour class reaches
        // this object, which is what makes §5.1's bindings portable.
        CHECK(object->answersTo("test.Quiet"));
        CHECK(object->answersTo("test.Loud"));
        CHECK(object->answersTo("NVDAObjects.NVDAObject"));
        REQUIRE(object->scriptClasses().size() == 3);
        CHECK(object->scriptClasses().front() == "test.Quiet");
        CHECK(object->scriptClasses().back() == "NVDAObjects.NVDAObject");
    }
}

TEST_CASE("the three stages run in NVDA's order", "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref panel = backend.add(backend.desktopRef(), PlatformRole::Panel, "Group");
    std::vector<std::string> order;

    auto api = fixture.registry.overlays().add(
        OverlayRegistry::Stage::Api,
        [&order](Object&, BehaviorList&) { order.emplace_back("api"); }, "api");
    auto appModule = fixture.registry.overlays().add(
        OverlayRegistry::Stage::AppModule,
        [&order](Object&, BehaviorList&) { order.emplace_back("appModule"); }, "app");
    auto plugin = fixture.registry.overlays().add(
        OverlayRegistry::Stage::Plugin,
        [&order](Object&, BehaviorList&) { order.emplace_back("plugin"); }, "plugin");

    (void)fixture.registry.objectFor(panel);
    // The built-in Api chooser runs too (first, before this test's Api-stage
    // one); it does not record itself.
    REQUIRE(order.size() == 3);
    CHECK(order[0] == "api");
    CHECK(order[1] == "appModule");
    CHECK(order[2] == "plugin");
}

TEST_CASE("a later stage can remove and reorder what an earlier one proposed",
          "[a11y][behavior]") {
    // NVDA: clsList.remove(...) and clsList[0:0] = (...). An app module that
    // cannot undo the platform layer's guess is an app module that cannot fix
    // a broken application.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref dialog = backend.add(backend.desktopRef(), PlatformRole::Dialog, "Save as");

    auto handle = fixture.registry.overlays().add(
        OverlayRegistry::Stage::AppModule,
        [](Object&, BehaviorList& list) {
            CHECK(hasBehavior(list, "NVDAObjects.behaviors.Dialog"));
            CHECK(removeBehavior(list, "NVDAObjects.behaviors.Dialog"));
            prependBehavior(list, behaviorEntry<LoudBehavior>("test.Loud"));
        },
        "app");

    const ObjectPtr object = fixture.registry.objectFor(dialog);
    REQUIRE(object->behaviors().size() == 1);
    CHECK(object->behaviors().front()->className() == "test.Loud");
    CHECK_FALSE(object->hasBehavior("NVDAObjects.behaviors.Dialog"));
}

TEST_CASE("a chooser handle deregisters", "[a11y][behavior]") {
    A11yFixture fixture;
    const std::size_t before = fixture.registry.overlays().chooserCount();
    {
        auto handle = fixture.registry.overlays().add(
            OverlayRegistry::Stage::Plugin, [](Object&, BehaviorList&) {}, "temporary");
        CHECK(fixture.registry.overlays().chooserCount() == before + 1);
    }
    CHECK(fixture.registry.overlays().chooserCount() == before);
}

TEST_CASE("the default behaviours are chosen by role", "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref bar = backend.add(backend.desktopRef(), PlatformRole::ProgressBar, "Copying");
    const Ref dialog = backend.add(backend.desktopRef(), PlatformRole::Dialog, "Save as");
    const Ref tip = backend.add(backend.desktopRef(), PlatformRole::ToolTip, "Save the file");
    const Ref toast = backend.add(backend.desktopRef(), PlatformRole::Notification, "Sent");
    const Ref button = backend.add(backend.desktopRef(), PlatformRole::Button, "Save");

    CHECK(fixture.registry.objectFor(bar)->hasBehavior("NVDAObjects.behaviors.ProgressBar"));
    CHECK(fixture.registry.objectFor(dialog)->hasBehavior("NVDAObjects.behaviors.Dialog"));
    CHECK(fixture.registry.objectFor(tip)->hasBehavior("NVDAObjects.behaviors.ToolTip"));
    // A notification is both: it announces itself when it shows, and its text
    // is assembled from its children like a dialog's.
    const ObjectPtr notification = fixture.registry.objectFor(toast);
    CHECK(notification->hasBehavior("NVDAObjects.behaviors.Notification"));
    CHECK(notification->hasBehavior("NVDAObjects.behaviors.Dialog"));
    // A plain button gets none, and pays nothing.
    CHECK(fixture.registry.objectFor(button)->behaviors().empty());
}

TEST_CASE("a tooltip is announced when it shows, if the user asked",
          "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Text Editor");
    const Ref tip = backend.add(application, PlatformRole::ToolTip, "Save the file");

    // Off by default, as in NVDA: a tooltip under the pointer is not what a
    // keyboard user is doing.
    REQUIRE_FALSE(fixture.registry.settings().reportTooltips);
    backend.emit("window:create", tip);
    fixture.pumpOnce();
    CHECK(fixture.spoken.empty());

    fixture.registry.settings().reportTooltips = true;
    backend.emit("window:create", tip);
    fixture.pumpOnce();
    REQUIRE(fixture.spoken.size() == 1);
    CHECK(fixture.spoken.front().text == "Save the file tool tip");
}

TEST_CASE("a notification announces itself", "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Chat");
    const Ref toast = backend.add(application, PlatformRole::Notification, "New message");

    REQUIRE(fixture.registry.settings().reportHelpBalloons);
    backend.emit("window:create", toast);
    fixture.pumpOnce();
    REQUIRE(fixture.spoken.size() == 1);
    CHECK(fixture.spoken.front().text == "New message alert");

    SECTION("unless the user switched notifications off") {
        fixture.registry.settings().reportHelpBalloons = false;
        fixture.clearRecords();
        backend.emit("window:create", toast);
        fixture.pumpOnce();
        CHECK(fixture.spoken.empty());
    }
}

TEST_CASE("a dialog's description is assembled from its children",
          "[a11y][behavior]") {
    // A GTK dialog puts its message in labels, not in its own description.
    // Without this a user hears "Save as dialog" and nothing about what it
    // asks.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Text Editor");
    const Ref dialog = backend.add(application, PlatformRole::Dialog, "Save as");
    backend.add(dialog, PlatformRole::Label, "The document has unsaved changes.");
    backend.add(dialog, PlatformRole::Static, "Save before closing?");
    // Buttons are not part of the message.
    const Ref save = backend.add(dialog, PlatformRole::Button, "Save");
    backend.addState(save, PlatformState::Focusable);
    // Neither is an invisible label.
    const Ref hidden = backend.add(dialog, PlatformRole::Label, "Not shown");
    backend.removeState(hidden, PlatformState::Visible);

    const ObjectPtr object = fixture.registry.objectFor(dialog);
    CHECK(object->description() ==
          "The document has unsaved changes.\nSave before closing?");
    // And a dialog has no value, whatever the toolkit put there.
    CHECK(object->value().empty());

    SECTION("a description of its own is left alone") {
        backend.node(dialog).description = "Choose where to save";
        alus::core::invalidateAllCaches();
        CHECK(fixture.registry.objectFor(dialog)->description() == "Choose where to save");
    }

    SECTION("a nested group is descended into") {
        const Ref group = backend.add(dialog, PlatformRole::Panel);
        backend.add(group, PlatformRole::Label, "Deeper text");
        alus::core::invalidateAllCaches();
        CHECK(fixture.registry.objectFor(dialog)->description() ==
              "The document has unsaved changes.\nSave before closing?\nDeeper text");
    }
}

TEST_CASE("a progress bar beeps, and not too often", "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref window = backend.add(application, PlatformRole::Frame, "Copying");
    backend.addState(window, PlatformState::Active);
    const Ref bar = backend.add(window, PlatformRole::ProgressBar, "Progress");
    backend.addInterface(bar, Interface::Value);
    backend.node(bar).value = ValueInfo{10.0, 0.0, 100.0, 1.0, ""};
    // The foreground has to be known for "background progress bars" to mean
    // anything.
    REQUIRE(fixture.registry.api().setForegroundObject(
        fixture.registry.objectFor(window)));

    backend.emit("object:value-changed", bar);
    fixture.pumpOnce();
    REQUIRE(fixture.progress.size() == 1);
    CHECK(fixture.progress.front().percentage == 10.0);
    // Beep by default, and no speech: NVDA's own default, and the only one
    // that is bearable for a long download.
    CHECK(fixture.progress.front().beep);
    CHECK_FALSE(fixture.progress.front().speak);

    SECTION("the same value again says nothing") {
        fixture.clearRecords();
        backend.emit("object:value-changed", bar);
        fixture.pumpOnce();
        CHECK(fixture.progress.empty());
    }

    SECTION("a percentage apart is a new beep") {
        fixture.clearRecords();
        backend.node(bar).value = ValueInfo{11.0, 0.0, 100.0, 1.0, ""};
        backend.emit("object:value-changed", bar);
        fixture.pumpOnce();
        REQUIRE(fixture.progress.size() == 1);
        CHECK(fixture.progress.front().percentage == 11.0);
    }

    SECTION("speech waits for the bigger interval") {
        fixture.registry.settings().progressBarOutput = ProgressOutput::Both;
        ProgressBarBehavior::clearCache();
        fixture.clearRecords();

        backend.node(bar).value = ValueInfo{20.0, 0.0, 100.0, 1.0, ""};
        backend.emit("object:value-changed", bar);
        fixture.pumpOnce();
        REQUIRE(fixture.progress.size() == 1);
        CHECK(fixture.progress.front().speak);

        // Five percent later: a beep, but not a word.
        fixture.clearRecords();
        backend.node(bar).value = ValueInfo{25.0, 0.0, 100.0, 1.0, ""};
        backend.emit("object:value-changed", bar);
        fixture.pumpOnce();
        REQUIRE(fixture.progress.size() == 1);
        CHECK(fixture.progress.front().beep);
        CHECK_FALSE(fixture.progress.front().speak);
    }

    SECTION("switched off entirely, the value change is announced as a value") {
        fixture.registry.settings().progressBarOutput = ProgressOutput::Off;
        fixture.clearRecords();
        backend.node(bar).value = ValueInfo{50.0, 0.0, 100.0, 1.0, ""};
        backend.emit("object:value-changed", bar);
        fixture.pumpOnce();
        CHECK(fixture.progress.empty());
    }
}

TEST_CASE("a progress bar in another window is silent", "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref foreground = backend.add(application, PlatformRole::Frame, "Home");
    backend.addState(foreground, PlatformState::Active);
    const Ref background = backend.add(application, PlatformRole::Frame, "Copying");
    const Ref bar = backend.add(background, PlatformRole::ProgressBar, "Progress");
    backend.addInterface(bar, Interface::Value);
    backend.node(bar).value = ValueInfo{10.0, 0.0, 100.0, 1.0, ""};
    REQUIRE(fixture.registry.api().setForegroundObject(
        fixture.registry.objectFor(foreground)));

    REQUIRE_FALSE(fixture.registry.settings().reportBackgroundProgressBars);
    backend.emit("object:value-changed", bar);
    fixture.pumpOnce();
    // A download in another window must not interrupt what the user is doing.
    CHECK(fixture.progress.empty());

    SECTION("unless the user asked for them") {
        fixture.registry.settings().reportBackgroundProgressBars = true;
        backend.emit("object:value-changed", bar);
        fixture.pumpOnce();
        CHECK(fixture.progress.size() == 1);
    }
}

TEST_CASE("an unfocusable container can be focused through a descendant",
          "[a11y][behavior]") {
    // An ARIA application the author never made focusable.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref container = backend.add(backend.desktopRef(), PlatformRole::Section, "Editor");
    backend.node(container).attributes = {{"xml-roles", "application"}};
    const Ref inner = backend.add(container, PlatformRole::Button, "Bold");
    backend.addState(inner, PlatformState::Focusable);

    const ObjectPtr object = fixture.registry.objectFor(container);
    REQUIRE(object->hasBehavior("NVDAObjects.behaviors.FocusableUnfocusableContainer"));
    CHECK(object->isFocusable());
    CHECK(object->setFocus());
    REQUIRE(fixture.backend.focused.size() == 1);
    // The descendant took the focus, not the container.
    CHECK(fixture.backend.focused.front() == inner);

    SECTION("with nothing focusable inside, it says so") {
        fixture.backend.removeState(inner, PlatformState::Focusable);
        alus::core::invalidateAllCaches();
        CHECK_FALSE(fixture.registry.objectFor(container)->setFocus());
    }
}

TEST_CASE("a behaviour that handles an event stops the chain", "[a11y][behavior]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Settings");
    const Ref window = backend.add(application, PlatformRole::Frame, "Settings");
    backend.addState(window, PlatformState::Active);
    const Ref button = backend.add(window, PlatformRole::Button, "Save");
    backend.addState(button, PlatformState::Focusable);

    LoudBehavior::handled = 0;
    fixture.registry.overlays()
        .add(OverlayRegistry::Stage::Plugin,
             [](Object&, BehaviorList& list) {
                 list.push_back(behaviorEntry<LoudBehavior>("test.Loud"));
             },
             "test")
        .release();

    backend.emit("object:state-changed:focused", button, 1);
    fixture.pumpOnce();

    CHECK(LoudBehavior::handled > 0);
    // The object's own handling never ran, so nothing was spoken for the
    // focus -- which is what "do not call super()" means.
    for (const A11yFixture::SpokenObject& spoken : fixture.spoken) {
        CHECK(spoken.ref != button);
    }
}
