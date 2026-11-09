// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The abbreviations braille uses instead of words.
//
// Port of NVDA's `braille/labels.py` -- `roleLabels`, `positiveStateLabels`,
// `negativeStateLabels` and `landmarkLabels`.
//
// WHY ABBREVIATIONS ARE NOT A COSMETIC CHOICE. A braille display has 14, 20
// or 40 cells. "check box checked" is 18 characters and leaves nothing for the
// label the user actually wants to read; "chk ⣏⣿⣹" is 7 and says the same
// thing. Every screen reader does this and the abbreviations are a vocabulary
// users know, so they are transferred verbatim rather than invented.
//
// THE DOT SHAPES ARE NOT TEXT. `⣏⣿⣹` (checked), `⢎⣿⡱` (pressed) and the rest
// are Unicode braille: they bypass the translation table on purpose, because a
// checkbox must look the same in every language and grade. They are passed
// through liblouis like any other character and `braille-patterns.cti` maps
// them to themselves, which is why that table is always in the table list.
//
// A ROLE WITH NO ABBREVIATION FALLS BACK TO THE SPOKEN LABEL, as in NVDA: the
// dictionary has ~60 entries out of 156 roles, and `a11y::roleLabel` answers
// for the rest. That is deliberate -- an invented abbreviation nobody knows is
// worse than a long word.
#pragma once

#include "alus/a11y/mapping.hpp"
#include "alus/a11y/role.hpp"
#include "alus/a11y/state.hpp"

#include <string>
#include <string_view>

namespace alus::braille {

// The abbreviation, or an empty view when this role has none.
std::string_view roleAbbreviation(a11y::Role role);
// The abbreviation if there is one, otherwise the spoken label. This is what
// every caller actually wants; `roleAbbreviation` is public for the tests.
std::string roleBrailleLabel(a11y::Role role);

// NVDA: `positiveStateLabels` / `negativeStateLabels`. Empty when the state
// has no braille abbreviation -- which, unlike roles, means SAY NOTHING: the
// braille state dictionaries are a filter as well as a translation, and
// NVDA's `processAndLabelStates` skips a state that is not in them.
std::string_view statePositiveAbbreviation(a11y::State state);
std::string_view stateNegativeAbbreviation(a11y::State state);

// NVDA: `landmarkLabels`. Four-letter forms ("navi", "cinf"); empty for an
// unknown token.
std::string_view landmarkAbbreviation(std::string_view token);

// NVDA: `IsCurrent.displayString` as braille shows it. Braille uses the same
// words speech does here -- NVDA has no abbreviation for aria-current -- so
// this is a thin wrapper that exists so the braille code never reaches into
// the speech labels directly.
std::string_view isCurrentAbbreviation(a11y::IsCurrent current);

}  // namespace alus::braille
