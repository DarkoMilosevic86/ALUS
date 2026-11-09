// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/typed_chars.hpp"

#include "alus/core/log.hpp"

#include <glib.h>

#include <utility>

namespace alus::input {
namespace {

// NVDA: the password placeholder, so that the rhythm of typing is still
// audible while the characters are not.
constexpr const char* kProtectedCharacter = "*";

bool isWordCharacter(const std::string& character) {
    if (character.empty()) {
        return false;
    }
    const gunichar codepoint = g_utf8_get_char(character.c_str());
    // NVDA: unicodedata.category(ch)[0] in "LMN" — letters, marks and
    // numbers. Marks matter: without them a combining accent would end the
    // word it belongs to.
    return g_unichar_isalnum(codepoint) != 0 || g_unichar_ismark(codepoint) != 0;
}

}  // namespace

TypingEcho typingEchoFromName(std::string_view name) {
    if (name == "always") {
        return TypingEcho::Always;
    }
    if (name == "editControls" || name == "edit-controls") {
        return TypingEcho::EditControls;
    }
    return TypingEcho::Off;
}

std::string_view typingEchoName(TypingEcho echo) {
    switch (echo) {
        case TypingEcho::Always:
            return "always";
        case TypingEcho::EditControls:
            return "editControls";
        case TypingEcho::Off:
            break;
    }
    return "off";
}

TypedChars::TypedChars(Hooks hooks) : hooks_(std::move(hooks)) {}

bool TypedChars::echoEnabled(TypingEcho echo) const {
    switch (echo) {
        case TypingEcho::Off:
            return false;
        case TypingEcho::Always:
            return true;
        case TypingEcho::EditControls:
            return hooks_.isFocusEditable && hooks_.isFocusEditable();
    }
    return false;
}

void TypedChars::onCharacter(const std::string& character) {
    if (character.empty()) {
        return;
    }
    const bool protectedTyping = hooks_.isTypingProtected && hooks_.isTypingProtected();
    const std::string spoken = protectedTyping ? kProtectedCharacter : character;

    if (character == "\b") {
        // Backspace: drop the last character of the buffer, so the word echo
        // reflects what is actually in the field.
        if (!wordBuffer_.empty()) {
            const char* last = g_utf8_find_prev_char(wordBuffer_.c_str(),
                                                     wordBuffer_.c_str() + wordBuffer_.size());
            if (last != nullptr) {
                wordBuffer_.resize(static_cast<std::size_t>(last - wordBuffer_.c_str()));
            } else {
                wordBuffer_.clear();
            }
        }
    } else if (character == "\x7f") {
        // The character control+backspace produces in some applications; NVDA
        // ignores it rather than treating it as a word boundary.
        return;
    } else if (isWordCharacter(character)) {
        wordBuffer_ += spoken;
    } else if (!wordBuffer_.empty()) {
        const std::string word = wordBuffer_;
        clearWordBuffer();
        ALUS_IO("typed word: {}", word);
        if (!protectedTyping && echoEnabled(wordEcho_) && hooks_.speakText) {
            hooks_.speakText(word);
        }
    }

    bool suppress = false;
    if (suppressCount_ > 0) {
        --suppressCount_;
        suppress = true;
    }
    if (suppress) {
        return;
    }
    // Control characters are not echoed: the key that produced them is
    // reported as a command instead.
    const gunichar codepoint = g_utf8_get_char(character.c_str());
    if (codepoint < ' ') {
        return;
    }
    if (echoEnabled(characterEcho_) && hooks_.speakSpelling) {
        hooks_.speakSpelling(spoken);
    }
}

void TypedChars::clearWordBuffer() { wordBuffer_.clear(); }

void TypedChars::suppressNextCharacters(std::size_t count) { suppressCount_ = count; }

}  // namespace alus::input
