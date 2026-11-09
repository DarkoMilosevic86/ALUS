// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/script/command_status.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/scriptable.hpp"

#include <fstream>
#include <sstream>

namespace alus::script {
namespace {

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

bool CommandStatus::load(const std::filesystem::path& path, std::string* error) {
    std::ifstream stream{path};
    if (!stream) {
        if (error != nullptr) {
            *error = "cannot read " + path.string();
        }
        return false;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    return loadFromString(text.str(), error);
}

bool CommandStatus::loadFromString(std::string_view text, std::string* error) {
    announce_.clear();
    platform_.clear();

    // Which map the lines are going into. A file with no section header at all
    // is a file that says nothing, and saying nothing here means "everything
    // is implemented" -- so an unrecognised section is REPORTED rather than
    // treated as one of the two we know.
    std::map<std::string, std::string, std::less<>>* current = nullptr;
    std::size_t lineNumber = 0;
    bool sane = true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line =
            trim(text.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                                  : end - start));
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;
        ++lineNumber;

        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            const std::string_view section =
                close == std::string_view::npos ? line.substr(1) : line.substr(1, close - 1);
            if (section == "announce") {
                current = &announce_;
            } else if (section == "platform") {
                current = &platform_;
            } else {
                current = nullptr;
                sane = false;
                ALUS_WARNING("commands-status.ini line {}: unknown section [{}]", lineNumber,
                             section);
            }
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            sane = false;
            ALUS_WARNING("commands-status.ini line {}: no '=' in '{}'", lineNumber, line);
            continue;
        }
        if (current == nullptr) {
            sane = false;
            ALUS_WARNING("commands-status.ini line {}: '{}' is outside any section", lineNumber,
                         line);
            continue;
        }
        const std::string_view name = trim(line.substr(0, equals));
        const std::string_view reason = trim(line.substr(equals + 1));
        if (name.empty()) {
            continue;
        }
        (*current)[std::string(name)] = std::string(reason);
    }

    if (!sane && error != nullptr) {
        *error = "commands-status.ini has lines that were skipped; see the log";
    }
    return sane;
}

CommandStatus::State CommandStatus::stateOf(std::string_view script) const {
    if (announce_.find(script) != announce_.end()) {
        return State::Announce;
    }
    if (platform_.find(script) != platform_.end()) {
        return State::Platform;
    }
    return State::Implemented;
}

std::string_view CommandStatus::reasonFor(std::string_view script) const {
    if (const auto it = announce_.find(script); it != announce_.end()) {
        return it->second;
    }
    if (const auto it = platform_.find(script); it != platform_.end()) {
        return it->second;
    }
    return {};
}

std::size_t bindAnnouncements(ScriptableObject& object, const Catalog& catalog,
                              std::function<void(const std::string&)> say) {
    const CommandStatus& status = catalog.status();
    std::size_t bound = 0;
    for (const ScriptInfo* info : catalog.all()) {
        if (status.stateOf(info->name) != CommandStatus::State::Announce) {
            continue;
        }
        if (!object.answersTo(info->location)) {
            continue;
        }
        if (object.scriptByName(info->name) != nullptr) {
            // Already implemented here. Not an error: the status file is
            // hand-maintained and may be one commit behind the code. The
            // coverage test is what reports the stale line.
            continue;
        }
        const std::string name = info->name;
        object.bindScript(name, [name, say](input::Gesture&) {
            ALUS_INFO("'{}' is not implemented in this version (data/commands-status.ini)",
                      name);
            if (say) {
                // Translators: spoken when a key runs a command that this
                // version of ALUS does not have yet. Short on purpose: what
                // matters is that the key is not silent.
                say(_("not in this version"));
            }
        });
        ++bound;
    }
    return bound;
}

}  // namespace alus::script
