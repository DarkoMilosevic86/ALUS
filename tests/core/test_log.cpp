// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Logging: levels, and degradation of expected exceptions (architecture.md §3.8).
#include "alus/core/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using alus::core::classifyDBusError;
using alus::core::Log;
using alus::core::LogLevel;
using alus::core::PlatformError;
using alus::core::PlatformErrorKind;

namespace {

// Captures the whole log into a string, without stderr noise.
class LogCapture {
public:
    LogCapture() : path_(std::filesystem::temp_directory_path() / "alus-test-log.txt") {
        std::filesystem::remove(path_);
        Log::instance().setStderrEnabled(false);
        Log::instance().setLevel(LogLevel::Debug);
        Log::instance().resetCounts();
        REQUIRE(Log::instance().openFile(path_.string()));
    }

    ~LogCapture() {
        Log::instance().closeFile();
        Log::instance().setStderrEnabled(true);
        Log::instance().setLevel(LogLevel::Info);
        std::filesystem::remove(path_);
    }

    std::string contents() const {
        std::ifstream stream{path_};
        std::ostringstream out;
        out << stream.rdbuf();
        return out.str();
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("levels filter records", "[core][log]") {
    LogCapture capture;
    Log::instance().setLevel(LogLevel::Warning);

    ALUS_DEBUG("this stays invisible");
    ALUS_IO("so does this");
    ALUS_WARNING("this is visible: {}", 1);
    ALUS_ERROR("and this one too");

    const std::string text = capture.contents();
    REQUIRE(text.find("this stays invisible") == std::string::npos);
    REQUIRE(text.find("this is visible: 1") != std::string::npos);
    REQUIRE(Log::instance().countFor(LogLevel::Warning) == 1);
    REQUIRE(Log::instance().countFor(LogLevel::Debug) == 0);
}

TEST_CASE("a record carries level, thread and code location", "[core][log]") {
    LogCapture capture;
    Log::setThreadName("main");
    ALUS_INFO("hello {}", "world");

    const std::string text = capture.contents();
    REQUIRE(text.find("INFO") != std::string::npos);
    REQUIRE(text.find("main") != std::string::npos);
    REQUIRE(text.find("test_log.cpp") != std::string::npos);
    REQUIRE(text.find("hello world") != std::string::npos);
}

TEST_CASE("an expected platform error degrades to DebugWarning", "[core][log]") {
    LogCapture capture;

    try {
        throw PlatformError{PlatformErrorKind::NoReply, "org.gnome.Nautilus does not answer"};
    } catch (...) {
        Log::instance().exception("get_name");
    }

    REQUIRE(Log::instance().countFor(LogLevel::DebugWarning) == 1);
    REQUIRE(Log::instance().countFor(LogLevel::Error) == 0);
    REQUIRE(capture.contents().find("get_name: org.gnome.Nautilus does not answer") !=
            std::string::npos);
}

TEST_CASE("an unexpected exception stays an Error", "[core][log]") {
    LogCapture capture;

    try {
        throw std::runtime_error{"bug in the core"};
    } catch (...) {
        Log::instance().exception();
    }

    REQUIRE(Log::instance().countFor(LogLevel::Error) == 1);
    REQUIRE(Log::instance().countFor(LogLevel::DebugWarning) == 0);
    REQUIRE(capture.contents().find("bug in the core") != std::string::npos);
}

TEST_CASE("PlatformErrorKind::Other is not degraded", "[core][log]") {
    LogCapture capture;

    try {
        throw PlatformError{PlatformErrorKind::Other, "unknown failure"};
    } catch (...) {
        Log::instance().exception();
    }

    REQUIRE(Log::instance().countFor(LogLevel::Error) == 1);
}

TEST_CASE("D-Bus error names are classified", "[core][log]") {
    REQUIRE(classifyDBusError("org.freedesktop.DBus.Error.NoReply") ==
            PlatformErrorKind::NoReply);
    REQUIRE(classifyDBusError("org.freedesktop.DBus.Error.ServiceUnknown") ==
            PlatformErrorKind::ServiceUnknown);
    REQUIRE(classifyDBusError("org.freedesktop.DBus.Error.UnknownObject") ==
            PlatformErrorKind::UnknownObject);
    REQUIRE(classifyDBusError("org.freedesktop.DBus.Error.TimedOut") == PlatformErrorKind::Timeout);
    REQUIRE(classifyDBusError("org.example.Something") == PlatformErrorKind::Other);
    REQUIRE(classifyDBusError("") == PlatformErrorKind::Other);
}

TEST_CASE("the Off level silences everything", "[core][log]") {
    LogCapture capture;
    Log::instance().setLevel(LogLevel::Off);
    ALUS_CRITICAL("not even this may show up");
    REQUIRE(capture.contents().empty());
}
