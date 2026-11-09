// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Where ALUS's installed data is.
//
// One rule, in one place, because more than one subsystem needs it and a
// SECOND copy of the rule is a second thing to get wrong:
//
//   1. the caller's preference (a `--data-dir` option),
//   2. $ALUS_DATA_DIR, so an uninstalled build can run against the source tree,
//   3. the directory the build was configured to install into,
//   4. `data/`, `../data`, `../../data` relative to the working directory.
//
// A candidate counts only if it contains `marker`, so a leftover empty
// directory does not win over the real one.
#pragma once

#include <filesystem>

namespace alus::core {

std::filesystem::path findDataDirectory(const std::filesystem::path& preferred,
                                        const std::filesystem::path& marker);

}  // namespace alus::core
