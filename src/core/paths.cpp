// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/core/paths.hpp"

#include "alus/core/log.hpp"

#include <cstdlib>

#ifndef ALUS_DATA_DIR
#define ALUS_DATA_DIR ""
#endif

namespace alus::core {

std::filesystem::path findDataDirectory(const std::filesystem::path& preferred,
                                        const std::filesystem::path& marker) {
    std::error_code code;
    const auto usable = [&](const std::filesystem::path& candidate) {
        return !candidate.empty() && std::filesystem::exists(candidate / marker, code);
    };
    if (usable(preferred)) {
        return preferred;
    }
    if (const char* fromEnvironment = std::getenv("ALUS_DATA_DIR")) {
        const std::filesystem::path candidate{fromEnvironment};
        if (usable(candidate)) {
            return candidate;
        }
        ALUS_WARNING("ALUS_DATA_DIR={} does not contain {}", fromEnvironment, marker.string());
    }
    // A program running from a build tree uses the source tree's data, even
    // when an installed copy exists: a developer who also has ALUS installed
    // would otherwise run new code against old data (found before 2026.1.0).
    // build/src/<program> -> build/meson-info marks the tree, ../../data is
    // the source.
    if (const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", code);
        !code) {
        const std::filesystem::path buildDir = self.parent_path().parent_path();
        if (std::filesystem::exists(buildDir / "meson-info", code)) {
            const std::filesystem::path source = buildDir.parent_path() / "data";
            if (usable(source)) {
                return source;
            }
        }
    }
    const std::filesystem::path installed{ALUS_DATA_DIR};
    if (usable(installed)) {
        return installed;
    }
    for (const std::filesystem::path candidate : {"data", "../data", "../../data"}) {
        if (usable(candidate)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace alus::core
