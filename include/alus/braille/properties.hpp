// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What a control reads as on a braille display.
//
// Port of NVDA's `braille/regions/properties.py`: `getPropertiesBraille`,
// `getControlFieldBraille` and `getFormatFieldBraille`.
//
// THIS IS THE BRAILLE TWIN OF `a11y/object_speech.hpp`, and the pairing is
// deliberate. Both answer "what is this control", from the same property set
// and the same `processStates` rules; they differ in two ways and only two:
//
//   * braille uses ABBREVIATIONS (braille/labels.hpp) and a state with no
//     abbreviation is SILENT, where speech would say a word;
//   * braille joins with a single space and no punctuation -- every comma is
//     a cell the label needed.
//
// It is also where the second handover promise of phase 6 is kept: the
// `ControlField` stream phase 6 built has a second consumer now, and the
// fields really were canonical (FAZA6.md O7). Nothing here normalises
// anything; it reads the keys the document layer already agreed on.
//
// PURE FUNCTIONS, like the speech side: no object, no platform call, no
// configuration lookup. That is what lets every rule below be one assertion in
// tests/braille, and it is why the settings are arguments.
#pragma once

#include "alus/a11y/mapping.hpp"
#include "alus/a11y/object.hpp"
#include "alus/a11y/output_reason.hpp"
#include "alus/a11y/role.hpp"
#include "alus/a11y/settings.hpp"
#include "alus/a11y/state.hpp"
#include "alus/braille/settings.hpp"
#include "alus/text/fields.hpp"

#include <optional>
#include <string>
#include <vector>

namespace alus::braille {

// NVDA's `**propertyValues`. An unset optional means "do not report this",
// which is a different thing from an empty string: an empty name is still a
// name the caller decided to pass.
struct PropertyValues {
    std::optional<std::string> name;
    // Set means SAY THE ROLE. `realRole` is always consulted for the rules.
    std::optional<a11y::Role> role;
    // aria-roledescription, or a role text braille computed by the caller (a
    // heading's "h2", a visited link's "vlnk"). When set it replaces the role
    // abbreviation entirely.
    std::string roleText;
    std::optional<a11y::StateSet> states;
    std::optional<std::string> value;
    std::optional<std::string> description;
    std::optional<std::string> keyboardShortcut;
    std::optional<std::string> placeholder;
    std::optional<std::string> errorMessage;

    // ---- tables ----
    std::optional<std::string> cellCoordsText;
    std::optional<std::string> columnHeaderText;
    std::optional<int> rowNumber;
    std::optional<int> columnNumber;
    int rowSpan = 1;
    int columnSpan = 1;
    bool includeTableCellCoords = true;

    // ---- position ----
    std::optional<int> level;
    std::optional<int> indexInGroup;
    std::optional<int> similarItemsInGroup;
    // NVDA's `_childcontrolcount`, appended to a list's own abbreviation.
    std::optional<int> childControlCount;

    a11y::IsCurrent current = a11y::IsCurrent::No;
    bool hasDetails = false;
    std::vector<a11y::Role> detailsRoles;
    // True when `detailsRoles` contained a null entry, which NVDA renders as
    // the generic word rather than "has X".
    bool hasGenericDetails = false;

    // NVDA's `_role` and `_states`: the real values, needed by the rules even
    // when the caller asked for neither to be reported.
    a11y::Role realRole = a11y::Role::Unknown;
    a11y::StateSet realStates;

    a11y::Settings settings;
    a11y::OutputReason reason = a11y::OutputReason::Focus;
};

// NVDA: `getPropertiesBraille`. The pieces, joined with one space, empty ones
// dropped.
std::string getPropertiesBraille(const PropertyValues& values);

// NVDA: `getControlFieldBraille`. Nothing when this field is worth no cells at
// all, which is the usual answer for a layout container.
//
// `ancestors` is the field stack above this one, outermost first.
// `reportStart` false asks for the END text ("lst end").
// `category` receives the presentation category the decision was made on,
// because the caller (the region) needs it to place the cursor on a marker
// field, exactly as NVDA caches `field._presCat`.
std::optional<std::string> getControlFieldBraille(
    const text::ControlField& field, const std::vector<text::ControlField>& ancestors,
    bool reportStart, const text::FormatReportSettings& format,
    const a11y::Settings& objectSettings, const Settings& brailleSettings,
    text::PresentationCategory* category = nullptr);

// NVDA: `getFormatFieldBraille`. Reports what CHANGED against `cache`, then
// replaces the cache with `field` -- the cache is in/out, as in NVDA, because
// the caller keeps one per object across region updates.
//
// `isAtStart` restricts the things that only make sense at the beginning of a
// line or paragraph: the paragraph marker, the line number, the list bullet
// and the heading level.
std::string getFormatFieldBraille(const text::FormatField& field, text::FormatField& cache,
                                  bool isAtStart, const text::FormatReportSettings& format,
                                  const Settings& brailleSettings);

// NVDA: the `NVDAObjectRegion.update` body -- the property values for a whole
// object rather than for a field. Here rather than in the region because it is
// pure and the region is not, and because T7 asserts on it directly.
PropertyValues propertiesForObject(const a11y::Object& object, const a11y::Settings& settings,
                                   const Settings& brailleSettings, bool hasUsefulText);

// NVDA: `NVDAObjectHasUsefulText`. Whether the object's TEXT is what the
// display should show, in which case its value is not reported as a property.
bool hasUsefulText(const a11y::Object& object);

}  // namespace alus::braille
