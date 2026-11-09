// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T8.5, T8.6, T8.7: the add-on ABI, against real `.so` files.
//
// THE PLUGINS HERE ARE COMPILED C, NOT MOCKS, and that is the test. §12.3
// promises an add-on built with any C compiler against one header; a fake
// plugin written in C++ inside this file would prove that ALUS calls function
// pointers, which was never in doubt. What is in doubt is whether
// `alus/alus-plugin.h` is self-contained, whether `dlopen` finds the entry
// point, whether the version check happens before anything is called, and
// whether a library can be unloaded without taking a registration with it.
//
// The four plugins live in tests/app/plugins/ and are built by meson as shared
// modules; their paths arrive as ALUS_TEST_PLUGIN_DIR.
#include "alus/app/plugin.hpp"
#include "alus/input/gesture.hpp"
#include "app_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dlfcn.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace alus;
using namespace alus::testing;

namespace {

#ifndef ALUS_TEST_PLUGIN_DIR
#error "ALUS_TEST_PLUGIN_DIR must be defined by the build"
#endif

std::filesystem::path builtPlugin(const std::string& name) {
    return std::filesystem::path{ALUS_TEST_PLUGIN_DIR} / (name + ".so");
}

// A plugin directory of its own per test, with the built plugins copied in --
// COPIED and not symlinked, because T8.7 deletes what it finds and must not
// delete the build output.
class PluginDir {
public:
    explicit PluginDir(const std::vector<std::string>& bare = {},
                       const std::vector<std::string>& addons = {}) {
        static int counter = 0;
        dir_ = std::filesystem::temp_directory_path() /
               ("alus-plugin-test-" + std::to_string(++counter) + "-" +
                std::to_string(::getpid()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        for (const std::string& name : bare) {
            std::filesystem::copy_file(builtPlugin(name), dir_ / (name + ".so"));
        }
        for (const std::string& name : addons) {
            // §12.3's user layout: ~/.local/share/alus/addons/<name>/<name>.so
            std::filesystem::create_directories(dir_ / name);
            std::filesystem::copy_file(builtPlugin(name), dir_ / name / (name + ".so"));
        }
    }

    ~PluginDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    PluginDir(const PluginDir&) = delete;
    PluginDir& operator=(const PluginDir&) = delete;

    const std::filesystem::path& path() const noexcept { return dir_; }

private:
    std::filesystem::path dir_;
};

// Reads an exported counter out of the plugin ALUS has loaded. The handle is
// the one the host opened, so this also proves the counters the test asserts
// on are the ones the running plugin incremented.
int counter(const app::PluginHost& host, const std::string& plugin,
            const char* symbol) {
    const app::PluginHost::Loaded* loaded = host.find(plugin);
    if (loaded == nullptr || loaded->handle == nullptr) {
        return -1;
    }
    void* address = dlsym(loaded->handle, symbol);
    return address != nullptr ? *static_cast<int*>(address) : -1;
}

}  // namespace

TEST_CASE("[T8.5] a plugin from another ABI is refused, uncalled", "[app][plugin]") {
    PluginDir plugins{{"badabi", "toonew"}};
    app::PluginHost host{app::Services{}};
    host.addDirectory(plugins.path());

    CHECK(host.loadAll() == 0);
    CHECK(host.pluginCount() == 0);
    CHECK(host.find("badabi") == nullptr);
    CHECK(host.find("toonew") == nullptr);

    // AND NOTHING IN THEM RAN. The counters live in the libraries, which are
    // closed again, so the check is on the loading side: a plugin that was
    // refused has no entry at all, and `load` says why.
    std::string error;
    CHECK_FALSE(host.load(builtPlugin("badabi"), &error));
    CHECK(error.find("ABI version") != std::string::npos);
    error.clear();
    CHECK_FALSE(host.load(builtPlugin("toonew"), &error));
    // The second half of the criterion: the right ABI is not enough, because
    // a call added in a later ALUS is a null pointer in this one.
    CHECK(error.find("99.0.0") != std::string::npos);
}

TEST_CASE("[T8.5] a plugin that refuses to start takes its registrations with it",
          "[app][plugin]") {
    PluginDir plugins{{"refuses"}};
    app::PluginHost host{app::Services{}};
    host.addDirectory(plugins.path());

    std::string error;
    CHECK_FALSE(host.load(builtPlugin("refuses"), &error));
    CHECK(error.find("init returned 7") != std::string::npos);
    CHECK(host.pluginCount() == 0);
    // It registered a script BEFORE refusing. A key bound into a library that
    // is no longer loaded crashes on the keystroke, not on the load, so the
    // registration has to be gone.
    CHECK(host.scriptObjects().empty());
    CHECK(host.gestureMap().empty());
}

TEST_CASE("[T8.6] a plugin adds a script, an overlay and an event handler",
          "[app][plugin]") {
    PluginDir plugins{{"good"}};
    AppFixture fixture;
    app::PluginHost& host = fixture.plugins();
    host.addDirectory(plugins.path());
    REQUIRE(host.loadAll() == 1);
    REQUIRE(host.pluginCount() == 1);

    const app::PluginHost::Loaded* loaded = host.find("good");
    REQUIRE(loaded != nullptr);
    CHECK(loaded->abiVersion == ALUS_PLUGIN_ABI_V1);
    CHECK(counter(host, "good", "alus_test_init_calls") == 1);

    SECTION("the script, with the gesture it asked for") {
        REQUIRE(loaded->scripts != nullptr);
        const script::Script* found = loaded->scripts->scriptByName("sayHello");
        REQUIRE(found != nullptr);
        REQUIRE(found->info != nullptr);
        CHECK(found->info->description == "Says hello, for the tests.");
        // The gesture map section a user writes to rebind it.
        CHECK(found->info->location == "plugins.good");
        const auto entries = host.gestureMap().allEntries();
        REQUIRE(entries.size() == 1);
        CHECK(entries.front().second.location == "plugins.good");
        CHECK(entries.front().second.script == "sayHello");

        // And it runs, with the user data it was registered with.
        // A gesture is needed only as an argument; the plugin ignores it.
        input::KeyboardGesture::Init init;
        input::KeyboardGesture gesture{std::move(init)};
        found->function(gesture);
        CHECK(counter(host, "good", "alus_test_script_calls") == 1);
    }

    SECTION("the overlay, added to the objects it claims") {
        const a11y::Ref application = fixture.addApplication("Probe", "testkit");
        const a11y::Ref button =
            fixture.backend.add(application, a11y::PlatformRole::Button, "Save");
        const a11y::Ref label =
            fixture.backend.add(application, a11y::PlatformRole::Label, "Name");

        const a11y::ObjectPtr buttonObject = fixture.registry.objectFor(button);
        REQUIRE(buttonObject != nullptr);
        // A C plugin cannot define a C++ behaviour, so what it adds is a NAME
        // -- which is what puts it in the object's script class chain and
        // makes a gesture map section for it apply here and nowhere else.
        CHECK(buttonObject->hasBehavior("plugins.good.Button"));
        CHECK(buttonObject->answersTo("plugins.good.Button"));

        const a11y::ObjectPtr labelObject = fixture.registry.objectFor(label);
        REQUIRE(labelObject != nullptr);
        CHECK_FALSE(labelObject->hasBehavior("plugins.good.Button"));
        // It was asked about both, which is the other half: a plugin's
        // chooser runs for every object of every application.
        CHECK(counter(host, "good", "alus_test_overlay_calls") >= 2);
    }

    SECTION("the event handler, as level 1 of the chain") {
        const a11y::Ref application = fixture.addApplication("Probe", "testkit");
        const a11y::Ref window =
            fixture.backend.add(application, a11y::PlatformRole::Frame, "Probe window");
        const a11y::Ref button =
            fixture.backend.add(window, a11y::PlatformRole::Button, "Save");

        fixture.clearRecords();
        fixture.backend.emit("object:state-changed:focused", button, 1);
        fixture.pumpOnce();
        CHECK(counter(host, "good", "alus_test_event_calls") == 1);
        // It continued, so the object layer announced the focus.
        CHECK_FALSE(fixture.spoken.empty());

        // And an object the plugin decides to swallow is swallowed: the whole
        // point of the chain contract (§3.3), now reachable from C.
        const a11y::Ref swallowed =
            fixture.backend.add(window, a11y::PlatformRole::Button, "swallow me");
        fixture.clearRecords();
        fixture.backend.emit("object:state-changed:focused", swallowed, 1);
        fixture.pumpOnce();
        CHECK(counter(host, "good", "alus_test_event_calls") == 2);
        CHECK(fixture.spoken.empty());
    }

    SECTION("the braille table source (phase 7's handover, item 1)") {
        // No braille service in this fixture, so the documented answer is -2
        // ("braille is not running") and not a crash or a lie.
        CHECK(counter(host, "good", "alus_test_braille_result") == -2);
    }
}

TEST_CASE("[T8.6] unloading takes everything back", "[app][plugin]") {
    PluginDir plugins{{"good"}};
    app::PluginHost host{app::Services{}};
    host.addDirectory(plugins.path());
    REQUIRE(host.loadAll() == 1);
    REQUIRE(host.scriptObjects().size() == 1);
    CHECK_FALSE(host.gestureMap().empty());

    host.unloadAll();
    CHECK(host.pluginCount() == 0);
    CHECK(host.scriptObjects().empty());
    // The gestures go too: a grab for a key whose script is in an unmapped
    // library is worse than no grab at all.
    CHECK(host.gestureMap().empty());
}

TEST_CASE("[T8.7] a plugin uninstalled while loaded goes at the next start",
          "[app][plugin]") {
    // The add-on layout, because that is the one a user installs and the one
    // the marker deletes whole.
    PluginDir plugins{{}, {"good"}};
    const std::filesystem::path addon = plugins.path() / "good";
    REQUIRE(std::filesystem::exists(addon / "good.so"));

    {
        app::PluginHost host{app::Services{}};
        host.addDirectory(plugins.path());
        REQUIRE(host.loadAll() == 1);

        // Nothing is marked yet.
        CHECK_FALSE(host.isMarkedForRemoval("good"));
        // Phase 1 of the uninstall: while the library is mapped, which is when
        // the user asks for it.
        std::string error;
        REQUIRE(host.markForRemoval("good", &error));
        CHECK(host.isMarkedForRemoval("good"));

        // THE PLUGIN IS STILL RUNNING. That is the contract: it cannot be
        // unmapped from under its own event handler, so it keeps working until
        // the process restarts.
        CHECK(host.pluginCount() == 1);
        CHECK(std::filesystem::exists(addon / "good.so"));
        // And phase 2 does nothing while it is loaded in THIS host, because
        // the marker is acted on before loading, not after.
    }

    // The next start.
    {
        app::PluginHost host{app::Services{}};
        host.addDirectory(plugins.path());
        CHECK(host.processPendingRemovals() == 1);
        // The whole add-on directory is gone, not only its `.so`.
        CHECK_FALSE(std::filesystem::exists(addon));
        CHECK(host.loadAll() == 0);
        CHECK(host.pluginCount() == 0);
    }
}

TEST_CASE("[T8.7] a bare .so is marked beside itself", "[app][plugin]") {
    PluginDir plugins{{"good", "refuses"}};
    app::PluginHost host{app::Services{}};
    host.addDirectory(plugins.path());
    REQUIRE(host.loadAll() == 1);  // `refuses` refuses

    std::string error;
    REQUIRE(host.markForRemoval("good", &error));
    CHECK(host.isMarkedForRemoval("good"));
    // The marker is beside the file and named after it, so two plugins in one
    // directory are marked independently.
    CHECK_FALSE(host.isMarkedForRemoval("refuses"));

    // A plugin that was REFUSED can still be uninstalled -- it is the one the
    // user most wants gone, and it is not in the loaded list.
    REQUIRE(host.markForRemoval("refuses", &error));
    CHECK(host.isMarkedForRemoval("refuses"));

    app::PluginHost next{app::Services{}};
    next.addDirectory(plugins.path());
    CHECK(next.processPendingRemovals() == 2);
    CHECK(next.loadAll() == 0);
    CHECK_FALSE(std::filesystem::exists(plugins.path() / "good.so"));
    CHECK_FALSE(std::filesystem::exists(plugins.path() / "refuses.so"));
}

TEST_CASE("marking a plugin that does not exist is an answer, not a crash",
          "[app][plugin]") {
    PluginDir plugins{};
    app::PluginHost host{app::Services{}};
    host.addDirectory(plugins.path());
    std::string error;
    CHECK_FALSE(host.markForRemoval("nothing-like-this", &error));
    CHECK(error.find("no plugin named") != std::string::npos);
}

TEST_CASE("the first directory wins, so a scratchpad shadows an installed plugin",
          "[app][plugin]") {
    PluginDir scratchpad{{"good"}};
    PluginDir installed{{"good"}};
    app::PluginHost host{app::Services{}};
    // §12.3's order: the scratchpad first.
    host.addDirectory(scratchpad.path());
    host.addDirectory(installed.path());

    REQUIRE(host.loadAll() == 1);
    const app::PluginHost::Loaded* loaded = host.find("good");
    REQUIRE(loaded != nullptr);
    CHECK(loaded->path.parent_path() == scratchpad.path());
}

TEST_CASE("a directory that is not there is not an error", "[app][plugin]") {
    app::PluginHost host{app::Services{}};
    host.addDirectory(std::filesystem::temp_directory_path() / "alus-no-such-dir-8ac1");
    CHECK(host.loadAll() == 0);
    CHECK(host.processPendingRemovals() == 0);
    // A user with no add-ons is the normal case.
    CHECK(host.pluginCount() == 0);
}

TEST_CASE("a file that is not a plugin is refused with a reason", "[app][plugin]") {
    PluginDir plugins{};
    const std::filesystem::path fake = plugins.path() / "nonsense.so";
    {
        std::ofstream stream{fake};
        stream << "this is not an ELF object at all\n";
    }
    app::PluginHost host{app::Services{}};
    host.addDirectory(plugins.path());
    std::string error;
    CHECK_FALSE(host.load(fake, &error));
    CHECK_FALSE(error.empty());
    CHECK(host.loadAll() == 0);
}
