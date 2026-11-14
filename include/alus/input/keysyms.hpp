// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The X11 keysym constants ALUS names in its own code.
//
// These are protocol constants from `X11/keysymdef.h`: stable for decades,
// and what both `libatspi` and the compositor's KeyboardMonitor signal speak
// (architecture.md §5.2). They are repeated here rather than included so
// that the input layer does not pull `x11proto-dev` into the build on a
// Wayland-only system. Everything that needs a NAME for a keysym, or a keysym
// for a character, goes through `KeyNames`, which uses libxkbcommon — the same
// library the compositor uses, so the two always agree.
#pragma once

#include <cstdint>

namespace alus::input::keysym {

// Editing and control
inline constexpr std::uint32_t kBackSpace = 0xFF08;
inline constexpr std::uint32_t kTab = 0xFF09;
inline constexpr std::uint32_t kReturn = 0xFF0D;
inline constexpr std::uint32_t kEscape = 0xFF1B;
inline constexpr std::uint32_t kSpace = 0x0020;
inline constexpr std::uint32_t kPlus = 0x002B;
inline constexpr std::uint32_t kDelete = 0xFFFF;

// Navigation
inline constexpr std::uint32_t kHome = 0xFF50;
inline constexpr std::uint32_t kLeft = 0xFF51;
inline constexpr std::uint32_t kUp = 0xFF52;
inline constexpr std::uint32_t kRight = 0xFF53;
inline constexpr std::uint32_t kDown = 0xFF54;
inline constexpr std::uint32_t kPrior = 0xFF55;  // page up
inline constexpr std::uint32_t kNext = 0xFF56;   // page down
inline constexpr std::uint32_t kEnd = 0xFF57;
inline constexpr std::uint32_t kBegin = 0xFF58;
inline constexpr std::uint32_t kInsert = 0xFF63;
inline constexpr std::uint32_t kMenu = 0xFF67;  // applications key

// Locks and system keys
inline constexpr std::uint32_t kPrint = 0xFF61;
inline constexpr std::uint32_t kPause = 0xFF13;
inline constexpr std::uint32_t kScrollLock = 0xFF14;
inline constexpr std::uint32_t kNumLock = 0xFF7F;
inline constexpr std::uint32_t kCapsLock = 0xFFE5;

// Modifiers
inline constexpr std::uint32_t kShiftL = 0xFFE1;
inline constexpr std::uint32_t kShiftR = 0xFFE2;
inline constexpr std::uint32_t kControlL = 0xFFE3;
inline constexpr std::uint32_t kControlR = 0xFFE4;
inline constexpr std::uint32_t kAltL = 0xFFE9;
inline constexpr std::uint32_t kAltR = 0xFFEA;
inline constexpr std::uint32_t kSuperL = 0xFFEB;
inline constexpr std::uint32_t kSuperR = 0xFFEC;
inline constexpr std::uint32_t kMetaL = 0xFFE7;
inline constexpr std::uint32_t kMetaR = 0xFFE8;
inline constexpr std::uint32_t kIsoLevel3Shift = 0xFE03;  // AltGr

// Numeric keypad with NumLock OFF: named keys, which is why the
// numpad-versus-navigation distinction is clean on Linux (§5.2).
inline constexpr std::uint32_t kKpEnter = 0xFF8D;
inline constexpr std::uint32_t kKpHome = 0xFF95;
inline constexpr std::uint32_t kKpLeft = 0xFF96;
inline constexpr std::uint32_t kKpUp = 0xFF97;
inline constexpr std::uint32_t kKpRight = 0xFF98;
inline constexpr std::uint32_t kKpDown = 0xFF99;
inline constexpr std::uint32_t kKpPrior = 0xFF9A;
inline constexpr std::uint32_t kKpNext = 0xFF9B;
inline constexpr std::uint32_t kKpEnd = 0xFF9C;
inline constexpr std::uint32_t kKpBegin = 0xFF9D;
inline constexpr std::uint32_t kKpInsert = 0xFF9E;
inline constexpr std::uint32_t kKpDelete = 0xFF9F;
inline constexpr std::uint32_t kKpEqual = 0xFFBD;
inline constexpr std::uint32_t kKpMultiply = 0xFFAA;
inline constexpr std::uint32_t kKpAdd = 0xFFAB;
inline constexpr std::uint32_t kKpSeparator = 0xFFAC;
inline constexpr std::uint32_t kKpSubtract = 0xFFAD;
inline constexpr std::uint32_t kKpDecimal = 0xFFAE;
inline constexpr std::uint32_t kKpDivide = 0xFFAF;
// With NumLock ON the same keys report digits.
inline constexpr std::uint32_t kKp0 = 0xFFB0;
inline constexpr std::uint32_t kKp9 = 0xFFB9;

// Function keys
inline constexpr std::uint32_t kF1 = 0xFFBE;
inline constexpr std::uint32_t kF24 = 0xFFD5;

// The X11 modifier state bits, as the compositor reports them. Mod1..Mod5
// assignments are the xkb defaults every Linux desktop uses.
inline constexpr std::uint32_t kStateShift = 1u << 0;
inline constexpr std::uint32_t kStateLock = 1u << 1;  // caps lock STATE, not a modifier
inline constexpr std::uint32_t kStateControl = 1u << 2;
inline constexpr std::uint32_t kStateMod1 = 1u << 3;  // alt
inline constexpr std::uint32_t kStateMod2 = 1u << 4;  // num lock
inline constexpr std::uint32_t kStateMod3 = 1u << 5;
inline constexpr std::uint32_t kStateMod4 = 1u << 6;  // super / windows
inline constexpr std::uint32_t kStateMod5 = 1u << 7;  // AltGr (ISO_Level3_Shift)

// Virtual modifiers start here; `atspi_device_map_keysym_modifier` returns a
// MASK, not an index — 32768 is `1 << 15`, the first virtual modifier
// (spikes/RESULTS.md N9). `1 << 32768` is the bug that finding exists to stop.
inline constexpr unsigned kFirstVirtualModifierIndex = 15;
inline constexpr unsigned kLastVirtualModifierIndex = 31;

}  // namespace alus::input::keysym
