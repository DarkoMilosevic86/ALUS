// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Character descriptions: "a, Avala".
//
// Port of the `CharacterDescriptions` half of NVDA's `characterProcessing.py`
// (architecture.md §11.1 covers the file; phase 2 ported the symbol half
// into text/symbols.hpp). This is what the THIRD press of a review command
// speaks, and what `alus+numpad2` three times gives: not the character, but a
// word that contains it, so "b" and "d" can be told apart over a telephone of
// a synthesizer.
//
// THE DATA IS NVDA'S DATA, unchanged, because a user who has learned "Avala"
// for `a` in Serbian must hear the same word here. The format is NVDA's too:
//
//     character<TAB>description[<TAB>another description...]
//
// A character may have SEVERAL descriptions, and both of NVDA's rules about
// that are kept: spelling a single character joins them with an ideographic
// comma (for the languages where that is the convention), and spelling a whole
// word uses only the first, or every letter would take a sentence.
//
// CONJUNCT SCRIPTS. In Hindi, Bengali, Tamil and the other scripts in
// `kLanguagesWithConjuncts`, a description belongs to a SEQUENCE of codepoints
// and not to one of them, so the lookup tries the longest prefix first
// (NVDA's `getCharDescListFromText`). Without that, spelling in those
// languages produces nonsense.
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::text {

// NVDA: `LANGS_WITH_CONJUNCT_CHARS`.
inline constexpr std::array<std::string_view, 13> kLanguagesWithConjuncts{
    "hi", "as", "bn", "gu", "kn", "kok", "ml", "mni", "mr", "pa", "te", "ur", "ta"};

bool languageHasConjuncts(std::string_view language);

// One locale's descriptions.
class CharacterDescriptions {
public:
    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    void loadFromString(std::string_view content);

    // Null when the character has none.
    const std::vector<std::string>* find(const std::string& character) const;

    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

private:
    std::map<std::string, std::vector<std::string>, std::less<>> entries_;
};

// Locale lookup with the same fallback as SymbolRegistry: "sr_RS" -> "sr" ->
// "en". A character with no description anywhere is normal (most punctuation
// has none), and the caller speaks the character itself instead.
class CharacterDescriptionRegistry {
public:
    CharacterDescriptionRegistry(std::filesystem::path dataDir = {});
    ~CharacterDescriptionRegistry();

    CharacterDescriptionRegistry(const CharacterDescriptionRegistry&) = delete;
    CharacterDescriptionRegistry& operator=(const CharacterDescriptionRegistry&) = delete;

    void setDataDirectory(std::filesystem::path dataDir);

    const std::vector<std::string>* find(const std::string& locale,
                                         const std::string& character);
    // NVDA looks the character up in lower case as well, because the English
    // file describes only lower-case letters (#5375).
    const std::vector<std::string>* findRelaxed(const std::string& locale,
                                                const std::string& character);

    void invalidateAll();

private:
    CharacterDescriptions* descriptionsFor(const std::string& language);

    std::filesystem::path dataDir_;
    std::map<std::string, std::unique_ptr<CharacterDescriptions>, std::less<>> entries_;
};

// One character of text with its description, as
// `getCharDescListFromText` returns them: for a conjunct script the character
// may be several codepoints.
struct DescribedCharacter {
    std::string character;
    std::vector<std::string> descriptions;
};

// NVDA: `getCharDescListFromText`. Splits text into the longest pieces that
// have a description, which is what makes spelling work in conjunct scripts.
std::vector<DescribedCharacter> describeText(CharacterDescriptionRegistry& registry,
                                             const std::string& locale,
                                             std::string_view text);

// The built-in English descriptions, compiled in for the same reason the
// English symbol dictionary is (text/symbols.hpp): a broken installation must
// still be able to tell "b" from "d".
std::string_view builtinEnglishCharacterDescriptions();

}  // namespace alus::text
