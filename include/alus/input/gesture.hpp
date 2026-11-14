// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// One piece of input from the user.
//
// Port of NVDA's `inputCore.InputGesture` and
// `keyboardHandler.KeyboardInputGesture` (architecture.md §5.1, §5.2). This
// is the layer users actually know — the key names, the identifier syntax and
// the normalization rules — so it is ported 1:1 and not improved upon.
//
// IDENTIFIERS. `source:keys`, e.g. `kb:alus+shift+t`, `kb(laptop):alus+a`,
// `br(alva.bc680):t1+t2`, `bk:dot7+dot8`. A gesture returns an ORDERED list of
// identifiers and the first one that is bound wins, which is how a
// layout-specific binding beats a layout-agnostic one.
//
// NORMALIZATION. Lower case, then the `+`-separated parts after the source
// prefix are SORTED. Without the sort, `alus+shift+t` and `shift+alus+t` are
// different strings for the same key press. `+` never appears as a key name;
// the literal key is called `plus`. Normalization is also where an imported
// map's spelling of the ALUS key is canonicalised (KeyNames::kAlusKeyName).
#pragma once

#include "alus/input/key_names.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::input {

// NVDA's keyboard layouts. The names are part of the gesture syntax
// (`kb(desktop):`), so they are not an enum detail.
enum class Layout { Desktop, Laptop };

std::string_view layoutName(Layout layout);
std::optional<Layout> layoutFromName(std::string_view name);

// What executing a gesture does to speech in flight (NVDA:
// `SPEECHEFFECT_*`). Cancel is the default: a key press stops the talking.
enum class SpeechEffect { None, Cancel, Pause, Resume };

// Normalizes a gesture identifier: lower case, then the `+`-separated parts
// after the source prefix sorted by character.
//
// Deviation from NVDA, deliberate: NVDA's `normalizeGestureIdentifier` raises
// if there is no `:` in the identifier. ALUS treats a prefixless identifier as
// a bare key list and normalizes it anyway, because the function is also how
// ALUS compares two identifiers in tests and in the gesture dialog, and
// crashing on "shift+t" would make it useless for both.
std::string normalizeGestureIdentifier(std::string_view identifier);

// A keyboard gesture identifier taken apart: what `kb(laptop):nvda+shift+t`
// means. Used to build the grab set out of the maps, to answer "which key is
// this" in the gesture dialog, and by `alus-cli gesture`.
struct ParsedKeyboardGesture {
    Modifiers modifiers = 0;
    std::string mainKeyName;
    // Set only for a layout-specific identifier (`kb(laptop):`).
    std::optional<Layout> layout;
    // 0 when no keyboard can produce that key name.
    std::uint32_t keysym = 0;
};

// Returns nothing when the identifier is not a keyboard gesture (`br:`, `bk:`)
// or has no single main key.
std::optional<ParsedKeyboardGesture> parseKeyboardGesture(
    std::string_view identifier, const KeyNames& names = KeyNames::shared());

// One key event as the platform delivers it. The fields are exactly what the
// compositor's `KeyEvent` signal carries (`b released, u state, u keysym,
// u unichar, q keycode`), so nothing is invented on the way in.
struct KeyEvent {
    bool pressed = true;
    std::uint32_t keycode = 0;
    std::uint32_t keysym = 0;
    std::uint32_t state = 0;    // X11 modifier state mask
    std::uint32_t unichar = 0;  // the character produced, 0 if none
    // True when this event arrived because ALUS asked for this key
    // specifically (a grab) rather than through the watcher. With the
    // KeyboardMonitor backend the compositor has ALREADY consumed a grabbed
    // key by the time it tells us, which is the central difference from
    // NVDA's hook model — see IInputBackend.
    bool fromGrab = false;
};

class Gesture {
public:
    virtual ~Gesture() = default;

    Gesture(const Gesture&) = delete;
    Gesture& operator=(const Gesture&) = delete;

    // Identifiers in priority order; the first bound one wins.
    virtual std::vector<std::string> identifiers() const = 0;
    // The same, normalized. Not virtual: subclasses must not change the rules.
    std::vector<std::string> normalizedIdentifiers() const;

    // What input help and "speak command keys" say.
    virtual std::string displayName() const;

    // A key press that types a character rather than issuing a command.
    virtual bool isCharacter() const { return false; }
    // Whether "speak command keys" should report this gesture.
    virtual bool shouldReportAsCommand() const { return true; }
    virtual SpeechEffect speechEffectWhenExecuted() const { return SpeechEffect::Cancel; }
    // A gesture that is only a modifier: no script is looked up for it.
    virtual bool isModifier() const { return false; }
    // Handled as if input help were off (NVDA: numLock must always work).
    virtual bool bypassInputHelp() const { return false; }
    // False only for flooding gestures (touch hovers); ALUS has none in v1.0.
    virtual bool reportInInputHelp() const { return true; }
    // An extra character input help should report beyond the key name.
    virtual std::optional<std::string> inputHelpCharacter() const { return std::nullopt; }
    // Report toggle state and the like, just after command reporting.
    virtual void reportExtra() {}

    // Send this gesture to the system. On Wayland this is NOT POSSIBLE through
    // AT-SPI: `atspi_generate_keyboard_event` is a no-op there (finding N18),
    // so the base class returns false and the callers must cope rather than
    // assume success. A privileged /dev/uinput helper is the v1.1 answer.
    virtual bool send() { return false; }

    // Set by the manager when say-all was running as this gesture arrived.
    bool wasInSayAll = false;

protected:
    Gesture() = default;
};

// What the [keyboard] configuration section decides about a key press.
struct KeyboardGestureConfig {
    Layout layout = Layout::Desktop;
    bool speechInterruptForCharacters = true;
    bool speechInterruptForEnter = true;
    // Whether speech is paused right now: shift resumes it instead of pausing.
    bool speechIsPaused = false;
    // Input help changes the speech effect to Cancel unconditionally.
    bool inputHelpActive = false;
};

class KeyboardGesture final : public Gesture {
public:
    struct Init {
        KeyEvent event;
        // Generalized modifiers held down, ALUS key included.
        Modifiers modifiers = 0;
        // This key press IS the ALUS key (insert / numpad insert / caps lock).
        bool isAlusKey = false;
        // The keysym identifying the physical key, independent of shift level
        // (KeyMap::baseKeysym). Zero means "use the event's keysym".
        std::uint32_t baseKeysym = 0;
        KeyboardGestureConfig config;
    };

    KeyboardGesture(Init init, const KeyNames& names = KeyNames::shared());

    const KeyEvent& event() const noexcept { return init_.event; }
    Modifiers modifiers() const noexcept { return init_.modifiers; }
    bool isAlusKey() const noexcept { return init_.isAlusKey; }
    Layout layout() const noexcept { return init_.config.layout; }

    // "t", "numpad7", "alus" — the key without its modifiers.
    const std::string& mainKeyName() const noexcept { return mainKeyName_; }
    const std::vector<std::string>& modifierNames() const noexcept { return modifierNames_; }
    // The key's identity for trapped-key bookkeeping: a consumed key down must
    // be matched by a consumed key up, or applications see an unbalanced key
    // state (§5.2).
    std::uint32_t keyIdentity() const noexcept { return init_.event.keycode; }

    std::vector<std::string> identifiers() const override;
    std::string displayName() const override;
    bool isCharacter() const override;
    bool shouldReportAsCommand() const override;
    SpeechEffect speechEffectWhenExecuted() const override;
    bool isModifier() const override;
    bool bypassInputHelp() const override;
    std::optional<std::string> inputHelpCharacter() const override;

    // The character this key press produces, if any.
    std::optional<std::string> character() const;

    // Whether a script was found for this gesture; set by the manager and used
    // by `inputHelpCharacter()`, exactly as NVDA's `_get_inputHelpCharacter`
    // consults `self.script`.
    void setHasScript(bool value) noexcept { hasScript_ = value; }

private:
    Init init_;
    const KeyNames* names_;
    std::string mainKeyName_;
    std::vector<std::string> modifierNames_;
    bool hasScript_ = false;
};

}  // namespace alus::input
