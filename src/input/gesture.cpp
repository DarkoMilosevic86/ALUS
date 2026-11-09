// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/gesture.hpp"

#include <glib.h>

#include <algorithm>
#include <array>
#include <utility>

namespace alus::input {
namespace {

std::string lowerUtf8(std::string_view text) {
    gchar* lowered = g_utf8_strdown(text.data(), static_cast<gssize>(text.size()));
    std::string result = lowered != nullptr ? lowered : std::string(text);
    g_free(lowered);
    return result;
}

std::vector<std::string> splitPlus(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t plus = text.find('+', start);
        if (plus == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, plus - start));
        start = plus + 1;
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, char separator) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            result.push_back(separator);
        }
        result += parts[i];
    }
    return result;
}

// The one place the imported spelling of the ALUS key is translated. Every
// lookup goes through normalizeGestureIdentifier or parseKeyboardGesture, so
// an imported `kb:NVDA+tab` and ALUS's own `kb:alus+tab` are one gesture, and
// only the second spelling is ever stored, printed or spoken.
std::string canonicalKeyName(std::string part) {
    if (part == KeyNames::kImportedAlusKeyName) {
        return std::string(KeyNames::kAlusKeyName);
    }
    return part;
}

}  // namespace

std::string_view layoutName(Layout layout) {
    return layout == Layout::Laptop ? "laptop" : "desktop";
}

std::optional<Layout> layoutFromName(std::string_view name) {
    if (name == "desktop") {
        return Layout::Desktop;
    }
    if (name == "laptop") {
        return Layout::Laptop;
    }
    return std::nullopt;
}

std::string normalizeGestureIdentifier(std::string_view identifier) {
    const std::string lowered = lowerUtf8(identifier);
    const std::size_t colon = lowered.find(':');
    std::string prefix;
    std::string main = lowered;
    if (colon != std::string::npos) {
        prefix = lowered.substr(0, colon + 1);
        main = lowered.substr(colon + 1);
    }
    std::vector<std::string> parts = splitPlus(main);
    for (std::string& part : parts) {
        part = canonicalKeyName(std::move(part));
    }
    // The order of the parts means nothing to the user but has to be
    // determinate for the map lookup, so it is sorted by character. That is
    // why `alus+shift+t` and `shift+alus+t` are one gesture.
    std::sort(parts.begin(), parts.end());
    return prefix + join(parts, '+');
}

std::optional<ParsedKeyboardGesture> parseKeyboardGesture(std::string_view identifier,
                                                          const KeyNames& names) {
    const std::string lowered = lowerUtf8(identifier);
    const std::size_t colon = lowered.find(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    const std::string prefix = lowered.substr(0, colon);
    ParsedKeyboardGesture parsed;
    if (prefix == "kb") {
        // Layout agnostic: active in BOTH layouts (§5.4).
    } else if (prefix.rfind("kb(", 0) == 0 && prefix.back() == ')') {
        const std::string layoutText = prefix.substr(3, prefix.size() - 4);
        parsed.layout = layoutFromName(layoutText);
        if (!parsed.layout.has_value()) {
            return std::nullopt;
        }
    } else {
        // Not a keyboard gesture at all.
        return std::nullopt;
    }

    for (std::string part : splitPlus(lowered.substr(colon + 1))) {
        if (part.empty()) {
            return std::nullopt;
        }
        part = canonicalKeyName(std::move(part));
        if (const std::optional<Modifier> modifier = names.modifierFor(part)) {
            parsed.modifiers |= *modifier;
            continue;
        }
        if (!parsed.mainKeyName.empty()) {
            // Two non-modifier keys in one identifier: not a keyboard gesture
            // ALUS can grab.
            return std::nullopt;
        }
        parsed.mainKeyName = std::move(part);
    }
    if (parsed.mainKeyName.empty()) {
        return std::nullopt;
    }
    parsed.keysym = names.keysym(parsed.mainKeyName).value_or(0);
    return parsed;
}

std::vector<std::string> Gesture::normalizedIdentifiers() const {
    std::vector<std::string> result;
    for (const std::string& identifier : identifiers()) {
        result.push_back(normalizeGestureIdentifier(identifier));
    }
    return result;
}

std::string Gesture::displayName() const {
    const std::vector<std::string> ids = identifiers();
    if (ids.empty()) {
        return {};
    }
    const std::string& first = ids.front();
    const std::size_t colon = first.find(':');
    return colon == std::string::npos ? first : first.substr(colon + 1);
}

// ---------------------------------------------------------------------------
// KeyboardGesture
// ---------------------------------------------------------------------------

KeyboardGesture::KeyboardGesture(Init init, const KeyNames& names)
    : init_(std::move(init)), names_(&names) {
    if (init_.isAlusKey) {
        // ALUS's own key, in every identifier it builds.
        mainKeyName_ = std::string(KeyNames::kAlusKeyName);
    } else {
        // The key's identity is its level-1 keysym where that is known: shift+2
        // must be "shift+2" and not "shift+@", or a gesture map written for one
        // layout stops matching on another (see KeyMap).
        const std::uint32_t keysym =
            init_.baseKeysym != 0 ? init_.baseKeysym : init_.event.keysym;
        mainKeyName_ = names_->name(keysym, init_.event.unichar);
    }
    modifierNames_ = KeyNames::modifierNames(init_.modifiers);
}

std::vector<std::string> KeyboardGesture::identifiers() const {
    std::vector<std::string> keys = modifierNames_;
    keys.push_back(mainKeyName_);
    const std::string keyName = join(keys, '+');
    // Layout-specific first: it must beat the layout-agnostic form (§5.1).
    return {
        std::string("kb(") + std::string(layoutName(init_.config.layout)) + "):" + keyName,
        "kb:" + keyName,
    };
}

std::string KeyboardGesture::displayName() const {
    std::vector<std::string> labels;
    labels.reserve(modifierNames_.size() + 1);
    for (const std::string& modifier : modifierNames_) {
        labels.push_back(names_->label(modifier));
    }
    labels.push_back(names_->label(mainKeyName_));
    return join(labels, '+');
}

std::optional<std::string> KeyboardGesture::character() const {
    if (init_.event.unichar == 0) {
        return std::nullopt;
    }
    if (g_unichar_isprint(init_.event.unichar) == 0) {
        return std::nullopt;
    }
    std::array<char, 8> buffer{};
    const gint length = g_unichar_to_utf8(init_.event.unichar, buffer.data());
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

bool KeyboardGesture::isCharacter() const {
    // NVDA: a key name longer than one character is a potential command, with
    // space as the one exception.
    if (mainKeyName_ != "space" && g_utf8_strlen(mainKeyName_.c_str(), -1) > 1) {
        return false;
    }
    // Modifiers other than shift make it a command: shift+f is a character,
    // control+f is not.
    const Modifiers withoutShift =
        init_.modifiers & ~static_cast<Modifiers>(Modifier::Shift);
    return withoutShift == 0;
}

bool KeyboardGesture::isModifier() const {
    if (init_.isAlusKey) {
        return true;
    }
    return names_->modifierFor(mainKeyName_).has_value();
}

bool KeyboardGesture::bypassInputHelp() const {
    // #4226 in NVDA: numLock must always be handled normally, or the keyboard
    // controller and the system disagree about its state from then on.
    return mainKeyName_ == "numLock";
}

bool KeyboardGesture::shouldReportAsCommand() const {
    if (names_->isToggle(mainKeyName_)) {
        // #5490: otherwise the key is reported twice, once as a command and
        // once as the state change it caused.
        return false;
    }
    if (mainKeyName_.rfind("volume", 0) == 0) {
        return false;
    }
    if (mainKeyName_.rfind("unknown_", 0) == 0) {
        return false;
    }
    return !isCharacter();
}

SpeechEffect KeyboardGesture::speechEffectWhenExecuted() const {
    if (init_.config.inputHelpActive) {
        return SpeechEffect::Cancel;
    }
    if (mainKeyName_.rfind("volume", 0) == 0 || mainKeyName_.rfind("unknown_", 0) == 0) {
        return SpeechEffect::None;
    }
    const bool isShiftKey = mainKeyName_ == "leftShift" || mainKeyName_ == "rightShift" ||
                            mainKeyName_ == "shift";
    if (!init_.config.speechInterruptForCharacters && (!shouldReportAsCommand() || isShiftKey)) {
        return SpeechEffect::None;
    }
    if (mainKeyName_ == "enter" && !init_.config.speechInterruptForEnter) {
        return SpeechEffect::None;
    }
    if (isShiftKey) {
        // Shift pauses speech and pauses again to resume it — the behaviour
        // users rely on for "hold on, let me think".
        return init_.config.speechIsPaused ? SpeechEffect::Resume : SpeechEffect::Pause;
    }
    return SpeechEffect::Cancel;
}

std::optional<std::string> KeyboardGesture::inputHelpCharacter() const {
    // Commands keep their behaviour even when they also produce a character.
    if (has(init_.modifiers, Modifier::Alus) || hasScript_) {
        return std::nullopt;
    }
    const std::optional<std::string> character = this->character();
    if (!character.has_value()) {
        return std::nullopt;
    }
    // Nothing is gained by saying "t" after saying "t".
    if (displayName() == *character) {
        return std::nullopt;
    }
    return character;
}

}  // namespace alus::input
