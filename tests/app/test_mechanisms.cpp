// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T8.2, T8.3 and the rest of §12.2: the five customisation mechanisms and the
// three generalised hooks, each tested THROUGH the service.
//
// Every test here drives the mock platform and asserts on what the app layer
// did to the result, rather than calling a module's method directly. That is
// deliberate: a mechanism that works but is never reached is the failure mode
// this phase has, and it is invisible to a test that calls the module itself
// (tests/app/support.hpp says more).
#include "alus/a11y/events.hpp"
#include "alus/app/modules.hpp"
#include "alus/script/dispatch.hpp"
#include "app_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace alus;
using namespace alus::testing;

namespace {

constexpr const char* kToolkit = "testkit";

// A behaviour that answers a property, so that a test can see whether the
// module's proposal reached the object.
class LoudBehavior final : public a11y::Behavior {
public:
    std::string_view className() const override { return "test.Loud"; }
    std::optional<std::string> value(const a11y::Object&) const override { return "loud"; }
};

// The module every test here installs: a Module whose hooks are std::functions
// the test fills in, so that one module class serves all of them.
class ProbeModule final : public app::Module {
public:
    ProbeModule(app::AppKey key, app::Services services)
        : Module("probe", std::move(key), std::move(services),
                 {"appModules.probe.AppModule"}) {}

    void initialize() override {
        ++initializeCalls;
        ALUS_SCRIPT(probeScript, .description = "A test script.", .category = "Testing",
                    .gestures = {"kb:alus+shift+p"});
    }

    void chooseOverlayClasses(a11y::Object& object, a11y::BehaviorList& list) override {
        ++overlayCalls;
        if (chooseOverlays) {
            chooseOverlays(object, list);
        }
    }

    void objectInit(a11y::Object& object) override {
        initialisedObjects.push_back(object.name());
    }

    bool shouldAllowFocusEvent(const a11y::ObjectPtr& object) const override {
        if (allowFocus) {
            return allowFocus(object);
        }
        return true;
    }

    a11y::ObjectPtr container(const a11y::Object& object) const override {
        if (containerOf) {
            return containerOf(object);
        }
        return nullptr;
    }

    void appGainFocus() override { ++gainFocusCalls; }
    void appLoseFocus() override { ++loseFocusCalls; }

    void probeScript(input::Gesture&) { ++scriptCalls; }

    std::function<void(a11y::Object&, a11y::BehaviorList&)> chooseOverlays;
    std::function<bool(const a11y::ObjectPtr&)> allowFocus;
    std::function<a11y::ObjectPtr(const a11y::Object&)> containerOf;
    std::vector<std::string> initialisedObjects;
    int initializeCalls = 0;
    int overlayCalls = 0;
    int gainFocusCalls = 0;
    int loseFocusCalls = 0;
    int scriptCalls = 0;

    // The module the registry made, so a test can reach its probes. Valid for
    // as long as the fixture lives.
    static ProbeModule* last;
};

ProbeModule* ProbeModule::last = nullptr;

// Installs the probe rule on a fixture, keyed on the TOOLKIT so that it works
// through the real service (the executable of a test process is the test
// binary; tests/app/support.hpp says why).
void installProbe(AppFixture& fixture) {
    ProbeModule::last = nullptr;
    fixture.modules().add(app::Registry::Rule{
        "probe", "", kToolkit,
        [](const app::AppKey& key, const app::Services& services) -> app::ModulePtr {
            auto module = std::make_unique<ProbeModule>(key, services);
            ProbeModule::last = module.get();
            return module;
        }});
}

}  // namespace

TEST_CASE("[T8.2] chooseOverlayClasses can prepend and remove", "[app][overlay]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);

    SECTION("prepend: the module's behaviour wins over the platform") {
        const a11y::Ref button =
            fixture.backend.add(application, a11y::PlatformRole::Button, "Save");
        // The module gets the object before anyone has asked it anything.
        fixture.modules().moduleFor(fixture.backend.node(application).app);
        REQUIRE(ProbeModule::last != nullptr);
        ProbeModule::last->chooseOverlays = [](a11y::Object&, a11y::BehaviorList& list) {
            a11y::prependBehavior(list, a11y::behaviorEntry<LoudBehavior>("test.Loud"));
        };

        const a11y::ObjectPtr object = fixture.registry.objectFor(button);
        REQUIRE(object != nullptr);
        CHECK(ProbeModule::last->overlayCalls == 1);
        CHECK(object->hasBehavior("test.Loud"));
        // NVDA's clsList[0:0]: at the front, so its answer is the one used.
        CHECK(object->value() == "loud");
        // And the name is in the script class chain, so a gesture map section
        // written for it reaches this object.
        CHECK(object->answersTo("test.Loud"));
    }

    SECTION("remove: the module takes away what the platform layer proposed") {
        // A progress bar gets the ProgressBar behaviour from the Api stage
        // (phase 4's §6.3). An application that reports progress in a way the
        // user does not want announced is exactly why a module may remove it.
        const a11y::Ref bar =
            fixture.backend.add(application, a11y::PlatformRole::ProgressBar, "Copying");
        fixture.modules().moduleFor(fixture.backend.node(application).app);
        REQUIRE(ProbeModule::last != nullptr);
        bool wasThere = false;
        ProbeModule::last->chooseOverlays = [&wasThere](a11y::Object&,
                                                        a11y::BehaviorList& list) {
            wasThere = a11y::hasBehavior(list, "NVDAObjects.behaviors.ProgressBar");
            a11y::removeBehavior(list, "NVDAObjects.behaviors.ProgressBar");
        };

        const a11y::ObjectPtr object = fixture.registry.objectFor(bar);
        REQUIRE(object != nullptr);
        // The point of the test: the module SAW the platform's proposal, which
        // is what "each stage sees what the previous one proposed" means.
        CHECK(wasThere);
        CHECK_FALSE(object->hasBehavior("NVDAObjects.behaviors.ProgressBar"));
    }
}

TEST_CASE("[T8.3] an event handler that does not continue eats the event",
          "[app][events]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe window");
    const a11y::Ref button =
        fixture.backend.add(window, a11y::PlatformRole::Button, "Save");

    app::Module* module = fixture.modules().moduleFor(fixture.backend.node(application).app);
    REQUIRE(module != nullptr);

    // The object has to have the focus for a name change to be announced at
    // all (object.cpp: NVDA announces a property change only on the focus), or
    // the test would compare silence with silence.
    fixture.backend.emit("object:state-changed:focused", button, 1);
    fixture.pumpOnce();
    REQUIRE(fixture.registry.api().focus() != nullptr);
    REQUIRE(fixture.registry.api().focus()->ref() == button);

    int seen = 0;
    module->bindEvent(std::string{a11y::events::kNameChange},
                      [&seen](const a11y::ObjectPtr&) {
                          ++seen;
                          // NVDA: not calling nextHandler.
                          return app::Module::Disposition::Consume;
                      });

    fixture.clearRecords();
    fixture.backend.node(button).name = "Saved";
    fixture.backend.emit("object:property-change:accessible-name", button);
    fixture.pumpOnce();

    CHECK(seen == 1);
    // The object layer never heard about it, so nothing was announced.
    CHECK(fixture.changes.empty());
    CHECK(fixture.spoken.empty());

    SECTION("and a handler that continues does not") {
        module->bindEvent(std::string{a11y::events::kNameChange},
                          [&seen](const a11y::ObjectPtr&) {
                              ++seen;
                              return app::Module::Disposition::Continue;
                          });
        fixture.clearRecords();
        fixture.backend.node(button).name = "Saved again";
        fixture.backend.emit("object:property-change:accessible-name", button);
        fixture.pumpOnce();
        CHECK(seen == 2);
        // The chain ran to the end: the object announced the change.
        CHECK_FALSE(fixture.changes.empty());
    }
}

TEST_CASE("objectInit runs once per object, after the behaviours", "[app][objectinit]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref button =
        fixture.backend.add(application, a11y::PlatformRole::Button, "Save");
    fixture.modules().moduleFor(fixture.backend.node(application).app);
    REQUIRE(ProbeModule::last != nullptr);
    ProbeModule::last->initialisedObjects.clear();

    const a11y::ObjectPtr object = fixture.registry.objectFor(button);
    REQUIRE(object != nullptr);
    REQUIRE(ProbeModule::last->initialisedObjects.size() == 1);
    CHECK(ProbeModule::last->initialisedObjects.front() == "Save");

    // Asked for again in the same cycle: the same object, so no second init.
    CHECK(fixture.registry.objectFor(button) == object);
    CHECK(ProbeModule::last->initialisedObjects.size() == 1);
}

TEST_CASE("shouldAllowFocusEvent suppresses a focus event completely",
          "[app][focus]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe window");
    const a11y::Ref good = fixture.backend.add(window, a11y::PlatformRole::Button, "Real");
    const a11y::Ref spurious =
        fixture.backend.add(window, a11y::PlatformRole::Button, "Spurious");
    fixture.modules().moduleFor(fixture.backend.node(application).app);
    REQUIRE(ProbeModule::last != nullptr);
    ProbeModule::last->allowFocus = [](const a11y::ObjectPtr& object) {
        return object == nullptr || object->name() != "Spurious";
    };

    fixture.clearRecords();
    fixture.backend.emit("object:state-changed:focused", spurious, 1);
    fixture.pumpOnce();
    // NOTHING happened: not the announcement, and not the focus itself.
    CHECK(fixture.spoken.empty());
    CHECK(fixture.registry.api().focus() == nullptr);

    fixture.backend.emit("object:state-changed:focused", good, 1);
    fixture.pumpOnce();
    REQUIRE(fixture.registry.api().focus() != nullptr);
    CHECK(fixture.registry.api().focus()->name() == "Real");
}

TEST_CASE("lastQueuedFocusObject is the duplicate detector", "[app][focus]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe window");
    const a11y::Ref first = fixture.backend.add(window, a11y::PlatformRole::Button, "First");
    const a11y::Ref second =
        fixture.backend.add(window, a11y::PlatformRole::Button, "Second");
    fixture.modules().moduleFor(fixture.backend.node(application).app);
    REQUIRE(ProbeModule::last != nullptr);

    // The rule an app module writes with it: a focus event for the object we
    // were already told about is a duplicate.
    int refused = 0;
    ProbeModule::last->allowFocus = [&fixture, &refused](const a11y::ObjectPtr& object) {
        const a11y::ObjectPtr& last = fixture.registry.lastQueuedFocusObject();
        if (last != nullptr && object != nullptr && last->sameAs(*object) &&
            fixture.registry.api().focus() != nullptr &&
            fixture.registry.api().focus()->sameAs(*object)) {
            ++refused;
            return false;
        }
        return true;
    };

    fixture.backend.emit("object:state-changed:focused", first, 1);
    fixture.pumpOnce();
    REQUIRE(fixture.registry.api().focus() != nullptr);
    CHECK(fixture.registry.api().focus()->name() == "First");
    // It is set at QUEUE time, which is what makes it usable before the event
    // has been dispatched.
    REQUIRE(fixture.registry.lastQueuedFocusObject() != nullptr);
    CHECK(fixture.registry.lastQueuedFocusObject()->name() == "First");

    // The same object again: refused as a duplicate.
    fixture.backend.emit("object:state-changed:focused", first, 1);
    fixture.pumpOnce();
    CHECK(refused == 1);

    // A different object is not.
    fixture.backend.emit("object:state-changed:focused", second, 1);
    fixture.pumpOnce();
    CHECK(refused == 1);
    CHECK(fixture.registry.api().focus()->name() == "Second");
}

TEST_CASE("the container override reshapes the ancestry", "[app][container]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref table =
        fixture.backend.add(application, a11y::PlatformRole::Table, "Sheet");
    const a11y::Ref row = fixture.backend.add(table, a11y::PlatformRole::TableRow, "Row 1");
    const a11y::Ref cell = fixture.backend.add(row, a11y::PlatformRole::TableCell, "A1");
    fixture.modules().moduleFor(fixture.backend.node(application).app);
    REQUIRE(ProbeModule::last != nullptr);

    const a11y::ObjectPtr cellObject = fixture.registry.objectFor(cell);
    REQUIRE(cellObject != nullptr);
    // The default is the parent, as phase 4 left it.
    REQUIRE(cellObject->container() != nullptr);
    CHECK(cellObject->container()->name() == "Row 1");

    // §12.2's own example: a cell's container is its table, not its row.
    ProbeModule::last->containerOf = [&fixture](const a11y::Object& object) {
        if (object.role() != a11y::Role::TableCell) {
            return a11y::ObjectPtr{};
        }
        const a11y::ObjectPtr parent = object.parent();
        return parent != nullptr ? parent->parent() : a11y::ObjectPtr{};
    };
    core::invalidateAllCaches();
    const a11y::ObjectPtr again = fixture.registry.objectFor(cell);
    REQUIRE(again->container() != nullptr);
    CHECK(again->container()->name() == "Sheet");
}

TEST_CASE("sleep mode silences the application but keeps the focus",
          "[app][sleep]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe window");
    const a11y::Ref button = fixture.backend.add(window, a11y::PlatformRole::Button, "Save");
    app::Module* module = fixture.modules().moduleFor(fixture.backend.node(application).app);
    REQUIRE(module != nullptr);

    fixture.clearRecords();
    module->setSleepMode(true);
    fixture.backend.emit("object:state-changed:focused", button, 1);
    fixture.pumpOnce();
    // Nothing was said...
    CHECK(fixture.spoken.empty());
    // ...but the focus is known, so the user can still navigate and wake ALUS
    // up again. This is the difference between sleep mode and a broken screen
    // reader.
    REQUIRE(fixture.registry.api().focus() != nullptr);
    CHECK(fixture.registry.api().focus()->name() == "Save");

    module->setSleepMode(false);
    fixture.clearRecords();
    const a11y::Ref other = fixture.backend.add(window, a11y::PlatformRole::Button, "Open");
    fixture.backend.emit("object:state-changed:focused", other, 1);
    fixture.pumpOnce();
    CHECK_FALSE(fixture.spoken.empty());
}

TEST_CASE("the app module is step 3 of the script lookup", "[app][scripts]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref button =
        fixture.backend.add(application, a11y::PlatformRole::Button, "Save");

    const a11y::ObjectPtr object = fixture.registry.objectFor(button);
    REQUIRE(object != nullptr);
    // What finally makes `Object::appModule()` answer: the provider the app
    // service installed. Until phase 8 this was null by construction.
    script::ScriptableObject* asScriptable = object->appModule();
    REQUIRE(asScriptable != nullptr);
    CHECK(asScriptable->answersTo("appModules.probe.AppModule"));
    CHECK(asScriptable->answersTo(std::string{app::Module::kBaseClass}));
    // Mechanism 4: the module declared its own script and its own gesture.
    REQUIRE(asScriptable->scriptByName("probeScript") != nullptr);
    const script::ScriptInfo* info = asScriptable->scriptByName("probeScript")->info;
    REQUIRE(info != nullptr);
    CHECK(info->location == "appModules.probe.AppModule");
    REQUIRE(info->gestures.size() == 1);
    CHECK(info->gestures.front() == "kb:alus+shift+p");
}

TEST_CASE("the status line comes from the module (mechanism 5)", "[app][statusbar]") {
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe window");
    fixture.backend.addState(window, a11y::PlatformState::Active);
    fixture.backend.add(window, a11y::PlatformRole::Button, "Save");
    const a11y::Ref bar = fixture.backend.add(window, a11y::PlatformRole::StatusBar, "");
    fixture.backend.add(bar, a11y::PlatformRole::Label, "Page 1 of 4");
    fixture.backend.add(bar, a11y::PlatformRole::Label, "1200 words");

    app::Module* module = fixture.modules().moduleFor(fixture.backend.node(application).app);
    REQUIRE(module != nullptr);
    // NVDA's own rule: the status bar is the window's last child with that
    // role, and its text is its children's names joined.
    REQUIRE(module->statusBar() != nullptr);
    CHECK(module->statusBarText() == "Page 1 of 4 1200 words");
}

TEST_CASE("the app layer picks up the focus that already exists", "[app][restart]") {
    // The case the live run found: this layer comes up LAST, so the object
    // model has already found the initial focus and dispatched its event
    // through a chain the app layer had not yet joined. Without the pickup,
    // the application the user is already in never gets its app switch -- its
    // profile trigger does not fire and `alus+end` answers "no focused
    // application" until the user alt-tabs away and back.
    //
    // It is the same code path as SIGHUP, which is why one test covers both.
    AppFixture fixture;
    installProbe(fixture);
    const a11y::Ref application = fixture.addApplication("Probe", kToolkit);
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe window");
    const a11y::Ref button = fixture.backend.add(window, a11y::PlatformRole::Button, "Save");

    fixture.backend.emit("object:state-changed:focused", button, 1);
    fixture.pumpOnce();
    REQUIRE(fixture.registry.api().focus() != nullptr);
    REQUIRE(fixture.service->currentModule() != nullptr);

    // SIGHUP: down and up again, with the focus unchanged throughout.
    fixture.service->terminate();
    CHECK(fixture.service->currentModule() == nullptr);
    fixture.service->initialize();

    REQUIRE(fixture.service->modules() != nullptr);
    REQUIRE(fixture.service->currentModule() != nullptr);
    // A restarted layer has its own registry, so the module is a new one --
    // but there IS one, and it is the right application's.
    CHECK(fixture.service->currentModule()->key().processId ==
          fixture.registry.api().focus()->processId());
    CHECK(fixture.service->modules()->switchCount() == 1);
}
