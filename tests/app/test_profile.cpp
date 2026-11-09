// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T8.4: switching application rebuilds the profile ONCE, not N times.
//
// WHY THIS IS A MEASUREMENT AND NOT A CODE REVIEW. Every subsystem in ALUS
// listens to `postProfileSwitch` and re-reads the configuration when it fires:
// the synthesizer its rate and voice, braille its table, the object model what
// to announce. An app switch is three profile operations (the old trigger
// leaving, the new one arriving, and whatever rides along), so without one
// transaction around them alt-tab costs three rebuilds -- and a rebuild of the
// speech layer mid-utterance is audible. The assertion is therefore on a
// COUNT, which is the only form in which this requirement can be checked.
#include "alus/app/modules.hpp"
#include "alus/app/registry.hpp"
#include "alus/config/config.hpp"
#include "app_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

using namespace alus;
using namespace alus::testing;

namespace {

app::AppKey keyOf(std::string executable, int processId) {
    app::AppKey key;
    key.executable = std::move(executable);
    key.toolkit = "testkit";
    key.processId = processId;
    return key;
}

void writeProfile(const std::filesystem::path& dir, const std::string& name,
                  const std::string& contents) {
    std::filesystem::create_directories(dir / "profiles");
    std::ofstream stream{dir / "profiles" / (name + ".ini")};
    stream << contents;
}

}  // namespace

TEST_CASE("[T8.4] an app switch is one profile switch", "[app][profile]") {
    TempConfigDir temp{"profile"};
    writeProfile(temp.dir(), "writing", "[speech]\nrate = 80\n");
    writeProfile(temp.dir(), "reading", "[speech]\nrate = 20\n");
    config::Manager config{temp.paths()};
    REQUIRE(config.load());
    // Two applications, each with a profile of its own. This is the setup the
    // feature exists for: a user who wants a faster voice in their editor.
    config.mapTrigger("app:writer", "writing");
    config.mapTrigger("app:reader", "reading");

    int rebuilds = 0;
    const core::HandlerHandle handle =
        config.postProfileSwitch().add([&rebuilds] { ++rebuilds; }, "test-counter");

    app::Registry registry{app::Services{}, &config};
    const int self = static_cast<int>(::getpid());
    registry.addAlias("writer-app", "writer-app");
    REQUIRE(registry.add(app::Registry::Rule{
        "writer", "writer-app", "",
        [](const app::AppKey& key, const app::Services& services) {
            return std::make_unique<app::Module>("writer", key, services);
        }}));
    REQUIRE(registry.add(app::Registry::Rule{
        "reader", "reader-app", "",
        [](const app::AppKey& key, const app::Services& services) {
            return std::make_unique<app::Module>("reader", key, services);
        }}));

    // Arriving in the first application.
    rebuilds = 0;
    REQUIRE(registry.setFocusedApplication(keyOf("writer-app", self)));
    CHECK(registry.switchCount() == 1);
    // ONE rebuild: the trigger entered inside the transaction.
    CHECK(rebuilds == 1);
    CHECK(config.activeProfiles() == std::vector<std::string>{"writing"});
    CHECK(config.getInt("speech", "rate") == 80);

    // Alt-tab to the second. Two trigger operations -- one leaving, one
    // arriving -- and still ONE rebuild. This is the measurement.
    rebuilds = 0;
    REQUIRE(registry.setFocusedApplication(keyOf("reader-app", self + 1)));
    CHECK(registry.switchCount() == 2);
    CHECK(rebuilds == 1);
    CHECK(config.activeProfiles() == std::vector<std::string>{"reading"});
    CHECK(config.getInt("speech", "rate") == 20);

    // Back again, twenty times. Forty trigger operations, twenty rebuilds --
    // one per switch, which is the floor: a switch that announced nothing
    // would leave the synthesizer on the wrong profile.
    rebuilds = 0;
    for (int index = 0; index < 20; ++index) {
        registry.setFocusedApplication(keyOf("writer-app", self));
        registry.setFocusedApplication(keyOf("reader-app", self + 1));
    }
    CHECK(rebuilds == 40);
}

TEST_CASE("[T8.4] a focus event inside one application is not a switch",
          "[app][profile]") {
    TempConfigDir temp{"profile"};
    writeProfile(temp.dir(), "writing", "[speech]\nrate = 80\n");
    config::Manager config{temp.paths()};
    REQUIRE(config.load());
    config.mapTrigger("app:writer", "writing");

    int rebuilds = 0;
    const core::HandlerHandle handle =
        config.postProfileSwitch().add([&rebuilds] { ++rebuilds; }, "test-counter");

    app::Registry registry{app::Services{}, &config};
    REQUIRE(registry.add(app::Registry::Rule{
        "writer", "writer-app", "",
        [](const app::AppKey& key, const app::Services& services) {
            return std::make_unique<app::Module>("writer", key, services);
        }}));

    const int self = static_cast<int>(::getpid());
    REQUIRE(registry.setFocusedApplication(keyOf("writer-app", self)));
    rebuilds = 0;
    // Tabbing through a dialog: one focus event per control, every one of them
    // reaching this function. None of them is an app switch, and the cost of
    // each must be nothing at all -- this is the case that happens dozens of
    // times a minute.
    for (int index = 0; index < 100; ++index) {
        CHECK_FALSE(registry.setFocusedApplication(keyOf("writer-app", self)));
    }
    CHECK(rebuilds == 0);
    CHECK(registry.switchCount() == 1);
}

TEST_CASE("the choreography runs in §12.1's order", "[app][profile]") {
    TempConfigDir temp{"profile"};
    writeProfile(temp.dir(), "writing", "[speech]\nrate = 80\n");
    config::Manager config{temp.paths()};
    REQUIRE(config.load());
    config.mapTrigger("app:writer", "writing");

    // A module that records when it was told what, and what the active profile
    // was at that moment. The ORDER is the contract: a module reading the
    // configuration in appGainFocus must see its own profile.
    class Recorder final : public app::Module {
    public:
        Recorder(app::AppKey key, app::Services services, std::vector<std::string>& log,
                 config::Manager& config)
            : Module("writer", std::move(key), std::move(services)),
              log_(log),
              config_(config) {}

        void appGainFocus() override {
            log_.push_back("gainFocus rate=" + std::to_string(config_.getInt("speech", "rate")));
        }
        void appLoseFocus() override {
            log_.push_back("loseFocus rate=" + std::to_string(config_.getInt("speech", "rate")));
        }

    private:
        std::vector<std::string>& log_;
        config::Manager& config_;
    };

    std::vector<std::string> log;
    app::Registry registry{app::Services{}, &config};
    REQUIRE(registry.add(app::Registry::Rule{
        "writer", "writer-app", "",
        [&log, &config](const app::AppKey& key, const app::Services& services) {
            return std::make_unique<Recorder>(key, services, log, config);
        }}));

    const int self = static_cast<int>(::getpid());
    registry.setFocusedApplication(keyOf("writer-app", self));
    REQUIRE(log.size() == 1);
    // The profile is active BEFORE the module is told it has the focus.
    CHECK(log.front() == "gainFocus rate=80");

    registry.setFocusedModule(nullptr);
    REQUIRE(log.size() == 2);
    // And on the way out the module still sees its own profile: it is told
    // first, and the trigger leaves afterwards.
    CHECK(log.back() == "loseFocus rate=80");
    CHECK(config.activeProfiles().empty());
}

TEST_CASE("an atomic switch is reentrant and notifies at most once",
          "[app][profile][config]") {
    TempConfigDir temp{"profile"};
    writeProfile(temp.dir(), "one", "[speech]\nrate = 10\n");
    writeProfile(temp.dir(), "two", "[speech]\npitch = 70\n");
    config::Manager config{temp.paths()};
    REQUIRE(config.load());

    int rebuilds = 0;
    const core::HandlerHandle handle =
        config.postProfileSwitch().add([&rebuilds] { ++rebuilds; }, "test-counter");

    {
        auto outer = config.atomicProfileSwitch();
        CHECK(config.inAtomicProfileSwitch());
        {
            // Nested, which is what happens when a module activates a profile
            // of its own from inside appGainFocus.
            auto inner = config.atomicProfileSwitch();
            REQUIRE(config.activateProfile("one"));
            REQUIRE(config.activateProfile("two"));
            CHECK(rebuilds == 0);
        }
        // The inner guard must NOT notify: the outer one is still open.
        CHECK(rebuilds == 0);
    }
    CHECK(rebuilds == 1);
    CHECK_FALSE(config.inAtomicProfileSwitch());
    CHECK(config.profileSwitchCount() == 1);

    SECTION("a switch in which nothing happened notifies nothing") {
        rebuilds = 0;
        { auto guard = config.atomicProfileSwitch(); }
        CHECK(rebuilds == 0);
    }

    SECTION("finish() ends it early and the destructor then does nothing") {
        rebuilds = 0;
        {
            auto guard = config.atomicProfileSwitch();
            REQUIRE(config.deactivateProfile("two"));
            guard.finish();
            CHECK(rebuilds == 1);
        }
        CHECK(rebuilds == 1);
    }
}
