// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The regular expression engine ALUS uses for text processing.
//
// WHY THIS IS A WRAPPER AND NOT std::regex: architecture.md §11.1 records a
// trap NVDA fell into and had to fix. The four speech-dictionary entry types
// with word boundaries (WORD, PART_OF_WORD, START_OF_WORD, END_OF_WORD) need a
// `\w` THAT INCLUDES COMBINING MARKS. Python's standard `re` excludes them, so
// NVDA uses the `regex` module with VERSION1 semantics; without it, dictionary
// entries break in every language with diacritics — which includes Serbian,
// ALUS's first locale. std::regex is worse than Python's `re` in every respect
// that matters here, so it is not a candidate.
//
// The architecture names ICU. This wrapper uses GLib's GRegex (PCRE2) instead,
// for a reason that was not visible when the plan was written:
//
//   * The patterns are DATA, not code. Complex symbols in `symbols.dic` are
//     written in Python `re` syntax, including `(?P<name>...)` groups and
//     `(?P=name)` backreferences, and those files come from NVDA's locale data.
//     PCRE2 accepts that syntax verbatim; ICU does not support `(?P<...>)` at
//     all. Choosing ICU would mean rewriting every locale's symbol patterns by
//     hand — a silent-breakage machine.
//   * GRegex is already linked (GLib is the core dependency), so this costs
//     nothing in dependencies or startup time.
//   * `\w` with combining marks is expressed explicitly as
//     kWordCharacterClass below, which is what `regex` VERSION1 does under the
//     hood. The behaviour is the requirement; the engine's spelling of it is
//     not.
//
// ICU is still needed in phase 5, for word and sentence SEGMENTATION, which
// PCRE2 cannot do. This decision does not touch that.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using GRegex = struct _GRegex;
using GMatchInfo = struct _GMatchInfo;

namespace alus::text {

// `\w` as the speech dictionaries need it: letters, MARKS, digits, underscore.
// The marks are the whole point (§11.1).
inline constexpr std::string_view kWordCharacterClass = "[\\p{L}\\p{M}\\p{N}_]";

enum class RegexOption : unsigned {
    None = 0,
    CaseInsensitive = 1U << 0,
    // `.` matches a newline too (Python's re.DOTALL).
    DotAll = 1U << 1,
    Multiline = 1U << 2,
    // Treat the subject as bytes rather than UTF-8. ALUS never wants this; it
    // exists so the flag set is honest about what the engine offers.
    Raw = 1U << 3,
};

inline RegexOption operator|(RegexOption a, RegexOption b) {
    return static_cast<RegexOption>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool has(RegexOption set, RegexOption flag) {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0;
}

class Regex;

// One match. Valid only for as long as the call it was handed to.
class Match {
public:
    Match(const Regex& regex, const std::string& subject, GMatchInfo* info)
        : regex_(regex), subject_(subject), info_(info) {}

    Match(const Match&) = delete;
    Match& operator=(const Match&) = delete;

    // Group 0 is the whole match.
    std::string group(int number = 0) const;
    std::string group(const std::string& name) const;
    bool participated(int number) const;
    bool participated(const std::string& name) const;

    // Byte offsets into the subject; -1 when the group did not participate.
    std::ptrdiff_t start(int number = 0) const;
    std::ptrdiff_t end(int number = 0) const;

    // The highest-numbered group that participated — Python's `m.lastindex`.
    int lastGroup() const;
    // The name of the last named group that participated — Python's
    // `m.lastgroup`. Empty when no named group matched. This is what the symbol
    // processor dispatches on (characterProcessing.py:599).
    std::string lastGroupName() const;

    const std::string& subject() const noexcept { return subject_; }

private:
    const Regex& regex_;
    const std::string& subject_;
    GMatchInfo* info_;
};

class Regex {
public:
    Regex() = default;
    ~Regex();

    Regex(const Regex&) = delete;
    Regex& operator=(const Regex&) = delete;
    Regex(Regex&& other) noexcept;
    Regex& operator=(Regex&& other) noexcept;

    // False and `error` filled on a bad pattern. A user's dictionary is allowed
    // to contain a bad pattern; it must be reported, not fatal.
    bool compile(std::string_view pattern, RegexOption options = RegexOption::None,
                 std::string* error = nullptr);
    bool valid() const noexcept { return regex_ != nullptr; }
    const std::string& pattern() const noexcept { return pattern_; }

    // The named groups, in the order they appear in the pattern.
    const std::vector<std::string>& groupNames() const noexcept { return groupNames_; }
    // -1 when there is no such group.
    int groupNumber(const std::string& name) const;

    bool matches(const std::string& subject) const;
    // Calls `onMatch` for the first match only.
    bool search(const std::string& subject,
                const std::function<void(const Match&)>& onMatch) const;

    // Replaces every match with what `replacer` returns. An empty match
    // advances by one character, so a pattern that can match nothing cannot
    // loop forever.
    std::string replace(const std::string& subject,
                        const std::function<std::string(const Match&)>& replacer) const;

    // Replacement text, with \1..\9 and \\ honoured and nothing else: this is
    // the speech dictionary's replacement syntax.
    std::string replaceWithTemplate(const std::string& subject,
                                    const std::string& replacement) const;

    static std::string escape(std::string_view literal);

    GRegex* native() const noexcept { return regex_; }

private:
    GRegex* regex_ = nullptr;
    std::string pattern_;
    std::vector<std::string> groupNames_;
};

// Expands \1..\9 group references and \\ in `templateText`, using `match`.
// Anything else after a backslash is an error: the dictionary entry is wrong
// and the caller logs it. Returns nullopt on such an error.
std::optional<std::string> expandTemplate(const Match& match, const std::string& templateText,
                                          int groupBase);

}  // namespace alus::text
