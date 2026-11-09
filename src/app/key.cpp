// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/app/key.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>

namespace alus::app {
namespace {

std::string lowered(std::string_view text) {
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

// NVDA's `EXECUTABLE_NAMES_TO_APP_MODS`, as the Linux equivalents.
//
// Each entry is a claim that two executables behave identically from a screen
// reader's point of view, and each one here was checked against the thing it
// claims about:
//
//   soffice.bin      the process LibreOffice actually runs as; `soffice` is
//                    the shell wrapper that execs it, so the name a user
//                    knows and the name /proc reports differ
//   code-insiders,   the nightly and the distribution build of VS Code; same
//   code-oss         Electron, same accessibility tree
//   codium           VSCodium, likewise
//   gnome-terminal   the process is `gnome-terminal-server`; the command the
//                    user types is `gnome-terminal`
//   mate-terminal    a GNOME Terminal fork, same VTE widget
//   firefox-esr,     Debian's package name and the nightly channel
//   firefox-bin
struct Alias {
    std::string_view from;
    std::string_view to;
};

constexpr std::array<Alias, 9> kAliases{{
    {"soffice", "soffice.bin"},
    {"libreoffice", "soffice.bin"},
    {"code-insiders", "code"},
    {"code-oss", "code"},
    {"codium", "code"},
    {"gnome-terminal", "gnome-terminal-server"},
    {"mate-terminal", "gnome-terminal-server"},
    {"firefox-esr", "firefox"},
    {"firefox-bin", "firefox"},
}};

}  // namespace

std::string AppKey::describe() const {
    std::string label = executable.empty() ? (name.empty() ? "?" : name) : executable;
    if (!name.empty() && name != executable) {
        label += std::format(" \"{}\"", name);
    }
    if (!toolkit.empty()) {
        label += std::format(" ({} {})", toolkit,
                             toolkitVersion.empty() ? "?" : toolkitVersion);
    }
    if (processId > 0) {
        label += std::format(" pid {}", processId);
    }
    return label;
}

std::string executableForPid(int processId) {
    if (processId <= 0) {
        return {};
    }
    // read_symlink and not readlink(2): /proc/<pid>/exe is a symlink whose
    // target may be longer than PATH_MAX for a deleted binary (" (deleted)"
    // is appended), and the filesystem library gets that right.
    std::error_code ec;
    const std::filesystem::path link =
        std::filesystem::read_symlink(std::format("/proc/{}/exe", processId), ec);
    if (ec) {
        // Ordinary: the process exited, or it is in another PID namespace (a
        // Flatpak application), or /proc is not mounted (a container). The
        // caller falls back on the accessible name and the toolkit.
        return {};
    }
    std::string name = link.filename().string();
    // A binary that was replaced while running: "/usr/bin/code (deleted)".
    // The suffix is the kernel's, not part of the name.
    constexpr std::string_view kDeleted = " (deleted)";
    if (name.size() > kDeleted.size() &&
        name.compare(name.size() - kDeleted.size(), kDeleted.size(), kDeleted) == 0) {
        name.erase(name.size() - kDeleted.size());
    }
    return lowered(name);
}

AppKey keyFor(const a11y::AppInfo& info) {
    AppKey key;
    key.processId = info.processId;
    key.executable = executableForPid(info.processId);
    key.name = info.name;
    key.toolkit = lowered(info.toolkitName);
    key.toolkitVersion = info.toolkitVersion;
    // No executable (a sandboxed application): the accessible name is the only
    // name there is, so it stands in. Lower-cased, because it is being used as
    // a lookup key and not shown.
    if (key.executable.empty() && !info.name.empty()) {
        key.executable = lowered(info.name);
    }
    return key;
}

std::string_view canonicalExecutable(std::string_view executable) {
    const auto it = std::find_if(kAliases.begin(), kAliases.end(), [executable](const Alias& alias) {
        return alias.from == executable;
    });
    return it != kAliases.end() ? it->to : executable;
}

}  // namespace alus::app
