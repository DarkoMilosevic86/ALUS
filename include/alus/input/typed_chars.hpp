// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Character and word echo.
//
// Port of NVDA's `speech.speakTypedCharacters` and its word buffer
// (architecture.md §5.2). Three behaviours matter and all three are here:
//
//   * a typed character is SPELLED, not spoken as text, so that "a" is "a"
//     and not a word;
//   * characters accumulate into a word buffer, and the word is spoken when a
//     non-word character ends it — that is why word echo says "hello" once
//     rather than five letters;
//   * backspace removes the last character from the buffer, so correcting a
//     typo does not corrupt the next word echo.
//
// WHERE THE CHARACTER COMES FROM, on Wayland. NVDA receives a `typedCharacter`
// event from the application. ALUS takes the character the compositor says the
// key produced (the `unichar` field of the KeyEvent). It is the same character
// in every ordinary case, and it is available without asking the application
// anything — but it is what the KEYBOARD produced, not what the application
// inserted, so with an input method in the loop the two can differ. Phase 5
// adds the application side of this for terminals (`Terminal` behaviour), and
// that is the layer where the difference is resolved.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace alus::input {

// NVDA's TypingEcho. "Edit controls only" is the default users expect: a
// screen reader that echoes keys while you are navigating a menu is noise.
enum class TypingEcho { Off, Always, EditControls };

TypingEcho typingEchoFromName(std::string_view name);
std::string_view typingEchoName(TypingEcho echo);

class TypedChars {
public:
    struct Hooks {
        // Speaks a word (as text).
        std::function<void(const std::string&)> speakText;
        // Speaks one character (spelled, with its description rules).
        std::function<void(const std::string&)> speakSpelling;
        // Whether the focus is an editable field; phase 4 answers it.
        std::function<bool()> isFocusEditable;
        // Whether the focus is a password field: then the echo is a
        // placeholder, never the character.
        std::function<bool()> isTypingProtected;
    };

    explicit TypedChars(Hooks hooks = {});

    void setCharacterEcho(TypingEcho echo) noexcept { characterEcho_ = echo; }
    void setWordEcho(TypingEcho echo) noexcept { wordEcho_ = echo; }
    TypingEcho characterEcho() const noexcept { return characterEcho_; }
    TypingEcho wordEcho() const noexcept { return wordEcho_; }

    // One typed character, as UTF-8. Backspace is "\b".
    void onCharacter(const std::string& character);

    void clearWordBuffer();
    const std::string& wordBuffer() const noexcept { return wordBuffer_; }

    // NVDA suppresses the echo of characters a script itself produced, for a
    // short window and a bounded count — the one that stops a command from
    // being read back letter by letter.
    void suppressNextCharacters(std::size_t count);

private:
    bool echoEnabled(TypingEcho echo) const;

    Hooks hooks_;
    TypingEcho characterEcho_ = TypingEcho::EditControls;
    TypingEcho wordEcho_ = TypingEcho::Off;
    std::string wordBuffer_;
    std::size_t suppressCount_ = 0;
};

}  // namespace alus::input
