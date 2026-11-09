// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/gesture_map.hpp"

#include "alus/core/log.hpp"
#include "alus/input/gesture.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace alus::input {
namespace {

constexpr std::string_view kUnbindScriptName = "None";

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// Whether what follows a comma is the start of ANOTHER gesture, and not a
// comma that is itself the key being bound. A gesture identifier always
// begins with its device source followed by `:` or `(`: `kb:`, `kb(laptop):`,
// `br(brlapi):`, `bk:`, `ts:`.
bool startsAGesture(std::string_view text) {
    text = trim(text);
    std::size_t index = 0;
    while (index < text.size() &&
           ((text[index] >= 'a' && text[index] <= 'z') ||
            (text[index] >= 'A' && text[index] <= 'Z'))) {
        ++index;
    }
    return index > 0 && index < text.size() && (text[index] == ':' || text[index] == '(');
}

// Splits `kb:enter, kb:numpadEnter, kb:space` into its three gestures.
//
// THE COMMA IS ALSO A KEY, which is why this is not a one-line split: upstream
// binds `movePastEndOfContainer = kb:,` and `moveToStartOfContainer =
// kb:shift+,`. Splitting on every comma turned those two into `kb:` and
// `kb:shift+` -- identifiers no keyboard can produce -- so both commands were
// unreachable, silently, in the generated map that every user gets
// (claude/FAZA9.md N102). A comma is a separator only when another gesture
// follows it.
std::vector<std::string_view> splitComma(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t comma = text.find(',', start);
        while (comma != std::string_view::npos &&
               !startsAGesture(text.substr(comma + 1))) {
            comma = text.find(',', comma + 1);
        }
        if (comma == std::string_view::npos) {
            parts.push_back(trim(text.substr(start)));
            break;
        }
        parts.push_back(trim(text.substr(start, comma - start)));
        start = comma + 1;
    }
    return parts;
}

}  // namespace

void GestureMap::clear() {
    map_.clear();
    lastUpdateContainedError_ = false;
}

bool GestureMap::load(const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open " + path.string();
        }
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    fileName_ = path;
    if (name_.empty()) {
        name_ = path.filename().string();
    }
    return loadFromString(contents.str(), error);
}

bool GestureMap::loadFromString(std::string_view text, std::string* error) {
    lastUpdateContainedError_ = false;
    // NVDA's own locale maps are shipped with a UTF-8 BOM (de/gestures.ini
    // has one), so a map that starts with one is not an error.
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) {
        text.remove_prefix(3);
    }
    std::string location;
    std::size_t lineNumber = 0;
    std::size_t bindings = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        std::string_view line =
            trim(text.substr(start, newline == std::string_view::npos ? std::string_view::npos
                                                                      : newline - start));
        start = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        ++lineNumber;

        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            if (close == std::string_view::npos) {
                ALUS_WARNING("gesture map {}: line {}: unterminated section header", name_,
                             lineNumber);
                lastUpdateContainedError_ = true;
                continue;
            }
            location = std::string(trim(line.substr(1, close - 1)));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            ALUS_WARNING("gesture map {}: line {}: no '=' in '{}'", name_, lineNumber,
                         std::string(line));
            lastUpdateContainedError_ = true;
            continue;
        }
        if (location.empty()) {
            ALUS_WARNING("gesture map {}: line {}: entry before any section", name_, lineNumber);
            lastUpdateContainedError_ = true;
            continue;
        }
        const std::string_view scriptName = trim(line.substr(0, equals));
        if (scriptName.empty()) {
            lastUpdateContainedError_ = true;
            continue;
        }
        const bool unbind = scriptName == kUnbindScriptName;
        for (const std::string_view gesture : splitComma(line.substr(equals + 1))) {
            if (gesture.empty()) {
                continue;
            }
            if (gesture.find(':') == std::string_view::npos) {
                // Every gesture carries its source; without one there is no
                // way to know which input device it belongs to.
                ALUS_WARNING("gesture map {}: line {}: invalid gesture '{}'", name_, lineNumber,
                             std::string(gesture));
                lastUpdateContainedError_ = true;
                continue;
            }
            add(gesture, location, unbind ? std::string_view() : scriptName);
            ++bindings;
        }
    }
    ALUS_DEBUG("gesture map {}: {} bindings in {} locations", name_, bindings, map_.size());
    if (error != nullptr && lastUpdateContainedError_) {
        *error = "the gesture map contained errors; see the log";
    }
    return true;
}

void GestureMap::add(std::string_view gesture, std::string_view location,
                     std::string_view script, bool replace) {
    const std::string normalized = normalizeGestureIdentifier(gesture);
    std::vector<Entry>& entries = map_[normalized];
    if (replace) {
        entries.clear();
    }
    // An EXACT duplicate is dropped. Found by phase 9's gesture dialog, which
    // is the first caller to add a binding a user typed rather than one a file
    // or the extractor produced (FAZA9.md N78).
    //
    // Why it matters, and why it is fixed here rather than in the dialog: the
    // same binding written twice is one binding. Two identical entries show up
    // as two identical rows in the gesture dialog, and `remove` takes out only
    // the first -- so the user removes a key, hears nothing change, removes it
    // again and it works. A hand-edited gestures.ini with a repeated line
    // already did this before there was a dialog; dispatch never noticed
    // because finding the same script twice runs it once.
    const auto duplicate =
        std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
            return entry.location == location && entry.script == script;
        });
    if (duplicate != entries.end()) {
        return;
    }
    entries.push_back(Entry{std::string(location), std::string(script)});
}

bool GestureMap::remove(std::string_view gesture, std::string_view location,
                        std::string_view script) {
    const std::string normalized = normalizeGestureIdentifier(gesture);
    const auto it = map_.find(normalized);
    if (it == map_.end()) {
        return false;
    }
    std::vector<Entry>& entries = it->second;
    const auto match = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
        return entry.location == location && entry.script == script;
    });
    if (match == entries.end()) {
        return false;
    }
    entries.erase(match);
    if (entries.empty()) {
        map_.erase(it);
    }
    return true;
}

const std::vector<GestureMap::Entry>* GestureMap::entriesForNormalized(
    std::string_view normalizedGesture) const {
    const auto it = map_.find(normalizedGesture);
    return it == map_.end() ? nullptr : &it->second;
}

std::vector<std::pair<std::string, GestureMap::Entry>> GestureMap::allEntries() const {
    std::vector<std::pair<std::string, Entry>> result;
    for (const auto& [gesture, entries] : map_) {
        for (const Entry& entry : entries) {
            result.emplace_back(gesture, entry);
        }
    }
    return result;
}

std::string GestureMap::serialize() const {
    // location -> script -> gestures, so that the file groups the way a human
    // reads it and the way NVDA writes it.
    std::map<std::string, std::map<std::string, std::vector<std::string>>> sections;
    for (const auto& [gesture, entries] : map_) {
        for (const Entry& entry : entries) {
            const std::string script =
                entry.script.empty() ? std::string(kUnbindScriptName) : entry.script;
            sections[entry.location][script].push_back(gesture);
        }
    }
    std::ostringstream out;
    out << "; SPDX-License-Identifier: GPL-2.0-or-later\n"
        << "; ALUS gesture map. One section per scriptable class, one entry\n"
        << "; per script, value is a comma separated gesture list.\n\n";
    for (const auto& [location, scripts] : sections) {
        out << '[' << location << "]\n";
        for (const auto& [script, gestures] : scripts) {
            out << '\t' << script << " = ";
            for (std::size_t i = 0; i < gestures.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << gestures[i];
            }
            out << '\n';
        }
        out << '\n';
    }
    return out.str();
}

bool GestureMap::save(const std::filesystem::path& path, std::string* error) const {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot write " + path.string();
        }
        return false;
    }
    file << serialize();
    if (!file) {
        if (error != nullptr) {
            *error = "write failed: " + path.string();
        }
        return false;
    }
    return true;
}

}  // namespace alus::input
