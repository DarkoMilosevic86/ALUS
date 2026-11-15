// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Core logging.
//
// Port of NVDA's `logHandler.py`. The one functionally important detail (see
// architecture.md §3.8): `Log::exception()` degrades EXPECTED platform errors
// to the DebugWarning level, because errors play an error sound, and a screen
// reader that chirps on every D-Bus timeout is unusable.
#pragma once

#include <cstdint>
#include <format>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>

namespace alus::core {

// The numeric values are NVDA's (logHandler.py:226-231) on purpose, so that
// levels mentioned in NVDA documentation and add-on code carry over untranslated.
enum class LogLevel : int {
    Debug = 10,
    Io = 12,            // traffic with the platform (D-Bus, SSIP, brlapi)
    DebugWarning = 15,  // expected problem; does not play an error sound
    Info = 20,
    Warning = 30,
    Error = 40,
    Critical = 50,
    Off = 100,
};

const char* levelName(LogLevel level);

// Platform error classification. `isExpected()` decides the degradation.
enum class PlatformErrorKind {
    Other,           // → Error
    NoReply,         // org.freedesktop.DBus.Error.NoReply
    ServiceUnknown,  // org.freedesktop.DBus.Error.ServiceUnknown
    UnknownObject,   // org.freedesktop.DBus.Error.UnknownObject
    Timeout,         // org.freedesktop.DBus.Error.Timeout / .TimedOut
    Defunct,         // the AT-SPI object died
    CallCancelled,   // the watchdog abandoned the call
    Disconnected,    // the bus connection broke
};

bool isExpected(PlatformErrorKind kind);

// D-Bus error name → classification. An empty or unknown name yields `Other`.
PlatformErrorKind classifyDBusError(std::string_view dbusErrorName);

// An exception carrying a classification. The platform layer (phase 3 onwards)
// throws these; the core only recognises them.
class PlatformError : public std::exception {
public:
    PlatformError(PlatformErrorKind kind, std::string what);

    const char* what() const noexcept override { return what_.c_str(); }
    PlatformErrorKind kind() const noexcept { return kind_; }
    bool expected() const noexcept { return isExpected(kind_); }

private:
    PlatformErrorKind kind_;
    std::string what_;
};

class Log {
public:
    static Log& instance();

    // Records below this level are dropped before they are formatted.
    void setLevel(LogLevel level);
    LogLevel level() const;
    bool isEnabledFor(LogLevel level) const;

    // Log to a file. An empty path closes the current file. Returns false and
    // fills `error` if the file cannot be opened; the caller then keeps stderr
    // as its only sink.
    bool openFile(const std::string& path, std::string* error = nullptr);
    // The file being logged to; empty when the log goes to stderr only.
    std::string filePath() const;
    void closeFile();

    void setStderrEnabled(bool enabled);
    bool stderrEnabled() const;

    // Thread name as it appears in records (main, watchdog, ssip-reader,
    // braille-io, ...).
    static void setThreadName(std::string name);
    static const std::string& threadName();

    void write(LogLevel level, const std::source_location& loc, std::string_view message);

    template <class... Args>
    void writef(LogLevel level, const std::source_location& loc,
                std::format_string<Args...> fmt, Args&&... args) {
        if (!isEnabledFor(level)) {
            return;
        }
        write(level, loc, std::format(fmt, std::forward<Args>(args)...));
    }

    // Called FROM a catch block. Classifies `std::current_exception()`: a
    // PlatformError of an expected kind → DebugWarning, everything else → Error.
    void exception(std::string_view message = {},
                   const std::source_location& loc = std::source_location::current());

    // The level `exception()` would use for a given exception. Exposed for
    // tests and for callers that decide for themselves.
    static LogLevel levelForException(const std::exception_ptr& eptr);

    // Record count per level — for tests and for `--log-stats`.
    std::uint64_t countFor(LogLevel level) const;
    void resetCounts();

private:
    Log() = default;
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    bool stderr_ = true;
    std::FILE* file_ = nullptr;
    std::string filePath_;
    std::uint64_t counts_[8] = {};
};

}  // namespace alus::core

#define ALUS_LOG_AT(level, ...)                                                   \
    ::alus::core::Log::instance().writef((level), ::std::source_location::current(), \
                                         __VA_ARGS__)

#define ALUS_DEBUG(...) ALUS_LOG_AT(::alus::core::LogLevel::Debug, __VA_ARGS__)
#define ALUS_IO(...) ALUS_LOG_AT(::alus::core::LogLevel::Io, __VA_ARGS__)
#define ALUS_DEBUG_WARNING(...) ALUS_LOG_AT(::alus::core::LogLevel::DebugWarning, __VA_ARGS__)
#define ALUS_INFO(...) ALUS_LOG_AT(::alus::core::LogLevel::Info, __VA_ARGS__)
#define ALUS_WARNING(...) ALUS_LOG_AT(::alus::core::LogLevel::Warning, __VA_ARGS__)
#define ALUS_ERROR(...) ALUS_LOG_AT(::alus::core::LogLevel::Error, __VA_ARGS__)
#define ALUS_CRITICAL(...) ALUS_LOG_AT(::alus::core::LogLevel::Critical, __VA_ARGS__)
