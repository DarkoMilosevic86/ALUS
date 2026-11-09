// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/text/regex.hpp"

#include "alus/core/log.hpp"

#include <glib.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace alus::text {

namespace {

// G_REGEX_OPTIMIZE asks PCRE2 to JIT-compile the pattern. That matters: the
// symbol regular expression runs over every utterance, so it is on the path
// from a key press to speech.
//
// The JIT's generated code reads the subject in 16-byte chunks and can read a
// few bytes past the end of the allocation, and it has no symbols for the
// unwinder, so Memcheck reports it as "invalid read" and "conditional jump on
// uninitialised value" with unnamed frames. Those reports are PCRE2's, not
// ALUS's, and suppressing them by frame is impossible — JIT code has no object
// file to name. So the JIT is turned OFF when ALUS_REGEX_NO_JIT is set, and
// meson's valgrind test setup sets it: the memory checker then sees only
// ALUS's own behaviour, and production keeps the JIT.
bool jitEnabled() {
    static const bool enabled = std::getenv("ALUS_REGEX_NO_JIT") == nullptr;
    return enabled;
}

GRegexCompileFlags compileFlags(RegexOption options) {
    unsigned flags = jitEnabled() ? static_cast<unsigned>(G_REGEX_OPTIMIZE) : 0U;
    if (has(options, RegexOption::CaseInsensitive)) {
        flags |= G_REGEX_CASELESS;
    }
    if (has(options, RegexOption::DotAll)) {
        flags |= G_REGEX_DOTALL;
    }
    if (has(options, RegexOption::Multiline)) {
        flags |= G_REGEX_MULTILINE;
    }
    if (has(options, RegexOption::Raw)) {
        flags |= G_REGEX_RAW;
    }
    return static_cast<GRegexCompileFlags>(flags);
}

// The names of the capturing groups, in order of appearance. PCRE2 can report
// the name table, but GRegex does not expose it, so the pattern is scanned for
// the three spellings PCRE2 accepts: (?P<name>, (?<name> and (?'name'.
// Lookbehind — (?<= and (?<! — is not a group and is skipped.
std::vector<std::string> extractGroupNames(std::string_view pattern) {
    std::vector<std::string> names;
    for (std::size_t i = 0; i + 2 < pattern.size(); ++i) {
        if (pattern[i] != '(' || pattern[i + 1] != '?') {
            continue;
        }
        if (i > 0 && pattern[i - 1] == '\\') {
            continue;  // an escaped parenthesis
        }
        std::size_t nameStart = 0;
        char terminator = '\0';
        if (pattern[i + 2] == 'P' && i + 3 < pattern.size() && pattern[i + 3] == '<') {
            nameStart = i + 4;
            terminator = '>';
        } else if (pattern[i + 2] == '<' && i + 3 < pattern.size() && pattern[i + 3] != '=' &&
                   pattern[i + 3] != '!') {
            nameStart = i + 3;
            terminator = '>';
        } else if (pattern[i + 2] == '\'') {
            nameStart = i + 3;
            terminator = '\'';
        } else {
            continue;
        }
        const std::size_t nameEnd = pattern.find(terminator, nameStart);
        if (nameEnd == std::string_view::npos) {
            continue;
        }
        std::string name{pattern.substr(nameStart, nameEnd - nameStart)};
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

}  // namespace

// ---------------------------------------------------------------------------
// Match
// ---------------------------------------------------------------------------

std::string Match::group(int number) const {
    gchar* text = g_match_info_fetch(info_, number);
    if (text == nullptr) {
        return {};
    }
    std::string out{text};
    g_free(text);
    return out;
}

std::string Match::group(const std::string& name) const {
    gchar* text = g_match_info_fetch_named(info_, name.c_str());
    if (text == nullptr) {
        return {};
    }
    std::string out{text};
    g_free(text);
    return out;
}

bool Match::participated(int number) const { return start(number) >= 0; }

bool Match::participated(const std::string& name) const {
    const int number = regex_.groupNumber(name);
    return number >= 0 && participated(number);
}

std::ptrdiff_t Match::start(int number) const {
    gint from = -1;
    gint to = -1;
    if (g_match_info_fetch_pos(info_, number, &from, &to) == FALSE) {
        return -1;
    }
    return from;
}

std::ptrdiff_t Match::end(int number) const {
    gint from = -1;
    gint to = -1;
    if (g_match_info_fetch_pos(info_, number, &from, &to) == FALSE) {
        return -1;
    }
    return to;
}

int Match::lastGroup() const {
    const int count = g_match_info_get_match_count(info_);
    for (int number = count - 1; number >= 1; --number) {
        if (participated(number)) {
            return number;
        }
    }
    return 0;
}

std::string Match::lastGroupName() const {
    std::string best;
    int bestNumber = -1;
    for (const std::string& name : regex_.groupNames()) {
        const int number = regex_.groupNumber(name);
        if (number < 0 || !participated(number)) {
            continue;
        }
        if (number > bestNumber) {
            bestNumber = number;
            best = name;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Regex
// ---------------------------------------------------------------------------

Regex::~Regex() {
    if (regex_ != nullptr) {
        g_regex_unref(regex_);
    }
}

Regex::Regex(Regex&& other) noexcept
    : regex_(std::exchange(other.regex_, nullptr)),
      pattern_(std::move(other.pattern_)),
      groupNames_(std::move(other.groupNames_)) {}

Regex& Regex::operator=(Regex&& other) noexcept {
    if (this != &other) {
        if (regex_ != nullptr) {
            g_regex_unref(regex_);
        }
        regex_ = std::exchange(other.regex_, nullptr);
        pattern_ = std::move(other.pattern_);
        groupNames_ = std::move(other.groupNames_);
    }
    return *this;
}

bool Regex::compile(std::string_view pattern, RegexOption options, std::string* error) {
    if (regex_ != nullptr) {
        g_regex_unref(regex_);
        regex_ = nullptr;
    }
    pattern_.assign(pattern);
    groupNames_.clear();
    GError* gerror = nullptr;
    regex_ = g_regex_new(pattern_.c_str(), compileFlags(options),
                         static_cast<GRegexMatchFlags>(0), &gerror);
    if (regex_ == nullptr) {
        if (error != nullptr) {
            *error = gerror != nullptr ? gerror->message : "invalid pattern";
        }
        if (gerror != nullptr) {
            g_error_free(gerror);
        }
        return false;
    }
    groupNames_ = extractGroupNames(pattern_);
    return true;
}

int Regex::groupNumber(const std::string& name) const {
    if (regex_ == nullptr) {
        return -1;
    }
    return g_regex_get_string_number(regex_, name.c_str());
}

bool Regex::matches(const std::string& subject) const {
    if (regex_ == nullptr) {
        return false;
    }
    return g_regex_match(regex_, subject.c_str(), static_cast<GRegexMatchFlags>(0), nullptr) !=
           FALSE;
}

bool Regex::search(const std::string& subject,
                   const std::function<void(const Match&)>& onMatch) const {
    if (regex_ == nullptr) {
        return false;
    }
    GMatchInfo* info = nullptr;
    const gboolean matched =
        g_regex_match_full(regex_, subject.c_str(), static_cast<gssize>(subject.size()), 0,
                           static_cast<GRegexMatchFlags>(0), &info, nullptr);
    bool result = false;
    if (matched != FALSE) {
        result = true;
        if (onMatch) {
            const Match match{*this, subject, info};
            onMatch(match);
        }
    }
    if (info != nullptr) {
        g_match_info_free(info);
    }
    return result;
}

std::string Regex::replace(const std::string& subject,
                           const std::function<std::string(const Match&)>& replacer) const {
    if (regex_ == nullptr || !replacer) {
        return subject;
    }
    std::string out;
    out.reserve(subject.size());
    std::size_t position = 0;
    while (position <= subject.size()) {
        GMatchInfo* info = nullptr;
        const gboolean matched = g_regex_match_full(
            regex_, subject.c_str(), static_cast<gssize>(subject.size()),
            static_cast<gint>(position), static_cast<GRegexMatchFlags>(0), &info, nullptr);
        if (matched == FALSE) {
            if (info != nullptr) {
                g_match_info_free(info);
            }
            break;
        }
        gint from = 0;
        gint to = 0;
        g_match_info_fetch_pos(info, 0, &from, &to);
        const auto matchStart = static_cast<std::size_t>(from);
        auto matchEnd = static_cast<std::size_t>(to);
        out.append(subject, position, matchStart - position);
        {
            const Match match{*this, subject, info};
            out += replacer(match);
        }
        g_match_info_free(info);

        if (matchEnd == matchStart) {
            // An empty match: copy one character and move past it, or a pattern
            // that can match nothing would spin here forever.
            if (matchEnd >= subject.size()) {
                position = matchEnd;
                break;
            }
            const char* next = g_utf8_next_char(subject.c_str() + matchEnd);
            const auto step = static_cast<std::size_t>(next - (subject.c_str() + matchEnd));
            out.append(subject, matchEnd, step);
            matchEnd += step;
        }
        position = matchEnd;
    }
    if (position < subject.size()) {
        out.append(subject, position, std::string::npos);
    }
    return out;
}

std::string Regex::replaceWithTemplate(const std::string& subject,
                                       const std::string& replacement) const {
    return replace(subject, [&replacement](const Match& match) {
        const std::optional<std::string> expanded = expandTemplate(match, replacement, 0);
        return expanded.value_or(std::string{});
    });
}

std::string Regex::escape(std::string_view literal) {
    gchar* escaped = g_regex_escape_string(literal.data(), static_cast<gint>(literal.size()));
    std::string out{escaped != nullptr ? escaped : ""};
    if (escaped != nullptr) {
        g_free(escaped);
    }
    return out;
}

std::optional<std::string> expandTemplate(const Match& match, const std::string& templateText,
                                          int groupBase) {
    std::string out;
    bool inEscape = false;
    for (const char character : templateText) {
        if (!inEscape) {
            if (character == '\\') {
                inEscape = true;
            } else {
                out += character;
            }
            continue;
        }
        inEscape = false;
        if (character == '\\') {
            out += '\\';
        } else if (character >= '0' && character <= '9') {
            const int number = groupBase + (character - '0');
            out += match.group(number);
        } else {
            ALUS_ERROR("invalid group reference \\{} in a replacement", character);
            return std::nullopt;
        }
    }
    if (inEscape) {
        ALUS_ERROR("unterminated backslash in a replacement");
        return std::nullopt;
    }
    return out;
}

}  // namespace alus::text
