// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/speech/dictionaries.hpp"

#include "alus/core/log.hpp"

#include <glib.h>

#include <algorithm>
#include <format>
#include <utility>

namespace alus::speech {

namespace {

std::vector<std::string_view> splitLines(std::string_view content) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t position = content.find('\n', start);
        if (position == std::string_view::npos) {
            lines.push_back(content.substr(start));
            return lines;
        }
        lines.push_back(content.substr(start, position - start));
        start = position + 1;
    }
}

std::string_view trimRight(std::string_view text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

bool isBlank(std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       [](unsigned char c) { return std::isspace(c) != 0; });
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return text;
    }
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

// Unix shell wildcards to a regular expression. Python's fnmatch.translate
// with its trailing \Z dropped, which is what NVDA does: the pattern must be
// able to match inside a longer string.
std::string globToRegex(std::string_view glob) {
    std::string out;
    for (std::size_t i = 0; i < glob.size(); ++i) {
        const char character = glob[i];
        switch (character) {
            case '*':
                out += ".*";
                break;
            case '?':
                out += ".";
                break;
            case '[': {
                const std::size_t close = glob.find(']', i + 1);
                if (close == std::string_view::npos) {
                    out += "\\[";
                    break;
                }
                std::string_view set = glob.substr(i + 1, close - i - 1);
                out += '[';
                if (!set.empty() && (set.front() == '!' || set.front() == '^')) {
                    out += '^';
                    set.remove_prefix(1);
                }
                for (const char inner : set) {
                    if (inner == '\\' || inner == ']' || inner == '^') {
                        out += '\\';
                    }
                    out += inner;
                }
                out += ']';
                i = close;
                break;
            }
            default:
                out += text::Regex::escape(std::string_view{&character, 1});
                break;
        }
    }
    return out;
}

constexpr std::string_view kWord = text::kWordCharacterClass;

}  // namespace

const char* entryTypeName(EntryType type) {
    switch (type) {
        case EntryType::Anywhere:
            return "anywhere";
        case EntryType::Regexp:
            return "regexp";
        case EntryType::Word:
            return "word";
        case EntryType::PartOfWord:
            return "part of word";
        case EntryType::StartOfWord:
            return "start of word";
        case EntryType::EndOfWord:
            return "end of word";
        case EntryType::Unix:
            return "unix wildcards";
    }
    return "?";
}

const char* dictionaryTypeName(DictionaryType type) {
    switch (type) {
        case DictionaryType::Builtin:
            return "builtin";
        case DictionaryType::Default:
            return "default";
        case DictionaryType::Voice:
            return "voice";
        case DictionaryType::Temp:
            return "temp";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// DictEntry
// ---------------------------------------------------------------------------

DictEntry::DictEntry(std::string pattern, std::string replacement, std::string comment,
                     bool caseSensitive, EntryType type)
    : pattern_(std::move(pattern)),
      replacement_(std::move(replacement)),
      comment_(std::move(comment)),
      caseSensitive_(caseSensitive),
      type_(type) {}

bool DictEntry::compile(std::string* error) {
    const std::string escaped = text::Regex::escape(pattern_);
    switch (type_) {
        case EntryType::Regexp:
            compiled_ = pattern_;
            break;
        case EntryType::Word:
            // NVDA writes \b...\b. Written out instead, because the boundary
            // has to count combining marks as part of a word (§11.1).
            compiled_ = std::format("(?<!{0}){1}(?!{0})", kWord, escaped);
            break;
        case EntryType::PartOfWord:
            compiled_ = std::format("(?<={0}){1}|{1}(?={0})", kWord, escaped);
            break;
        case EntryType::StartOfWord:
            compiled_ = std::format("(?<!{0}){1}(?={0})", kWord, escaped);
            break;
        case EntryType::EndOfWord:
            compiled_ = std::format("(?<={0}){1}(?!{0})", kWord, escaped);
            break;
        case EntryType::Unix:
            compiled_ = globToRegex(pattern_);
            break;
        case EntryType::Anywhere:
        default:
            type_ = EntryType::Anywhere;  // keep the stored value sane
            compiled_ = escaped;
            break;
    }

    auto regex = std::make_shared<text::Regex>();
    text::RegexOption options = text::RegexOption::None;
    if (!caseSensitive_) {
        options = options | text::RegexOption::CaseInsensitive;
    }
    std::string compileError;
    if (!regex->compile(compiled_, options, &compileError)) {
        if (error != nullptr) {
            *error = compileError;
        }
        regex_.reset();
        return false;
    }
    regex_ = std::move(regex);
    return true;
}

bool DictEntry::valid() const { return regex_ != nullptr && regex_->valid(); }

std::string DictEntry::sub(const std::string& text) const {
    if (!valid()) {
        return text;
    }
    // Only a regular expression entry may use group references; for every other
    // type a backslash in the replacement is a literal backslash.
    const std::string replacement =
        type_ == EntryType::Regexp ? replacement_ : replaceAll(replacement_, "\\", "\\\\");
    return regex_->replaceWithTemplate(text, replacement);
}

// ---------------------------------------------------------------------------
// SpeechDict
// ---------------------------------------------------------------------------

bool SpeechDict::load(const std::filesystem::path& path, std::string* error) {
    fileName_ = path;
    entries_.clear();
    gchar* contents = nullptr;
    gsize length = 0;
    GError* gerror = nullptr;
    if (g_file_get_contents(path.c_str(), &contents, &length, &gerror) == FALSE) {
        if (error != nullptr) {
            *error = gerror != nullptr ? gerror->message : "cannot read the file";
        }
        if (gerror != nullptr) {
            g_error_free(gerror);
        }
        return false;
    }
    loadFromString(std::string_view{contents, length});
    g_free(contents);
    return true;
}

void SpeechDict::loadFromString(std::string_view content) {
    if (content.starts_with("\xEF\xBB\xBF")) {
        content.remove_prefix(3);
    }
    std::string comment;
    for (const std::string_view rawLine : splitLines(content)) {
        if (isBlank(rawLine)) {
            comment.clear();
            continue;
        }
        const std::string_view line = trimRight(rawLine);
        if (line.starts_with("#")) {
            if (!comment.empty()) {
                comment += " ";
            }
            comment.append(line.substr(1));
            continue;
        }
        std::vector<std::string_view> fields;
        std::size_t start = 0;
        for (;;) {
            const std::size_t position = line.find('\t', start);
            if (position == std::string_view::npos) {
                fields.push_back(line.substr(start));
                break;
            }
            fields.push_back(line.substr(start, position - start));
            start = position + 1;
        }
        if (fields.size() != 4) {
            ALUS_WARNING("cannot parse the dictionary line {}", std::string{line});
            continue;
        }
        const std::string pattern = replaceAll(std::string{fields[0]}, "\\#", "#");
        const std::string replacement = replaceAll(std::string{fields[1]}, "\\#", "#");
        bool caseSensitive = true;
        int typeValue = 0;
        try {
            caseSensitive = std::stoi(std::string{fields[2]}) != 0;
            typeValue = std::stoi(std::string{fields[3]});
        } catch (...) {
            ALUS_WARNING("invalid dictionary entry for {}", std::string{line});
            comment.clear();
            continue;
        }
        if (typeValue < static_cast<int>(EntryType::Anywhere) ||
            typeValue > static_cast<int>(EntryType::Unix)) {
            ALUS_WARNING("unknown dictionary entry type {} for {}", typeValue,
                         std::string{line});
            comment.clear();
            continue;
        }
        DictEntry entry{pattern, replacement, comment, caseSensitive,
                        static_cast<EntryType>(typeValue)};
        std::string error;
        if (!entry.compile(&error)) {
            ALUS_WARNING("invalid dictionary pattern {} in {}: {}", pattern, fileName_.string(),
                         error);
            comment.clear();
            continue;
        }
        entries_.push_back(std::move(entry));
        comment.clear();
    }
    ALUS_DEBUG("loaded {} dictionary entries from {}", entries_.size(), fileName_.string());
}

bool SpeechDict::save(const std::filesystem::path& path, std::string* error) const {
    const std::filesystem::path target = path.empty() ? fileName_ : path;
    if (target.empty()) {
        if (error != nullptr) {
            *error = "no file name";
        }
        return false;
    }
    std::string out = "\xEF\xBB\xBF";
    for (const DictEntry& entry : entries_) {
        if (!entry.comment().empty()) {
            out += std::format("#{}\n", entry.comment());
        }
        out += std::format("{}\t{}\t{}\t{}\n", replaceAll(entry.pattern(), "#", "\\#"),
                           replaceAll(entry.replacement(), "#", "\\#"),
                           entry.caseSensitive() ? 1 : 0, static_cast<int>(entry.type()));
    }
    std::error_code code;
    std::filesystem::create_directories(target.parent_path(), code);
    GError* gerror = nullptr;
    const gboolean written = g_file_set_contents(target.c_str(), out.c_str(),
                                                 static_cast<gssize>(out.size()), &gerror);
    if (written == FALSE) {
        if (error != nullptr) {
            *error = gerror != nullptr ? gerror->message : "cannot write the file";
        }
        if (gerror != nullptr) {
            g_error_free(gerror);
        }
        return false;
    }
    return true;
}

void SpeechDict::add(DictEntry entry) {
    if (!entry.valid()) {
        std::string error;
        if (!entry.compile(&error)) {
            ALUS_WARNING("invalid dictionary pattern {}: {}", entry.pattern(), error);
            return;
        }
    }
    entries_.push_back(std::move(entry));
}

void SpeechDict::clear() { entries_.clear(); }

bool SpeechDict::eraseAt(std::size_t position) {
    if (position >= entries_.size()) {
        return false;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(position));
    return true;
}

std::string SpeechDict::sub(std::string text) {
    std::vector<std::size_t> invalid;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].valid()) {
            invalid.push_back(i);
            continue;
        }
        text = entries_[i].sub(text);
    }
    for (auto it = invalid.rbegin(); it != invalid.rend(); ++it) {
        ALUS_WARNING("dropping invalid dictionary entry {} in {}", entries_[*it].pattern(),
                     fileName_.empty() ? std::string{"the temporary dictionary"}
                                       : fileName_.string());
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    return text;
}

// ---------------------------------------------------------------------------
// Dictionaries
// ---------------------------------------------------------------------------

Dictionaries::Dictionaries(std::filesystem::path dataDir, std::filesystem::path userDir)
    : dataDir_(std::move(dataDir)), userDir_(std::move(userDir)) {
    for (const DictionaryType type : {DictionaryType::Builtin, DictionaryType::Default,
                                      DictionaryType::Voice, DictionaryType::Temp}) {
        dictionaries_[type];
        enabled_[type] = true;
    }
}

const SpeechDict& Dictionaries::dictionary(DictionaryType type) const {
    return dictionaries_.at(type);
}

void Dictionaries::setEnabled(DictionaryType type, bool enabled) { enabled_[type] = enabled; }

bool Dictionaries::isEnabled(DictionaryType type) const {
    const auto it = enabled_.find(type);
    return it == enabled_.end() || it->second;
}

void Dictionaries::load() {
    if (!dataDir_.empty()) {
        const std::filesystem::path builtin = dataDir_ / "builtin.dic";
        if (std::filesystem::exists(builtin)) {
            dictionaries_[DictionaryType::Builtin].load(builtin);
        }
    }
    if (!userDir_.empty()) {
        const std::filesystem::path userDict = userDir_ / "dictionaries" / "default.dic";
        if (std::filesystem::exists(userDict)) {
            dictionaries_[DictionaryType::Default].load(userDict);
        } else {
            // Remember where it belongs, so the dialog can save a new one.
            dictionaries_[DictionaryType::Default].load(userDict, nullptr);
        }
    }
}

void Dictionaries::loadVoiceDict(const std::string& synthName, const std::string& voiceName) {
    SpeechDict& voice = dictionaries_[DictionaryType::Voice];
    voice.clear();
    if (userDir_.empty()) {
        return;
    }
    std::string safeVoice = voiceName;
    // A voice name may contain anything, including a path separator.
    std::replace_if(
        safeVoice.begin(), safeVoice.end(),
        [](char c) { return c == '/' || c == '\\' || c == ':' || c == '\0'; }, '_');
    const std::filesystem::path path =
        userDir_ / "dictionaries" / std::format("{}-{}.dic", synthName, safeVoice);
    if (std::filesystem::exists(path)) {
        voice.load(path);
    } else {
        voice.load(path, nullptr);  // records the name for a later save
    }
}

std::string Dictionaries::processText(std::string text) {
    if (!processingEnabled_) {
        return text;
    }
    for (const DictionaryType type : {DictionaryType::Builtin, DictionaryType::Default,
                                      DictionaryType::Voice, DictionaryType::Temp}) {
        if (!isEnabled(type)) {
            continue;
        }
        text = dictionaries_[type].sub(std::move(text));
    }
    return text;
}

}  // namespace alus::speech
