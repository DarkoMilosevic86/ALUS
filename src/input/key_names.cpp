// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/key_names.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"

#include <glib.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

namespace alus::input {
namespace {

using keysym::kF1;
using keysym::kKp0;

// The named keys, in NVDA's spelling. Everything that is not here is named
// after the character it produces, which covers letters, digits and
// punctuation on every layout.
constexpr std::pair<std::uint32_t, std::string_view> kNamedKeys[] = {
    // Editing and control
    {keysym::kBackSpace, "backspace"},
    {keysym::kTab, "tab"},
    {keysym::kReturn, "enter"},
    {keysym::kEscape, "escape"},
    {keysym::kSpace, "space"},
    {keysym::kDelete, "delete"},
    {0xFF0A, "linefeed"},
    {0xFF0B, "clear"},
    {0xFF62, "select"},
    {0xFF60, "select"},
    {0xFF6A, "help"},
    {0xFF65, "undo"},
    {0xFF66, "redo"},
    {0xFF68, "find"},
    {0xFF69, "break"},

    // Navigation
    {keysym::kHome, "home"},
    {keysym::kLeft, "leftArrow"},
    {keysym::kUp, "upArrow"},
    {keysym::kRight, "rightArrow"},
    {keysym::kDown, "downArrow"},
    {keysym::kPrior, "pageUp"},
    {keysym::kNext, "pageDown"},
    {keysym::kEnd, "end"},
    {keysym::kBegin, "home"},
    {keysym::kInsert, "insert"},
    {keysym::kMenu, "applications"},

    // Locks and system keys
    {keysym::kPrint, "printScreen"},
    {keysym::kPause, "pause"},
    {keysym::kScrollLock, "scrollLock"},
    {keysym::kNumLock, "numLock"},
    {keysym::kCapsLock, "capsLock"},
    {0xFF7E, "IMEModeChange"},  // Mode_switch
    {0xFF20, "IMEConvert"},     // Multi_key as the nearest equivalent
    {0xFF21, "IMEKanji"},

    // Modifiers. NVDA names the sides, and generalizes them when it builds an
    // identifier; both names must exist so that a gesture map can bind either.
    {keysym::kShiftL, "leftShift"},
    {keysym::kShiftR, "rightShift"},
    {keysym::kControlL, "leftControl"},
    {keysym::kControlR, "rightControl"},
    {keysym::kAltL, "leftAlt"},
    {keysym::kAltR, "rightAlt"},
    {keysym::kIsoLevel3Shift, "rightAlt"},
    {keysym::kSuperL, "leftWindows"},
    {keysym::kSuperR, "rightWindows"},
    {keysym::kMetaL, "leftAlt"},
    {keysym::kMetaR, "rightAlt"},

    // Numeric keypad, NumLock OFF (§5.2). This table is the whole reason the
    // numpad commands can work on Linux without a hardware-level hack.
    {keysym::kKpEnter, "numpadEnter"},
    {keysym::kKpHome, "numpad7"},
    {keysym::kKpUp, "numpad8"},
    {keysym::kKpPrior, "numpad9"},
    {keysym::kKpLeft, "numpad4"},
    {keysym::kKpBegin, "numpad5"},
    {keysym::kKpRight, "numpad6"},
    {keysym::kKpEnd, "numpad1"},
    {keysym::kKpDown, "numpad2"},
    {keysym::kKpNext, "numpad3"},
    {keysym::kKpInsert, "numpadInsert"},
    {keysym::kKpDelete, "numpadDelete"},
    {keysym::kKpMultiply, "numpadMultiply"},
    {keysym::kKpAdd, "numpadPlus"},
    {keysym::kKpSeparator, "numpadSeparator"},
    {keysym::kKpSubtract, "numpadMinus"},
    {keysym::kKpDecimal, "numpadDecimal"},
    {keysym::kKpDivide, "numpadDivide"},
    {keysym::kKpEqual, "numpadEquals"},
    {0xFF80, "numpadSpace"},
    {0xFF89, "numpadTab"},

    // Multimedia and browser keys (XF86*), by NVDA's names.
    {0x1008FF12, "volumeMute"},
    {0x1008FF11, "volumeDown"},
    {0x1008FF13, "volumeUp"},
    {0x1008FF17, "mediaNextTrack"},
    {0x1008FF16, "mediaPrevTrack"},
    {0x1008FF15, "mediaStop"},
    {0x1008FF14, "mediaPlayPause"},
    {0x1008FF26, "browserBack"},
    {0x1008FF27, "browserForward"},
    {0x1008FF29, "browserRefresh"},
    {0x1008FF28, "browserStop"},
    {0x1008FF1B, "browserSearch"},
    {0x1008FF30, "browserFavorites"},
    {0x1008FF18, "browserHome"},
    {0x1008FF19, "launchMail"},
    {0x1008FF32, "launchMediaPlayer"},
    {0x1008FF5D, "launchApp1"},  // XF86Explorer
    {0x1008FF1D, "launchApp2"},  // XF86Calculator
    {0x1008FF2F, "sleep"},
};

// Keys that only ever act as modifiers, with the modifier they generalize to.
constexpr std::pair<std::string_view, Modifier> kModifierNames[] = {
    {"shift", Modifier::Shift},
    {"leftshift", Modifier::Shift},
    {"rightshift", Modifier::Shift},
    {"control", Modifier::Control},
    {"leftcontrol", Modifier::Control},
    {"rightcontrol", Modifier::Control},
    {"alt", Modifier::Alt},
    {"leftalt", Modifier::Alt},
    {"rightalt", Modifier::Alt},
    {"windows", Modifier::Windows},
    {"leftwindows", Modifier::Windows},
    {"rightwindows", Modifier::Windows},
    {"alus", Modifier::Alus},
    // Accepted only so that an imported gesture map keeps working (Z3); see
    // KeyNames::kImportedAlusKeyName.
    {"nvda", Modifier::Alus},
};

std::string toLower(std::string_view text) {
    gchar* lowered = g_utf8_strdown(text.data(), static_cast<gssize>(text.size()));
    std::string result = lowered != nullptr ? lowered : std::string(text);
    g_free(lowered);
    return result;
}

std::string encodeUtf8(std::uint32_t codepoint) {
    std::array<char, 8> buffer{};
    const gint length = g_unichar_to_utf8(codepoint, buffer.data());
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

}  // namespace

KeyNames::KeyNames() {
    for (const auto& [code, keyName] : kNamedKeys) {
        // The first spelling wins for keysym -> name (so `KP_Begin` is
        // "numpad5" and `Begin` is "home"), while every spelling is accepted
        // for name -> keysym.
        byKeysym_.emplace(code, std::string(keyName));
        byName_.emplace(toLower(keyName), code);
    }
    addRange(kF1, keysym::kF24, "f", 1);
    addRange(kKp0, keysym::kKp9, "numLockNumpad", 0);
    // Letters and digits: the keysym IS the character for ASCII, so the name
    // is built rather than tabulated.
    for (std::uint32_t c = '0'; c <= '9'; ++c) {
        byName_.emplace(std::string(1, static_cast<char>(c)), c);
    }
    for (std::uint32_t c = 'a'; c <= 'z'; ++c) {
        byName_.emplace(std::string(1, static_cast<char>(c)), c);
    }
    // A gesture identifier cannot contain a literal "+" — it is the
    // separator — so NVDA calls that key "plus" and so does ALUS.
    byName_.emplace("plus", keysym::kPlus);
}

void KeyNames::addRange(std::uint32_t firstKeysym, std::uint32_t lastKeysym,
                        std::string_view prefix, int firstIndex) {
    int index = firstIndex;
    for (std::uint32_t code = firstKeysym; code <= lastKeysym; ++code, ++index) {
        std::string keyName = std::string(prefix) + std::to_string(index);
        byKeysym_.emplace(code, keyName);
        byName_.emplace(toLower(keyName), code);
    }
}

KeyNames& KeyNames::shared() {
    static KeyNames instance;
    return instance;
}

std::string KeyNames::name(std::uint32_t keysym, std::uint32_t unichar) const {
    if (const auto it = byKeysym_.find(keysym); it != byKeysym_.end()) {
        return it->second;
    }
    std::uint32_t codepoint = unichar;
    if (codepoint == 0) {
        codepoint = xkb_keysym_to_utf32(keysym);
    }
    if (codepoint == '+') {
        return "plus";
    }
    // Control characters are not names; a key that produces one (enter, tab,
    // escape) is in the table above under its own name.
    if (codepoint >= ' ' && g_unichar_isprint(codepoint) != 0) {
        return toLower(encodeUtf8(codepoint));
    }
    // #3468 in NVDA: a key the system cannot name still has to be bindable
    // and reportable, so it gets a stable name rather than nothing.
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "unknown_%04x", keysym);
    return std::string(buffer.data());
}

std::optional<std::uint32_t> KeyNames::keysym(std::string_view keyName) const {
    const std::string lowered = toLower(keyName);
    if (const auto it = byName_.find(lowered); it != byName_.end()) {
        return it->second;
    }
    // A single character names itself. This is the path that makes
    // `kb(laptop):nvda+š` work: libxkbcommon turns U+0161 into the LEGACY
    // keysym XK_scaron (0x1b9), which is what the compositor compares a grab
    // against — the Unicode form 0x1000161 would never match.
    if (g_utf8_strlen(lowered.c_str(), -1) == 1) {
        const gunichar codepoint = g_utf8_get_char(lowered.c_str());
        const xkb_keysym_t converted = xkb_utf32_to_keysym(codepoint);
        if (converted != XKB_KEY_NoSymbol) {
            return converted;
        }
    }
    // Last resort: an X11 keysym name ("KP_Home", "XF86AudioMute"). ALUS does
    // not write these, but a hand-edited gestures.ini may.
    const xkb_keysym_t byXkbName =
        xkb_keysym_from_name(std::string(keyName).c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (byXkbName != XKB_KEY_NoSymbol) {
        return byXkbName;
    }
    return std::nullopt;
}

std::string KeyNames::label(std::string_view keyName) const {
    const std::string lowered = toLower(keyName);
    if (const auto it = labels_.find(lowered); it != labels_.end()) {
        // Translated on the way OUT and not on the way in, because the table
        // is loaded once at start-up and [general] language can change
        // afterwards without a restart (§3.9). The msgids are the English
        // labels in data/key-labels.ini, marked in src/i18n/data_strings.cpp.
        return it->second.empty() ? it->second : std::string(_(it->second.c_str()));
    }
    // A letter or a digit: its own name IS the label, and translating it would
    // be wrong -- `kb:alus+t` must say "t" in every language.
    return std::string(keyName);
}

bool KeyNames::isNumpad(std::string_view keyName) const {
    const std::string lowered = toLower(keyName);
    return lowered.rfind("numpad", 0) == 0 || lowered.rfind("numlocknumpad", 0) == 0;
}

bool KeyNames::isToggle(std::string_view keyName) const {
    const std::string lowered = toLower(keyName);
    return lowered == "capslock" || lowered == "numlock" || lowered == "scrolllock";
}

std::optional<Modifier> KeyNames::modifierFor(std::string_view keyName) const {
    const std::string lowered = toLower(keyName);
    for (const auto& [name, modifier] : kModifierNames) {
        if (lowered == name) {
            return modifier;
        }
    }
    return std::nullopt;
}

void KeyNames::setLabel(std::string keyName, std::string labelText) {
    labels_[toLower(keyName)] = std::move(labelText);
}

bool KeyNames::loadLabels(const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        if (error != nullptr) {
            *error = "cannot open " + path.string();
        }
        return false;
    }
    std::size_t loaded = 0;
    std::string line;
    while (std::getline(file, line)) {
        std::string_view view(line);
        while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) {
            view.remove_prefix(1);
        }
        if (view.empty() || view.front() == ';' || view.front() == '#' || view.front() == '[') {
            continue;
        }
        const std::size_t equals = view.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        std::string_view keyName = view.substr(0, equals);
        std::string_view labelText = view.substr(equals + 1);
        const auto trim = [](std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                                     text.back() == '\r')) {
                text.remove_suffix(1);
            }
            return text;
        };
        keyName = trim(keyName);
        labelText = trim(labelText);
        if (keyName.empty() || labelText.empty()) {
            continue;
        }
        setLabel(std::string(keyName), std::string(labelText));
        ++loaded;
    }
    ALUS_DEBUG("key labels: loaded {} from {}", loaded, path.string());
    return loaded > 0;
}

Modifiers KeyNames::modifiersFromState(std::uint32_t state,
                                       std::uint32_t alusVirtualModifierMask) {
    Modifiers modifiers = 0;
    if ((state & keysym::kStateShift) != 0) {
        modifiers |= Modifier::Shift;
    }
    if ((state & keysym::kStateControl) != 0) {
        modifiers |= Modifier::Control;
    }
    if ((state & keysym::kStateMod1) != 0) {
        modifiers |= Modifier::Alt;
    }
    if ((state & keysym::kStateMod4) != 0) {
        modifiers |= Modifier::Windows;
    }
    if ((state & keysym::kStateMod2) != 0) {
        modifiers |= Modifier::NumLock;
    }
    if (alusVirtualModifierMask != 0 && (state & alusVirtualModifierMask) != 0) {
        modifiers |= Modifier::Alus;
    }
    return modifiers;
}

std::vector<std::string> KeyNames::modifierNames(Modifiers modifiers) {
    std::vector<std::string> names;
    // Fixed order; see the header for why it is fixed and NVDA's is not.
    if (has(modifiers, Modifier::Alus)) {
        names.emplace_back(kAlusKeyName);
    }
    if (has(modifiers, Modifier::Control)) {
        names.emplace_back("control");
    }
    if (has(modifiers, Modifier::Alt)) {
        names.emplace_back("alt");
    }
    if (has(modifiers, Modifier::Shift)) {
        names.emplace_back("shift");
    }
    if (has(modifiers, Modifier::Windows)) {
        names.emplace_back("windows");
    }
    if (has(modifiers, Modifier::NumLock)) {
        names.emplace_back("numLock");
    }
    return names;
}

}  // namespace alus::input
