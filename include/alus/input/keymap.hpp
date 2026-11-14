// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The keyboard layout: which key is which, independently of shift.
//
// WHY THIS EXISTS AT ALL. NVDA identifies a key by its virtual key code, which
// is a property of the PHYSICAL key: shift+2 is `shift+2`, whatever character
// the layout puts on it. Wayland hands ALUS a keysym at the CURRENT shift
// level instead, so the same key press arrives as `@` on a US layout and as
// `"` on a Serbian one — and a gesture written `kb:nvda+shift+'` (which the
// Serbian locale map really does use, vision.md H8) would never match.
//
// So the level-1 keysym has to be recovered from the keycode. libxkbcommon can
// do that from a keymap built out of the user's active layout, which GNOME
// keeps in GSettings (`org.gnome.desktop.input-sources sources`). This class
// is that lookup, and nothing else.
//
// It is BEST EFFORT by design: without a usable keymap, `baseKeysym()` returns
// nothing and the caller falls back to the keysym the compositor sent. A
// screen reader that refuses to start because it could not read a GSettings
// key would be worse than one whose shifted punctuation bindings are off.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct xkb_context;
struct xkb_keymap;

namespace alus::input {

struct KeyMapOptions {
    // Empty means: $ALUS_XKB_LAYOUT, then GSettings, then $XKB_DEFAULT_LAYOUT,
    // then "us".
    std::string layout;
    std::string variant;
    std::string options;
    std::string model;
    // The compositor reports X11-style keycodes (evdev code + 8), as libatspi
    // has always done. Measured, not assumed — see docs/history/phase-3.md.
    int keycodeOffset = 0;
};

class KeyMap {
public:
    KeyMap();
    ~KeyMap();

    KeyMap(const KeyMap&) = delete;
    KeyMap& operator=(const KeyMap&) = delete;

    // Builds the keymap. Returns false (with `error` filled) when libxkbcommon
    // could not compile one; the object then answers "unknown" to everything.
    bool load(KeyMapOptions options = {}, std::string* error = nullptr);
    bool loaded() const noexcept { return keymap_ != nullptr; }

    // The layout string that was actually used.
    const std::string& layout() const noexcept { return layout_; }

    // The keysym this keycode produces with no modifiers at all — the key's
    // identity, which is what a gesture identifier names.
    std::optional<std::uint32_t> baseKeysym(std::uint32_t keycode) const;

    // The keysym the same key produces with shift held. Needed because a grab
    // is matched by the compositor against the keysym of the event, and it is
    // not yet measured WHICH level that is for a shifted key; registering both
    // costs one entry in the grab set and removes the question.
    std::optional<std::uint32_t> shiftedKeysym(std::uint32_t keycode) const;

    // Same, found by the base keysym instead of the keycode: the path used
    // when building grabs out of gesture identifiers, where no keycode exists.
    std::optional<std::uint32_t> shiftedKeysymFor(std::uint32_t baseKeysym) const;

    std::size_t keyCount() const noexcept { return base_.size(); }

    // Reads the active layout out of GNOME's GSettings. Public because the
    // input service logs it at start-up, and because it is the one part that
    // can legitimately find nothing.
    static std::optional<std::pair<std::string, std::string>> layoutFromGSettings();

private:
    void index();

    xkb_context* context_ = nullptr;
    xkb_keymap* keymap_ = nullptr;
    std::string layout_;
    int keycodeOffset_ = 0;
    // keycode -> level 1 / level 2 keysym
    std::unordered_map<std::uint32_t, std::uint32_t> base_;
    std::unordered_map<std::uint32_t, std::uint32_t> shifted_;
    // level 1 keysym -> level 2 keysym
    std::unordered_map<std::uint32_t, std::uint32_t> baseToShifted_;
};

}  // namespace alus::input
