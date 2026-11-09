// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/log.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <utility>

namespace alus::core {
namespace {

constexpr std::array<std::pair<LogLevel, const char*>, 8> kLevelNames{{
    {LogLevel::Debug, "DEBUG"},
    {LogLevel::Io, "IO"},
    {LogLevel::DebugWarning, "DEBUGWARNING"},
    {LogLevel::Info, "INFO"},
    {LogLevel::Warning, "WARNING"},
    {LogLevel::Error, "ERROR"},
    {LogLevel::Critical, "CRITICAL"},
    {LogLevel::Off, "OFF"},
}};

std::size_t levelIndex(LogLevel level) {
    for (std::size_t i = 0; i < kLevelNames.size(); ++i) {
        if (kLevelNames[i].first == level) {
            return i;
        }
    }
    return 3;  // an unknown level is counted as Info
}

// Base name only — full build paths carry no value in the log.
std::string_view baseName(const char* path) {
    std::string_view view{path != nullptr ? path : "?"};
    const auto slash = view.rfind('/');
    return slash == std::string_view::npos ? view : view.substr(slash + 1);
}

std::string& mutableThreadName() {
    static thread_local std::string name{"unnamed"};
    return name;
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto secs = time_point_cast<seconds>(now);
    const auto millis = duration_cast<milliseconds>(now - secs).count();

    const std::time_t tt = system_clock::to_time_t(secs);
    std::tm tm{};
    localtime_r(&tt, &tm);

    char buf[32];
    const std::size_t len = std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return std::format("{}.{:03}", std::string_view{buf, len}, millis);
}

}  // namespace

const char* levelName(LogLevel level) {
    return kLevelNames[levelIndex(level)].second;
}

bool isExpected(PlatformErrorKind kind) {
    switch (kind) {
        case PlatformErrorKind::NoReply:
        case PlatformErrorKind::ServiceUnknown:
        case PlatformErrorKind::UnknownObject:
        case PlatformErrorKind::Timeout:
        case PlatformErrorKind::Defunct:
        case PlatformErrorKind::CallCancelled:
        case PlatformErrorKind::Disconnected:
            return true;
        case PlatformErrorKind::Other:
            break;
    }
    return false;
}

PlatformErrorKind classifyDBusError(std::string_view name) {
    struct Entry {
        std::string_view name;
        PlatformErrorKind kind;
    };
    // The list from 01-ARHITEKTURA.md §3.8, plus the names GDBus really returns.
    static constexpr Entry kTable[] = {
        {"org.freedesktop.DBus.Error.NoReply", PlatformErrorKind::NoReply},
        {"org.freedesktop.DBus.Error.ServiceUnknown", PlatformErrorKind::ServiceUnknown},
        {"org.freedesktop.DBus.Error.UnknownObject", PlatformErrorKind::UnknownObject},
        {"org.freedesktop.DBus.Error.UnknownInterface", PlatformErrorKind::UnknownObject},
        {"org.freedesktop.DBus.Error.UnknownMethod", PlatformErrorKind::UnknownObject},
        {"org.freedesktop.DBus.Error.Timeout", PlatformErrorKind::Timeout},
        {"org.freedesktop.DBus.Error.TimedOut", PlatformErrorKind::Timeout},
        {"org.freedesktop.DBus.Error.Disconnected", PlatformErrorKind::Disconnected},
        {"org.freedesktop.DBus.Error.NoServer", PlatformErrorKind::Disconnected},
    };
    for (const auto& entry : kTable) {
        if (entry.name == name) {
            return entry.kind;
        }
    }
    return PlatformErrorKind::Other;
}

PlatformError::PlatformError(PlatformErrorKind kind, std::string what)
    : kind_(kind), what_(std::move(what)) {}

Log& Log::instance() {
    static Log log;
    return log;
}

Log::~Log() {
    closeFile();
}

void Log::setLevel(LogLevel level) {
    const std::lock_guard lock{mutex_};
    level_ = level;
}

LogLevel Log::level() const {
    const std::lock_guard lock{mutex_};
    return level_;
}

bool Log::isEnabledFor(LogLevel level) const {
    const std::lock_guard lock{mutex_};
    return static_cast<int>(level) >= static_cast<int>(level_) && level_ != LogLevel::Off;
}

bool Log::openFile(const std::string& path, std::string* error) {
    const std::lock_guard lock{mutex_};
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
        filePath_.clear();
    }
    if (path.empty()) {
        return true;
    }

    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path{path}.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    std::FILE* file = std::fopen(path.c_str(), "we");
    if (file == nullptr) {
        if (error != nullptr) {
            *error = std::format("{}: {}", path, std::strerror(errno));
        }
        return false;
    }
    file_ = file;
    filePath_ = path;
    return true;
}

void Log::closeFile() {
    const std::lock_guard lock{mutex_};
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    filePath_.clear();
}

void Log::setStderrEnabled(bool enabled) {
    const std::lock_guard lock{mutex_};
    stderr_ = enabled;
}

bool Log::stderrEnabled() const {
    const std::lock_guard lock{mutex_};
    return stderr_;
}

void Log::setThreadName(std::string name) {
    mutableThreadName() = std::move(name);
}

const std::string& Log::threadName() {
    return mutableThreadName();
}

void Log::write(LogLevel level, const std::source_location& loc, std::string_view message) {
    const std::string line = std::format("{} {:<12} {} {}:{} {}: {}\n", timestamp(),
                                         levelName(level), threadName(), baseName(loc.file_name()),
                                         loc.line(), loc.function_name(), message);

    const std::lock_guard lock{mutex_};
    if (static_cast<int>(level) < static_cast<int>(level_) || level_ == LogLevel::Off) {
        return;
    }
    ++counts_[levelIndex(level)];

    if (file_ != nullptr) {
        std::fwrite(line.data(), 1, line.size(), file_);
        // A screen reader dies together with the session; an unflushed buffer is a lost log.
        std::fflush(file_);
    }
    if (stderr_) {
        std::fwrite(line.data(), 1, line.size(), stderr);
    }
}

LogLevel Log::levelForException(const std::exception_ptr& eptr) {
    if (!eptr) {
        return LogLevel::Error;
    }
    try {
        std::rethrow_exception(eptr);
    } catch (const PlatformError& err) {
        return err.expected() ? LogLevel::DebugWarning : LogLevel::Error;
    } catch (...) {
        return LogLevel::Error;
    }
}

void Log::exception(std::string_view message, const std::source_location& loc) {
    const std::exception_ptr eptr = std::current_exception();
    const LogLevel level = levelForException(eptr);
    if (!isEnabledFor(level)) {
        return;
    }

    std::string detail{"izuzetak bez opisa"};
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& err) {
            detail = err.what();
        } catch (...) {
            detail = "nepoznat izuzetak (ne izvodi se iz std::exception)";
        }
    }

    write(level, loc, message.empty() ? detail : std::format("{}: {}", message, detail));
}

std::uint64_t Log::countFor(LogLevel level) const {
    const std::lock_guard lock{mutex_};
    return counts_[levelIndex(level)];
}

void Log::resetCounts() {
    const std::lock_guard lock{mutex_};
    for (auto& count : counts_) {
        count = 0;
    }
}

}  // namespace alus::core
