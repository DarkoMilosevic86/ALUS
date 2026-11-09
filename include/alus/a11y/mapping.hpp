// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// AT-SPI -> ALUS: roles, states, and the refinement that makes them specific.
//
// 01-ARHITEKTURA.md §6.5 measured the shapes of the two vocabularies, and the
// measurement on this system (AT-SPI 2.56.2) is:
//
//     roles     131 AT-SPI  ->  156 ALUS (NVDA's)
//     states     44 AT-SPI  ->   53 ALUS
//     relations  23 AT-SPI
//
// (The plan said 133 roles and 45 states; those counts include
// ATSPI_ROLE_LAST_DEFINED and the ATSPI_ROLE_PUSH_BUTTON backwards
// compatibility alias, which are not roles. ATSPI_ROLE_COUNT in
// atspi-constants.h is 131.)
//
// The gap is not a defect of either side. AT-SPI has one role where NVDA has
// six (a heading against HEADING1..HEADING6), and NVDA has roles that are
// document semantics rather than widgets (LANDMARK, REGION, FIGURE,
// BLOCKQUOTE). Closing it needs two steps, and both are here:
//
//   1. THE TABLE (src/a11y/role_map.cpp): every AT-SPI role has one ALUS role.
//      Every single one is listed, and T4.6 fails if any is missing or lands
//      on Role::Unknown without being on the deliberate list.
//   2. THE REFINEMENT (src/a11y/refine.cpp): `Accessible::get_attributes()`
//      carries `xml-roles`, `level` and `tag`, whose keys in Gecko and
//      Chromium are the SAME ones NVDA already reads as IA2 attributes. That
//      is what turns a heading into "heading level 2" and a section into a
//      banner landmark (T4.7).
#pragma once

#include "alus/a11y/backend.hpp"
#include "alus/a11y/platform.hpp"
#include "alus/a11y/role.hpp"
#include "alus/a11y/state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace alus::a11y {

// ---------------------------------------------------------------------------
// Roles
// ---------------------------------------------------------------------------

// The table of step 1. Never fails: an unknown numeric value answers
// Role::Unknown, because a newer AT-SPI with a role ALUS has never heard of
// must not crash the screen reader.
Role roleFromPlatform(PlatformRole platform);

// True for the AT-SPI roles that map to Role::Unknown ON PURPOSE. There are
// only a few, and each has a reason recorded next to it in role_map.cpp.
// T4.6 uses this to tell a deliberate hole from a forgotten row.
bool isDeliberatelyUnknownRole(PlatformRole platform);

// ---------------------------------------------------------------------------
// States
// ---------------------------------------------------------------------------

// `bits` is a bitset over PlatformState, as Backend::states() returns it.
// The role is needed because a few AT-SPI states only mean something for
// certain roles (ATSPI_STATE_CHECKED on a toggle button is "pressed", not
// "checked"), exactly as NVDA's own IAccessible state mapping does.
StateSet statesFromPlatform(std::uint64_t bits, PlatformRole platform);

// The inverse, for the tests and for the mock backend: one platform state as a
// bit.
constexpr std::uint64_t platformStateBit(PlatformState state) {
    return std::uint64_t{1} << static_cast<std::uint64_t>(static_cast<std::int32_t>(state));
}

// NVDA: controlTypes.transformRoleStates. Some role/state combinations are
// better said as another combination: an indeterminate progress bar is a busy
// indicator, not a half-checked progress bar.
void transformRoleStates(Role& role, StateSet& states);

// ---------------------------------------------------------------------------
// aria-current (NVDA: controlTypes.IsCurrent)
// ---------------------------------------------------------------------------

enum class IsCurrent { No, Yes, Page, Step, Location, Date, Time };

// The attribute value as the toolkit spells it ("true", "page", ...). An
// unrecognised non-empty value is IsCurrent::Yes, as in NVDA.
IsCurrent isCurrentFromString(std::string_view value);
// The translatable label; empty for IsCurrent::No.
const char* isCurrentLabel(IsCurrent value);

// ---------------------------------------------------------------------------
// Refinement through get_attributes() (§6.5)
// ---------------------------------------------------------------------------

struct Refined {
    Role role = Role::Unknown;
    StateSet states;
    // The ARIA landmark token ("banner", "main", ...), empty when this is not
    // a landmark. NVDA's `landmark` property.
    std::string landmark;
    // `level` from the attributes: a heading level or a tree depth. 0 = none.
    int level = 0;
    IsCurrent current = IsCurrent::No;
    // `placeholder-text`, which is spoken only when the field is empty.
    std::string placeholder;
};

// Applies the refinement to a role and state set that already came out of the
// tables. `attributes` is what Backend::attributes() returned.
Refined refine(Role role, const StateSet& states, const Attributes& attributes);

// The ARIA role -> ALUS role table (NVDA: aria.ariaRolesToNVDARoles).
std::optional<Role> roleFromAriaRole(std::string_view ariaRole);
// NVDA: aria.landmarkRoles. The label is translatable and is what the user
// hears ("banner"); an unknown token answers null.
const char* landmarkLabel(std::string_view landmark);
bool isLandmarkToken(std::string_view token);

}  // namespace alus::a11y
