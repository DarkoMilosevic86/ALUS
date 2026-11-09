// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/script/catalog.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/key_names.hpp"

#include <fstream>
#include <sstream>

namespace alus::script {
namespace {

// An empty string stays empty: gettext reads the empty msgid as a request for
// the catalogue's own header, which must never reach the user.
std::string translated(std::string text) {
    return text.empty() ? text : std::string(_(text.c_str()));
}

// A CSV reader just big enough for the generated file: quoted fields with
// doubled quotes, no embedded newlines (the extractor never writes any).
std::vector<std::string> parseCsvLine(std::string_view line) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                current.push_back(c);
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
}

std::vector<std::string> splitSpace(const std::string& text) {
    std::vector<std::string> parts;
    std::istringstream stream(text);
    std::string part;
    while (stream >> part) {
        parts.push_back(part);
    }
    return parts;
}

bool toBool(const std::string& text) { return text == "1" || text == "true"; }

std::string makeKey(std::string_view location, std::string_view name) {
    return std::string(location) + "/" + std::string(name);
}

}  // namespace

Catalog::Catalog() = default;

bool Catalog::loadScripts(const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open " + path.string();
        }
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return loadScriptsFromString(contents.str(), error);
}

bool Catalog::loadScriptsFromString(std::string_view csv, std::string* error) {
    std::istringstream stream{std::string(csv)};
    std::string line;
    if (!std::getline(stream, line)) {
        if (error != nullptr) {
            *error = "the script catalogue is empty";
        }
        return false;
    }
    const std::vector<std::string> header = parseCsvLine(line);
    std::map<std::string, std::size_t, std::less<>> column;
    for (std::size_t i = 0; i < header.size(); ++i) {
        column[header[i]] = i;
    }
    const auto require = [&](const char* name) -> bool {
        return column.find(name) != column.end();
    };
    if (!require("module") || !require("class") || !require("script")) {
        if (error != nullptr) {
            *error = "the script catalogue is missing its module/class/script columns";
        }
        return false;
    }
    const auto field = [&](const std::vector<std::string>& fields,
                           const char* name) -> std::string {
        const auto it = column.find(name);
        if (it == column.end() || it->second >= fields.size()) {
            return {};
        }
        return fields[it->second];
    };

    std::size_t loaded = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = parseCsvLine(line);
        if (fields.size() < 3) {
            continue;
        }
        if (field(fields, "scope") == "deferred") {
            // A v1.0 non-goal (00-VIZIJA.md §5). Deliberately not registered:
            // a gesture bound to a command that does not exist would show up
            // in the gesture dialog as a command ALUS cannot run.
            ++deferred_;
            continue;
        }
        auto info = std::make_unique<ScriptInfo>();
        info->name = field(fields, "script");
        info->location = field(fields, "module") + "." + field(fields, "class");
        // Translated HERE, as the row loads, and not at each use: the
        // description is spoken by input help on every key press while input
        // help is on, and the category is a gesture-dialog heading. The
        // msgids are in src/i18n/data_strings.cpp, which the extractor writes
        // from this same CSV so that xgettext can see them at all.
        info->description = translated(field(fields, "description"));
        info->category = translated(field(fields, "category"));
        for (const std::string& gesture : splitSpace(field(fields, "gestures"))) {
            info->gestures.push_back(gesture);
        }
        for (const std::string& gesture : splitSpace(field(fields, "gestures_desktop"))) {
            info->gestures.push_back(gesture);
        }
        for (const std::string& gesture : splitSpace(field(fields, "gestures_laptop"))) {
            info->gestures.push_back(gesture);
        }
        info->canPropagate = toBool(field(fields, "can_propagate"));
        info->bypassInputHelp = toBool(field(fields, "bypass_input_help"));
        info->allowInSleepMode = toBool(field(fields, "allow_in_sleep_mode"));
        info->speakOnDemand = toBool(field(fields, "speak_on_demand"));
        info->resumeSayAllMode = sayAllCursorFromName(field(fields, "resume_say_all_mode"));
        const std::string key = makeKey(info->location, info->name);
        byKey_[key] = std::move(info);
        ++loaded;
    }
    ALUS_INFO("script catalogue: {} scripts, {} deferred", loaded, deferred_);
    return loaded > 0;
}

bool Catalog::loadBuiltinGestures(const std::filesystem::path& path, std::string* error) {
    return builtin_.load(path, error);
}

bool Catalog::loadBuiltinGesturesFromString(std::string_view text, std::string* error) {
    return builtin_.loadFromString(text, error);
}

bool Catalog::loadFromDataDirectory(const std::filesystem::path& dataDir) {
    bool complete = true;
    std::string error;
    if (!loadScripts(dataDir / "scripts.csv", &error)) {
        ALUS_WARNING("script catalogue: {} — commands will carry no descriptions", error);
        complete = false;
    }
    error.clear();
    if (!loadBuiltinGestures(dataDir / "gestures-builtin.ini", &error)) {
        ALUS_WARNING("built-in gesture map: {} — only explicit bindings will work", error);
        complete = false;
    }
    error.clear();
    // Not fatal, and the failure mode is worth naming: with no status file
    // every unimplemented command goes back to being SILENT, which is the
    // defect this file exists to prevent (claude/FAZA9.md N99).
    if (!loadStatus(dataDir / "commands-status.ini", &error)) {
        ALUS_WARNING("command status: {} — unimplemented commands will be silent", error);
        complete = false;
    }
    error.clear();
    if (!input::KeyNames::shared().loadLabels(dataDir / "key-labels.ini", &error)) {
        ALUS_WARNING("key labels: {} — keys will be spoken by their internal names", error);
        complete = false;
    }
    return complete;
}

bool Catalog::loadStatus(const std::filesystem::path& path, std::string* error) {
    return status_.load(path, error);
}

const ScriptInfo* Catalog::find(std::string_view location, std::string_view name) const {
    const auto it = byKey_.find(makeKey(location, name));
    return it == byKey_.end() ? nullptr : it->second.get();
}

std::vector<const ScriptInfo*> Catalog::all() const {
    std::vector<const ScriptInfo*> result;
    result.reserve(byKey_.size());
    for (const auto& [key, info] : byKey_) {
        result.push_back(info.get());
    }
    return result;
}

}  // namespace alus::script
