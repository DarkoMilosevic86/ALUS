// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// liblouis: text to braille cells and back.
//
// Port of NVDA's `louisHelper.py` plus the part of `brailleTables/__init__.py`
// that a running screen reader needs (01-ARHITEKTURA.md §14). The library is
// the same one NVDA uses, so the three details §14 warns about are the three
// things this file exists to get right:
//
//  1. `liblouis` MUTATES THE TYPEFORM ARRAY IN PLACE. `lou_translate` takes
//     `formtype*`, not `const formtype*`, and writes back into it. NVDA passes
//     a tuple so Python copies; ALUS copies explicitly, in `translate()`, and
//     T7.2 is the test that the caller's vector is untouched afterwards.
//  2. `dotsIO` MEANS THE CELLS COME BACK AS CHARACTERS. With that mode set,
//     every output `widechar` is a dot pattern with bit `0x8000` set, so the
//     cell is `value & 0xFF`. Without the mask every cell on the display would
//     be wrong, and T7.3 asserts the bit really arrives.
//  3. THE TABLE RESOLVER IS OURS, NOT LIBLOUIS'S. The default resolver reads
//     `LOUIS_TABLEPATH` and gives the first table's directory a special role.
//     ALUS registers its own, which ignores the environment entirely and
//     resolves a name through `TableRegistry` -- that is what lets an add-on
//     (phase 8) ship a table, and it is why a user's stray `LOUIS_TABLEPATH`
//     cannot silently change what the display shows (T7.4).
//
// THREADING. liblouis's state -- the table cache, the log callback, the
// resolver -- is process global and not re-entrant, so every call here happens
// on the main thread. §13 lists `liblouis` on long lines as a candidate for
// the thread pool; that is deliberately NOT done in phase 7 (see
// claude/FAZA7.md): a region is at most a paragraph, the measurement is in
// microseconds, and a second thread in a non-re-entrant C library would buy
// nothing for a real risk.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::braille {

// NVDA: `louisHelper.Typeform`. The values are liblouis's own `typeforms`
// enum (`italic` = emph_1, `underline` = emph_2, `bold` = emph_3), because
// they are passed straight to the library.
enum class Typeform : std::uint16_t {
    PlainText = 0x0000,
    Italic = 0x0001,
    Underline = 0x0002,
    Bold = 0x0004,
};

constexpr Typeform operator|(Typeform a, Typeform b) {
    return static_cast<Typeform>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}
constexpr Typeform& operator|=(Typeform& a, Typeform b) {
    a = a | b;
    return a;
}
constexpr bool has(Typeform set, Typeform one) {
    return (static_cast<std::uint16_t>(set) & static_cast<std::uint16_t>(one)) != 0;
}

// NVDA: `louisHelper.TranslationMode`. `dotsIO` is NOT here: it is added by
// `translate()` and `backTranslate()` unconditionally, because the cell
// representation is not a caller's choice (detail 2 above).
enum class TranslationMode : int {
    None = 0,
    // liblouis `compbrlAtCursor`: the word under the cursor in computer
    // braille, which is what makes an edit field's current word readable.
    CompBrlAtCursor = 2,
    // liblouis `partialTrans`: the input is the START of a longer text, so a
    // trailing contraction may still be incomplete. Braille INPUT uses it.
    PartialTrans = 256,
};

constexpr TranslationMode operator|(TranslationMode a, TranslationMode b) {
    return static_cast<TranslationMode>(static_cast<int>(a) | static_cast<int>(b));
}
constexpr TranslationMode& operator|=(TranslationMode& a, TranslationMode b) {
    a = a | b;
    return a;
}

// What `translate()` answers. All positions are CODEPOINT indices, never
// bytes: liblouis counts in `widechar`, and §7.3's rule is that the
// conversion happens at the boundary (here) and nowhere else.
struct Translation {
    // One dot pattern per cell, already masked to 8 dots.
    std::vector<std::uint8_t> cells;
    // liblouis `inputPos`: for every cell, the codepoint it came from.
    std::vector<int> brailleToRawPos;
    // liblouis `outputPos`: for every codepoint, the cell it produced.
    std::vector<int> rawToBraillePos;
    // Where the cursor landed, when one was given.
    std::optional<int> cursorPos;
    // False when liblouis refused the table list. The cells are then the
    // input's own codepoints masked to a byte, which is wrong but readable --
    // a screen reader with a broken table must not go blank (§3.8).
    bool ok = true;
};

// NVDA: `brailleTables.BrailleTable`.
struct Table {
    // The file name, which is also the identity: "sr-Cyrl.ctb". This is what
    // the configuration stores and what liblouis is given.
    std::string fileName;
    // The translatable display name for the settings dialog (phase 9).
    std::string displayName;
    // Where the table came from: empty for the system tables, otherwise the
    // name of the source that `TableRegistry::addSource` registered (an
    // add-on). The resolver searches that source's directory first.
    std::string source;
    // NVDA: `contracted`. A grade 2 table, which decides whether braille
    // INPUT is translated per cell or per word.
    bool contracted = false;
    // Usable for output (display) and/or input (braille keyboard).
    bool output = true;
    bool input = true;
};

// NVDA: the `brailleTables` module. The name -> file mapping, the source
// directories, and the one question the rest of ALUS asks ("which table do I
// use for this language").
class TableRegistry {
public:
    TableRegistry();

    // The directory the system tables live in. Resolved once, from
    // $ALUS_LOUIS_TABLES_DIR, then the compiled-in liblouis table directory,
    // then /usr/share/liblouis/tables.
    const std::filesystem::path& systemDirectory() const noexcept { return systemDir_; }
    void setSystemDirectory(std::filesystem::path directory);

    // NVDA: `brailleTables._tablesDirs`. A source is an add-on (phase 8) or a
    // test fixture; its directory is searched BEFORE the system one for the
    // tables registered against it.
    void addSource(std::string source, std::filesystem::path directory);
    const std::map<std::string, std::filesystem::path, std::less<>>& sources() const noexcept {
        return sources_;
    }

    // NVDA: `brailleTables.addTable`. Registering the same file name again
    // replaces the entry, which is how an add-on overrides a system table.
    void addTable(Table table);
    const Table* find(std::string_view fileName) const;
    const std::vector<Table>& tables() const noexcept { return tables_; }

    // The absolute path a name resolves to, or empty when no directory has
    // it. `base` is liblouis's own argument: when a table includes another,
    // the included one is looked for next to the includer first.
    std::filesystem::path resolve(std::string_view fileName,
                                  const std::filesystem::path& base = {}) const;

    // NVDA: `getDefaultTableForCurLang`. The table for a language tag
    // ("sr-Cyrl", "sr", "en"), falling back to the generic table for the
    // language and then to the English one, which is liblouis's own default.
    std::string defaultTableForLanguage(std::string_view language, bool forInput) const;

    // Registers the tables ALUS knows by name. Deliberately a SHORT list and
    // not a scan of the directory: these are the ones whose grade and
    // language ALUS states, and any other table still works by file name
    // through `resolve`.
    void addKnownTables();

private:
    std::filesystem::path systemDir_;
    std::map<std::string, std::filesystem::path, std::less<>> sources_;
    std::vector<Table> tables_;
};

// The liblouis connection. One per process, because the library's state is.
class Louis {
public:
    static Louis& shared();

    // Registers the log callback and the table resolver. Idempotent.
    void initialize();
    // Unregisters both and frees liblouis's table cache. After this the
    // object can be initialized again, which is what a SIGHUP does.
    void terminate();
    bool isInitialized() const noexcept { return initialized_; }

    TableRegistry& registry() noexcept { return registry_; }
    const TableRegistry& registry() const noexcept { return registry_; }

    // "3.33.0", or empty when the library would not answer.
    std::string version() const;

    // NVDA: `louisHelper.translate`. `text` is UTF-8; `typeform` has one entry
    // per CODEPOINT of it, or is empty for plain text. The vector is COPIED
    // before liblouis sees it (detail 1).
    Translation translate(const std::vector<std::string>& tables, std::string_view text,
                          const std::vector<Typeform>& typeform = {},
                          std::optional<int> cursorPos = std::nullopt,
                          TranslationMode mode = TranslationMode::None);

    // NVDA: `louisHelper.backTranslate`. Cells in, UTF-8 out. Cells whose dot
    // pattern the table does not define are dropped (`noUndefinedDots`).
    std::string backTranslate(const std::vector<std::string>& tables,
                              const std::vector<std::uint8_t>& cells,
                              TranslationMode mode = TranslationMode::None);

    // NVDA: `louisHelper.getTableLanguage`. The `language` metadata field of a
    // table, empty when the table does not declare one.
    std::string tableLanguage(const std::string& fileName);

    // The codepoint positions inside `word` where it may be broken, as
    // liblouis's own hyphenation rules give them. Empty when the table carries
    // no hyphenation -- most do not, and the caller must treat that as "do not
    // break this word" rather than as an error.
    //
    // NVDA uses a separate hyphenation library here; ALUS uses the one the
    // braille table itself ships, which is one dependency fewer and the same
    // answer where a table has the rules (claude/FAZA7.md O4).
    std::vector<int> hyphenationPositions(const std::vector<std::string>& tables,
                                          std::string_view word);

    // How many times liblouis was asked to translate, and how often it
    // refused. `alus-cli braille-info` prints both; a table that is quietly
    // failing is otherwise invisible.
    struct Stats {
        std::uint64_t translations = 0;
        std::uint64_t backTranslations = 0;
        std::uint64_t failures = 0;
        std::uint64_t resolverCalls = 0;
        std::uint64_t resolverMisses = 0;
    };
    const Stats& stats() const noexcept { return stats_; }

private:
    Louis();
    ~Louis();

    Louis(const Louis&) = delete;
    Louis& operator=(const Louis&) = delete;

    TableRegistry registry_;
    bool initialized_ = false;
    Stats stats_;
};

// The dot numbers of a cell, as the log and input help say them: "1 2 5" for
// the letter h, "-" for an empty cell. NVDA: `formatCellsForLog` for a whole
// row, `formatDotNumbers` for one cell.
std::string formatDotNumbers(std::uint8_t cell);
std::string formatCellsForLog(const std::vector<std::uint8_t>& cells);
// The Unicode braille character for a cell (U+2800 + dots), which is what the
// untranslated-input indicator and the braille viewer show.
std::string unicodeBrailleFor(std::uint8_t cell);

}  // namespace alus::braille
