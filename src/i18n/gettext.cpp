// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/i18n/gettext.hpp"

#include "alus/core/log.hpp"
#include "alus/core/paths.hpp"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <libintl.h>
#include <mutex>

#ifndef ALUS_LOCALE_DIR
#define ALUS_LOCALE_DIR ""
#endif

#ifdef __GLIBC__
extern "C" int _nl_msg_cat_cntr;
#endif

namespace alus::i18n {
namespace {

// The gettext domain. Also the .mo file name, and the name the `alus-pot`
// target writes, so it appears in exactly one more place: po/meson.build.
constexpr const char* kDomain = "alus";

struct State {
    std::string language;
    std::filesystem::path localeDir;
    bool bound = false;
    bool haveCatalog = false;
};

State& state() {
    static State value;
    return value;
}

// Where the catalogues are. Same four-step rule as core::findDataDirectory,
// but the marker cannot be a fixed file name -- which language is installed is
// not known in advance -- so the marker is "the directory exists and holds at
// least one LC_MESSAGES/alus.mo".
bool holdsCatalog(const std::filesystem::path& dir) {
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator{dir, code}) {
        if (entry.is_directory(code) &&
            std::filesystem::exists(entry.path() / "LC_MESSAGES" / "alus.mo", code)) {
            return true;
        }
    }
    return false;
}

std::filesystem::path resolveLocaleDirectory(const std::filesystem::path& preferred) {
    if (!preferred.empty()) {
        return preferred;
    }
    if (const char* fromEnvironment = std::getenv("ALUS_LOCALE_DIR")) {
        const std::filesystem::path candidate{fromEnvironment};
        if (holdsCatalog(candidate)) {
            return candidate;
        }
        ALUS_WARNING("ALUS_LOCALE_DIR={} holds no alus.mo", fromEnvironment);
    }
    if (const std::filesystem::path installed{ALUS_LOCALE_DIR}; holdsCatalog(installed)) {
        return installed;
    }
    // An uninstalled build: meson compiles the catalogues into
    // <builddir>/po/<lang>/LC_MESSAGES/alus.mo, which is this layout already.
    for (const std::filesystem::path candidate : {"po", "../po", "build/po", "../build/po"}) {
        if (holdsCatalog(candidate)) {
            return candidate;
        }
    }
    // Nothing found. Returning the configured location rather than nothing
    // keeps the start-up log useful: it says where ALUS LOOKED.
    return std::filesystem::path{ALUS_LOCALE_DIR};
}

// The language actually in effect, in the order of authority: an explicit
// request, then $LANGUAGE (which gettext itself honours first), then the
// LC_MESSAGES locale.
std::string effectiveLanguage(const std::string& requested) {
    if (!requested.empty()) {
        return requested;
    }
    for (const char* name : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
            std::string_view text{value};
            // "sr_RS.UTF-8:sr" -> "sr_RS.UTF-8"; "sr_RS.UTF-8" -> "sr_RS.UTF-8".
            if (const auto colon = text.find(':'); colon != std::string_view::npos) {
                text = text.substr(0, colon);
            }
            if (text == "C" || text == "POSIX") {
                return "en";
            }
            return std::string{text};
        }
    }
    return "en";
}

// "sr_RS.UTF-8@latin" -> "sr_RS", "sr". The directory names a catalogue can
// live under, most specific first; this is gettext's own search order, redone
// here only to ANSWER WHETHER a catalogue exists (haveCatalog), never to load
// one -- loading stays libintl's job.
std::vector<std::string> catalogCandidates(std::string_view language) {
    std::vector<std::string> candidates;
    std::string base{language};
    if (const auto at = base.find('@'); at != std::string::npos) {
        candidates.push_back(base.substr(0, base.find('.')) + base.substr(at));
        base = base.substr(0, at);
    }
    if (const auto dot = base.find('.'); dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    candidates.push_back(base);
    if (const auto underscore = base.find('_'); underscore != std::string::npos) {
        candidates.push_back(base.substr(0, underscore));
    }
    return candidates;
}

}  // namespace

const std::string& initialize(const Options& options) {
    State& current = state();

    // 1. The locale. Without this the catalogue is never consulted, however
    //    correctly it is bound.
    //
    //    The order matters: LC_ALL first so that collation, character class
    //    tables and GTK all follow the user's locale, and LC_NUMERIC pinned
    //    back to "C" immediately after. ALUS writes doubles into GKeyFile
    //    (speech rate, pitch, beepMinHz); g_key_file_set_double is
    //    locale-independent but std::to_chars-era printf paths in third-party
    //    code are not, and a configuration file holding "1,5" does not load.
    //    liblouis table parsing and the .dic parsers have the same exposure.
    if (std::setlocale(LC_ALL, "") == nullptr) {
        ALUS_WARNING("setlocale failed; staying in the C locale");
    }
    std::setlocale(LC_NUMERIC, "C");

    current.language = effectiveLanguage(options.language);
    // An explicitly requested language has to reach libintl, and the only
    // portable way is the environment: gettext reads $LANGUAGE itself, and
    // nothing else overrides an already-bound domain.
    if (!options.language.empty()) {
        ::setenv("LANGUAGE", current.language.c_str(), 1);
#ifdef __GLIBC__
        // glibc caches the evaluated language list per domain, so a bare
        // bindtextdomain after a $LANGUAGE change does not drop it and the
        // first language stays in effect for the life of the process.
        // Bumping _nl_msg_cat_cntr is the documented way to invalidate that
        // cache (gettext manual, "Being a gettext grok"), and it is what lets
        // changing [general] language take effect without a restart -- which
        // §3.9 requires of every subsystem.
        ++_nl_msg_cat_cntr;
#endif
    }

    current.localeDir = resolveLocaleDirectory(options.localeDir);

    // 2. Bind the domain, and 3. pin the catalogue's charset to UTF-8.
    //    Without the second call gettext transcodes to the locale's charset,
    //    so a `sr_RS` locale with no `.UTF-8` turns Cyrillic into mojibake on
    //    the braille display and in every GTK label.
    if (::bindtextdomain(kDomain, current.localeDir.c_str()) == nullptr) {
        ALUS_WARNING("bindtextdomain({}) failed", current.localeDir.string());
    }
    ::bind_textdomain_codeset(kDomain, "UTF-8");
    ::textdomain(kDomain);
    current.bound = true;

    std::error_code code;
    current.haveCatalog = false;
    for (const std::string& candidate : catalogCandidates(current.language)) {
        if (std::filesystem::exists(
                current.localeDir / candidate / "LC_MESSAGES" / "alus.mo", code)) {
            current.haveCatalog = true;
            break;
        }
    }

    if (current.haveCatalog) {
        ALUS_INFO("language {}, catalogue from {}", current.language,
                  current.localeDir.string());
    } else if (catalogCandidates(current.language).back() == "en") {
        ALUS_INFO("language en, no catalogue needed");
    } else {
        // Not an error, and deliberately not a warning sound (§3.8): ALUS
        // speaks correct English, which is a degraded but working screen
        // reader. It IS logged, because "why is my Serbian screen reader
        // speaking English" has to be answerable from the log.
        ALUS_INFO("no catalogue for language {} under {}; speaking English",
                  current.language, current.localeDir.string());
    }
    return current.language;
}

const char* translate(const char* msgid) {
    if (msgid == nullptr || *msgid == '\0') {
        // An empty msgid addresses the catalogue's own header in gettext, which
        // would return the metadata block. Never let that reach the user.
        return "";
    }
    if (!state().bound) {
        return msgid;
    }
    return ::dgettext(kDomain, msgid);
}

const char* translatePlural(const char* singular, const char* plural, unsigned long count) {
    if (singular == nullptr || plural == nullptr) {
        return "";
    }
    if (!state().bound) {
        return count == 1 ? singular : plural;
    }
    return ::dngettext(kDomain, singular, plural, count);
}

const char* translateContext(const char* context, const char* msgid) {
    if (msgid == nullptr || *msgid == '\0') {
        return "";
    }
    if (context == nullptr || !state().bound) {
        return msgid;
    }
    // pgettext is a macro over a "context\004msgid" lookup; doing it by hand
    // keeps the context runtime-variable, which the GUI needs (a page's title
    // carries its own context).
    std::string key;
    key.reserve(std::strlen(context) + std::strlen(msgid) + 1);
    key.append(context).push_back('\004');
    key.append(msgid);
    const char* found = ::dgettext(kDomain, key.c_str());
    // dgettext returns the KEY when there is no translation, separator and all.
    return found == key.c_str() || std::strcmp(found, key.c_str()) == 0 ? msgid : found;
}

const std::string& currentLanguage() { return state().language; }

bool haveCatalog() { return state().haveCatalog; }

const std::filesystem::path& localeDirectory() { return state().localeDir; }

std::vector<std::string> availableLanguages() {
    std::vector<std::string> languages;
    std::error_code code;
    const std::filesystem::path& dir = state().localeDir;
    for (const auto& entry : std::filesystem::directory_iterator{dir, code}) {
        if (entry.is_directory(code) &&
            std::filesystem::exists(entry.path() / "LC_MESSAGES" / "alus.mo", code)) {
            languages.push_back(entry.path().filename().string());
        }
    }
    std::sort(languages.begin(), languages.end());
    return languages;
}

std::string substitute(std::string_view pattern, std::string_view value) {
    const auto at = pattern.find("%s");
    if (at == std::string_view::npos) {
        // The translation lost its placeholder. Wrong word order beats a throw.
        std::string result{pattern};
        if (!result.empty() && !value.empty()) {
            result.push_back(' ');
        }
        result.append(value);
        return result;
    }
    std::string result{pattern.substr(0, at)};
    result.append(value);
    result.append(pattern.substr(at + 2));
    return result;
}

std::string substituteNumber(std::string_view pattern, std::string_view value) {
    for (const std::string_view placeholder : {"%d", "%s"}) {
        const auto at = pattern.find(placeholder);
        if (at == std::string_view::npos) {
            continue;
        }
        std::string result{pattern.substr(0, at)};
        result.append(value);
        result.append(pattern.substr(at + placeholder.size()));
        return result;
    }
    // The translation lost its placeholder. Wrong word order beats a throw.
    std::string result{pattern};
    if (!result.empty() && !value.empty()) {
        result.push_back(' ');
    }
    result.append(value);
    return result;
}

std::string substituteNamed(std::string_view pattern, std::string_view name,
                            std::string_view value) {
    std::string placeholder;
    placeholder.reserve(name.size() + 2);
    placeholder.push_back('{');
    placeholder.append(name);
    placeholder.push_back('}');
    const auto at = pattern.find(placeholder);
    if (at == std::string_view::npos) {
        // The translation lost its placeholder. Wrong word order beats a throw.
        std::string result{pattern};
        if (!result.empty() && !value.empty()) {
            result.push_back(' ');
        }
        result.append(value);
        return result;
    }
    std::string result{pattern.substr(0, at)};
    result.append(value);
    result.append(pattern.substr(at + placeholder.size()));
    return result;
}

std::string substitute(std::string_view pattern, std::string_view first,
                       std::string_view second) {
    // NOT substitute(substitute(...)): if `first` itself contains "%s" -- an
    // object's name is user data and may contain anything -- the second pass
    // would substitute INTO the value. So both placeholders are found in the
    // pattern before anything is inserted.
    const auto one = pattern.find("%s");
    if (one == std::string_view::npos) {
        return substitute(substitute(pattern, first), second);
    }
    const auto two = pattern.find("%s", one + 2);
    if (two == std::string_view::npos) {
        std::string result = substitute(pattern, first);
        if (!result.empty() && !second.empty()) {
            result.push_back(' ');
        }
        result.append(second);
        return result;
    }
    std::string result{pattern.substr(0, one)};
    result.append(first);
    result.append(pattern.substr(one + 2, two - one - 2));
    result.append(second);
    result.append(pattern.substr(two + 2));
    return result;
}

}  // namespace alus::i18n
