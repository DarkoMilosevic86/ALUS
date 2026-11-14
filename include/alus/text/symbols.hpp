// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Symbol pronunciation.
//
// Port of NVDA's `characterProcessing.py` (the symbol half: SpeechSymbol,
// SpeechSymbols, SpeechSymbolProcessor). See architecture.md §11.1. The file
// format is NVDA's `symbols.dic`, unchanged, because the locale data IS NVDA's
// locale data and ALUS must be able to read it as it stands.
//
//   complexSymbols:
//   identifier<TAB>regular expression
//
//   symbols:
//   identifier<TAB>replacement<TAB>level<TAB>preserve<TAB># display name
//
// The source order (§11.1) is a security property as much as a convenience:
//
//   1. cldr      <data>/symbols/cldr-<lang>.dic     emoji names; optional
//   2. builtin   <data>/symbols/<lang>.dic          MANDATORY, hidden from the
//                                                   GUI, THE ONLY SOURCE THAT
//                                                   MAY DEFINE COMPLEX (REGEX)
//                                                   SYMBOLS
//   3. add-on    <addon>/symbols/<lang>-<name>.dic  phase 8
//   4. user      ~/.config/alus/symbols-<lang>.dic  no regex
//
// "Only builtin may use regex" is deliberate and must be preserved: a user
// dictionary is edited through the GUI and synchronised between machines, and a
// regex there is both a hang risk and a way to rewrite arbitrary speech.
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::text {

class Regex;

// NVDA's values, with the gaps left in on purpose: they are room for future
// levels, and they appear in configuration files.
enum class SymbolLevel : int {
    None = 0,
    Some = 100,
    Most = 200,
    All = 300,
    Char = 1000,
    Unchanged = -1,
};

// What the user cycles through with nvda+p.
inline constexpr std::array<SymbolLevel, 4> kConfigurableSymbolLevels{
    SymbolLevel::None, SymbolLevel::Some, SymbolLevel::Most, SymbolLevel::All};

const char* symbolLevelName(SymbolLevel level);
std::optional<SymbolLevel> parseSymbolLevel(std::string_view name);
// The nvda+p cycle: none → some → most → all → none.
SymbolLevel nextSymbolLevel(SymbolLevel level);

// Whether the symbol itself is also sent to the synthesizer, as well as its
// replacement.
enum class SymbolPreserve {
    Never = 0,
    Always = 1,
    // Only when the symbol's own level is above the current level — i.e. when
    // the replacement is NOT being spoken.
    NoRep = 2,
};

const char* symbolPreserveName(SymbolPreserve preserve);
std::optional<SymbolPreserve> parseSymbolPreserve(std::string_view name);

struct SpeechSymbol {
    std::string identifier;
    // Complex symbols only: the regular expression that recognises it.
    std::string pattern;
    // Unset means "inherit from a lower-priority source"; a symbol that is
    // still unset after merging is useless and is dropped with a warning.
    std::optional<std::string> replacement;
    std::optional<SymbolLevel> level;
    std::optional<SymbolPreserve> preserve;
    std::optional<std::string> displayName;
};

// One `.dic` file. Raw data only: no inheritance, no text processing.
class SymbolDictionary {
public:
    SymbolDictionary() = default;
    explicit SymbolDictionary(std::filesystem::path fileName);

    // `allowComplexSymbols` false silently ignores a complexSymbols: section —
    // that is what makes a user dictionary unable to carry a regex.
    bool load(const std::filesystem::path& path, bool allowComplexSymbols = true,
              std::string* error = nullptr);
    // For tests and for the built-in fallback data.
    void loadFromString(std::string_view content, bool allowComplexSymbols = true);
    bool save(const std::filesystem::path& path = {}, std::string* error = nullptr) const;

    // Insertion order, which is the order of the file and therefore the order
    // the user wrote.
    const std::vector<SpeechSymbol>& symbols() const noexcept { return symbols_; }
    const std::vector<std::pair<std::string, std::string>>& complexSymbols() const noexcept {
        return complexSymbols_;
    }

    const SpeechSymbol* find(const std::string& identifier) const;
    bool contains(const std::string& identifier) const { return find(identifier) != nullptr; }
    // Adds or replaces, keeping the original position when replacing.
    void set(SpeechSymbol symbol);
    bool erase(const std::string& identifier);
    void setComplexSymbol(std::string identifier, std::string pattern);

    const std::filesystem::path& fileName() const noexcept { return fileName_; }
    bool empty() const noexcept { return symbols_.empty() && complexSymbols_.empty(); }

private:
    void parseLine(std::string_view line, bool inComplexSection);

    std::filesystem::path fileName_;
    std::vector<SpeechSymbol> symbols_;
    std::map<std::string, std::size_t> index_;
    std::vector<std::pair<std::string, std::string>> complexSymbols_;
};

// The merged symbol data for one locale, and the one regular expression built
// from it.
class SymbolProcessor {
public:
    SymbolProcessor();
    ~SymbolProcessor();

    SymbolProcessor(const SymbolProcessor&) = delete;
    SymbolProcessor& operator=(const SymbolProcessor&) = delete;
    // Movable: a processor is built and then handed to its owner.
    SymbolProcessor(SymbolProcessor&&) noexcept;
    SymbolProcessor& operator=(SymbolProcessor&&) noexcept;

    // `sources` in priority order, highest first (user, add-ons, builtin,
    // cldr). `userSourceCount` is how many of the leading sources are the
    // user's, which is what isBuiltin() answers against. The dictionaries must
    // outlive the processor.
    bool build(std::vector<const SymbolDictionary*> sources, std::size_t userSourceCount = 1,
               std::string* error = nullptr);

    std::string processText(const std::string& text, SymbolLevel level) const;
    // The replacement for exactly one symbol, or the symbol itself.
    std::string processSymbol(const std::string& symbol) const;

    const SpeechSymbol* computed(const std::string& identifier) const;
    std::vector<SpeechSymbol> computedSymbols() const;
    bool isBuiltin(const std::string& identifier) const;

    bool valid() const noexcept { return regex_ != nullptr; }

private:
    std::string replacementFor(const SpeechSymbol& symbol, const std::string& matchedText,
                               SymbolLevel level, const std::string& replacement) const;

    std::vector<const SymbolDictionary*> sources_;
    std::size_t userSourceCount_ = 1;
    std::map<std::string, SpeechSymbol> computed_;
    std::vector<std::string> order_;              // insertion order of computed_
    std::vector<std::string> complexIdentifiers_;  // index → identifier, for group cN
    std::unique_ptr<Regex> regex_;
};

// Locale lookup with fallback, and the file layout from §11.1.
class SymbolRegistry {
public:
    // `dataDir` holds the built-in dictionaries (<prefix>/share/alus/symbols or
    // the source tree's `data/symbols`); `userDir` is ~/.config/alus.
    SymbolRegistry(std::filesystem::path dataDir = {}, std::filesystem::path userDir = {});
    ~SymbolRegistry();

    SymbolRegistry(const SymbolRegistry&) = delete;
    SymbolRegistry& operator=(const SymbolRegistry&) = delete;

    void setDataDirectory(std::filesystem::path dataDir);
    void setUserDirectory(std::filesystem::path userDir);
    // Add-on dictionaries (phase 8). Searched after the user's, before builtin.
    void addSourceDirectory(std::filesystem::path directory);

    // "sr_RS" falls back to "sr", then to "en", exactly as NVDA's
    // LocaleDataMap does. Null only when even English is missing, which means
    // the installation is broken.
    SymbolProcessor* processorFor(const std::string& locale);

    std::string processText(const std::string& locale, const std::string& text,
                            SymbolLevel level);
    std::string processSymbol(const std::string& locale, const std::string& symbol);

    // NVDA: clearSpeechSymbols(). Called when the configuration changes.
    void invalidateAll();

private:
    struct Entry;
    Entry* entryFor(const std::string& language);
    static std::vector<std::string> localeCandidates(const std::string& locale);

    std::filesystem::path dataDir_;
    std::filesystem::path userDir_;
    std::vector<std::filesystem::path> extraDirs_;
    std::map<std::string, std::unique_ptr<Entry>> entries_;
};

// The built-in English dictionary, compiled in. It is the floor under the whole
// symbol system: if the installed data is missing, ALUS still pronounces
// punctuation instead of saying nothing.
std::string_view builtinEnglishSymbols();

}  // namespace alus::text
