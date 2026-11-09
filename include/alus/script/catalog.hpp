// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The generated command catalogue.
//
// `tools/extract-nvda-commands.py` reads NVDA's source and writes
// `data/scripts.csv` (one row per script: description, category, gestures,
// flags) and `data/gestures-builtin.ini` (the built-in gesture layer). This
// class loads both.
//
// WHY THE METADATA IS DATA AND NOT CODE. NVDA has ~300 scripts in 21
// categories, with descriptions that are also its user documentation. Typing
// those into C++ by hand would be several thousand lines of transcription,
// and every typo would be a command that reads wrong to a blind user or a key
// that is bound to nothing. §5.4 of the architecture is explicit: the
// extractor is the SOURCE OF TRUTH. So ALUS_SCRIPT writes down only what
// changes behaviour, and the catalogue supplies the rest.
//
// A script with no catalogue row still works — it just carries only what its
// ALUS_SCRIPT spec says, which is how ALUS-specific commands (ones NVDA has
// no counterpart for) are declared.
#pragma once

#include "alus/input/gesture_map.hpp"
#include "alus/script/command_status.hpp"
#include "alus/script/script.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace alus::script {

class Catalog {
public:
    Catalog();

    // data/scripts.csv
    bool loadScripts(const std::filesystem::path& path, std::string* error = nullptr);
    bool loadScriptsFromString(std::string_view csv, std::string* error = nullptr);
    // data/gestures-builtin.ini
    bool loadBuiltinGestures(const std::filesystem::path& path, std::string* error = nullptr);
    bool loadBuiltinGesturesFromString(std::string_view text, std::string* error = nullptr);
    // data/commands-status.ini -- the one file in the set that is NOT
    // generated. See command_status.hpp for why it is here and not in a test.
    bool loadStatus(const std::filesystem::path& path, std::string* error = nullptr);

    // Loads all three generated files out of a data directory, and the key
    // labels with them. Missing files are logged, not fatal: a broken
    // installation must still start and still speak.
    bool loadFromDataDirectory(const std::filesystem::path& dataDir);

    const ScriptInfo* find(std::string_view location, std::string_view name) const;
    std::size_t scriptCount() const noexcept { return byKey_.size(); }
    // Scripts that are in v1.0 scope, grouped by category; for the gesture
    // dialog in phase 9 and for the generated command reference.
    std::vector<const ScriptInfo*> all() const;

    const input::GestureMap& builtinGestures() const noexcept { return builtin_; }
    input::GestureMap& builtinGestures() noexcept { return builtin_; }

    // Which commands ALUS has not written yet, and what their keys do.
    const CommandStatus& status() const noexcept { return status_; }

    // Rows marked `deferred` by the extractor (v1.0 non-goals) are kept out of
    // the catalogue; this is how many were skipped, for the start-up log.
    std::size_t deferredCount() const noexcept { return deferred_; }

private:
    std::map<std::string, std::unique_ptr<ScriptInfo>, std::less<>> byKey_;
    input::GestureMap builtin_{"built-in"};
    CommandStatus status_;
    std::size_t deferred_ = 0;
};

}  // namespace alus::script
