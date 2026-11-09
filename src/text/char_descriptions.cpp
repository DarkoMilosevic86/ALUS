// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/text/char_descriptions.hpp.

#include "alus/text/char_descriptions.hpp"

#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"

#include <glib.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace alus::text {
namespace {

std::string_view trimRight(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    return line;
}

bool isBlank(std::string_view line) {
    return std::all_of(line.begin(), line.end(),
                       [](char character) { return std::isspace(static_cast<unsigned char>(character)) != 0; });
}

std::string lowerCase(std::string_view text) {
    gchar* lowered = g_utf8_strdown(text.data(), static_cast<gssize>(text.size()));
    if (lowered == nullptr) {
        return std::string(text);
    }
    std::string result(lowered);
    g_free(lowered);
    return result;
}

// "sr_RS" -> {"sr_RS", "sr", "en"}, the same ladder SymbolRegistry climbs.
std::vector<std::string> localeCandidates(const std::string& locale) {
    std::vector<std::string> candidates;
    if (!locale.empty()) {
        candidates.push_back(locale);
        const std::size_t underscore = locale.find('_');
        if (underscore != std::string::npos) {
            candidates.push_back(locale.substr(0, underscore));
        }
    }
    if (std::find(candidates.begin(), candidates.end(), "en") == candidates.end()) {
        candidates.emplace_back("en");
    }
    return candidates;
}

}  // namespace

bool languageHasConjuncts(std::string_view language) {
    const std::size_t underscore = language.find('_');
    const std::string_view base =
        underscore == std::string_view::npos ? language : language.substr(0, underscore);
    return std::find(kLanguagesWithConjuncts.begin(), kLanguagesWithConjuncts.end(), base) !=
           kLanguagesWithConjuncts.end();
}

void CharacterDescriptions::loadFromString(std::string_view content) {
    std::istringstream stream{std::string(content)};
    std::string rawLine;
    while (std::getline(stream, rawLine)) {
        std::string_view line = trimRight(rawLine);
        if (line.empty() || isBlank(line) || line.front() == '#') {
            continue;
        }
        const std::size_t firstTab = line.find('\t');
        if (firstTab == std::string_view::npos) {
            ALUS_DEBUG_WARNING("character description line without a tab: '{}'", line);
            continue;
        }
        const std::string character(line.substr(0, firstTab));
        std::vector<std::string> descriptions;
        std::size_t start = firstTab + 1;
        while (start <= line.size()) {
            const std::size_t tab = line.find('\t', start);
            const std::string_view part =
                line.substr(start, tab == std::string_view::npos ? std::string_view::npos
                                                                 : tab - start);
            if (!part.empty()) {
                descriptions.emplace_back(part);
            }
            if (tab == std::string_view::npos) {
                break;
            }
            start = tab + 1;
        }
        if (descriptions.empty()) {
            continue;
        }
        entries_[character] = std::move(descriptions);
    }
}

bool CharacterDescriptions::load(const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open " + path.string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    // NVDA opens these files as utf_8_sig: the byte order mark is part of the
    // format, and leaving it in makes the first character unlookupable.
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }
    loadFromString(content);
    return true;
}

const std::vector<std::string>* CharacterDescriptions::find(
    const std::string& character) const {
    const auto it = entries_.find(character);
    return it == entries_.end() ? nullptr : &it->second;
}

CharacterDescriptionRegistry::CharacterDescriptionRegistry(std::filesystem::path dataDir)
    : dataDir_(std::move(dataDir)) {}

CharacterDescriptionRegistry::~CharacterDescriptionRegistry() = default;

void CharacterDescriptionRegistry::setDataDirectory(std::filesystem::path dataDir) {
    dataDir_ = std::move(dataDir);
    invalidateAll();
}

void CharacterDescriptionRegistry::invalidateAll() {
    entries_.clear();
}

CharacterDescriptions* CharacterDescriptionRegistry::descriptionsFor(
    const std::string& language) {
    const auto existing = entries_.find(language);
    if (existing != entries_.end()) {
        return existing->second.get();
    }
    auto descriptions = std::make_unique<CharacterDescriptions>();
    bool loaded = false;
    if (!dataDir_.empty()) {
        const std::filesystem::path path = dataDir_ / "characters" / (language + ".dic");
        std::error_code code;
        if (std::filesystem::is_regular_file(path, code)) {
            std::string error;
            loaded = descriptions->load(path, &error);
            if (!loaded) {
                ALUS_WARNING("character descriptions for '{}': {}", language, error);
            }
        }
    }
    if (!loaded && language == "en") {
        // The floor: English descriptions are compiled in, so that a broken
        // installation can still tell "b" from "d".
        descriptions->loadFromString(builtinEnglishCharacterDescriptions());
        loaded = true;
    }
    if (!loaded) {
        entries_.emplace(language, nullptr);
        return nullptr;
    }
    CharacterDescriptions* result = descriptions.get();
    entries_.emplace(language, std::move(descriptions));
    return result;
}

const std::vector<std::string>* CharacterDescriptionRegistry::find(
    const std::string& locale, const std::string& character) {
    for (const std::string& candidate : localeCandidates(locale)) {
        CharacterDescriptions* descriptions = descriptionsFor(candidate);
        if (descriptions == nullptr) {
            continue;
        }
        if (const std::vector<std::string>* found = descriptions->find(character)) {
            return found;
        }
        // NVDA's LocaleDataMap stops at the first locale that HAS a file, and
        // only falls back to English when none was found at all. Continuing
        // here instead is deliberate and better: a Serbian file that describes
        // the Serbian letters still lets an English letter in the same text be
        // described, rather than being silent about it.
    }
    return nullptr;
}

const std::vector<std::string>* CharacterDescriptionRegistry::findRelaxed(
    const std::string& locale, const std::string& character) {
    if (const std::vector<std::string>* found = find(locale, character)) {
        return found;
    }
    const std::string lowered = lowerCase(character);
    if (lowered != character) {
        // #5375: the English file describes only lower-case letters, so an
        // upper-case one has to be looked up folded.
        return find(locale, lowered);
    }
    return nullptr;
}

std::vector<DescribedCharacter> describeText(CharacterDescriptionRegistry& registry,
                                            const std::string& locale,
                                            std::string_view text) {
    // NVDA's `getCharDescListFromText`, kept whole: the LONGEST prefix with a
    // description wins, so a conjunct of three codepoints is described once
    // instead of three times.
    std::vector<DescribedCharacter> result;
    std::string remaining(text);
    while (!remaining.empty()) {
        const OffsetConverter converter(remaining);
        std::size_t characters = converter.codepointLength();
        bool matched = false;
        while (characters > 0) {
            const std::string candidate = remaining.substr(0, converter.toByte(characters));
            const std::vector<std::string>* descriptions =
                registry.findRelaxed(locale, candidate);
            if (descriptions != nullptr || characters == 1) {
                DescribedCharacter entry;
                entry.character = candidate;
                if (descriptions != nullptr) {
                    entry.descriptions = *descriptions;
                }
                result.push_back(std::move(entry));
                remaining.erase(0, candidate.size());
                matched = true;
                break;
            }
            --characters;
        }
        if (!matched) {
            break;
        }
    }
    return result;
}

}  // namespace alus::text
