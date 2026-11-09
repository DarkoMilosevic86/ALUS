// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T1.10 — resetConfiguration() restarts every subsystem without restarting the
//         process.
#include "alus/config/config.hpp"

#include "alus/core/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using alus::config::FunctionSubsystem;
using alus::config::Manager;
using alus::config::Paths;
using alus::config::resetConfiguration;
using alus::config::SubsystemRegistry;
using alus::core::HandlerHandle;
using alus::core::Log;
using alus::core::LogLevel;

namespace {

struct QuietLog {
    QuietLog() {
        previous = Log::instance().stderrEnabled();
        Log::instance().setStderrEnabled(false);
        Log::instance().resetCounts();
    }
    ~QuietLog() { Log::instance().setStderrEnabled(previous); }
    bool previous = true;
};

// A configuration directory of its own per test.
class TempConfig {
public:
    TempConfig() {
        static int counter = 0;
        dir_ = std::filesystem::temp_directory_path() /
               ("alus-config-test-" + std::to_string(++counter) + "-" +
                std::to_string(::getpid()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_ / "profiles");
    }

    ~TempConfig() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    Paths paths() const { return Paths::forDirectory(dir_); }
    const std::filesystem::path& dir() const { return dir_; }

    void write(const std::filesystem::path& relative, const std::string& contents) const {
        const std::filesystem::path path = dir_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path};
        stream << contents;
    }

    std::string read(const std::filesystem::path& relative) const {
        std::ifstream stream{dir_ / relative};
        return std::string{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    }

private:
    std::filesystem::path dir_;
};

}  // namespace

TEST_CASE("T1.10 resetConfiguration restarts every subsystem in place",
          "[config][reset]") {
    QuietLog quiet;
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=30\n");

    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    REQUIRE(manager.getInt("speech", "rate") == 30);

    std::vector<std::string> events;
    SubsystemRegistry subsystems;

    FunctionSubsystem speech{"speech", [&events] { events.emplace_back("init:speech"); },
                             [&events] { events.emplace_back("term:speech"); }};
    FunctionSubsystem braille{"braille", [&events] { events.emplace_back("init:braille"); },
                              [&events] { events.emplace_back("term:braille"); }};
    FunctionSubsystem input{"input", [&events] { events.emplace_back("init:input"); },
                            [&events] { events.emplace_back("term:input"); }};

    subsystems.add(&speech);
    subsystems.add(&braille);
    subsystems.add(&input);
    subsystems.initializeAll();
    REQUIRE(events == std::vector<std::string>{"init:speech", "init:braille", "init:input"});
    REQUIRE(subsystems.initialized());
    events.clear();

    // An unsaved change in memory, and a different value on disk.
    manager.setInt("speech", "rate", 99);
    REQUIRE(manager.getInt("speech", "rate") == 99);
    temp.write("alus.ini", "[speech]\nrate=45\n");

    int resetNotifications = 0;
    HandlerHandle handle = manager.postConfigReset().add([&] { ++resetNotifications; });

    resetConfiguration(manager, subsystems);

    // Down in reverse order, up in registration order — NVDA's rule.
    REQUIRE(events == std::vector<std::string>{"term:input", "term:braille", "term:speech",
                                               "init:speech", "init:braille", "init:input"});
    // The unsaved change is gone and the file on disk is in force.
    REQUIRE(manager.getInt("speech", "rate") == 45);
    REQUIRE(resetNotifications == 1);
    REQUIRE(subsystems.initialized());
}

TEST_CASE("a factory reset returns to the defaults", "[config][reset]") {
    QuietLog quiet;
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=45\n");

    Manager manager{temp.paths()};
    manager.setDefault("speech", "rate", "20");
    REQUIRE(manager.load());
    REQUIRE(manager.getInt("speech", "rate") == 45);

    SubsystemRegistry subsystems;
    resetConfiguration(manager, subsystems, true);

    REQUIRE(manager.getInt("speech", "rate") == 20);
}

TEST_CASE("a subsystem that throws does not stop the others", "[config][reset]") {
    QuietLog quiet;
    TempConfig temp;
    Manager manager{temp.paths()};
    SubsystemRegistry subsystems;

    bool laterInitialized = false;
    bool earlierTerminated = false;

    FunctionSubsystem first{"first", [] {}, [&earlierTerminated] { earlierTerminated = true; }};
    FunctionSubsystem broken{"broken", [] { throw std::runtime_error("cannot start"); },
                             [] { throw std::runtime_error("cannot stop"); }};
    FunctionSubsystem last{"last", [&laterInitialized] { laterInitialized = true; }, [] {}};

    subsystems.add(&first);
    subsystems.add(&broken);
    subsystems.add(&last);
    subsystems.initializeAll();
    REQUIRE(laterInitialized);

    laterInitialized = false;
    Log::instance().resetCounts();  // count only what the reset itself logs
    resetConfiguration(manager, subsystems);

    REQUIRE(earlierTerminated);  // teardown continued past the thrower
    REQUIRE(laterInitialized);   // and so did startup
    REQUIRE(Log::instance().countFor(LogLevel::Error) == 2);
}

TEST_CASE("values survive a save and load round trip", "[config][values]") {
    TempConfig temp;
    {
        Manager manager{temp.paths()};
        REQUIRE(manager.load());
        manager.setString("speech", "synth", "ssip");
        manager.setInt("speech", "rate", 30);
        manager.setBool("braille", "showCursor", true);
        manager.setDouble("speech", "pitch", 1.5);
        REQUIRE(manager.save());
    }
    {
        Manager manager{temp.paths()};
        REQUIRE(manager.load());
        REQUIRE(manager.getString("speech", "synth") == "ssip");
        REQUIRE(manager.getInt("speech", "rate") == 30);
        REQUIRE(manager.getBool("braille", "showCursor"));
        REQUIRE(manager.getDouble("speech", "pitch") == 1.5);
    }
    // The file is ini, the same shape as NVDA's, so gestures.ini stays readable.
    const std::string contents = temp.read("alus.ini");
    REQUIRE(contents.find("[speech]") != std::string::npos);
    REQUIRE(contents.find("synth=ssip") != std::string::npos);
}

TEST_CASE("defaults answer for values nobody set", "[config][values]") {
    TempConfig temp;
    Manager manager{temp.paths()};
    manager.setDefault("general", "logLevel", "info");

    REQUIRE(manager.getString("general", "logLevel") == "info");
    REQUIRE(manager.has("general", "logLevel"));
    REQUIRE_FALSE(manager.has("general", "nothingLikeThis"));
    REQUIRE(manager.getString("general", "nothingLikeThis", "fallback") == "fallback");
    REQUIRE(manager.getInt("speech", "rate", 20) == 20);
    REQUIRE(manager.getBool("speech", "nope", true));
}

TEST_CASE("a bad value falls back instead of throwing", "[config][values]") {
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=not a number\n");
    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    REQUIRE(manager.getInt("speech", "rate", 20) == 20);
}

TEST_CASE("a later profile wins, and takes the writes", "[config][profiles]") {
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=30\nsynth=ssip\n");
    temp.write("profiles/sayAll.ini", "[speech]\nrate=50\n");
    temp.write("profiles/firefox.ini", "[speech]\nrate=70\n");

    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    REQUIRE(manager.getInt("speech", "rate") == 30);

    REQUIRE(manager.activateProfile("sayAll"));
    REQUIRE(manager.getInt("speech", "rate") == 50);
    // A profile need only carry a subset; the rest comes from the base.
    REQUIRE(manager.getString("speech", "synth") == "ssip");

    REQUIRE(manager.activateProfile("firefox"));
    REQUIRE(manager.getInt("speech", "rate") == 70);
    REQUIRE(manager.activeProfiles() == std::vector<std::string>{"sayAll", "firefox"});

    // Writes land in the most recently activated profile.
    manager.setInt("speech", "rate", 80);
    REQUIRE(manager.getInt("speech", "rate") == 80);
    REQUIRE(manager.save());
    REQUIRE(temp.read("profiles/firefox.ini").find("rate=80") != std::string::npos);
    REQUIRE(temp.read("alus.ini").find("rate=30") != std::string::npos);

    REQUIRE(manager.deactivateProfile("firefox"));
    REQUIRE(manager.getInt("speech", "rate") == 50);
    REQUIRE(manager.deactivateProfile("sayAll"));
    REQUIRE(manager.getInt("speech", "rate") == 30);
}

TEST_CASE("base-only sections cannot be overridden by a profile",
          "[config][profiles]") {
    TempConfig temp;
    temp.write("alus.ini", "[general]\nlogLevel=info\n");
    temp.write("profiles/loud.ini", "[general]\nlogLevel=debug\n");

    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    REQUIRE(manager.activateProfile("loud"));

    // NVDA: BASE_ONLY_SECTIONS — these describe the installation, not the moment.
    REQUIRE(manager.getString("general", "logLevel") == "info");

    // And a write to such a section goes to the base file, not to the profile.
    manager.setString("general", "logLevel", "warning");
    REQUIRE(manager.save());
    REQUIRE(temp.read("alus.ini").find("logLevel=warning") != std::string::npos);
}

TEST_CASE("profiles can be created, listed, renamed and deleted",
          "[config][profiles]") {
    TempConfig temp;
    Manager manager{temp.paths()};
    REQUIRE(manager.load());

    REQUIRE(manager.createProfile("firefox"));
    REQUIRE(manager.createProfile("sayAll"));
    REQUIRE_FALSE(manager.createProfile("firefox"));  // already there
    REQUIRE(manager.listProfiles() == std::vector<std::string>{"firefox", "sayAll"});

    REQUIRE(manager.renameProfile("firefox", "browser"));
    REQUIRE(manager.listProfiles() == std::vector<std::string>{"browser", "sayAll"});

    REQUIRE(manager.deleteProfile("sayAll"));
    REQUIRE(manager.listProfiles() == std::vector<std::string>{"browser"});
    REQUIRE_FALSE(manager.deleteProfile("sayAll"));
}

TEST_CASE("a trigger activates and deactivates its profile", "[config][triggers]") {
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=30\n");
    temp.write("profiles/firefox.ini", "[speech]\nrate=70\n");

    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    manager.mapTrigger("app:firefox", "firefox");

    REQUIRE(manager.profileForTrigger("app:firefox") == "firefox");
    REQUIRE_FALSE(manager.profileForTrigger("app:nautilus").has_value());

    REQUIRE(manager.triggerEnter("app:firefox"));
    REQUIRE(manager.getInt("speech", "rate") == 70);

    REQUIRE(manager.triggerExit("app:firefox"));
    REQUIRE(manager.getInt("speech", "rate") == 30);

    // A trigger with no profile does nothing, and that is not a failure state.
    REQUIRE_FALSE(manager.triggerEnter("app:nautilus"));
}

TEST_CASE("two triggers on one profile keep it until the last one leaves",
          "[config][triggers]") {
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=30\n");
    temp.write("profiles/quiet.ini", "[speech]\nrate=10\n");

    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    manager.mapTrigger("sayAll", "quiet");
    manager.mapTrigger("app:reader", "quiet");

    REQUIRE(manager.triggerEnter("sayAll"));
    REQUIRE(manager.triggerEnter("app:reader"));
    REQUIRE(manager.getInt("speech", "rate") == 10);

    REQUIRE(manager.triggerExit("sayAll"));
    REQUIRE(manager.getInt("speech", "rate") == 10);  // the other trigger still holds it

    REQUIRE(manager.triggerExit("app:reader"));
    REQUIRE(manager.getInt("speech", "rate") == 30);
}

TEST_CASE("triggers can be suspended and resumed", "[config][triggers]") {
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=30\n");
    temp.write("profiles/firefox.ini", "[speech]\nrate=70\n");

    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    manager.mapTrigger("app:firefox", "firefox");
    REQUIRE(manager.triggerEnter("app:firefox"));
    REQUIRE(manager.getInt("speech", "rate") == 70);

    // The speech manager's profile latch needs exactly this mid-utterance.
    manager.suspendTriggers();
    REQUIRE(manager.triggersSuspended());
    REQUIRE(manager.getInt("speech", "rate") == 30);

    manager.resumeTriggers();
    REQUIRE_FALSE(manager.triggersSuspended());
    REQUIRE(manager.getInt("speech", "rate") == 70);
}

TEST_CASE("disabling triggers drops the active ones", "[config][triggers]") {
    TempConfig temp;
    temp.write("alus.ini", "[speech]\nrate=30\n");
    temp.write("profiles/firefox.ini", "[speech]\nrate=70\n");

    Manager manager{temp.paths()};
    REQUIRE(manager.load());
    manager.mapTrigger("app:firefox", "firefox");
    REQUIRE(manager.triggerEnter("app:firefox"));

    manager.setTriggersEnabled(false);
    REQUIRE(manager.getInt("speech", "rate") == 30);
    REQUIRE_FALSE(manager.triggerEnter("app:firefox"));  // refused while disabled

    manager.setTriggersEnabled(true);
    REQUIRE(manager.triggerEnter("app:firefox"));
    REQUIRE(manager.getInt("speech", "rate") == 70);
}

TEST_CASE("a profile switch notifies the extension point", "[config][profiles]") {
    TempConfig temp;
    temp.write("profiles/firefox.ini", "[speech]\nrate=70\n");
    Manager manager{temp.paths()};
    REQUIRE(manager.load());

    int switches = 0;
    HandlerHandle handle = manager.postProfileSwitch().add([&] { ++switches; });

    REQUIRE(manager.activateProfile("firefox"));
    REQUIRE(switches == 1);
    REQUIRE(manager.activateProfile("firefox"));  // already active: no switch
    REQUIRE(switches == 1);
    REQUIRE(manager.deactivateProfile("firefox"));
    REQUIRE(switches == 2);
}

TEST_CASE("ALUS_CONFIG_DIR decides where the configuration lives",
          "[config][paths]") {
    TempConfig temp;
    const Paths paths = Paths::forDirectory(temp.dir());
    REQUIRE(paths.mainFile == temp.dir() / "alus.ini");
    REQUIRE(paths.profilesDir == temp.dir() / "profiles");
}
