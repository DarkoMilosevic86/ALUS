// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `alus+f12` (the time, then the date) and `alus+shift+b` (the battery).
//
// WHAT MAKES THESE TESTABLE AT ALL, and why both seams exist: the clock and
// the /sys directory are both injected. A test that read the real clock would
// assert on the minute it happened to run in, and a test that read the real
// /sys would pass on a laptop and fail on a build machine -- so the battery
// cases a user actually hits (7% and unplugged, charging, no battery at all,
// a kernel that reports no rate) are written here as directories.
#include "alus/commands/system.hpp"

#include "alus/input/gesture.hpp"
#include "alus/input/gesture_map.hpp"
#include "alus/script/dispatch.hpp"

#include "input/support.hpp"
#include "support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

using namespace alus;
using namespace std::chrono_literals;

namespace {

// A /sys/class/power_supply of the test's own making.
class FakePowerSupply {
public:
    FakePowerSupply() {
        root_ = std::filesystem::temp_directory_path() /
                ("alus-power-" + std::to_string(::getpid()) + "-" + std::to_string(counter()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    ~FakePowerSupply() {
        std::error_code code;
        std::filesystem::remove_all(root_, code);
    }

    FakePowerSupply(const FakePowerSupply&) = delete;
    FakePowerSupply& operator=(const FakePowerSupply&) = delete;

    // One device, as the kernel lays it out: a directory with `type` and
    // whatever files that type has.
    void device(const std::string& name, const std::map<std::string, std::string>& files) {
        const std::filesystem::path dir = root_ / name;
        std::filesystem::create_directories(dir);
        for (const auto& [file, value] : files) {
            std::ofstream out{dir / file};
            out << value << "\n";
        }
    }

    const std::filesystem::path& path() const { return root_; }

private:
    static int counter() {
        static int next = 0;
        return next++;
    }

    std::filesystem::path root_;
};

struct Said {
    std::vector<std::string> lines;

    std::function<void(const std::string&)> sink() {
        return [this](const std::string& text) { lines.push_back(text); };
    }

    const std::string& last() const {
        REQUIRE_FALSE(lines.empty());
        return lines.back();
    }
};

// 2025-11-09 09:30:00 UTC. Fixed, so the assertions below are about the
// FORMAT and not about the moment the suite ran.
constexpr std::time_t kFixedTime = 1762680600;

}  // namespace

TEST_CASE("the battery is read from /sys, with every case the kernel can give",
          "[commands][system]") {
    SECTION("a laptop on battery: percentage, time left, unplugged") {
        FakePowerSupply power;
        power.device("AC", {{"type", "Mains"}, {"online", "0"}});
        power.device("BAT0", {{"type", "Battery"},
                             {"capacity", "73"},
                             {"status", "Discharging"},
                             // 36 Wh left, drawing 24 W: an hour and a half.
                             {"energy_now", "36000000"},
                             {"energy_full", "50000000"},
                             {"power_now", "24000000"}});
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        CHECK(status.known);
        CHECK(status.present);
        CHECK_FALSE(status.pluggedIn);
        CHECK(status.percent == 73);
        REQUIRE(status.minutesRemaining.has_value());
        CHECK(*status.minutesRemaining == 90);
        CHECK(commands::describeBattery(status) ==
              "73 percent 1 hour and 30 minutes remaining Unplugged");
    }

    SECTION("charging: the time left is the time to FULL, not to empty") {
        FakePowerSupply power;
        power.device("AC", {{"type", "Mains"}, {"online", "1"}});
        power.device("BAT0", {{"type", "Battery"},
                             {"capacity", "50"},
                             {"status", "Charging"},
                             // 25 of 50 Wh, charging at 50 W: half an hour.
                             {"energy_now", "25000000"},
                             {"energy_full", "50000000"},
                             {"power_now", "50000000"}});
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        CHECK(status.pluggedIn);
        REQUIRE(status.minutesRemaining.has_value());
        CHECK(*status.minutesRemaining == 30);
        CHECK(commands::describeBattery(status) == "50 percent 30 minutes remaining Plugged in");
    }

    SECTION("charge and current instead of energy and power") {
        // Half the drivers report one pair, half the other. Both must work:
        // which one a user's laptop has is not something they can change.
        FakePowerSupply power;
        power.device("BAT0", {{"type", "Battery"},
                              {"capacity", "20"},
                              {"status", "Discharging"},
                              {"charge_now", "1000000"},
                              {"charge_full", "5000000"},
                              {"current_now", "1000000"}});
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        REQUIRE(status.minutesRemaining.has_value());
        CHECK(*status.minutesRemaining == 60);
        CHECK(commands::describeBattery(status) == "20 percent 1 hour remaining Unplugged");
    }

    SECTION("a driver that reports no rate says the percentage and stops") {
        FakePowerSupply power;
        power.device("BAT0",
                     {{"type", "Battery"}, {"capacity", "41"}, {"status", "Discharging"}});
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        CHECK(status.present);
        CHECK_FALSE(status.minutesRemaining.has_value());
        CHECK(commands::describeBattery(status) == "41 percent Unplugged");
    }

    SECTION("a rate of zero is not a division and not three days") {
        FakePowerSupply power;
        power.device("BAT0", {{"type", "Battery"},
                              {"capacity", "41"},
                              {"status", "Discharging"},
                              {"energy_now", "20000000"},
                              {"energy_full", "50000000"},
                              {"power_now", "0"}});
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        CHECK_FALSE(status.minutesRemaining.has_value());
    }

    SECTION("under a minute left is said as that, not as `0 minutes`") {
        FakePowerSupply power;
        power.device("BAT0", {{"type", "Battery"},
                              {"capacity", "2"},
                              {"status", "Discharging"},
                              {"energy_now", "100000"},
                              {"energy_full", "50000000"},
                              {"power_now", "50000000"}});
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        REQUIRE(status.minutesRemaining.has_value());
        CHECK(*status.minutesRemaining == 0);
        CHECK(commands::describeBattery(status) ==
              "2 percent Less than 1 minute remaining Unplugged");
    }

    SECTION("a desktop: the kernel listed no battery, which is an ANSWER") {
        FakePowerSupply power;
        power.device("AC", {{"type", "Mains"}, {"online", "1"}});
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        CHECK(status.known);
        CHECK_FALSE(status.present);
        CHECK(commands::describeBattery(status) == "No system battery");
    }

    SECTION("an empty power-supply class is still an answer: no battery") {
        FakePowerSupply power;
        const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
        CHECK(status.known);
        CHECK(commands::describeBattery(status) == "No system battery");
    }

    SECTION("no power-supply class at all is `unknown`, NOT `no battery`") {
        // The difference matters: one is a fact about the machine, the other
        // is ALUS not being able to see. Saying the first when it means the
        // second is how a user ends up trusting a wrong answer.
        const commands::BatteryStatus status =
            commands::readBatteryStatus(std::filesystem::temp_directory_path() /
                                        "alus-no-such-power-supply-directory");
        CHECK_FALSE(status.known);
        CHECK(commands::describeBattery(status) == "Unknown power status");
    }

    SECTION("`Full` and `Not charging` both mean plugged in") {
        for (const char* const state : {"Full", "Not charging"}) {
            FakePowerSupply power;
            power.device("BAT0", {{"type", "Battery"}, {"capacity", "100"}, {"status", state}});
            const commands::BatteryStatus status = commands::readBatteryStatus(power.path());
            INFO("battery status was " << state);
            CHECK(status.pluggedIn);
        }
    }
}

TEST_CASE("the clock is the locale's, without seconds", "[commands][system]") {
    // The tests run in whatever locale the build machine has, so what is
    // asserted is the SHAPE: a time with no seconds, and a date with the
    // month and the weekday written out rather than numbered.
    const std::string time = commands::formatTime(kFixedTime);
    const std::string date = commands::formatDate(kFixedTime);
    INFO("time: " << time << ", date: " << date);
    CHECK_FALSE(time.empty());
    CHECK_FALSE(date.empty());
    // Two colons would be hours:minutes:seconds.
    CHECK(std::count(time.begin(), time.end(), ':') <= 1);
    // The date is the long one, so it is longer than any numeric date.
    CHECK(date.size() > 10);
    // Neither ends up with the double space `%e` leaves behind.
    CHECK(date.find("  ") == std::string::npos);
}

TEST_CASE("alus+f12 says the time, then the date", "[commands][system]") {
    testing::QuietLog quiet;
    testing::LoopFixture fixture;
    Said said;

    script::Dispatcher dispatcher{&fixture.loop()};
    dispatcher.setMultiPressTimeout(2s);
    input::GestureMap builtin{"built-in"};
    builtin.add("kb:alus+f12", "globalCommands.GlobalCommands", "dateTime");
    builtin.add("kb:alus+b", "globalCommands.GlobalCommands", "say_battery_status");
    dispatcher.setBuiltinMap(&builtin);

    FakePowerSupply power;
    power.device("BAT0",
                 {{"type", "Battery"}, {"capacity", "55"}, {"status", "Discharging"}});

    commands::SystemCommands::Context context;
    context.dispatcher = &dispatcher;
    context.message = said.sink();
    context.powerSupplyRoot = power.path();
    context.now = [] { return kFixedTime; };
    commands::SystemCommands system{std::move(context), nullptr};
    dispatcher.setContextProvider(
        [&system](script::DispatchContext& ctx) { ctx.globalCommands = {&system}; });

    const auto press = [&](std::uint32_t keysym) {
        input::KeyboardGesture::Init init;
        init.event = testing::keyDown(keysym);
        init.modifiers = static_cast<input::Modifiers>(input::Modifier::Alus);
        init.config.layout = input::Layout::Desktop;
        const auto gesture = std::make_shared<input::KeyboardGesture>(std::move(init));
        const std::optional<script::Script> found = dispatcher.findScript(*gesture);
        REQUIRE(found.has_value());
        dispatcher.executeScript(*found, *gesture);
    };

    press(0xffc9);  // XK_F12
    CHECK(said.last() == commands::formatTime(kFixedTime));
    press(0xffc9);
    CHECK(said.last() == commands::formatDate(kFixedTime));
    // A third press is a repeat too, and upstream says the date for any
    // repeat -- not the time again.
    press(0xffc9);
    CHECK(said.last() == commands::formatDate(kFixedTime));

    SECTION("and the battery key reads the battery, not the clock") {
        press('b');
        CHECK(said.last() == "55 percent Unplugged");
    }
}
