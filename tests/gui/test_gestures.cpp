// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The gesture dialog: T9.2 and T9.3.
//
// Both against the REAL generated command table and the REAL gesture maps,
// because both criteria are about what the user sees in a dialog listing ~390
// commands, and a hand-made catalogue of three would prove nothing about
// either.

#include "alus/config/config.hpp"
#include "alus/core/loop.hpp"
#include "alus/gui/gesture_model.hpp"
#include "alus/input/gesture_map.hpp"
#include "alus/input/key_names.hpp"
#include "alus/input/manager.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/dispatch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path sourceRoot() { return std::filesystem::path{ALUS_SOURCE_ROOT}; }

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream{path};
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

// A file's contents as ONE line, for INFO().
//
// Catch2's TAP reporter emits an INFO verbatim, and meson's TAP parser rejects
// any line it cannot classify -- so a multi-line INFO fails the whole test
// executable with "Unknown TAP output lines", even when every assertion
// passed. The newlines are shown as \n rather than dropped, because what the
// message is for is reading the ini file that was written.
std::string oneLine(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\t') {
            out += "\\t";
        } else if (ch != '\r') {
            out.push_back(ch);
        }
    }
    return out;
}

// The generated command table, the built-in gesture layer and the key labels,
// loaded from the source tree exactly as the daemon loads them from $datadir.
struct Harness {
    alus::script::Catalog catalog;
    alus::core::Loop loop;
    alus::script::Dispatcher dispatcher{&loop};
    std::unique_ptr<alus::input::GestureManager> manager;
    std::filesystem::path dir;
    std::unique_ptr<alus::config::Manager> config;

    explicit Harness(const std::string& name) {
        REQUIRE(catalog.loadFromDataDirectory(sourceRoot() / "data"));
        manager = std::make_unique<alus::input::GestureManager>(loop, dispatcher);
        manager->setBuiltinMap(&catalog.builtinGestures());
        manager->refreshMaps();

        dir = std::filesystem::temp_directory_path() / ("alus-gui-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        config = std::make_unique<alus::config::Manager>(
            alus::config::Paths::forDirectory(dir));
    }

    ~Harness() { std::filesystem::remove_all(dir); }

    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
};

}  // namespace

TEST_CASE("the dialog lists the whole command table with its keys", "[gui]") {
    Harness harness{"model"};
    const alus::gui::Model model = alus::gui::buildModel(
        harness.catalog, *harness.manager, &alus::input::KeyNames::shared());

    // ~390 commands are in v1.0 scope, 75 of them have no description and
    // about a hundred are not in this version (N154: hidden), so the dialog
    // shows two hundred and more. The exact number moves; what must hold is
    // that it is the whole table of what ALUS does and not a fragment.
    CHECK(model.commands.size() > 200);
    // N154: the other screen reader's add-on store is not offered.
    CHECK(std::none_of(model.commands.begin(), model.commands.end(),
                       [](const alus::gui::Command& command) {
                           return command.script == "activateAddonsManager" ||
                                  command.script == "activatePythonConsole";
                       }));
    CHECK(model.categories.size() > 10);
    CHECK(model.bindingCount() > 200);

    // Every command carries a description (T9.3's other half) and a category,
    // and every binding a display name.
    for (const alus::gui::Command& command : model.commands) {
        INFO(command.location << "." << command.script);
        CHECK_FALSE(command.description.empty());
        CHECK_FALSE(command.location.empty());
        CHECK_FALSE(command.script.empty());
        for (const alus::gui::Binding& binding : command.bindings) {
            INFO("binding " << binding.gesture);
            CHECK_FALSE(binding.gesture.empty());
            CHECK_FALSE(binding.display.empty());
            // Exactly one of the two is true: a binding is either the user's
            // (deletable) or someone else's (shadowable).
            CHECK(binding.removableFromFile != binding.shadowable);
        }
    }

    // A command whose key the generated table binds, with the ALUS key named
    // the way the user hears it rather than the way the file spells it.
    const alus::gui::Command* inputHelp =
        model.find("globalCommands.GlobalCommands", "toggleInputHelp");
    REQUIRE(inputHelp != nullptr);
    REQUIRE_FALSE(inputHelp->bindings.empty());
    CHECK(inputHelp->bindings.front().layer == alus::gui::Layer::Builtin);
    INFO("display name: " << inputHelp->bindings.front().display);
    // The ALUS key by the product's name and not by the identifier's
    // lowercase spelling, and not by one of the three physical keys it may be
    // -- the user who bound it does not know which of them ALUS is watching.
    CHECK(inputHelp->bindings.front().display.find("ALUS") != std::string::npos);
    CHECK(inputHelp->bindings.front().display.find("+1") != std::string::npos);
}

TEST_CASE("T9.3 — a script with no description is not shown", "[gui][T9.3]") {
    Harness harness{"hidden"};
    const alus::gui::Model model =
        alus::gui::buildModel(harness.catalog, *harness.manager);

    // The generated table has 75 rows with no description in v1.0 scope.
    // Upstream hides them and so must ALUS: a command that cannot be explained
    // cannot be rebound sensibly.
    std::size_t withoutDescription = 0;
    for (const alus::script::ScriptInfo* info : harness.catalog.all()) {
        if (info->description.empty()) {
            ++withoutDescription;
            INFO(info->location << "." << info->name << " has no description");
            CHECK(model.find(info->location, info->name) == nullptr);
        }
    }
    // If this is ever zero the test has stopped testing anything.
    CHECK(withoutDescription > 10);

    // And one by name, so a reader can see what kind of script this is.
    const alus::script::ScriptInfo* tab =
        harness.catalog.find("browseMode.BrowseModeTreeInterceptor", "tab");
    if (tab != nullptr && tab->description.empty()) {
        CHECK(model.find(tab->location, tab->name) == nullptr);
    }
}

TEST_CASE("T9.2 — adding a gesture writes gestures.ini", "[gui][T9.2]") {
    Harness harness{"add"};
    const std::filesystem::path file = alus::gui::userGestureFile(*harness.config);
    REQUIRE(file == harness.dir / "gestures.ini");
    REQUIRE_FALSE(std::filesystem::exists(file));

    const alus::gui::WriteResult added =
        alus::gui::addBinding(*harness.manager, file, "alus+shift+t",
                              "globalCommands.GlobalCommands", "toggleInputHelp");
    INFO(added.error);
    REQUIRE(added.ok);
    CHECK(added.file == file);

    // The FILE, in the format upstream uses, so a user's own map is portable
    // in both directions.
    REQUIRE(std::filesystem::exists(file));
    const std::string text = readFile(file);
    INFO(oneLine(text));
    CHECK(text.find("[globalCommands.GlobalCommands]") != std::string::npos);
    CHECK(text.find("toggleInputHelp") != std::string::npos);

    // And the LIVE map, so the key works before the user closes the dialog.
    const alus::gui::Model model =
        alus::gui::buildModel(harness.catalog, *harness.manager);
    const alus::gui::Command* command =
        model.find("globalCommands.GlobalCommands", "toggleInputHelp");
    REQUIRE(command != nullptr);
    bool found = false;
    for (const alus::gui::Binding& binding : command->bindings) {
        if (binding.layer == alus::gui::Layer::User) {
            found = true;
            CHECK(binding.removableFromFile);
            // Stored normalized, whatever order the user typed.
            CHECK(binding.gesture.find("shift") != std::string::npos);
            CHECK(binding.gesture.find("alus") != std::string::npos);
        }
    }
    CHECK(found);

    // The order the user types in must not matter: these are the same binding.
    const alus::gui::Model again =
        alus::gui::buildModel(harness.catalog, *harness.manager);
    const std::size_t before = again.bindingCount();
    REQUIRE(alus::gui::addBinding(*harness.manager, file, "shift+alus+t",
                                  "globalCommands.GlobalCommands", "toggleInputHelp")
                .ok);
    CHECK(alus::gui::buildModel(harness.catalog, *harness.manager).bindingCount() ==
          before);
}

TEST_CASE("T9.2 — removing the user's own gesture deletes it", "[gui][T9.2]") {
    Harness harness{"remove-own"};
    const std::filesystem::path file = alus::gui::userGestureFile(*harness.config);

    REQUIRE(alus::gui::addBinding(*harness.manager, file, "kb:alus+shift+t",
                                  "globalCommands.GlobalCommands", "toggleInputHelp")
                .ok);
    const alus::gui::Model withIt =
        alus::gui::buildModel(harness.catalog, *harness.manager);
    const alus::gui::Command* command =
        withIt.find("globalCommands.GlobalCommands", "toggleInputHelp");
    REQUIRE(command != nullptr);
    const alus::gui::Binding* mine = nullptr;
    for (const alus::gui::Binding& binding : command->bindings) {
        if (binding.layer == alus::gui::Layer::User) {
            mine = &binding;
        }
    }
    REQUIRE(mine != nullptr);

    const alus::gui::WriteResult removed =
        alus::gui::removeBinding(*harness.manager, file, mine->gesture,
                                 "globalCommands.GlobalCommands", "toggleInputHelp");
    INFO(removed.error);
    REQUIRE(removed.ok);

    const alus::gui::Model after =
        alus::gui::buildModel(harness.catalog, *harness.manager);
    const alus::gui::Command* now =
        after.find("globalCommands.GlobalCommands", "toggleInputHelp");
    REQUIRE(now != nullptr);
    for (const alus::gui::Binding& binding : now->bindings) {
        CHECK(binding.layer != alus::gui::Layer::User);
    }
    // Deleted and not shadowed: the file must not grow an `None =` entry for a
    // binding the user themselves put there, or every add/remove pair would
    // leave a line behind.
    const std::string text = readFile(file);
    INFO(oneLine(text));
    CHECK(text.find("None") == std::string::npos);
}

TEST_CASE("T9.2 — removing a built-in gesture shadows it and survives a reload",
          "[gui][T9.2]") {
    Harness harness{"remove-builtin"};
    const std::filesystem::path file = alus::gui::userGestureFile(*harness.config);

    const alus::gui::Model before =
        alus::gui::buildModel(harness.catalog, *harness.manager);
    const alus::gui::Command* command =
        before.find("globalCommands.GlobalCommands", "toggleInputHelp");
    REQUIRE(command != nullptr);
    REQUIRE_FALSE(command->bindings.empty());
    const alus::gui::Binding builtin = command->bindings.front();
    REQUIRE(builtin.layer == alus::gui::Layer::Builtin);
    REQUIRE(builtin.shadowable);
    REQUIRE_FALSE(builtin.removableFromFile);

    REQUIRE(alus::gui::removeBinding(*harness.manager, file, builtin.gesture,
                                     "globalCommands.GlobalCommands", "toggleInputHelp")
                .ok);

    // The built-in binding is gone from what the dialog shows...
    const alus::gui::Model after =
        alus::gui::buildModel(harness.catalog, *harness.manager);
    const alus::gui::Command* now =
        after.find("globalCommands.GlobalCommands", "toggleInputHelp");
    REQUIRE(now != nullptr);
    for (const alus::gui::Binding& binding : now->bindings) {
        INFO("still shown: " << binding.gesture);
        CHECK(binding.gesture != builtin.gesture);
    }

    // ...and the way it is gone is an `None =` entry in the USER's file, which
    // is what makes it survive a restart. Deleting from the built-in map would
    // appear to work and come back on the next start.
    const std::string text = readFile(file);
    INFO(oneLine(text));
    CHECK(text.find("None") != std::string::npos);

    // Proven by actually reloading: a fresh manager, the same file.
    {
        alus::core::Loop loop;
        alus::script::Dispatcher dispatcher{&loop};
        alus::input::GestureManager fresh{loop, dispatcher};
        fresh.setBuiltinMap(&harness.catalog.builtinGestures());
        std::string error;
        REQUIRE(fresh.userMap().load(file, &error));
        fresh.refreshMaps();

        const alus::gui::Model reloaded =
            alus::gui::buildModel(harness.catalog, fresh);
        const alus::gui::Command* reloadedCommand =
            reloaded.find("globalCommands.GlobalCommands", "toggleInputHelp");
        REQUIRE(reloadedCommand != nullptr);
        for (const alus::gui::Binding& binding : reloadedCommand->bindings) {
            INFO("came back after a reload: " << binding.gesture);
            CHECK(binding.gesture != builtin.gesture);
        }
    }
}

TEST_CASE("a malformed gesture file does not lose the rest of it", "[gui]") {
    // GestureMap::lastUpdateContainedError exists for this: a user who hand
    // edited one line must not lose the other fifty. The dialog reports it;
    // what matters here is that the load still happened.
    Harness harness{"malformed"};
    const std::filesystem::path file = alus::gui::userGestureFile(*harness.config);
    {
        std::ofstream stream{file};
        stream << "[globalCommands.GlobalCommands]\n"
               << "\ttoggleInputHelp = kb:alus+shift+t\n"
               << "this line is not a binding at all\n"
               << "\treportCurrentLine = kb:alus+shift+l\n";
    }
    std::string error;
    CHECK(harness.manager->userMap().load(file, &error));
    CHECK(harness.manager->userMap().lastUpdateContainedError());
    // Both good lines survived.
    CHECK(harness.manager->userMap().size() >= 2);
}
