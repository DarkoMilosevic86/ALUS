// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// How an object is said out loud.
//
// Port of the object half of NVDA's `speech/speech.py`: `getObjectSpeech`,
// `getObjectPropertiesSpeech`, `getPropertiesSpeech`, and
// `controlTypes.processAndLabelStates`. The plan calls this an addition to
// `alus::speech::Presenter`; it lives in the object model instead, for one
// reason: `speech::Presenter` must not know what an a11y::Object is, or the
// speech library and the object model depend on each other. The dependency
// runs one way -- the object model reaches speech, never the reverse -- and
// Registry's PresentationHooks are the seam.
//
// THE ORDER IS THE CONTRACT. `getPropertiesSpeech` appends in exactly NVDA's
// order, and that order is what a user has learned to hear:
//
//     name, role, value, states, description, keyboard shortcut,
//     cell coordinates, row/column headers, table size, aria-current,
//     details, placeholder, "3 of 7", "level 2", error message
//
// Two rules in it are easy to miss and audible when missed:
//   * THE ROLE IS NOT ALWAYS SAID. On a focus change, a role in
//     `silentRolesOnFocus` is dropped when the object has a name or a value --
//     a list item announces its text, not the word "list item".
//   * "level 2" MOVES TO THE FRONT for a tree item or a list item whose level
//     has changed since the last one, because the level is the thing that
//     changed and the user needs it first.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/a11y/output_reason.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/a11y/settings.hpp"
#include "alus/speech/commands.hpp"

#include <optional>
#include <string>
#include <vector>

namespace alus::speech {
class Presenter;
}

namespace alus::a11y {

// NVDA passes `**allowedProperties`; this is that keyword set, typed.
struct PropertySelection {
    bool name = false;
    bool role = false;
    bool roleText = false;
    bool states = false;
    bool value = false;
    bool description = false;
    bool keyboardShortcut = false;
    bool positionInfoLevel = false;
    bool positionInfoIndexInGroup = false;
    bool positionInfoSimilarItemsInGroup = false;
    bool cellCoordsText = false;
    bool rowNumber = false;
    bool columnNumber = false;
    // Verbosity, not a value: "say the coordinates you worked out".
    bool includeTableCellCoords = true;
    bool rowCount = false;
    bool columnCount = false;
    bool rowHeaderText = false;
    bool columnHeaderText = false;
    bool rowSpan = false;
    bool columnSpan = false;
    bool current = false;
    bool hasDetails = false;
    bool placeholder = false;
    bool errorMessage = false;
};

// The resolved values (NVDA's `newPropertyValues`). An unset optional means
// the caller did not ask; an empty string means the object has nothing.
struct PropertyValues {
    std::optional<std::string> name;
    std::optional<Role> role;
    std::optional<std::string> roleText;
    std::optional<std::string> value;
    std::optional<std::string> description;
    std::optional<std::string> keyboardShortcut;
    std::optional<StateSet> states;
    // For OutputReason::Change: which states went positive and which negative.
    StateSet negativeStates;
    std::optional<std::string> cellCoordsText;
    std::optional<int> rowNumber;
    std::optional<int> columnNumber;
    std::optional<int> rowSpan;
    std::optional<int> columnSpan;
    std::optional<int> rowCount;
    std::optional<int> columnCount;
    std::optional<std::string> rowHeaderText;
    std::optional<std::string> columnHeaderText;
    std::optional<int> level;
    std::optional<int> indexInGroup;
    std::optional<int> similarItemsInGroup;
    std::optional<IsCurrent> current;
    std::optional<bool> hasDetails;
    std::vector<Role> detailsRoles;
    std::optional<std::string> placeholder;
    std::optional<std::string> errorMessage;
    bool includeTableCellCoords = true;

    // NVDA's `_role` and `_states`: the real values, needed to decide what to
    // say about the ones above even when those were filtered out.
    Role realRole = Role::Unknown;
    StateSet realStates;
    Ref tableId = 0;
    // What the user asked to hear. NVDA reads `config.conf` from inside
    // getPropertiesSpeech; carrying it here keeps that function pure, which is
    // what makes it testable one property at a time.
    Settings settings;
};

// NVDA: `controlTypes._processPositiveStates` + `_processNegativeStates`. The
// ~30 rules, WITHOUT the labelling: which states are worth reporting for this
// role and this reason, as two sets.
//
// It is public because NVDA's `processAndLabelStates` takes the label
// dictionaries as arguments and BRAILLE PASSES DIFFERENT ONES -- its
// abbreviations are a filter as well as a translation (braille/labels.hpp).
// Exposing the rules rather than duplicating them is what keeps the two
// outputs from drifting: a rule added here reaches speech and braille at once.
struct ProcessedStates {
    StateSet positive;
    StateSet negative;
};

ProcessedStates processStates(Role role, const StateSet& realStates, OutputReason reason,
                              const StateSet& positive, const StateSet& negative,
                              const Settings& settings);

// NVDA: controlTypes.processAndLabelStates. The positive and negative state
// labels, in bit order, after the ~30 rules that decide which states are worth
// a word for this role and this reason.
std::vector<std::string> processAndLabelStates(Role role, const StateSet& realStates,
                                               OutputReason reason, const StateSet& positive,
                                               const StateSet& negative,
                                               const Settings& settings);

// NVDA: speech.getPropertiesSpeech.
speech::SpeechSequence getPropertiesSpeech(const PropertyValues& values, OutputReason reason);

// NVDA: speech._objectSpeech_calculateAllowedProps.
PropertySelection allowedProperties(OutputReason reason, bool shouldReportTextContent, Role role,
                                    const Settings& settings);

// NVDA: speech.getObjectPropertiesSpeech. Reads the asked-for properties off
// the object, applies the Change filtering against what was last said, and
// updates that record.
speech::SpeechSequence getObjectPropertiesSpeech(Object& object, OutputReason reason,
                                                 const PropertySelection& selection);

// NVDA: speech.getObjectSpeech.
//
// `presenter` is what phase 5 added: an object WITH NAVIGABLE TEXT reads its
// text rather than its value (NVDA's `shouldReportTextContent`), and reading
// text needs the spelling path and the format reporting. Passing null keeps
// phase 4's behaviour -- the value -- which is what a test that only wants the
// properties asks for.
speech::SpeechSequence getObjectSpeech(Object& object, OutputReason reason,
                                       const speech::Presenter* presenter);
inline speech::SpeechSequence getObjectSpeech(Object& object, OutputReason reason) {
    return getObjectSpeech(object, reason, nullptr);
}

// NVDA: `shouldReportTextContent` inside getObjectSpeech. True when the text
// of the object is what the user wants to hear, rather than its value. This is
// the check claude/FAZA4.md O10 left open.
bool shouldReportTextContent(const Object& object, OutputReason reason);

// The row/column/level memory that makes "row 3" silent when the row did not
// change. NVDA keeps it in its global speech state; it is here for the same
// reason, and resettable because a test that does not reset it is a test that
// depends on the one before it.
struct PresentationMemory {
    Ref tableId = 0;
    int rowNumber = 0;
    int rowSpan = 1;
    int columnNumber = 0;
    int columnSpan = 1;
    int treeLevel = 0;
};

PresentationMemory& presentationMemory();
void resetPresentationMemory();

// Fills in Registry::presentation() so that the object model actually speaks.
// Called by a11y::Service; a test that wants to see what WOULD be said
// installs its own hooks instead and never needs a synthesizer.
void installSpeechPresentation(Registry& registry, speech::Presenter& presenter);

}  // namespace alus::a11y
