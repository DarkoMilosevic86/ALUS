// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Translation: one process-wide gettext domain, bound once at start-up.
//
// 00-VIZIJA.md Z8 makes localization a hard requirement and names Serbian, and
// §6 of the same document says why the STRINGS and not only the code are
// derived from NVDA: the command descriptions, the role and state labels and
// the symbol dictionaries are user-visible text that must read the same in
// ALUS as in NVDA, per language, or a user's own documentation stops matching
// what they hear. So the msgids here are NVDA's English strings, verbatim
// where NVDA has one, and a translator who already translated NVDA recognises
// them.
//
// THE PART THAT IS EASY TO GET WRONG, and the reason this is a module rather
// than three calls in main():
//
//  1. `setlocale(LC_ALL, "")` must run before any GTK call and before the
//     first translated string, or the catalogue is never consulted. It also
//     must NOT change LC_NUMERIC: ALUS writes floating-point values into
//     GKeyFile (speech rate, pitch) and a locale that prints "1,5" produces a
//     configuration file that cannot be read back. So LC_NUMERIC is pinned to
//     "C" after the fact, deliberately (FAZA9.md O1).
//  2. The catalogue must be bound to UTF-8 explicitly. Without
//     `bind_textdomain_codeset`, gettext converts the catalogue to the
//     locale's charset, and a user whose locale is `sr_RS` (ISO-8859-5, no
//     `.UTF-8`) gets mojibake on the braille display and in the GUI.
//  3. `translate()` must be callable before `initialize()`, because static
//     initialisation order is not ours to choose and a label table is allowed
//     to be touched from a constructor. Unbound, it returns the msgid, which
//     is correct English.
//
// The macros are NOT here. They are in `i18n.hpp`, which only .cpp files
// include -- a public header that defines `_` would put a one-character macro
// into every translation unit that includes ALUS (the GNOME convention, and
// the reason gi18n-lib.h exists separately from glib.h).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace alus::i18n {

struct Options {
    // Where the .mo files are: <localeDir>/<lang>/LC_MESSAGES/alus.mo.
    // Empty means "work it out" -- $ALUS_LOCALE_DIR, then the configured
    // install prefix, then `po/` beside an uninstalled build.
    std::filesystem::path localeDir;
    // Force a language ("sr", "sr_RS.UTF-8"). Empty means "whatever the
    // environment says", which is the normal case. This is what
    // [general] language in the configuration feeds, so that a user whose
    // session is English can still have a Serbian screen reader.
    std::string language;
};

// Binds the domain. Safe to call again (a configuration reload does: changing
// [general] language must not need a restart, §3.9). Returns the language that
// ended up in effect.
const std::string& initialize(const Options& options = {});

// `_()`. Never returns nullptr; returns the msgid when there is no catalogue.
const char* translate(const char* msgid);
// `ngettext()`. Serbian has three plural forms, which is the whole reason the
// count travels with the string instead of being formatted in afterwards.
const char* translatePlural(const char* singular, const char* plural, unsigned long count);
// `pgettext()`. For a msgid whose English is ambiguous out of context --
// "edit" the role versus "edit" the verb.
const char* translateContext(const char* context, const char* msgid);

// What is in effect, for the start-up log, `alus-cli i18n-info` and the tests.
const std::string& currentLanguage();
// Whether a catalogue was actually found and loaded for `currentLanguage()`.
// False is not an error: English needs no catalogue.
bool haveCatalog();
const std::filesystem::path& localeDirectory();
// Every language ALUS ships a catalogue for, from the locale directory.
std::vector<std::string> availableLanguages();

// Substitutes the single "%s" in an already-translated pattern.
//
// Why this exists rather than std::format: a translator may move "%s" within
// the sentence but has no way to declare an argument index, and a pattern that
// lost its placeholder must not abort. Missing "%s" appends the value, which
// is wrong word order and obviously wrong -- a crash in the screen reader
// would be worse than wrong word order.
std::string substitute(std::string_view pattern, std::string_view value);
std::string substitute(std::string_view pattern, std::string_view first,
                       std::string_view second);

// The same, for upstream's OTHER placeholder spelling: `{mode}`, `{hourText}`,
// `{name}`.
//
// WHY BOTH SPELLINGS EXIST HERE. The msgid is the lookup key into upstream's
// Serbian catalogue (tools/import-nvda-po.py matches by msgid, and 1105 of
// ALUS's 1106 strings are translated because of it). Upstream writes some of
// its patterns with "%s" and some with a named brace, so a string rewritten
// from one to the other is a string that silently falls back to English for
// every one of its users. The pattern is therefore copied EXACTLY as upstream
// wrote it, and this function fills in the named kind.
//
// A pattern that lost its placeholder appends the value, for the same reason
// the "%s" version does: wrong word order beats a throw in a screen reader.
std::string substituteNamed(std::string_view pattern, std::string_view name,
                            std::string_view value);

// And for upstream's THIRD spelling, the C one: "%d percent".
//
// Same reason as substituteNamed: the msgid is the lookup key, so "%d" stays
// "%d". Replaces the first "%d" (or "%s", if a translator wrote that), and
// appends the value when the pattern has neither.
std::string substituteNumber(std::string_view pattern, std::string_view value);

}  // namespace alus::i18n
