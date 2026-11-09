// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/louis.hpp.

#include "alus/braille/louis.hpp"

#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"

#include <liblouis/liblouis.h>

// liblouis defines these as macros for its own typeform enum. They would
// rewrite any identifier of the same name below (and in every header this
// file includes later), so they go as soon as the declarations are in.
#undef italic
#undef underline
#undef bold

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace alus::braille {
namespace {

constexpr std::uint32_t kDotsIoStart = 0x8000;
constexpr std::uint32_t kUnicodeBrailleStart = 0x2800;

// ---------------------------------------------------------------------------
// UTF-8 <-> widechar (UTF-32 on this platform: lou_charSize() == 4)
// ---------------------------------------------------------------------------

std::vector<widechar> toWide(std::string_view text) {
    std::vector<widechar> out;
    out.reserve(text.size());
    std::size_t offset = 0;
    const text::OffsetConverter converter{text};
    const std::size_t count = converter.codepointLength();
    for (std::size_t index = 0; index < count; ++index) {
        offset = converter.toByte(index);
        out.push_back(static_cast<widechar>(text::codepointAt(text, offset)));
    }
    return out;
}

void appendUtf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::string fromWide(const widechar* data, std::size_t length) {
    std::string out;
    out.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        appendUtf8(out, static_cast<std::uint32_t>(data[index]));
    }
    return out;
}

std::string joinTables(const std::vector<std::string>& tables) {
    std::string joined;
    for (const std::string& table : tables) {
        if (table.empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined.push_back(',');
        }
        joined += table;
    }
    return joined;
}

// ---------------------------------------------------------------------------
// The table resolver (detail 3 of the header)
// ---------------------------------------------------------------------------
//
// liblouis hands us a comma-separated list and expects a NULL-terminated
// array of C strings back, which it then copies. NVDA keeps a reference to
// the last array for exactly that reason, and so does this: `storage` owns
// the strings until the next call, and `pointers` is the array liblouis sees.
//
// The mutex is not for parallelism -- translation is main-thread only -- but
// because liblouis calls the resolver from inside `lou_translate`, and a
// future thread-pool user of this file must find the invariant already
// protected rather than discover it is not.

// All three are allocated once and NEVER destroyed, on purpose. liblouis's
// state, this storage and the mutex that guards it are process globals with no
// defined destruction order between them, and `Louis::terminate()` touches all
// three -- so a static destructor running in the wrong order would lock a
// destroyed mutex. The memory is reclaimed by the OS at exit, which is the
// correct lifetime for a process-global C library's scratch space.
std::mutex& resolverMutex() {
    static std::mutex* mutex = new std::mutex();
    return *mutex;
}

std::vector<std::string>& resolverStorage() {
    static std::vector<std::string>* storage = new std::vector<std::string>();
    return *storage;
}

std::vector<char*>& resolverPointers() {
    static std::vector<char*>* pointers = new std::vector<char*>();
    return *pointers;
}

char** resolveTables(const char* tableList, const char* base) {
    Louis& louis = Louis::shared();
    const std::lock_guard<std::mutex> lock{resolverMutex()};
    std::vector<std::string>& storage = resolverStorage();
    std::vector<char*>& pointers = resolverPointers();
    storage.clear();
    pointers.clear();
    if (tableList == nullptr) {
        return nullptr;
    }
    const std::filesystem::path baseDir =
        base != nullptr ? std::filesystem::path{base}.parent_path() : std::filesystem::path{};

    std::string_view list{tableList};
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? list.size() : comma;
        const std::string_view name = list.substr(start, end - start);
        if (!name.empty()) {
            const std::filesystem::path path = louis.registry().resolve(name, baseDir);
            if (path.empty()) {
                // One unresolvable table makes the whole list unusable:
                // liblouis would otherwise compile a partial table and
                // produce cells that look right and are not.
                ALUS_WARNING("braille table not found: {} (base {})", std::string{name},
                             base != nullptr ? base : "-");
                storage.clear();
                pointers.clear();
                return nullptr;
            }
            storage.push_back(path.string());
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    pointers.reserve(storage.size() + 1);
    for (std::string& path : storage) {
        pointers.push_back(path.data());
    }
    pointers.push_back(nullptr);
    return pointers.data();
}

void louisLog(logLevels level, const char* message) {
    if (message == nullptr) {
        return;
    }
    if (level >= LOU_LOG_ERROR) {
        ALUS_WARNING("liblouis: {}", message);
    } else if (level >= LOU_LOG_WARN) {
        ALUS_DEBUG_WARNING("liblouis: {}", message);
    } else {
        ALUS_DEBUG("liblouis: {}", message);
    }
}

// The language a table name begins with: "sr-Cyrl.ctb" -> "sr-cyrl",
// "en-ueb-g2.ctb" -> "en". Lower case, because language tags are compared
// case-insensitively and a table file name is not authoritative about case.
std::string lowered(std::string_view text) {
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// TableRegistry
// ---------------------------------------------------------------------------

TableRegistry::TableRegistry() {
    if (const char* fromEnvironment = std::getenv("ALUS_LOUIS_TABLES_DIR")) {
        systemDir_ = fromEnvironment;
        return;
    }
#ifdef ALUS_LOUIS_TABLES_DIR
    systemDir_ = ALUS_LOUIS_TABLES_DIR;
    if (std::filesystem::exists(systemDir_)) {
        return;
    }
#endif
    if (systemDir_.empty() || !std::filesystem::exists(systemDir_)) {
        systemDir_ = "/usr/share/liblouis/tables";
    }
}

void TableRegistry::setSystemDirectory(std::filesystem::path directory) {
    systemDir_ = std::move(directory);
}

void TableRegistry::addSource(std::string source, std::filesystem::path directory) {
    sources_[std::move(source)] = std::move(directory);
}

void TableRegistry::addTable(Table table) {
    const auto existing =
        std::find_if(tables_.begin(), tables_.end(),
                     [&](const Table& one) { return one.fileName == table.fileName; });
    if (existing != tables_.end()) {
        *existing = std::move(table);
        return;
    }
    tables_.push_back(std::move(table));
}

const Table* TableRegistry::find(std::string_view fileName) const {
    for (const Table& table : tables_) {
        if (table.fileName == fileName) {
            return &table;
        }
    }
    return nullptr;
}

std::filesystem::path TableRegistry::resolve(std::string_view fileName,
                                             const std::filesystem::path& base) const {
    const std::string name{fileName};
    // An absolute or explicitly relative path is taken as given: that is how
    // a test fixture and `alus-cli` name a table that is registered nowhere.
    if (name.find('/') != std::string::npos) {
        const std::filesystem::path direct{name};
        return std::filesystem::is_regular_file(direct) ? direct : std::filesystem::path{};
    }

    std::vector<std::filesystem::path> directories;
    if (!base.empty()) {
        // liblouis is resolving an `include` from inside another table: the
        // includer's own directory wins, which is what keeps an add-on's
        // table self-contained.
        directories.push_back(base);
    } else if (const Table* table = find(name); table != nullptr && !table->source.empty()) {
        const auto source = sources_.find(table->source);
        if (source != sources_.end()) {
            directories.push_back(source->second);
        }
    }
    // An unregistered name is still looked for in every registered source,
    // because a table an add-on ships may be included by file name only.
    for (const auto& [sourceName, directory] : sources_) {
        (void)sourceName;
        if (std::find(directories.begin(), directories.end(), directory) == directories.end()) {
            directories.push_back(directory);
        }
    }
    if (std::find(directories.begin(), directories.end(), systemDir_) == directories.end()) {
        directories.push_back(systemDir_);
    }

    for (const std::filesystem::path& directory : directories) {
        if (directory.empty()) {
            continue;
        }
        std::error_code ignored;
        const std::filesystem::path candidate = directory / name;
        if (std::filesystem::is_regular_file(candidate, ignored)) {
            return candidate;
        }
    }
    return {};
}

void TableRegistry::addKnownTables() {
    // Serbian first, and both scripts, because it is the language this
    // project is written for and `sr-Cyrl.ctb` is the T7.1 measurement.
    addTable({"sr-Cyrl.ctb", "Serbian (Cyrillic)", {}, false, true, true});
    addTable({"sr-g1.ctb", "Serbian grade 1", {}, false, true, true});
    addTable({"en-ueb-g1.ctb", "English (UEB) grade 1", {}, false, true, true});
    addTable({"en-ueb-g2.ctb", "English (UEB) grade 2", {}, true, true, true});
    addTable({"en-us-g1.ctb", "English (US) grade 1", {}, false, true, true});
    addTable({"en-us-g2.ctb", "English (US) grade 2", {}, true, true, true});
    addTable({"en-us-comp8.ctb", "English (US) 8 dot computer braille", {}, false, true, true});
    addTable({"hr-comp8.utb", "Croatian 8 dot computer braille", {}, false, true, true});
    addTable({"de-g1.ctb", "German grade 1", {}, false, true, true});
    addTable({"fr-bfu-comp8.utb", "French 8 dot computer braille", {}, false, true, true});
    addTable({"ru.ctb", "Russian", {}, false, true, true});
    addTable({"unicode-braille.utb", "Unicode braille", {}, false, true, true});
}

std::string TableRegistry::defaultTableForLanguage(std::string_view language,
                                                   bool forInput) const {
    const std::string wanted = lowered(language);
    // The most specific match first ("sr-cyrl" before "sr"), which is why the
    // tables are tried in registration order with a prefix test rather than
    // by a map lookup.
    for (int pass = 0; pass < 2; ++pass) {
        for (const Table& table : tables_) {
            if ((forInput && !table.input) || (!forInput && !table.output)) {
                continue;
            }
            const std::string name = lowered(table.fileName);
            const bool exact = name.rfind(wanted + ".", 0) == 0;
            const bool prefix = name.rfind(wanted + "-", 0) == 0;
            if ((pass == 0 && exact) || (pass == 1 && prefix)) {
                return table.fileName;
            }
        }
        // A tag with a script subtag falls back to the bare language.
        if (pass == 0) {
            const std::size_t dash = wanted.find('-');
            if (dash != std::string::npos) {
                return defaultTableForLanguage(wanted.substr(0, dash), forInput);
            }
        }
    }
    return "en-ueb-g1.ctb";
}

// ---------------------------------------------------------------------------
// Louis
// ---------------------------------------------------------------------------

Louis& Louis::shared() {
    // Never destroyed, for the reason the resolver storage is not: the
    // destructor would call `lou_free()` during static destruction, racing
    // with whatever else is being torn down. `braille::Service` calls
    // terminate() explicitly while the process is still alive, which is where
    // liblouis's memory is actually released.
    static Louis* louis = new Louis();
    return *louis;
}

Louis::Louis() { registry_.addKnownTables(); }

Louis::~Louis() {
    if (initialized_) {
        terminate();
    }
}

void Louis::initialize() {
    if (initialized_) {
        return;
    }
    lou_registerLogCallback(&louisLog);
    // Everything is reported to us and filtered by ALUS's own log level, as
    // NVDA does: liblouis's idea of a warning is not ALUS's.
    lou_setLogLevel(LOU_LOG_DEBUG);
    lou_registerTableResolver(&resolveTables);
    initialized_ = true;
    ALUS_INFO("liblouis {} ready, tables in {}", version(),
             registry_.systemDirectory().string());
}

void Louis::terminate() {
    if (!initialized_) {
        return;
    }
    lou_setLogLevel(LOU_LOG_OFF);
    lou_registerTableResolver(nullptr);
    lou_registerLogCallback(nullptr);
    lou_free();
    {
        const std::lock_guard<std::mutex> lock{resolverMutex()};
        resolverStorage().clear();
        resolverPointers().clear();
    }
    initialized_ = false;
}

std::string Louis::version() const {
    const char* value = lou_version();
    return value != nullptr ? std::string{value} : std::string{};
}

Translation Louis::translate(const std::vector<std::string>& tables, std::string_view text,
                             const std::vector<Typeform>& typeform,
                             std::optional<int> cursorPos, TranslationMode mode) {
    Translation result;
    // NVDA strips NUL before translating: liblouis treats it as a terminator
    // and a terminal line can legitimately contain one.
    std::string cleaned;
    cleaned.reserve(text.size());
    for (const char c : text) {
        if (c != '\0') {
            cleaned.push_back(c);
        }
    }
    std::vector<widechar> input = toWide(cleaned);
    if (input.empty()) {
        result.cursorPos = cursorPos.has_value() ? std::optional<int>{0} : std::nullopt;
        return result;
    }
    const std::string tableList = joinTables(tables);
    if (tableList.empty()) {
        result.ok = false;
        ++stats_.failures;
        return result;
    }
    ++stats_.translations;

    // DETAIL 1: the copy. liblouis writes into the typeform array, so the
    // caller's vector must never be the one it is given. The copy is also
    // padded to the input length, because liblouis reads `inlen` entries
    // whatever the caller provided. How LONG that copy has to be is decided
    // per attempt below, because it is not the input length.
    std::vector<formtype> forms;
    if (!typeform.empty()) {
        forms.resize(input.size(), static_cast<formtype>(Typeform::PlainText));
        const std::size_t count = std::min(typeform.size(), forms.size());
        for (std::size_t index = 0; index < count; ++index) {
            forms[index] = static_cast<formtype>(typeform[index]);
        }
    }

    const int wholeMode = static_cast<int>(mode) | dotsIO;
    std::size_t capacity = std::max<std::size_t>(input.size() * 2 + 32, 128);
    for (int attempt = 0; attempt < 4; ++attempt) {
        int inLength = static_cast<int>(input.size());
        int outLength = static_cast<int>(capacity);
        std::vector<widechar> output(capacity);
        std::vector<int> outputPos(input.size(), 0);
        std::vector<int> inputPos(capacity, 0);
        int cursor = cursorPos.value_or(0);
        // liblouis writes the OUTPUT typeform back into the SAME array, one
        // entry per CELL, so the buffer has to be as long as the OUTPUT
        // buffer. NVDA allocates it at `outlen` for exactly this reason
        // (`louisHelper.translate`: `typeformbuf = (c_ushort*outlen.value)()`
        // with only `inlen` entries filled in). Sizing it at the input length
        // is a write past the end of the vector on any text that produces
        // more cells than it has characters -- which every capital sign,
        // number sign and grade 2 contraction does.
        std::vector<formtype> attemptForms;
        if (!forms.empty()) {
            attemptForms.assign(std::max(capacity, forms.size()),
                                static_cast<formtype>(Typeform::PlainText));
            std::copy(forms.begin(), forms.end(), attemptForms.begin());
        }
        const int ok = lou_translate(tableList.c_str(), input.data(), &inLength, output.data(),
                                     &outLength, attemptForms.empty() ? nullptr
                                                                      : attemptForms.data(),
                                     nullptr, outputPos.data(), inputPos.data(),
                                     cursorPos.has_value() ? &cursor : nullptr, wholeMode);
        if (ok == 0) {
            ++stats_.failures;
            result.ok = false;
            ALUS_DEBUG_WARNING("liblouis refused the table list: {}", tableList);
            return result;
        }
        if (inLength < static_cast<int>(input.size()) && attempt < 3) {
            // The output buffer was too small: liblouis consumed only part of
            // the input. Grow and try again rather than show a truncated line.
            capacity *= 2;
            continue;
        }
        const std::size_t cells = static_cast<std::size_t>(std::max(outLength, 0));
        result.cells.resize(cells);
        for (std::size_t index = 0; index < cells; ++index) {
            // DETAIL 2: the mask. In dotsIO mode the cell arrives as a
            // character with 0x8000 set.
            result.cells[index] = static_cast<std::uint8_t>(output[index] & 0xFF);
        }
        result.brailleToRawPos.assign(inputPos.begin(),
                                      inputPos.begin() + static_cast<std::ptrdiff_t>(cells));
        result.rawToBraillePos.assign(
            outputPos.begin(),
            outputPos.begin() + static_cast<std::ptrdiff_t>(
                                    std::min<std::size_t>(input.size(),
                                                          static_cast<std::size_t>(inLength))));
        // A short input count means the tail was dropped; map what is left to
        // the end of the cells so no caller indexes past the vector.
        result.rawToBraillePos.resize(input.size(), static_cast<int>(cells));
        if (cursorPos.has_value()) {
            result.cursorPos = cursor;
        }
        return result;
    }
    ++stats_.failures;
    result.ok = false;
    return result;
}

std::string Louis::backTranslate(const std::vector<std::string>& tables,
                                 const std::vector<std::uint8_t>& cells,
                                 TranslationMode mode) {
    if (cells.empty()) {
        return {};
    }
    const std::string tableList = joinTables(tables);
    if (tableList.empty()) {
        ++stats_.failures;
        return {};
    }
    ++stats_.backTranslations;
    std::vector<widechar> input;
    input.reserve(cells.size());
    for (const std::uint8_t cell : cells) {
        input.push_back(static_cast<widechar>(cell) | kDotsIoStart);
    }
    std::size_t capacity = std::max<std::size_t>(cells.size() * 4 + 32, 128);
    for (int attempt = 0; attempt < 4; ++attempt) {
        int inLength = static_cast<int>(input.size());
        int outLength = static_cast<int>(capacity);
        std::vector<widechar> output(capacity);
        const int wholeMode = static_cast<int>(mode) | dotsIO | noUndefinedDots;
        const int ok = lou_backTranslate(tableList.c_str(), input.data(), &inLength,
                                         output.data(), &outLength, nullptr, nullptr, nullptr,
                                         nullptr, nullptr, wholeMode);
        if (ok == 0) {
            ++stats_.failures;
            ALUS_DEBUG_WARNING("liblouis refused to back translate with {}", tableList);
            return {};
        }
        if (inLength < static_cast<int>(input.size()) && attempt < 3) {
            capacity *= 2;
            continue;
        }
        return fromWide(output.data(), static_cast<std::size_t>(std::max(outLength, 0)));
    }
    ++stats_.failures;
    return {};
}

std::vector<int> Louis::hyphenationPositions(const std::vector<std::string>& tables,
                                             std::string_view word) {
    const std::string tableList = joinTables(tables);
    std::vector<widechar> input = toWide(word);
    if (tableList.empty() || input.size() < 2) {
        return {};
    }
    // liblouis wants one byte per character, and it wants the FIRST one to be
    // '0': its contract is "hyphens[i] == '1' means a break is allowed before
    // character i", and a break before the first character is never allowed.
    std::vector<char> hyphens(input.size() + 1, '0');
    if (lou_hyphenate(tableList.c_str(), input.data(), static_cast<int>(input.size()),
                      hyphens.data(), 0) == 0) {
        return {};
    }
    std::vector<int> positions;
    for (std::size_t index = 1; index < input.size(); ++index) {
        if (hyphens[index] == '1') {
            positions.push_back(static_cast<int>(index));
        }
    }
    return positions;
}

std::string Louis::tableLanguage(const std::string& fileName) {
    char* value = lou_getTableInfo(fileName.c_str(), "language");
    if (value == nullptr) {
        return {};
    }
    std::string language{value};
    std::free(value);
    return language;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::string formatDotNumbers(std::uint8_t cell) {
    std::string out;
    for (int dot = 0; dot < 8; ++dot) {
        if ((cell & (1U << dot)) != 0) {
            if (!out.empty()) {
                out.push_back(' ');
            }
            out.push_back(static_cast<char>('1' + dot));
        }
    }
    return out;
}

std::string formatCellsForLog(const std::vector<std::uint8_t>& cells) {
    std::string out;
    for (const std::uint8_t cell : cells) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        if (cell == 0) {
            out.push_back('-');
            continue;
        }
        for (int dot = 0; dot < 8; ++dot) {
            if ((cell & (1U << dot)) != 0) {
                out.push_back(static_cast<char>('1' + dot));
            }
        }
    }
    return out;
}

std::string unicodeBrailleFor(std::uint8_t cell) {
    std::string out;
    appendUtf8(out, kUnicodeBrailleStart + cell);
    return out;
}

}  // namespace alus::braille
