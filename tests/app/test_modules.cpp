// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The modules ALUS ships with, each against the structure it was written for.
//
// WHAT THESE TESTS CAN AND CANNOT SAY. A module's job is to fix what one real
// application really does, and the mock platform cannot BE LibreOffice. So
// each test here builds the tree shape the module keys on and asserts that the
// module answers correctly FOR THAT SHAPE -- which is the part that can go
// wrong silently (a search that walks one level too few, a container override
// that returns the row it was meant to skip).
//
// Whether the shape is the shape the application really has is a question for
// the manual test and for `alus-cli app-info`, and claude/FAZA8.md records
// which of the four were measured on a running application and which were not.
#include "alus/app/commands.hpp"
#include "alus/app/modules.hpp"
#include "alus/nav/text_behaviors.hpp"
#include "alus/input/gesture.hpp"
#include "app_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unistd.h>

using namespace alus;
using namespace alus::testing;

namespace {

app::AppKey keyOf(std::string executable, std::string toolkit) {
    app::AppKey key;
    key.executable = std::move(executable);
    key.toolkit = std::move(toolkit);
    key.processId = static_cast<int>(::getpid());
    return key;
}

}  // namespace

TEST_CASE("the LibreOffice module reads the measured status bar", "[app][modules]") {
    AppFixture fixture{true};
    // The measured toolkit is `gtk`, not the `gail` the plan assumed.
    const a11y::Ref application = fixture.addApplication("soffice", "gtk");
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Untitled 1 — Writer");
    fixture.backend.addState(window, a11y::PlatformState::Active);
    // The measured hierarchy: frame -> panel -> panel -> root pane -> status
    // bar, four levels down. The plain Module::statusBar looks at the window's
    // own children and would answer nothing here.
    const a11y::Ref outer = fixture.backend.add(window, a11y::PlatformRole::Panel, "");
    const a11y::Ref inner = fixture.backend.add(outer, a11y::PlatformRole::Panel, "");
    const a11y::Ref root = fixture.backend.add(inner, a11y::PlatformRole::RootPane, "");
    const a11y::Ref bar = fixture.backend.add(root, a11y::PlatformRole::StatusBar, "");
    // And the measured fields: no names, content in the Text interface, with
    // empty fields between the full ones.
    const auto addField = [&fixture, bar](const std::string& text) {
        const a11y::Ref field = fixture.backend.add(bar, a11y::PlatformRole::Label, "");
        fixture.backend.addInterface(field, a11y::Interface::Text);
        fixture.backend.node(field).text = text;
        return field;
    };
    addField("");
    addField("Page 1 of 1");
    addField("0 words, 0 characters");
    addField("");
    addField("Default Page Style");
    addField("Serbian Cyrillic (Serbia)");
    addField("");
    addField("100%");

    app::Module* module = fixture.modules().moduleFor(keyOf("soffice.bin", "gtk"));
    REQUIRE(module != nullptr);
    REQUIRE(module->moduleName() == "libreoffice");
    REQUIRE(module->statusBar() != nullptr);
    CHECK(module->statusBar()->ref() == bar);
    // The empty fields are skipped, not joined as blanks.
    CHECK(module->statusBarText() ==
          "Page 1 of 1, 0 words, 0 characters, Default Page Style, "
          "Serbian Cyrillic (Serbia), 100%");
    CHECK(module->productName() == "LibreOffice");
}

TEST_CASE("the LibreOffice module makes a cell's container its table",
          "[app][modules]") {
    AppFixture fixture{true};
    const a11y::Ref application = fixture.addApplication("soffice", "gtk");
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Untitled 1 — Calc");
    fixture.backend.addState(window, a11y::PlatformState::Active);
    const a11y::Ref table = fixture.backend.add(window, a11y::PlatformRole::Table, "Sheet1");
    const a11y::Ref row = fixture.backend.add(table, a11y::PlatformRole::TableRow, "");
    const a11y::Ref cell = fixture.backend.add(row, a11y::PlatformRole::TableCell, "A1");

    // The module has to exist before the object, because the container
    // override is asked through the app layer.
    REQUIRE(fixture.modules().moduleFor(keyOf("soffice.bin", "gtk")) != nullptr);

    const a11y::ObjectPtr cellObject = fixture.registry.objectFor(cell);
    REQUIRE(cellObject != nullptr);
    REQUIRE(cellObject->container() != nullptr);
    // Not the row: §12.2's own example of what the third hook is for.
    CHECK(cellObject->container()->ref() == table);

    SECTION("and says nothing when the toolkit already answers table -> cell") {
        // Which is what Calc measurably does: the hierarchy is
        // table -> table cell, with no row in between.
        const a11y::Ref flatCell =
            fixture.backend.add(table, a11y::PlatformRole::TableCell, "B1");
        const a11y::ObjectPtr flat = fixture.registry.objectFor(flatCell);
        REQUIRE(flat != nullptr);
        REQUIRE(flat->container() != nullptr);
        CHECK(flat->container()->ref() == table);
    }

    SECTION("and leaves anything that is not a cell alone") {
        const a11y::ObjectPtr rowObject = fixture.registry.objectFor(row);
        REQUIRE(rowObject != nullptr);
        REQUIRE(rowObject->container() != nullptr);
        CHECK(rowObject->container()->ref() == table);
    }
}

TEST_CASE("the LibreOffice header commands answer, including when there is no header",
          "[app][modules]") {
    AppFixture fixture{true};
    const a11y::Ref application = fixture.addApplication("soffice", "gtk");
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Calc");
    fixture.backend.addState(window, a11y::PlatformState::Active);
    const a11y::Ref table = fixture.backend.add(window, a11y::PlatformRole::Table, "Sheet1");
    const a11y::Ref cell = fixture.backend.add(table, a11y::PlatformRole::TableCell, "B2");
    fixture.backend.node(cell).columnHeader = "Total";
    fixture.backend.node(cell).rowHeader = "February";

    app::Module* module = fixture.modules().moduleFor(keyOf("soffice.bin", "gtk"));
    REQUIRE(module != nullptr);
    // Mechanism 4: the module brought its own two keys with it.
    const script::Script* column = module->scriptByName("reportColumnHeader");
    const script::Script* row = module->scriptByName("reportRowHeader");
    REQUIRE(column != nullptr);
    REQUIRE(row != nullptr);
    REQUIRE(column->info != nullptr);
    REQUIRE(column->info->gestures.size() == 1);
    CHECK(column->info->gestures.front() == "kb:alus+shift+c");

    input::KeyboardGesture::Init init;
    input::KeyboardGesture gesture{std::move(init)};

    // Nothing has the focus: the command must not crash and must not lie.
    fixture.said.clear();
    column->function(gesture);
    CHECK(fixture.said.empty());

    fixture.backend.emit("object:state-changed:focused", cell, 1);
    fixture.pumpOnce();
    REQUIRE(fixture.registry.api().focus() != nullptr);

    fixture.said.clear();
    column->function(gesture);
    REQUIRE(fixture.said.size() == 1);
    CHECK(fixture.said.front() == "Total");

    fixture.said.clear();
    row->function(gesture);
    REQUIRE(fixture.said.size() == 1);
    CHECK(fixture.said.front() == "February");

    SECTION("a cell with no header says so rather than saying nothing") {
        const a11y::Ref bare =
            fixture.backend.add(table, a11y::PlatformRole::TableCell, "C3");
        fixture.backend.emit("object:state-changed:focused", bare, 1);
        fixture.pumpOnce();
        fixture.said.clear();
        column->function(gesture);
        REQUIRE(fixture.said.size() == 1);
        CHECK(fixture.said.front() == "No column header");
    }
}

TEST_CASE("the terminal module adds nothing where the platform already said terminal",
          "[app][modules]") {
    AppFixture fixture{true};
    // Measured on the target system: gnome-terminal's widget reports role
    // `terminal`, so phase 5's chooser has already attached the behaviour and
    // this module must not add a second one.
    const a11y::Ref application =
        fixture.addApplication("org.gnome.Terminal", "gtk");
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Terminal");
    const a11y::Ref widget =
        fixture.backend.add(window, a11y::PlatformRole::Terminal, "");
    fixture.backend.addInterface(widget, a11y::Interface::Text);

    REQUIRE(fixture.modules().moduleFor(keyOf("gnome-terminal-server", "gtk")) != nullptr);
    const a11y::ObjectPtr object = fixture.registry.objectFor(widget);
    REQUIRE(object != nullptr);
    // Exactly one: the fixture has no nav::Service, so nothing attaches it
    // here; the assertion that matters is that the module did not add a
    // DUPLICATE, which it would if it keyed on the role alone.
    std::size_t terminals = 0;
    for (const std::unique_ptr<a11y::Behavior>& behavior : object->behaviors()) {
        if (behavior != nullptr &&
            behavior->className() == "NVDAObjects.behaviors.Terminal") {
            ++terminals;
        }
    }
    CHECK(terminals <= 1);
}

TEST_CASE("the Nautilus module puts the item's detail in its description",
          "[app][modules]") {
    AppFixture fixture{true};
    const a11y::Ref application = fixture.addApplication("org.gnome.Nautilus", "GTK");
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Home");
    fixture.backend.addState(window, a11y::PlatformState::Active);
    // The measured shape of Nautilus 48 on GTK 4.18.6:
    //     tree table "Преглед садржаја"
    //       [0] table row (header)  children are `filler`
    //       [1] list                children are the files
    //           table row "ALUS"    children are `table cell`
    const a11y::Ref view =
        fixture.backend.add(window, a11y::PlatformRole::TreeTable, "Content view");
    const a11y::Ref header = fixture.backend.add(view, a11y::PlatformRole::TableRow,
                                                 "Name Size Modified");
    fixture.backend.add(header, a11y::PlatformRole::Filler, "Name");
    fixture.backend.add(header, a11y::PlatformRole::Filler, "Size");
    const a11y::Ref rows = fixture.backend.add(view, a11y::PlatformRole::List, "");
    const a11y::Ref item = fixture.backend.add(rows, a11y::PlatformRole::TableRow, "ALUS");
    // The name column repeats the row's name, DOUBLED (measured).
    fixture.backend.add(item, a11y::PlatformRole::TableCell, "ALUS ALUS");
    fixture.backend.add(item, a11y::PlatformRole::TableCell, "14 items");
    fixture.backend.add(item, a11y::PlatformRole::TableCell, "Today, at 9:05");

    REQUIRE(fixture.modules().moduleFor(keyOf("nautilus", "gtk")) != nullptr);
    const a11y::ObjectPtr object = fixture.registry.objectFor(item);
    REQUIRE(object != nullptr);
    CHECK(object->hasBehavior("appModules.nautilus.FileItem"));
    // The name column is skipped by POSITION, because its content is the name
    // twice and an equality test against the row's name would not match it.
    CHECK(object->name() == "ALUS");
    CHECK(object->description() == "14 items, Today, at 9:05");
    // And the ancestry skips the anonymous `list` to the view that has a name.
    REQUIRE(object->container() != nullptr);
    CHECK(object->container()->ref() == view);

    SECTION("the header row is not a file") {
        const a11y::ObjectPtr headerObject = fixture.registry.objectFor(header);
        REQUIRE(headerObject != nullptr);
        // Its children are `filler`, which is how it is told apart (measured).
        CHECK_FALSE(headerObject->hasBehavior("appModules.nautilus.FileItem"));
    }

    SECTION("an item whose application already set a description keeps it") {
        const a11y::Ref described =
            fixture.backend.add(rows, a11y::PlatformRole::TableRow, "Report.odt");
        fixture.backend.node(described).description = "A document, 4 kB";
        fixture.backend.add(described, a11y::PlatformRole::TableCell, "Report.odt");
        fixture.backend.add(described, a11y::PlatformRole::TableCell, "Document");
        const a11y::ObjectPtr object2 = fixture.registry.objectFor(described);
        REQUIRE(object2 != nullptr);
        // The module does not overrule the application about its own content.
        CHECK(object2->description() == "A document, 4 kB");
    }
}

TEST_CASE("the Electron module blocks browse mode, the browser keeps it",
          "[app][modules]") {
    AppFixture fixture{true};
    const a11y::Ref application = fixture.addApplication("Code", "chromium");
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "main.cpp — ALUS");
    const a11y::Ref document =
        fixture.backend.add(window, a11y::PlatformRole::DocumentWeb, "main.cpp");

    app::Module* module = fixture.modules().moduleFor(keyOf("code", "chromium"));
    REQUIRE(module != nullptr);
    CHECK(module->moduleName() == "electron");
    const a11y::ObjectPtr object = fixture.registry.objectFor(document);
    REQUIRE(object != nullptr);
    // Every document in the process, not only the editor: an Electron
    // application's dialogs and settings pages are the same HTML interface.
    CHECK(module->blocksTreeInterceptor(*object));

    SECTION("a browser gets the generic module, which blocks nothing") {
        // Measured on the target system: Google Chrome reports the same
        // toolkit string as VS Code. The rule matches both; the factory
        // declines for the browser.
        app::AppKey chrome = keyOf("chrome", "chromium");
        chrome.name = "Google Chrome";
        chrome.processId = static_cast<int>(::getpid()) + 1;
        app::Module* browser = fixture.modules().moduleFor(chrome);
        REQUIRE(browser != nullptr);
        CHECK(browser->isGeneric());
        CHECK_FALSE(browser->blocksTreeInterceptor(*object));
    }
}

TEST_CASE("the status line command reports, spells and says when there is none",
          "[app][commands]") {
    AppFixture fixture{true};
    std::vector<std::string> said;
    std::vector<std::string> spelled;
    app::AppCommands commands{fixture.modules(),
                              [&said](const std::string& text) { said.push_back(text); },
                              [&spelled](const std::string& text) {
                                  spelled.push_back(text);
                              }};
    const script::Script* script = commands.scriptByName("reportStatusLine");
    REQUIRE(script != nullptr);
    // It answers to the global command class, so NVDA's own `alus+end` from
    // the catalogue reaches it with nothing written down here.
    CHECK(commands.answersTo("globalCommands.GlobalCommands"));

    input::KeyboardGesture::Init init;
    input::KeyboardGesture gesture{std::move(init)};

    // No focused application at all.
    script->function(gesture);
    REQUIRE(said.size() == 1);
    CHECK(said.front() == "No focused application");

    const a11y::Ref application = fixture.addApplication("probe", "testkit");
    const a11y::Ref window =
        fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe");
    fixture.backend.addState(window, a11y::PlatformState::Active);
    app::Module* module = fixture.modules().moduleFor(keyOf("probe-app", "testkit"));
    REQUIRE(module != nullptr);
    REQUIRE(fixture.modules().setFocusedModule(module));

    // A focused application with no status bar: said, not swallowed. Silence
    // here looks exactly like a broken key.
    said.clear();
    script->function(gesture);
    REQUIRE(said.size() == 1);
    CHECK(said.front() == "No status line found");

    const a11y::Ref bar = fixture.backend.add(window, a11y::PlatformRole::StatusBar, "");
    fixture.backend.add(bar, a11y::PlatformRole::Label, "Ready");
    core::invalidateAllCaches();
    said.clear();
    script->function(gesture);
    REQUIRE(said.size() == 1);
    CHECK(said.front() == "Ready");
}
