// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Object navigation: the commands of the phase 4 manual test.
//
//     alus+tab      what has the focus
//     alus+numpad5  what is the navigator object
//     alus+numpad8  up        alus+numpad2  down (into)
//     alus+numpad4  previous  alus+numpad6  next
//     alus+numpadEnter  activate
//
// The keys themselves come from data/gestures-builtin.ini, generated from
// NVDA's own assignments in phase 3; what is tested here is that the scripts
// exist under NVDA's names, carry NVDA's descriptions, and do what their
// descriptions say. The edges matter as much as the moves: a navigation that
// runs out of tree must SAY so (§3.8), because silence is indistinguishable
// from a broken screen reader.
#include "alus/a11y/commands.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/input/gesture.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/script.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace alus::testing;
using namespace alus::a11y;

namespace {

// The tree the manual test walks: a window with a toolbar and a list.
struct NavigationTree {
    explicit NavigationTree(MockBackend& backend) {
        application = backend.addApplication("Files");
        window = backend.add(application, PlatformRole::Frame, "Home");
        backend.addState(window, PlatformState::Active);
        toolbar = backend.add(window, PlatformRole::ToolBar, "Main");
        back = backend.add(toolbar, PlatformRole::Button, "Back");
        forward = backend.add(toolbar, PlatformRole::Button, "Forward");
        list = backend.add(window, PlatformRole::List, "Files");
        first = backend.add(list, PlatformRole::ListItem, "Documents");
        second = backend.add(list, PlatformRole::ListItem, "Downloads");
        for (const Ref control : {back, forward, first, second}) {
            backend.addState(control, PlatformState::Focusable);
        }
        backend.node(back).actionNames = {"click"};
        backend.addInterface(back, Interface::Action);
    }

    Ref application = 0;
    Ref window = 0;
    Ref toolbar = 0;
    Ref back = 0;
    Ref forward = 0;
    Ref list = 0;
    Ref first = 0;
    Ref second = 0;
};

// The scripts are invoked directly: the gesture pipeline that would find them
// is phase 3's, and it has its own tests.
class CommandFixture : public A11yFixture {
public:
    CommandFixture() {
        // The real catalogue, so the descriptions and categories under test
        // are the generated ones and not something retyped here.
        catalogLoaded = catalog.loadFromDataDirectory(
            std::filesystem::path{ALUS_TEST_DATA_DIR});
        commands = std::make_unique<ObjectCommands>(registry, &catalog, nullptr);
    }

    void run(const std::string& name) {
        const alus::script::Script* script = commands->scriptByName(name);
        REQUIRE(script != nullptr);
        alus::input::KeyboardGesture::Init init;
        alus::input::KeyboardGesture gesture{std::move(init)};
        script->function(gesture);
    }

    // Loading the catalogue logs how many scripts it found; a hundred test
    // runs of that is noise.
    QuietLog quiet;
    alus::script::Catalog catalog;
    bool catalogLoaded = false;
    std::unique_ptr<ObjectCommands> commands;
};

}  // namespace

TEST_CASE("the object navigation scripts exist with NVDA's metadata",
          "[a11y][commands]") {
    CommandFixture fixture;
    REQUIRE(fixture.catalogLoaded);

    const alus::script::Script* next = fixture.commands->scriptByName("navigatorObject_next");
    REQUIRE(next != nullptr);
    CHECK(next->info->description == "Moves the navigator object to the next object");
    CHECK(next->info->category == "Object navigation");
    // The key assignment comes from the generated built-in map, under NVDA's
    // own name for the ALUS key.
    CHECK(next->info->location == "globalCommands.GlobalCommands");

    const alus::script::Script* focus = fixture.commands->scriptByName("reportCurrentFocus");
    REQUIRE(focus != nullptr);
    CHECK(focus->info->category == "System focus");
    // NVDA marks it speak-on-demand: it answers even in on-demand speech
    // mode, because the user asked.
    CHECK(focus->info->speakOnDemand);

    // Every script the phase 4 manual test presses.
    for (const char* name : {"reportCurrentFocus", "navigatorObject_current",
                             "navigatorObject_parent", "navigatorObject_firstChild",
                             "navigatorObject_next", "navigatorObject_previous",
                             "navigatorObject_nextInFlow", "navigatorObject_previousInFlow",
                             "navigatorObject_toFocus", "navigatorObject_moveFocus",
                             "navigatorObject_currentDimensions", "review_activate", "title"}) {
        CHECK(fixture.commands->scriptByName(name) != nullptr);
    }
}

TEST_CASE("alus+tab reports the focus", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    REQUIRE(fixture.registry.api().setFocusObject(fixture.registry.objectFor(tree.back)));
    fixture.clearRecords();

    fixture.run("reportCurrentFocus");
    REQUIRE(fixture.spoken.size() == 1);
    CHECK(fixture.spoken.front().reason == OutputReason::Query);
    CHECK(fixture.spoken.front().text == "Back button");

    SECTION("and says so when there is none") {
        fixture.registry.api().clear();
        fixture.clearRecords();
        fixture.run("reportCurrentFocus");
        CHECK(fixture.messages.size() == 1);
        CHECK(fixture.messages.front() == "No focus");
    }
}

TEST_CASE("the numpad walks the tree", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    Api& api = fixture.registry.api();
    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.back)));
    fixture.clearRecords();

    SECTION("alus+numpad6 to the next object") {
        fixture.run("navigatorObject_next");
        CHECK(api.navigator()->id() == tree.forward);
        REQUIRE(fixture.spoken.size() == 1);
        CHECK(fixture.spoken.front().text == "Forward button");
    }

    SECTION("alus+numpad4 to the previous object") {
        REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.forward)));
        fixture.clearRecords();
        fixture.run("navigatorObject_previous");
        CHECK(api.navigator()->id() == tree.back);
    }

    SECTION("alus+numpad8 up to the containing object") {
        fixture.run("navigatorObject_parent");
        CHECK(api.navigator()->id() == tree.toolbar);
        REQUIRE(fixture.spoken.size() == 1);
        CHECK(fixture.spoken.front().text == "Main tool bar");
    }

    SECTION("alus+numpad2 down into the first object inside") {
        REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.list)));
        fixture.clearRecords();
        fixture.run("navigatorObject_firstChild");
        CHECK(api.navigator()->id() == tree.first);
        REQUIRE(fixture.spoken.size() == 1);
        CHECK(fixture.spoken.front().text == "Documents 1 of 2");
    }
}

TEST_CASE("the edges of the tree are spoken, not silent", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    Api& api = fixture.registry.api();
    REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.back)));
    fixture.clearRecords();

    fixture.run("navigatorObject_previous");
    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "No previous");
    // And the navigator object did not move.
    CHECK(api.navigator()->id() == tree.back);

    fixture.clearRecords();
    REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.forward)));
    fixture.clearRecords();
    fixture.run("navigatorObject_next");
    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "No next");

    fixture.clearRecords();
    fixture.run("navigatorObject_firstChild");
    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "No objects inside");

    fixture.clearRecords();
    REQUIRE(api.setNavigatorObject(fixture.registry.desktop()));
    fixture.clearRecords();
    fixture.run("navigatorObject_parent");
    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "No containing object");
}

TEST_CASE("the flattened view walks the whole tree", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    Api& api = fixture.registry.api();
    REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.back)));

    // From a button: into nothing, so on to the next sibling.
    fixture.run("navigatorObject_nextInFlow");
    CHECK(api.navigator()->id() == tree.forward);
    // From the LAST button of the toolbar: out of the toolbar and on to the
    // list, which is what "flattened" means.
    fixture.run("navigatorObject_nextInFlow");
    CHECK(api.navigator()->id() == tree.list);
    // And into the list.
    fixture.run("navigatorObject_nextInFlow");
    CHECK(api.navigator()->id() == tree.first);

    SECTION("and backwards, into the deepest previous descendant") {
        REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.list)));
        fixture.run("navigatorObject_previousInFlow");
        // Not the toolbar: its last child.
        CHECK(api.navigator()->id() == tree.forward);
    }
}

TEST_CASE("the navigator and the focus can be brought together",
          "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    Api& api = fixture.registry.api();
    REQUIRE(api.setFocusObject(fixture.registry.objectFor(tree.back)));
    REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.second)));
    fixture.clearRecords();

    SECTION("alus+numpadMinus brings the navigator to the focus") {
        fixture.run("navigatorObject_toFocus");
        CHECK(api.navigator()->id() == tree.back);
        CHECK(fixture.messages.front() == "Move to focus");
    }

    SECTION("alus+shift+numpadMinus brings the focus to the navigator") {
        fixture.run("navigatorObject_moveFocus");
        REQUIRE(fixture.backend.focused.size() == 1);
        CHECK(fixture.backend.focused.front() == tree.second);
        CHECK(fixture.messages.front() == "Move focus");
    }

    SECTION("and says so when the object refuses the focus") {
        fixture.backend.node(tree.second).focusWorks = false;
        fixture.run("navigatorObject_moveFocus");
        REQUIRE(fixture.messages.size() == 2);
        CHECK(fixture.messages.back() == "Cannot focus this object");
    }
}

TEST_CASE("alus+numpadEnter activates the navigator object", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    Api& api = fixture.registry.api();
    REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.back)));
    fixture.clearRecords();

    fixture.run("review_activate");
    REQUIRE(fixture.backend.performed.size() == 1);
    CHECK(fixture.backend.performed.front().ref == tree.back);
    // The toolkit's own word for the action is what the user hears.
    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "click");

    SECTION("it walks up to find something that can be activated") {
        // The review cursor is often on a label inside the button.
        const Ref label = fixture.backend.add(tree.back, PlatformRole::Label, "Back");
        REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(label)));
        fixture.clearRecords();
        fixture.backend.performed.clear();
        fixture.run("review_activate");
        REQUIRE(fixture.backend.performed.size() == 1);
        CHECK(fixture.backend.performed.front().ref == tree.back);
    }

    SECTION("and says so when nothing can be") {
        REQUIRE(api.setNavigatorObject(fixture.registry.objectFor(tree.list)));
        fixture.clearRecords();
        fixture.backend.performed.clear();
        fixture.run("review_activate");
        CHECK(fixture.backend.performed.empty());
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages.front() == "No action");
    }
}

TEST_CASE("alus+numpad5 reports the navigator object", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    REQUIRE(fixture.registry.api().setNavigatorObject(
        fixture.registry.objectFor(tree.first)));
    fixture.clearRecords();

    fixture.run("navigatorObject_current");
    REQUIRE(fixture.spoken.size() == 1);
    CHECK(fixture.spoken.front().reason == OutputReason::Query);
    CHECK(fixture.spoken.front().text == "Documents list item 1 of 2");
}

TEST_CASE("alus+t reports the title of the active window", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    REQUIRE(fixture.registry.api().setForegroundObject(
        fixture.registry.objectFor(tree.window)));
    fixture.clearRecords();

    fixture.run("title");
    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "Home");

    SECTION("falling back to the application name") {
        fixture.backend.node(tree.window).name.clear();
        alus::core::invalidateAllCaches();
        fixture.clearRecords();
        fixture.run("title");
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages.front() == "Files");
    }

    SECTION("and saying so when there is neither") {
        fixture.registry.api().clear();
        fixture.clearRecords();
        fixture.run("title");
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages.front() == "No title");
    }
}

TEST_CASE("the location of the navigator object", "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    fixture.backend.node(tree.back).extents = Rect{10, 20, 80, 30};
    REQUIRE(fixture.registry.api().setNavigatorObject(
        fixture.registry.objectFor(tree.back)));
    fixture.clearRecords();

    fixture.run("navigatorObject_currentDimensions");
    REQUIRE(fixture.messages.size() == 1);
    CHECK(fixture.messages.front() == "80 by 30 pixels at 10, 20");

    SECTION("an object with no location says so") {
        fixture.backend.node(tree.back).extents = Rect{0, 0, 0, 0};
        alus::core::invalidateAllCaches();
        fixture.clearRecords();
        fixture.run("navigatorObject_currentDimensions");
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages.front() == "No location information");
    }
}

TEST_CASE("every object command complains when there is no navigator object",
          "[a11y][commands]") {
    CommandFixture fixture;
    const NavigationTree tree{fixture.backend};
    (void)tree;
    fixture.registry.api().clear();

    for (const char* name : {"navigatorObject_current", "navigatorObject_parent",
                             "navigatorObject_firstChild", "navigatorObject_next",
                             "navigatorObject_previous", "navigatorObject_nextInFlow",
                             "navigatorObject_previousInFlow", "navigatorObject_moveFocus",
                             "navigatorObject_currentDimensions", "review_activate"}) {
        fixture.clearRecords();
        fixture.run(name);
        REQUIRE(fixture.messages.size() == 1);
        CHECK(fixture.messages.front() == "No navigator object");
    }
}
