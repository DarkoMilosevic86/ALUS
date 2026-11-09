// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/text/symbols.hpp"

#include "alus/core/log.hpp"
#include "alus/text/regex.hpp"

#include <glib.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <utility>

namespace alus::text {

namespace {

constexpr std::string_view kComplexSection = "complexSymbols:";
constexpr std::string_view kSymbolSection = "symbols:";

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

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t position = text.find(separator, start);
        if (position == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, position - start));
        start = position + 1;
    }
}

// NVDA: SpeechSymbols.IDENTIFIER_ESCAPES_INPUT.
std::string unescapeIdentifier(std::string_view field) {
    if (field.size() < 2 || field.front() != '\\') {
        return std::string{field};
    }
    std::string out;
    switch (field[1]) {
        case '0':
            out += '\0';
            break;
        case 't':
            out += '\t';
            break;
        case 'n':
            out += '\n';
            break;
        case 'r':
            out += '\r';
            break;
        case 'f':
            out += '\f';
            break;
        case 'v':
            out += '\v';
            break;
        case '#':
            out += '#';
            break;
        case '\\':
            out += '\\';
            break;
        default:
            out += field[1];
            break;
    }
    out.append(field.substr(2));
    return out;
}

std::string escapeIdentifier(const std::string& identifier) {
    if (identifier.empty()) {
        return identifier;
    }
    const char first = identifier.front();
    const char* escape = nullptr;
    switch (first) {
        case '\0':
            escape = "\\0";
            break;
        case '\t':
            escape = "\\t";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\f':
            escape = "\\f";
            break;
        case '\v':
            escape = "\\v";
            break;
        case '#':
            escape = "\\#";
            break;
        case '\\':
            escape = "\\\\";
            break;
        default:
            break;
    }
    if (escape == nullptr) {
        return identifier;
    }
    return std::string{escape} + identifier.substr(1);
}

std::size_t characterCount(const std::string& text) {
    const auto length = g_utf8_strlen(text.c_str(), static_cast<gssize>(text.size()));
    return length < 0 ? 0 : static_cast<std::size_t>(length);
}

// Escapes one character for use INSIDE a character class. Regex::escape() is
// not enough here: it leaves '-' alone (harmless in a pattern, a range
// operator in a class), so a dictionary containing both '$' and '-' produced
// "[\$-\t]" — an out-of-order range, and the whole locale failed to compile.
// Only single-byte non-alphanumerics are escaped; a multi-byte UTF-8 character
// is literal in a class and must not have a backslash before its first byte.
std::string escapeForCharacterClass(const std::string& character) {
    if (character.size() != 1) {
        return character;
    }
    const unsigned char byte = static_cast<unsigned char>(character.front());
    if (std::isalnum(byte) != 0) {
        return character;
    }
    return std::string{'\\'} + character;
}

std::string firstCharacter(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const char* next = g_utf8_next_char(text.c_str());
    return text.substr(0, static_cast<std::size_t>(next - text.c_str()));
}

int levelValue(SymbolLevel level) { return static_cast<int>(level); }

}  // namespace

const char* symbolLevelName(SymbolLevel level) {
    switch (level) {
        case SymbolLevel::None:
            return "none";
        case SymbolLevel::Some:
            return "some";
        case SymbolLevel::Most:
            return "most";
        case SymbolLevel::All:
            return "all";
        case SymbolLevel::Char:
            return "character";
        case SymbolLevel::Unchanged:
            return "unchanged";
    }
    return "?";
}

std::optional<SymbolLevel> parseSymbolLevel(std::string_view name) {
    if (name == "none") {
        return SymbolLevel::None;
    }
    if (name == "some") {
        return SymbolLevel::Some;
    }
    if (name == "most") {
        return SymbolLevel::Most;
    }
    if (name == "all") {
        return SymbolLevel::All;
    }
    if (name == "char" || name == "character") {
        return SymbolLevel::Char;
    }
    if (name == "unchanged") {
        return SymbolLevel::Unchanged;
    }
    // Configuration files store the number.
    try {
        const int value = std::stoi(std::string{name});
        for (const SymbolLevel candidate :
             {SymbolLevel::None, SymbolLevel::Some, SymbolLevel::Most, SymbolLevel::All,
              SymbolLevel::Char, SymbolLevel::Unchanged}) {
            if (levelValue(candidate) == value) {
                return candidate;
            }
        }
    } catch (...) {
        // not a number either
    }
    return std::nullopt;
}

SymbolLevel nextSymbolLevel(SymbolLevel level) {
    const auto it = std::find(kConfigurableSymbolLevels.begin(), kConfigurableSymbolLevels.end(),
                              level);
    if (it == kConfigurableSymbolLevels.end()) {
        return kConfigurableSymbolLevels.front();
    }
    const auto next = std::next(it);
    return next == kConfigurableSymbolLevels.end() ? kConfigurableSymbolLevels.front() : *next;
}

const char* symbolPreserveName(SymbolPreserve preserve) {
    switch (preserve) {
        case SymbolPreserve::Never:
            return "never";
        case SymbolPreserve::Always:
            return "always";
        case SymbolPreserve::NoRep:
            return "norep";
    }
    return "?";
}

std::optional<SymbolPreserve> parseSymbolPreserve(std::string_view name) {
    if (name == "never") {
        return SymbolPreserve::Never;
    }
    if (name == "always") {
        return SymbolPreserve::Always;
    }
    if (name == "norep") {
        return SymbolPreserve::NoRep;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// SymbolDictionary
// ---------------------------------------------------------------------------

SymbolDictionary::SymbolDictionary(std::filesystem::path fileName)
    : fileName_(std::move(fileName)) {}

bool SymbolDictionary::load(const std::filesystem::path& path, bool allowComplexSymbols,
                            std::string* error) {
    fileName_ = path;
    symbols_.clear();
    index_.clear();
    complexSymbols_.clear();

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
    const std::string_view text{contents, length};
    loadFromString(text, allowComplexSymbols);
    g_free(contents);
    return true;
}

void SymbolDictionary::loadFromString(std::string_view content, bool allowComplexSymbols) {
    // A UTF-8 BOM is normal in these files: NVDA writes them as utf_8_sig.
    if (content.starts_with("\xEF\xBB\xBF")) {
        content.remove_prefix(3);
    }
    bool inComplex = false;
    bool inSymbols = false;
    for (const std::string_view rawLine : split(content, '\n')) {
        const std::string_view line = trimRight(rawLine);
        if (isBlank(line) || line.starts_with("#")) {
            continue;
        }
        if (line == kComplexSection) {
            inComplex = allowComplexSymbols;
            inSymbols = false;
            if (!allowComplexSymbols) {
                // Deliberate, not an error: only the built-in dictionary may
                // define complex symbols (§11.1).
                ALUS_DEBUG_WARNING("ignoring the complexSymbols section of {}",
                                   fileName_.string());
            }
            continue;
        }
        if (line == kSymbolSection) {
            inSymbols = true;
            inComplex = false;
            continue;
        }
        if (!inComplex && !inSymbols) {
            ALUS_WARNING("invalid line in {}: {}", fileName_.string(), std::string{line});
            continue;
        }
        if (inComplex) {
            parseLine(line, true);
        } else {
            parseLine(line, false);
        }
    }
}

void SymbolDictionary::parseLine(std::string_view line, bool inComplexSection) {
    std::vector<std::string_view> fields = split(line, '\t');
    if (inComplexSection) {
        if (fields.size() != 2 || fields[0].empty() || fields[1].empty()) {
            ALUS_WARNING("invalid complex symbol in {}: {}", fileName_.string(),
                         std::string{line});
            return;
        }
        setComplexSymbol(std::string{fields[0]}, std::string{fields[1]});
        return;
    }

    SpeechSymbol symbol;
    if (!fields.empty() && fields.back().starts_with("#")) {
        // However many fields there are, a trailing comment is the display name.
        std::string_view name = fields.back().substr(1);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
            name.remove_prefix(1);
        }
        symbol.displayName = std::string{name};
        fields.pop_back();
    }
    if (fields.size() < 2 || fields[0].empty()) {
        ALUS_WARNING("invalid line in {}: {}", fileName_.string(), std::string{line});
        return;
    }
    symbol.identifier = unescapeIdentifier(fields[0]);
    if (fields[1] != "-") {
        symbol.replacement = std::string{fields[1]};
    }
    if (fields.size() >= 3 && fields[2] != "-") {
        const std::optional<SymbolLevel> level = parseSymbolLevel(fields[2]);
        if (!level.has_value()) {
            ALUS_WARNING("invalid level in {}: {}", fileName_.string(), std::string{line});
            return;
        }
        symbol.level = level;
    }
    if (fields.size() >= 4 && fields[3] != "-") {
        const std::optional<SymbolPreserve> preserve = parseSymbolPreserve(fields[3]);
        if (!preserve.has_value()) {
            ALUS_WARNING("invalid preserve value in {}: {}", fileName_.string(),
                         std::string{line});
            return;
        }
        symbol.preserve = preserve;
    }
    set(std::move(symbol));
}

const SpeechSymbol* SymbolDictionary::find(const std::string& identifier) const {
    const auto it = index_.find(identifier);
    return it == index_.end() ? nullptr : &symbols_[it->second];
}

void SymbolDictionary::set(SpeechSymbol symbol) {
    const auto it = index_.find(symbol.identifier);
    if (it != index_.end()) {
        symbols_[it->second] = std::move(symbol);
        return;
    }
    index_.emplace(symbol.identifier, symbols_.size());
    symbols_.push_back(std::move(symbol));
}

bool SymbolDictionary::erase(const std::string& identifier) {
    const auto it = index_.find(identifier);
    if (it == index_.end()) {
        return false;
    }
    const std::size_t position = it->second;
    symbols_.erase(symbols_.begin() + static_cast<std::ptrdiff_t>(position));
    index_.erase(it);
    for (auto& [key, value] : index_) {
        if (value > position) {
            --value;
        }
    }
    return true;
}

void SymbolDictionary::setComplexSymbol(std::string identifier, std::string pattern) {
    for (auto& entry : complexSymbols_) {
        if (entry.first == identifier) {
            entry.second = std::move(pattern);
            return;
        }
    }
    complexSymbols_.emplace_back(std::move(identifier), std::move(pattern));
}

bool SymbolDictionary::save(const std::filesystem::path& path, std::string* error) const {
    const std::filesystem::path target = path.empty() ? fileName_ : path;
    if (target.empty()) {
        if (error != nullptr) {
            *error = "no file name";
        }
        return false;
    }
    std::string out = "\xEF\xBB\xBF";
    if (!complexSymbols_.empty()) {
        out += "complexSymbols:\n";
        for (const auto& [identifier, pattern] : complexSymbols_) {
            out += std::format("{}\t{}\n", identifier, pattern);
        }
        out += "\n";
    }
    out += "symbols:\n";
    for (const SpeechSymbol& symbol : symbols_) {
        out += escapeIdentifier(symbol.identifier);
        out += '\t';
        out += symbol.replacement.value_or("-");
        out += '\t';
        out += symbol.level.has_value() ? symbolLevelName(*symbol.level) : "-";
        out += '\t';
        out += symbol.preserve.has_value() ? symbolPreserveName(*symbol.preserve) : "-";
        if (symbol.displayName.has_value()) {
            out += std::format("\t# {}", *symbol.displayName);
        }
        out += '\n';
    }

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

// ---------------------------------------------------------------------------
// SymbolProcessor
// ---------------------------------------------------------------------------

SymbolProcessor::SymbolProcessor() = default;
SymbolProcessor::~SymbolProcessor() = default;
SymbolProcessor::SymbolProcessor(SymbolProcessor&&) noexcept = default;
SymbolProcessor& SymbolProcessor::operator=(SymbolProcessor&&) noexcept = default;

bool SymbolProcessor::build(std::vector<const SymbolDictionary*> sources,
                            std::size_t userSourceCount, std::string* error) {
    sources_ = std::move(sources);
    userSourceCount_ = userSourceCount;
    computed_.clear();
    order_.clear();
    complexIdentifiers_.clear();
    regex_.reset();

    std::vector<std::string> multiChars;
    std::vector<std::string> characters;

    const auto remember = [this](const std::string& identifier, SpeechSymbol symbol) {
        computed_.emplace(identifier, std::move(symbol));
        order_.push_back(identifier);
    };

    // Complex symbols first: they take priority over simple ones.
    for (const SymbolDictionary* source : sources_) {
        if (source == nullptr) {
            continue;
        }
        for (const auto& [identifier, pattern] : source->complexSymbols()) {
            if (computed_.contains(identifier)) {
                continue;  // already defined by a higher priority source
            }
            SpeechSymbol symbol;
            symbol.identifier = identifier;
            symbol.pattern = pattern;
            remember(identifier, std::move(symbol));
            complexIdentifiers_.push_back(identifier);
        }
    }

    // Then the simple symbols, and the rest of the data for the complex ones.
    for (const SymbolDictionary* source : sources_) {
        if (source == nullptr) {
            continue;
        }
        for (const SpeechSymbol& sourceSymbol : source->symbols()) {
            auto it = computed_.find(sourceSymbol.identifier);
            if (it == computed_.end()) {
                SpeechSymbol symbol;
                symbol.identifier = sourceSymbol.identifier;
                remember(sourceSymbol.identifier, std::move(symbol));
                it = computed_.find(sourceSymbol.identifier);
                if (characterCount(sourceSymbol.identifier) == 1) {
                    characters.push_back(sourceSymbol.identifier);
                } else {
                    multiChars.push_back(sourceSymbol.identifier);
                }
            }
            // Unset fields inherit from lower priority sources.
            SpeechSymbol& symbol = it->second;
            if (!symbol.replacement.has_value()) {
                symbol.replacement = sourceSymbol.replacement;
            }
            if (!symbol.level.has_value()) {
                symbol.level = sourceSymbol.level;
            }
            if (!symbol.preserve.has_value()) {
                symbol.preserve = sourceSymbol.preserve;
            }
            if (!symbol.displayName.has_value()) {
                symbol.displayName = sourceSymbol.displayName;
            }
        }
    }

    // Defaults, and drop what is unusable.
    for (auto it = computed_.begin(); it != computed_.end();) {
        SpeechSymbol& symbol = it->second;
        if (!symbol.replacement.has_value()) {
            ALUS_WARNING("no replacement defined for the symbol {}; ignoring it",
                         symbol.identifier);
            const std::string identifier = symbol.identifier;
            std::erase(characters, identifier);
            std::erase(multiChars, identifier);
            std::erase(complexIdentifiers_, identifier);
            std::erase(order_, identifier);
            it = computed_.erase(it);
            continue;
        }
        if (!symbol.level.has_value()) {
            symbol.level = SymbolLevel::All;
        }
        if (!symbol.preserve.has_value()) {
            symbol.preserve = SymbolPreserve::Never;
        }
        if (!symbol.displayName.has_value()) {
            symbol.displayName = symbol.identifier;
        }
        ++it;
    }

    // One regular expression for the whole lot. Each complex symbol gets its
    // own named group so the match can be attributed to it; the simple symbols
    // share one group, because the matched text identifies them.
    std::string characterClass;
    if (!characters.empty()) {
        characterClass = "[";
        for (const std::string& character : characters) {
            characterClass += escapeForCharacterClass(character);
        }
        characterClass += "]";
    }

    // Longest first, so "..." wins over ".".
    std::sort(multiChars.begin(), multiChars.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });

    std::vector<std::string> patterns;
    for (std::size_t i = 0; i < complexIdentifiers_.size(); ++i) {
        const SpeechSymbol& symbol = computed_.at(complexIdentifiers_[i]);
        patterns.push_back(std::format("(?P<c{}>{})", i, symbol.pattern));
    }
    // Trailing runs of spaces are stripped before the repeat rule sees them.
    patterns.emplace_back("(?P<rstripSpace>  +$)");
    if (!characterClass.empty()) {
        // More than three of the same character: "5 dots" instead of five dots.
        patterns.push_back(
            std::format("(?P<repeated>(?P<repTmp>{})(?P=repTmp){{3,}})", characterClass));
    }
    std::vector<std::string> simpleParts;
    for (const std::string& identifier : multiChars) {
        simpleParts.push_back(Regex::escape(identifier));
    }
    if (!characterClass.empty()) {
        simpleParts.push_back(characterClass);
    }
    if (!simpleParts.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < simpleParts.size(); ++i) {
            if (i != 0) {
                joined += "|";
            }
            joined += simpleParts[i];
        }
        patterns.push_back(std::format("(?P<simple>{})", joined));
    }

    std::string pattern;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (i != 0) {
            pattern += "|";
        }
        pattern += patterns[i];
    }

    regex_ = std::make_unique<Regex>();
    std::string compileError;
    if (!regex_->compile(pattern, RegexOption::None, &compileError)) {
        ALUS_ERROR("invalid complex symbol pattern: {}", compileError);
        if (error != nullptr) {
            *error = compileError;
        }
        regex_.reset();
        return false;
    }
    return true;
}

const SpeechSymbol* SymbolProcessor::computed(const std::string& identifier) const {
    const auto it = computed_.find(identifier);
    return it == computed_.end() ? nullptr : &it->second;
}

std::vector<SpeechSymbol> SymbolProcessor::computedSymbols() const {
    std::vector<SpeechSymbol> out;
    out.reserve(order_.size());
    for (const std::string& identifier : order_) {
        const auto it = computed_.find(identifier);
        if (it != computed_.end()) {
            out.push_back(it->second);
        }
    }
    return out;
}

bool SymbolProcessor::isBuiltin(const std::string& identifier) const {
    for (std::size_t i = userSourceCount_; i < sources_.size(); ++i) {
        if (sources_[i] != nullptr && sources_[i]->contains(identifier)) {
            return true;
        }
    }
    return false;
}

std::string SymbolProcessor::replacementFor(const SpeechSymbol& symbol,
                                            const std::string& matchedText, SymbolLevel level,
                                            const std::string& replacement) const {
    const SymbolPreserve preserve = symbol.preserve.value_or(SymbolPreserve::Never);
    const SymbolLevel symbolLevel = symbol.level.value_or(SymbolLevel::All);
    const bool speakReplacement = levelValue(level) >= levelValue(symbolLevel);

    std::string suffix = " ";
    if (preserve == SymbolPreserve::Always ||
        (preserve == SymbolPreserve::NoRep && !speakReplacement)) {
        suffix = matchedText;
    }
    if (speakReplacement && !replacement.empty()) {
        return std::format(" {}{}", replacement, suffix);
    }
    return suffix;
}

std::string SymbolProcessor::processText(const std::string& text, SymbolLevel level) const {
    if (regex_ == nullptr || !regex_->valid()) {
        return text;
    }
    return regex_->replace(text, [this, level](const Match& match) -> std::string {
        // Which alternative matched? Checked explicitly rather than through
        // "the last named group", so a named group inside a locale's own
        // complex pattern cannot confuse the dispatch.
        for (std::size_t i = 0; i < complexIdentifiers_.size(); ++i) {
            const std::string groupName = std::format("c{}", i);
            if (!match.participated(groupName)) {
                continue;
            }
            const SpeechSymbol& symbol = computed_.at(complexIdentifiers_[i]);
            const int groupBase = regex_->groupNumber(groupName);
            const std::optional<std::string> expanded =
                expandTemplate(match, symbol.replacement.value_or(std::string{}), groupBase);
            if (!expanded.has_value()) {
                return match.group(0);  // a broken replacement leaves the text alone
            }
            return replacementFor(symbol, match.group(0), level, *expanded);
        }

        if (match.participated("rstripSpace")) {
            return {};
        }

        const std::string matched = match.group(0);
        if (match.participated("repeated")) {
            const std::string first = firstCharacter(matched);
            const auto it = computed_.find(first);
            if (it == computed_.end()) {
                return matched;
            }
            const SpeechSymbol& symbol = it->second;
            const SymbolLevel symbolLevel = symbol.level.value_or(SymbolLevel::All);
            if (levelValue(level) >= levelValue(symbolLevel)) {
                return std::format("  {} {} ", characterCount(matched),
                                   symbol.replacement.value_or(std::string{}));
            }
            const SymbolPreserve preserve = symbol.preserve.value_or(SymbolPreserve::Never);
            if (preserve == SymbolPreserve::Always || preserve == SymbolPreserve::NoRep) {
                return matched;
            }
            return " ";
        }

        const auto it = computed_.find(matched);
        if (it == computed_.end()) {
            return matched;
        }
        return replacementFor(it->second, matched, level,
                              it->second.replacement.value_or(std::string{}));
    });
}

std::string SymbolProcessor::processSymbol(const std::string& symbol) const {
    const auto it = computed_.find(symbol);
    if (it == computed_.end() || !it->second.replacement.has_value()) {
        return symbol;
    }
    return *it->second.replacement;
}

// ---------------------------------------------------------------------------
// SymbolRegistry
// ---------------------------------------------------------------------------

struct SymbolRegistry::Entry {
    SymbolDictionary builtin;
    SymbolDictionary cldr;
    SymbolDictionary user;
    std::vector<std::unique_ptr<SymbolDictionary>> addons;
    std::unique_ptr<SymbolProcessor> processor;
    bool usable = false;
};

SymbolRegistry::SymbolRegistry(std::filesystem::path dataDir, std::filesystem::path userDir)
    : dataDir_(std::move(dataDir)), userDir_(std::move(userDir)) {}

SymbolRegistry::~SymbolRegistry() = default;

void SymbolRegistry::setDataDirectory(std::filesystem::path dataDir) {
    dataDir_ = std::move(dataDir);
    invalidateAll();
}

void SymbolRegistry::setUserDirectory(std::filesystem::path userDir) {
    userDir_ = std::move(userDir);
    invalidateAll();
}

void SymbolRegistry::addSourceDirectory(std::filesystem::path directory) {
    extraDirs_.push_back(std::move(directory));
    invalidateAll();
}

void SymbolRegistry::invalidateAll() { entries_.clear(); }

std::vector<std::string> SymbolRegistry::localeCandidates(const std::string& locale) {
    std::vector<std::string> candidates;
    if (!locale.empty()) {
        candidates.push_back(locale);
        const std::size_t separator = locale.find_first_of("_-");
        if (separator != std::string::npos) {
            candidates.push_back(locale.substr(0, separator));
        }
    }
    if (std::find(candidates.begin(), candidates.end(), "en") == candidates.end()) {
        candidates.emplace_back("en");
    }
    return candidates;
}

SymbolRegistry::Entry* SymbolRegistry::entryFor(const std::string& language) {
    const auto existing = entries_.find(language);
    if (existing != entries_.end()) {
        return existing->second.get();
    }
    auto entry = std::make_unique<Entry>();

    if (!dataDir_.empty()) {
        const std::filesystem::path builtinPath = dataDir_ / (language + ".dic");
        if (std::filesystem::exists(builtinPath)) {
            entry->builtin.load(builtinPath, true);
        }
        const std::filesystem::path cldrPath = dataDir_ / ("cldr-" + language + ".dic");
        if (std::filesystem::exists(cldrPath)) {
            entry->cldr.load(cldrPath, false);
        }
    }
    if (entry->builtin.empty() && language == "en") {
        // The floor: English punctuation always works, even uninstalled.
        entry->builtin.loadFromString(builtinEnglishSymbols(), true);
    }
    for (const std::filesystem::path& directory : extraDirs_) {
        const std::filesystem::path path = directory / ("symbols-" + language + ".dic");
        if (!std::filesystem::exists(path)) {
            continue;
        }
        auto addon = std::make_unique<SymbolDictionary>();
        // Add-ons may not define complex symbols either (§11.1).
        addon->load(path, false);
        entry->addons.push_back(std::move(addon));
    }
    if (!userDir_.empty()) {
        const std::filesystem::path userPath = userDir_ / ("symbols-" + language + ".dic");
        if (std::filesystem::exists(userPath)) {
            entry->user.load(userPath, false);
        }
    }

    entry->usable = !entry->builtin.empty();
    if (entry->usable) {
        std::vector<const SymbolDictionary*> sources;
        sources.push_back(&entry->user);
        for (const auto& addon : entry->addons) {
            sources.push_back(addon.get());
        }
        sources.push_back(&entry->builtin);
        sources.push_back(&entry->cldr);
        // English is always the base, as in NVDA: a locale dictionary need only
        // carry what differs.
        if (language != "en") {
            Entry* english = entryFor("en");
            if (english != nullptr && english->usable) {
                sources.push_back(&english->builtin);
                sources.push_back(&english->cldr);
            }
        }
        entry->processor = std::make_unique<SymbolProcessor>();
        if (!entry->processor->build(std::move(sources), 1)) {
            entry->processor.reset();
            entry->usable = false;
        }
    }

    Entry* result = entry.get();
    entries_.emplace(language, std::move(entry));
    return result;
}

SymbolProcessor* SymbolRegistry::processorFor(const std::string& locale) {
    for (const std::string& candidate : localeCandidates(locale)) {
        Entry* entry = entryFor(candidate);
        if (entry != nullptr && entry->usable && entry->processor != nullptr) {
            return entry->processor.get();
        }
    }
    return nullptr;
}

std::string SymbolRegistry::processText(const std::string& locale, const std::string& text,
                                        SymbolLevel level) {
    SymbolProcessor* processor = processorFor(locale);
    if (processor == nullptr) {
        return text;
    }
    return processor->processText(text, level);
}

std::string SymbolRegistry::processSymbol(const std::string& locale, const std::string& symbol) {
    SymbolProcessor* processor = processorFor(locale);
    if (processor == nullptr) {
        return symbol;
    }
    return processor->processSymbol(symbol);
}

}  // namespace alus::text
