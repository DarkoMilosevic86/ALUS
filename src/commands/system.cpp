// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/commands/system.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/ui/message.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <ctime>
#include <fstream>
#include <langinfo.h>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace alus::commands {

namespace {
// The repeat count of the script running now, 0 when there is no dispatcher.
int repeatsOf(const std::function<const script::Dispatcher*()>& dispatcher) {
    const script::Dispatcher* current = dispatcher ? dispatcher() : nullptr;
    return current != nullptr ? current->lastScriptRepeatCount() : 0;
}
}  // namespace

namespace {

constexpr const char* kPowerSupplyRoot = "/sys/class/power_supply";

// One line of a /sys file, trimmed. Nothing when the file is not there, which
// is the normal answer for most of these on most machines.
std::optional<std::string> readLine(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::string line;
    if (!std::getline(file, line)) {
        return std::nullopt;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

std::optional<long long> readNumber(const std::filesystem::path& path) {
    const std::optional<std::string> text = readLine(path);
    if (!text.has_value() || text->empty()) {
        return std::nullopt;
    }
    long long value = 0;
    const char* begin = text->data();
    const char* end = begin + text->size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

// strftime into a std::string, with the locale ALUS set up.
std::string formatWith(const char* pattern, std::time_t when) {
    std::tm parts{};
    if (localtime_r(&when, &parts) == nullptr) {
        return {};
    }
    std::array<char, 256> buffer{};
    const std::size_t written = std::strftime(buffer.data(), buffer.size(), pattern, &parts);
    if (written == 0) {
        return {};
    }
    return std::string{buffer.data(), written};
}

}  // namespace

// The locale's time format with the seconds taken out.
//
// Upstream asks Windows whether the taskbar clock shows seconds and matches
// it. Linux has no such single setting, and a spoken clock that counts
// seconds is noise in every case a user presses this key -- so the seconds go
// and the LOCALE's own arrangement stays, which is the part that matters
// (12-hour versus 24-hour, and where the am/pm goes).
std::string timePatternWithoutSeconds(std::string pattern) {
    // The shorthands first: `%T` is `%H:%M:%S` and `%r` is `%I:%M:%S %p`, and
    // `%X` is the locale's own time format, which glibc spells as one of them.
    // Without this the search below finds no `%S` and the seconds stay --
    // which is what sr_RS did ("12:17:46").
    const auto expand = [&pattern](std::string_view from, std::string_view to) {
        for (std::size_t at = pattern.find(from); at != std::string::npos;
             at = pattern.find(from, at + to.size())) {
            pattern.replace(at, from.size(), to);
        }
    };
    expand("%T", "%H:%M:%S");
    expand("%r", "%I:%M:%S %p");
    expand("%X", "%H:%M:%S");
    if (pattern.empty()) {
        return "%H:%M";
    }
    // Drop the seconds field together with whatever separates it from the
    // minutes: ":%S", ".%S" or a bare "%S".
    for (const std::string_view seconds : {":%S", ".%S", " %S", "%S"}) {
        const std::size_t at = pattern.find(seconds);
        if (at != std::string::npos) {
            pattern.erase(at, seconds.size());
            break;
        }
    }
    return pattern.empty() ? "%H:%M" : pattern;
}

std::string formatTime(std::time_t when) {
    const char* localeTime = nl_langinfo(T_FMT);
    return formatWith(timePatternWithoutSeconds(localeTime != nullptr ? localeTime : "").c_str(),
                      when);
}

std::string formatDate(std::time_t when) {
    // The long date: the weekday and the month written out, which is what
    // upstream's DATE_LONGDATE gives and what a spoken date wants.
    //
    // glibc exposes no "long date" format the way Windows does (D_FMT is the
    // short, numeric one), so the arrangement is ALUS's and the NAMES are the
    // locale's. `%e` rather than `%d`: a space-padded day, so a synthesizer
    // does not read "09" as "zero nine".
    std::string text = formatWith("%A, %e %B %Y", when);
    // %e pads with a space, which leaves a double space after the comma.
    const std::size_t doubled = text.find("  ");
    if (doubled != std::string::npos) {
        text.erase(doubled, 1);
    }
    return text;
}

BatteryStatus readBatteryStatus(const std::filesystem::path& root) {
    const std::filesystem::path base = root.empty() ? std::filesystem::path{kPowerSupplyRoot}
                                                    : root;
    BatteryStatus status;
    std::error_code code;
    if (!std::filesystem::is_directory(base, code)) {
        // No /sys/class/power_supply at all: a kernel without the class, or a
        // container. NOT "no battery": that is a different sentence and a
        // different fact.
        ALUS_DEBUG("battery: {} is not a directory; power status unknown", base.string());
        return status;
    }

    // The directory was readable, so the kernel HAS answered: whatever it
    // lists is the truth about this machine. An empty directory therefore
    // means "no battery" and not "unknown" -- which is the difference between
    // a desktop (where "No system battery" is correct and final) and a kernel
    // with no power-supply class at all (where saying that would be a guess).
    status.known = true;

    std::vector<std::filesystem::path> batteries;
    std::vector<std::filesystem::path> mains;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{base, code}) {
        const std::optional<std::string> type = readLine(entry.path() / "type");
        if (!type.has_value()) {
            continue;
        }
        if (*type == "Battery") {
            batteries.push_back(entry.path());
        } else if (*type == "Mains" || *type == "USB") {
            mains.push_back(entry.path());
        }
    }
    // Sorted, so a machine with two batteries answers the same way twice.
    std::sort(batteries.begin(), batteries.end());
    std::sort(mains.begin(), mains.end());

    for (const std::filesystem::path& supply : mains) {
        if (const std::optional<long long> online = readNumber(supply / "online");
            online.value_or(0) != 0) {
            status.pluggedIn = true;
            break;
        }
    }

    if (batteries.empty()) {
        // A desktop: the mains told us the power state, and there is no
        // battery to report. `known` is already set if any mains was read.
        return status;
    }

    const std::filesystem::path& battery = batteries.front();
    if (batteries.size() > 1) {
        // Deliberately the first one and said out loud in the log rather than
        // averaged: two batteries at different charges have no single
        // percentage, and inventing one would be worse than naming which.
        ALUS_DEBUG("battery: {} batteries present; reporting {}", batteries.size(),
                   battery.filename().string());
    }

    const std::optional<long long> capacity = readNumber(battery / "capacity");
    if (!capacity.has_value()) {
        ALUS_DEBUG("battery: {} has no `capacity`; power status unknown", battery.string());
        return status;
    }
    status.present = true;
    status.percent = static_cast<int>(std::clamp<long long>(*capacity, 0, 100));

    // `status` is Charging, Discharging, Full, "Not charging" or Unknown. It
    // is the battery's own view, and it is what tells plugged-in from
    // discharging on a machine whose mains supply reports nothing.
    if (const std::optional<std::string> state = readLine(battery / "status");
        state.has_value()) {
        if (*state == "Charging" || *state == "Full" || *state == "Not charging") {
            status.pluggedIn = true;
        } else if (*state == "Discharging") {
            status.pluggedIn = false;
        }
    }

    // How long is left. The kernel reports either energy (µWh) with power
    // (µW), or charge (µAh) with current (µA), depending on the driver -- so
    // both pairs are tried, and neither being there is a normal answer.
    const bool charging = status.pluggedIn;
    const auto remaining = [&](const char* nowFile, const char* fullFile,
                               const char* rateFile) -> std::optional<int> {
        const std::optional<long long> rate = readNumber(battery / rateFile);
        if (!rate.has_value() || *rate <= 0) {
            return std::nullopt;
        }
        const std::optional<long long> now = readNumber(battery / nowFile);
        if (!now.has_value() || *now < 0) {
            return std::nullopt;
        }
        long long amount = *now;
        if (charging) {
            const std::optional<long long> full = readNumber(battery / fullFile);
            if (!full.has_value() || *full < *now) {
                return std::nullopt;
            }
            amount = *full - *now;
        }
        const long long minutes = amount * 60 / *rate;
        if (minutes < 0 || minutes > 60 * 72) {
            // Three days is not a battery estimate, it is a driver reporting
            // a rate of almost nothing. Saying nothing beats saying that.
            return std::nullopt;
        }
        return static_cast<int>(minutes);
    };
    status.minutesRemaining = remaining("energy_now", "energy_full", "power_now");
    if (!status.minutesRemaining.has_value()) {
        status.minutesRemaining = remaining("charge_now", "charge_full", "current_now");
    }
    return status;
}

std::string describeBattery(const BatteryStatus& status) {
    if (!status.known) {
        // Translators: spoken when the battery status cannot be read at all.
        return _("Unknown power status");
    }
    if (!status.present) {
        // Translators: spoken on a machine with no battery -- a desktop, or a
        // laptop with the battery taken out.
        return _("No system battery");
    }

    std::vector<std::string> parts;
    // Translators: the current battery charge. %d is the percentage.
    parts.push_back(i18n::substituteNumber(P_("%d percent", "%d percent", status.percent),
                                           std::to_string(status.percent)));

    if (status.minutesRemaining.has_value()) {
        const int total = *status.minutesRemaining;
        const int hours = total / 60;
        const int minutes = total % 60;
        if (hours == 0 && minutes == 0) {
            // Translators: spoken when less than a minute of battery is left.
            parts.push_back(_("Less than 1 minute remaining"));
        } else {
            // Translators: the hours part of the remaining battery time. In
            // "1 hour and 34 minutes remaining", this is "1 hour".
            const std::string hourText =
                hours > 0 ? i18n::substituteNamed(P_("{hours:d} hour", "{hours:d} hours", hours),
                                                  "hours:d", std::to_string(hours))
                          : std::string{};
            // Translators: the minutes part of the remaining battery time. In
            // "1 hour and 34 minutes remaining", this is "34 minutes".
            const std::string minuteText =
                minutes > 0
                    ? i18n::substituteNamed(
                          P_("{minutes:d} minute", "{minutes:d} minutes", minutes), "minutes:d",
                          std::to_string(minutes))
                    : std::string{};
            if (!hourText.empty() && !minuteText.empty()) {
                // Translators: the remaining battery time, hours and minutes.
                parts.push_back(i18n::substituteNamed(
                    i18n::substituteNamed(_("{hourText} and {minuteText} remaining"), "hourText",
                                          hourText),
                    "minuteText", minuteText));
            } else if (!hourText.empty()) {
                // Translators: the remaining battery time, whole hours.
                parts.push_back(
                    i18n::substituteNamed(_("{hourText} remaining"), "hourText", hourText));
            } else {
                // Translators: the remaining battery time, minutes only.
                parts.push_back(
                    i18n::substituteNamed(_("{minuteText} remaining"), "minuteText", minuteText));
            }
        }
    }

    parts.push_back(status.pluggedIn
                        // Translators: spoken when the machine is on mains power.
                        ? _("Plugged in")
                        // Translators: spoken when the machine is on battery.
                        : _("Unplugged"));

    std::string text;
    for (const std::string& part : parts) {
        if (!text.empty()) {
            text += ' ';
        }
        text += part;
    }
    return text;
}

SystemCommands::SystemCommands(Context context, const script::Catalog* catalog)
    // One class name, NVDA's: it is what the generated gesture map addresses.
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      context_(std::move(context)) {
    ALUS_SCRIPT(dateTime);
    ALUS_SCRIPT(say_battery_status);
}

void SystemCommands::say(const std::string& text) const {
    if (context_.message) {
        context_.message(text);
        return;
    }
    ui::message(text);
}

int SystemCommands::repeatCount() const {
    return repeatsOf(context_.dispatcher);
}

std::time_t SystemCommands::clock() const {
    return context_.now ? context_.now() : std::time(nullptr);
}

void SystemCommands::dateTime(input::Gesture& /*gesture*/) {
    const std::time_t when = clock();
    // Upstream's rule: the first press is the time, any repeat is the date.
    const std::string text = repeatCount() == 0 ? formatTime(when) : formatDate(when);
    if (text.empty()) {
        // Never silent, even here: a locale with no usable time format would
        // otherwise make this key look broken.
        ALUS_WARNING("dateTime: the locale produced no time or date string");
        // Translators: spoken when the clock cannot be read at all, which
        // takes a locale with no time format. Never silence: a key that says
        // nothing is indistinguishable from a key that did not arrive.
        say(_("cannot read the clock"));
        return;
    }
    say(text);
}

void SystemCommands::say_battery_status(input::Gesture& /*gesture*/) {
    say(describeBattery(readBatteryStatus(context_.powerSupplyRoot)));
}

}  // namespace alus::commands
