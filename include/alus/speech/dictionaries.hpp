// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Speech dictionaries: user-defined pronunciation.
//
// Port of NVDA's `speechDictHandler/`. See 01-ARHITEKTURA.md §11.1. Four
// dictionaries are applied IN THIS ORDER, and the order is user-visible:
//
//   builtin → default → voice → temp
//
// `builtin` ships with ALUS, `default` is the user's own, `voice` belongs to
// the current synthesizer voice, `temp` is scratch (the "temporary dictionary"
// dialog). Dictionary processing happens BEFORE symbol processing, so a
// dictionary entry can introduce punctuation that the symbol level then speaks
// or swallows.
//
// THE TRAP THIS PORT EXISTS TO AVOID (§11.1): the four entry types with word
// boundaries must use a `\w` that INCLUDES COMBINING MARKS. Python's standard
// `re` excludes them, which breaks every language with diacritics — Serbian
// included — and NVDA had to move those entry types to the `regex` module with
// VERSION1 semantics. Here the word character class is written out explicitly
// (alus::text::kWordCharacterClass) rather than trusting any engine's `\w`.
#pragma once

#include "alus/text/regex.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace alus::speech {

// NVDA's EntryType values, which appear as the fourth column of a .dic file.
enum class EntryType : int {
    // Matches anywhere in the text.
    Anywhere = 0,
    Regexp = 1,
    // Word boundaries on both sides.
    Word = 2,
    // Preceded or followed by a word character.
    PartOfWord = 3,
    // Word boundary at the start, word character at the end.
    StartOfWord = 4,
    // Word character at the start, word boundary at the end.
    EndOfWord = 5,
    // Unix shell-style wildcards.
    Unix = 6,
};

const char* entryTypeName(EntryType type);

class DictEntry {
public:
    DictEntry() = default;
    DictEntry(std::string pattern, std::string replacement, std::string comment = {},
              bool caseSensitive = true, EntryType type = EntryType::Anywhere);

    // Compiles the pattern. False and `error` filled for a bad user pattern —
    // which is a thing that happens and must be reported, not fatal.
    bool compile(std::string* error = nullptr);
    bool valid() const;

    // The text with this entry applied. Unchanged if the entry never compiled.
    std::string sub(const std::string& text) const;

    const std::string& pattern() const noexcept { return pattern_; }
    const std::string& replacement() const noexcept { return replacement_; }
    const std::string& comment() const noexcept { return comment_; }
    bool caseSensitive() const noexcept { return caseSensitive_; }
    EntryType type() const noexcept { return type_; }
    // What the pattern was turned into. For diagnostics and tests.
    const std::string& compiledPattern() const noexcept { return compiled_; }

private:
    std::string pattern_;
    std::string replacement_;
    std::string comment_;
    bool caseSensitive_ = true;
    EntryType type_ = EntryType::Anywhere;
    std::string compiled_;
    std::shared_ptr<text::Regex> regex_;
};

// One dictionary file.
class SpeechDict {
public:
    SpeechDict() = default;

    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    void loadFromString(std::string_view content);
    bool save(const std::filesystem::path& path = {}, std::string* error = nullptr) const;

    void add(DictEntry entry);
    void clear();
    bool eraseAt(std::size_t position);
    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }
    const std::vector<DictEntry>& entries() const noexcept { return entries_; }

    // Applies every entry, in file order. An entry that fails at run time is
    // dropped and logged — NVDA's behaviour, and the only sane one: a broken
    // entry must not silence the screen reader.
    std::string sub(std::string text);

    const std::filesystem::path& fileName() const noexcept { return fileName_; }

private:
    std::filesystem::path fileName_;
    std::vector<DictEntry> entries_;
};

enum class DictionaryType { Builtin, Default, Voice, Temp };

const char* dictionaryTypeName(DictionaryType type);

// The registry, in application order.
class Dictionaries {
public:
    // `dataDir` holds the built-in dictionary (<prefix>/share/alus/dictionaries
    // or the source tree's `data/dictionaries`); `userDir` is ~/.config/alus.
    Dictionaries(std::filesystem::path dataDir = {}, std::filesystem::path userDir = {});

    void setDataDirectory(std::filesystem::path dataDir) { dataDir_ = std::move(dataDir); }
    void setUserDirectory(std::filesystem::path userDir) { userDir_ = std::move(userDir); }

    // Loads builtin and default. The voice dictionary needs the synthesizer's
    // name and voice, so it is loaded separately.
    void load();
    // NVDA: loadVoiceDict(). The file name is
    // <userDir>/dictionaries/<synth>-<voice>.dic, with the voice name made
    // safe for a file name.
    void loadVoiceDict(const std::string& synthName, const std::string& voiceName);

    SpeechDict& dictionary(DictionaryType type) { return dictionaries_[type]; }
    const SpeechDict& dictionary(DictionaryType type) const;

    void setEnabled(DictionaryType type, bool enabled);
    bool isEnabled(DictionaryType type) const;

    // NVDA: globalVars.speechDictionaryProcessing. Turned off while reading
    // back what the user is typing into a dictionary entry, so the dialog does
    // not rewrite its own input.
    void setProcessingEnabled(bool enabled) { processingEnabled_ = enabled; }
    bool processingEnabled() const noexcept { return processingEnabled_; }

    std::string processText(std::string text);

private:
    std::filesystem::path dataDir_;
    std::filesystem::path userDir_;
    std::map<DictionaryType, SpeechDict> dictionaries_;
    std::map<DictionaryType, bool> enabled_;
    bool processingEnabled_ = true;
};

}  // namespace alus::speech
