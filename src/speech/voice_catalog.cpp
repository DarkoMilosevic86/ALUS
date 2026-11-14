// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/voice_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_set>

namespace alus::speech {
namespace {

// "none" is what speech-dispatcher writes for "no variant".
bool isNoVariant(std::string_view variant) { return variant.empty() || variant == "none"; }

// The primary language subtag, lower-cased: "sr-RS", "sr_RS.UTF-8" -> "sr".
std::string primaryLanguage(std::string_view language) {
    std::string out;
    for (const char c : language) {
        if (c == '-' || c == '_' || c == '.' || c == '@') {
            break;
        }
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string lowered(std::string_view text) {
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

VoiceCatalog VoiceCatalog::build(const std::vector<VoiceInfo>& all,
                                 std::string_view preferredLanguage) {
    VoiceCatalog catalog;
    std::unordered_set<std::string> baseIds;
    for (const VoiceInfo& voice : all) {
        if (isNoVariant(voice.variant)) {
            baseIds.insert(voice.id);
        }
    }
    // A voice is "base + variant" only when the base exists and the id is
    // spelled exactly that way; anything else stays a voice of its own.
    std::set<std::string> variants;
    std::vector<VoiceInfo> voices;
    for (const VoiceInfo& voice : all) {
        if (!isNoVariant(voice.variant)) {
            const std::string suffix = "+" + voice.variant;
            if (voice.id.size() > suffix.size() &&
                voice.id.compare(voice.id.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                baseIds.count(voice.id.substr(0, voice.id.size() - suffix.size())) != 0) {
                variants.insert(voice.variant);
                continue;
            }
        }
        voices.push_back(voice);
    }
    catalog.variants_.assign(variants.begin(), variants.end());
    std::sort(catalog.variants_.begin(), catalog.variants_.end(),
              [](const std::string& a, const std::string& b) { return lowered(a) < lowered(b); });

    const std::string preferred = primaryLanguage(preferredLanguage);
    if (!preferred.empty()) {
        std::stable_sort(voices.begin(), voices.end(),
                         [&preferred](const VoiceInfo& a, const VoiceInfo& b) {
                             const bool aFirst = primaryLanguage(a.language) == preferred;
                             const bool bFirst = primaryLanguage(b.language) == preferred;
                             if (aFirst != bFirst) {
                                 return aFirst;
                             }
                             return lowered(a.name.empty() ? a.id : a.name) <
                                    lowered(b.name.empty() ? b.id : b.name);
                         });
    }
    catalog.voices_ = std::move(voices);
    return catalog;
}

std::pair<std::string, std::string> VoiceCatalog::split(std::string_view id) const {
    const std::size_t plus = id.rfind('+');
    if (plus != std::string_view::npos && plus > 0) {
        const std::string_view variant = id.substr(plus + 1);
        if (indexOfVariant(variant) >= 0) {
            return {std::string{id.substr(0, plus)}, std::string{variant}};
        }
    }
    return {std::string{id}, std::string{}};
}

std::string VoiceCatalog::join(std::string_view base, std::string_view variant) const {
    if (variant.empty() || base.empty()) {
        return std::string{base};
    }
    return std::string{base} + "+" + std::string{variant};
}

int VoiceCatalog::indexOfVoice(std::string_view baseId) const {
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (voices_[i].id == baseId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int VoiceCatalog::indexOfVariant(std::string_view variant) const {
    for (std::size_t i = 0; i < variants_.size(); ++i) {
        if (variants_[i] == variant) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string VoiceCatalog::label(const VoiceInfo& voice) {
    const std::string name = voice.name.empty() ? voice.id : voice.name;
    if (voice.language.empty() || lowered(name).find(lowered(voice.language)) != std::string::npos) {
        return name;
    }
    return name + " (" + voice.language + ")";
}

}  // namespace alus::speech
