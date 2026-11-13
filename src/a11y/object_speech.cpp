// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/object_speech.hpp"

#include "alus/a11y/text_speech.hpp"

#include "alus/audio/tones.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/ui/message.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace alus::a11y {
namespace {

// The comments below are NVDA's own translator notes, kept because they are
// what tells a translator what "through 5" means.
//
// formatOne() used to live here, before i18n::substitute existed; it is now
// that one function, so the placeholder rule has one implementation.

std::string formatOne(std::string_view pattern, const std::string& value) {
    return i18n::substitute(pattern, value);
}

std::string formatOne(std::string_view pattern, int value) {
    return i18n::substitute(pattern, std::to_string(value));
}

// P_ and not two separate _() calls: English has two forms and Serbian has
// three (1 ред, 2 реда, 5 редова), so the COUNT has to reach the catalogue.
// Picking the form in C++ with `count == 1` would make every Slavic
// translation wrong at 2, 3 and 4 and there would be no way for a translator
// to fix it.
std::string rowCountText(int count) {
    // Translators: "3 rows", part of "with 3 rows and 2 columns".
    return formatOne(P_("%s row", "%s rows", count), count);
}

std::string columnCountText(int count) {
    // Translators: "2 columns", part of "with 3 rows and 2 columns".
    return formatOne(P_("%s column", "%s columns", count), count);
}

// Patterns and not concatenation. "with " + x + " and " + y reads as English
// word order with English spacing, and no translation can move either piece:
// Serbian needs the case of the noun to agree with the preposition, which the
// translator can only do if the preposition and the noun are in one string.
std::string rowAndColumnCountText(int rowCount, int columnCount) {
    if (rowCount != 0 && columnCount != 0) {
        // Translators: the compound form, e.g. "with 3 rows and 2 columns";
        // the first %s is the row count and the second the column count.
        return i18n::substitute(_("with %s and %s"), rowCountText(rowCount),
                                columnCountText(columnCount));
    }
    if (columnCount != 0) {
        // Translators: a table's size when only the column count is known,
        // e.g. "with 2 columns".
        return formatOne(_("with %s"), columnCountText(columnCount));
    }
    if (rowCount != 0) {
        // Translators: a table's size when only the row count is known.
        return formatOne(_("with %s"), rowCountText(rowCount));
    }
    return {};
}

void append(speech::SpeechSequence& sequence, std::string text) {
    if (!text.empty()) {
        sequence.push_back(std::move(text));
    }
}

// NVDA: _processPositiveStates. The user never cares about certain states, and
// which ones depends on the role and the reason.
StateSet processPositiveStates(Role role, const StateSet& states, OutputReason reason,
                               const StateSet& positiveIn, const Settings& settings) {
    StateSet positive = positiveIn;
    if (role == Role::EditableText) {
        // It is an edit field; of course it is editable.
        positive.remove(State::Editable);
    }
    if (role != Role::Link) {
        positive.remove(State::Visited);
        positive.remove(State::InternalLink);
    }
    positive.remove(State::Selectable);
    if (!settings.reportMultiSelect || role == Role::ListItem || role == Role::TreeViewItem ||
        role == Role::MenuItem || role == Role::TableRow || role == Role::TableCell ||
        role == Role::CheckBox) {
        positive.remove(State::MultiSelectable);
    }
    positive.remove(State::Focusable);
    positive.remove(State::Checkable);
    if (positive.has(State::Dragging)) {
        // Obvious that it can be dragged while it is being dragged.
        positive.remove(State::Draggable);
    }
    if (role == Role::ComboBox) {
        // A combo box inherently has a popup.
        positive.remove(State::HasPopup);
    }
    if (!settings.reportClickable || isClickableRole(role)) {
        positive.remove(State::Clickable);
    }
    if (!settings.reportLinkType) {
        positive.remove(State::InternalLink);
    }
    if (reason == OutputReason::Query) {
        return positive;
    }
    positive.remove(State::Defunct);
    positive.remove(State::Modal);
    positive.remove(State::Focused);
    positive.remove(State::Offscreen);
    positive.remove(State::Invisible);
    positive.remove(State::Indeterminate);
    if (reason != OutputReason::Change) {
        positive.remove(State::Linked);
        const bool itemRole = role == Role::ListItem || role == Role::TreeViewItem ||
                              role == Role::MenuItem || role == Role::TableRow ||
                              role == Role::CheckBox;
        if (itemRole && states.has(State::Selectable)) {
            positive.remove(State::Selected);
        }
    }
    if (role != Role::EditableText && role != Role::CheckBox) {
        positive.remove(State::ReadOnly);
    }
    if (role == Role::CheckBox) {
        positive.remove(State::Pressed);
    }
    if (role == Role::MenuItem && positive.has(State::HasPopup)) {
        // Nobody cares whether a submenu is expanded.
        positive.remove(State::Collapsed);
        positive.remove(State::Expanded);
    }
    if (!states.has(State::Focusable)) {
        positive.remove(State::Editable);
    }
    return positive;
}

// NVDA: _processNegativeStates. Which states are worth saying in the negative
// -- "not checked" is information, "not sorted" usually is not.
StateSet processNegativeStates(Role role, const StateSet& states, OutputReason reason,
                               const StateSet& negativeIn) {
    StateSet speakNegatives;
    // "not selected" only where a selection is really possible, and only on a
    // focus change or a change to the focused object. The condition is what
    // stops "not selected" from being said by broken controls that send the
    // state change for the PREVIOUS focus after the focus has moved.
    const bool selectableItemRole = role == Role::ListItem || role == Role::TreeViewItem ||
                                    role == Role::TableRow || role == Role::TableCell ||
                                    role == Role::TableColumnHeader ||
                                    role == Role::TableRowHeader || role == Role::CheckBox;
    if (states.has(State::Selectable) && states.has(State::Focusable) &&
        (reason == OutputReason::Focus ||
         (reason == OutputReason::Change && states.has(State::Focused))) &&
        selectableItemRole) {
        speakNegatives.add(State::Selected);
    }
    // "not checked", restricted the same way.
    const bool checkableRole = role == Role::CheckBox || role == Role::RadioButton ||
                               role == Role::CheckMenuItem;
    if ((checkableRole || states.has(State::Checkable)) && !states.has(State::HalfChecked) &&
        (reason != OutputReason::Change || states.has(State::Focused))) {
        speakNegatives.add(State::Checked);
    }
    if (role == Role::ToggleButton && !states.has(State::HalfPressed)) {
        speakNegatives.add(State::Pressed);
    }
    if (role == Role::Switch && !states.has(State::On)) {
        speakNegatives.add(State::On);
    }
    if (reason == OutputReason::Change) {
        // A drop target that has stopped being one is worth a word.
        speakNegatives.add(State::DropTarget);
        speakNegatives = speakNegatives & negativeIn;
        // NVDA #6946: half-checked going away without checked arriving means
        // the thing is now unchecked.
        if (negativeIn.has(State::HalfChecked) && !states.has(State::Checked)) {
            speakNegatives.add(State::Checked);
        }
        if (negativeIn.has(State::HalfPressed) && !states.has(State::Pressed)) {
            speakNegatives.add(State::Pressed);
        }
        if (sortedStates().intersects(negativeIn) && !sortedStates().intersects(states)) {
            // It stopped being sorted; how it used to be sorted is history.
            speakNegatives.add(State::Sorted);
        }
        return speakNegatives;
    }
    return speakNegatives - states;
}

std::string negativeLabel(State state) {
    if (stateHasOwnNegativeLabel(state)) {
        return stateNegativeLabel(state);
    }
    // Translators: a state of an object is negated; %s is the state's own
    // label (e.g. "selected"). NVDA keeps separate strings for the states that
    // are commonly negated and falls back to this one for the rest.
    return formatOne(_("not %s"), std::string{stateLabel(state)});
}

}  // namespace

ProcessedStates processStates(Role role, const StateSet& realStates, OutputReason reason,
                              const StateSet& positiveIn, const StateSet& negativeIn,
                              const Settings& settings) {
    return {processPositiveStates(role, realStates, reason, positiveIn, settings),
            processNegativeStates(role, realStates, reason, negativeIn)};
}

std::vector<std::string> processAndLabelStates(Role role, const StateSet& realStates,
                                               OutputReason reason, const StateSet& positiveIn,
                                               const StateSet& negativeIn,
                                               const Settings& settings) {
    const ProcessedStates processed =
        processStates(role, realStates, reason, positiveIn, negativeIn, settings);
    const StateSet& positive = processed.positive;
    const StateSet& negative = processed.negative;
    std::vector<std::string> labels;
    // In bit order, which is NVDA's `sorted(positive | negative)`, and that
    // order is user-visible.
    for (const State state : (positive | negative).toVector()) {
        if (positive.has(state)) {
            const char* label = stateLabel(state);
            if (label != nullptr && *label != '\0') {
                labels.emplace_back(label);
            }
        } else if (negative.has(state)) {
            labels.push_back(negativeLabel(state));
        }
    }
    return labels;
}

PresentationMemory& presentationMemory() {
    static PresentationMemory memory;
    return memory;
}

void resetPresentationMemory() { presentationMemory() = PresentationMemory{}; }

speech::SpeechSequence getPropertiesSpeech(const PropertyValues& values, OutputReason reason) {
    speech::SpeechSequence sequence;
    PresentationMemory& memory = presentationMemory();

    if (values.name.has_value()) {
        append(sequence, *values.name);
    }

    // `role` present means the caller asked for the role to be SAID;
    // `realRole` is always there, because states and values need it to be
    // described correctly.
    bool speakRole = values.role.has_value();
    const Role role = values.role.value_or(values.realRole);

    const std::string value =
        (values.value.has_value() && !isSilentValue(role)) ? *values.value : std::string{};
    const std::string cellCoordsText = values.cellCoordsText.value_or(std::string{});
    const int rowNumber = values.rowNumber.value_or(0);
    const int columnNumber = values.columnNumber.value_or(0);
    const bool includeTableCellCoords = values.includeTableCellCoords;

    if (role == Role::ChartElement) {
        speakRole = false;
    }
    const std::string roleText = values.roleText.value_or(std::string{});
    const bool quietReason = reason == OutputReason::SayAll || reason == OutputReason::Caret ||
                             reason == OutputReason::Focus || reason == OutputReason::QuickNav;
    const bool hasSomethingElse = !values.name.value_or(std::string{}).empty() ||
                                  !value.empty() || !cellCoordsText.empty() || rowNumber != 0 ||
                                  columnNumber != 0;
    // FILLER is never said. It is AT-SPI's word for "a box with nothing to
    // say" -- GTK pads layouts with it, and the desktop-icons extension wraps
    // every column of icons in one -- so the role label is the one property
    // of it that is certainly not information. NVDA never meets it (MSAA has
    // no filler) and Orca never speaks it. A filler with a NAME still has its
    // name said; only the word "filler" goes.
    if (speakRole && role != Role::Filler &&
        (!roleText.empty() || !quietReason || !hasSomethingElse || !isSilentOnFocus(role)) &&
        (role != Role::Math ||
         (reason != OutputReason::Caret && reason != OutputReason::SayAll))) {
        // An author's own role description wins over the role's label: that is
        // what aria-roledescription is for.
        append(sequence, roleText.empty() ? std::string{roleLabel(role)} : roleText);
    }
    append(sequence, value);

    // The caller wanting no states at all is `states` unset; an EMPTY state set
    // still means "say the states", and an unchecked check box with no other
    // state depends on that distinction.
    if (values.states.has_value() || !values.negativeStates.empty()) {
        const StateSet positive = values.states.value_or(StateSet{});
        const std::vector<std::string> labels =
            processAndLabelStates(role, values.realStates, reason, positive,
                                  values.negativeStates, values.settings);
        for (const std::string& label : labels) {
            append(sequence, label);
        }
    }

    append(sequence, values.description.value_or(std::string{}));
    append(sequence, values.keyboardShortcut.value_or(std::string{}));

    if (includeTableCellCoords && !cellCoordsText.empty()) {
        append(sequence, cellCoordsText);
    }

    if (!cellCoordsText.empty() || rowNumber != 0 || columnNumber != 0) {
        // Without a table identity, every cell is treated as a new table --
        // which is the safe direction to be wrong in.
        const bool sameTable = values.tableId != 0 && values.tableId == memory.tableId;
        if (values.tableId != 0 && !sameTable) {
            memory.tableId = values.tableId;
        }
        // A cell with no span implemented spans one row and one column.
        const int rowSpan = values.rowSpan.value_or(1) > 0 ? values.rowSpan.value_or(1) : 1;
        const int columnSpan =
            values.columnSpan.value_or(1) > 0 ? values.columnSpan.value_or(1) : 1;

        if (rowNumber != 0 &&
            (!sameTable || rowNumber != memory.rowNumber || rowSpan != memory.rowSpan)) {
            append(sequence, values.rowHeaderText.value_or(std::string{}));
            if (includeTableCellCoords && cellCoordsText.empty()) {
                // Translators: the current row number, e.g. "row 3".
                append(sequence, formatOne(_("row %s"), rowNumber));
                if (rowSpan > 1 && columnSpan <= 1) {
                    // Translators: the row span, e.g. "through 5".
                    append(sequence, formatOne(_("through %s"), rowNumber + rowSpan - 1));
                }
            }
            memory.rowNumber = rowNumber;
            memory.rowSpan = rowSpan;
        }
        if (columnNumber != 0 &&
            (!sameTable || columnNumber != memory.columnNumber ||
             columnSpan != memory.columnSpan)) {
            append(sequence, values.columnHeaderText.value_or(std::string{}));
            if (includeTableCellCoords && cellCoordsText.empty()) {
                // Translators: the current column number, e.g. "column 3".
                append(sequence, formatOne(_("column %s"), columnNumber));
                if (columnSpan > 1 && rowSpan <= 1) {
                    // Translators: the column span, e.g. "through 5".
                    append(sequence,
                           formatOne(_("through %s"), columnNumber + columnSpan - 1));
                }
            }
            memory.columnNumber = columnNumber;
            memory.columnSpan = columnSpan;
        }
        if (includeTableCellCoords && cellCoordsText.empty() && rowSpan > 1 && columnSpan > 1) {
            // Translators: a cell spanning rows AND columns, e.g. "through
            // row 5 column 3".
            append(sequence, i18n::substitute(
                                 _("through row %s column %s"),
                                 std::to_string(rowNumber + rowSpan - 1),
                                 std::to_string(columnNumber + columnSpan - 1)));
        }
    }

    const int rowCount = values.rowCount.value_or(0);
    const int columnCount = values.columnCount.value_or(0);
    append(sequence, rowAndColumnCountText(rowCount, columnCount));
    if (rowCount != 0 || columnCount != 0) {
        // Entering a table: the next cell is in a new table even if it is the
        // same one.
        memory.tableId = 0;
    }

    const IsCurrent current = values.current.value_or(IsCurrent::No);
    if (current != IsCurrent::No) {
        append(sequence, isCurrentLabel(current));
    }

    if (values.hasDetails.value_or(false)) {
        if (values.detailsRoles.empty()) {
            // Translators: there are further details that can be fetched.
            append(sequence, _("has details"));
        } else {
            for (const Role detailRole : values.detailsRoles) {
                // Translators: "has comment", "has suggestion"; %s is the kind
                // of detail.
                append(sequence, formatOne(_("has %s"), std::string{roleLabel(detailRole)}));
            }
        }
    }

    append(sequence, values.placeholder.value_or(std::string{}));

    const int indexInGroup = values.indexInGroup.value_or(0);
    const int similarItems = values.similarItemsInGroup.value_or(0);
    if (indexInGroup > 0 && indexInGroup <= similarItems) {
        // Translators: the position of an item in a group; the first %s is
        // the item's number and the second how many there are, e.g. "3 of 7".
        append(sequence, i18n::substitute(_("%s of %s"), std::to_string(indexInGroup),
                                          std::to_string(similarItems)));
    }
    if (values.level.has_value() && *values.level > 0) {
        // Translators: the item level in a tree, e.g. "level 2".
        const std::string levelText = formatOne(_("level %s"), *values.level);
        const bool treeLike = values.role.has_value() &&
                              (*values.role == Role::TreeViewItem || *values.role == Role::ListItem);
        if (treeLike && *values.level != memory.treeLevel) {
            // The level CHANGED, so it is the first thing the user needs.
            sequence.insert(sequence.begin(), levelText);
            memory.treeLevel = *values.level;
        } else {
            append(sequence, levelText);
        }
    }

    append(sequence, values.errorMessage.value_or(std::string{}));
    return sequence;
}

PropertySelection allowedProperties(OutputReason reason, bool shouldReportTextContent, Role role,
                                    const Settings& settings) {
    PropertySelection selection;
    selection.name = true;
    selection.role = true;
    selection.roleText = true;
    selection.states = true;
    selection.errorMessage = true;
    selection.value = true;
    selection.description = true;
    selection.hasDetails = settings.reportDetails;
    selection.keyboardShortcut = true;
    selection.positionInfoLevel = true;
    selection.positionInfoIndexInGroup = true;
    selection.positionInfoSimilarItemsInGroup = true;
    selection.cellCoordsText = true;
    selection.rowNumber = true;
    selection.columnNumber = true;
    selection.includeTableCellCoords = true;
    selection.columnCount = true;
    selection.rowCount = true;
    selection.rowHeaderText = true;
    selection.columnHeaderText = true;
    selection.rowSpan = true;
    selection.columnSpan = true;
    selection.current = true;

    if (reason == OutputReason::FocusEntered || reason == OutputReason::Mouse) {
        selection.value = false;
        // NVDA #15826: a container's shortcut key is often defined and does not
        // work. The one container where it is useful is a list.
        if (role != Role::List) {
            selection.keyboardShortcut = false;
        }
        selection.positionInfoLevel = false;
    }
    if (reason == OutputReason::Mouse) {
        // The name is usually part of the text under the mouse anyway.
        selection.name = false;
        selection.description = false;
        selection.positionInfoIndexInGroup = false;
        selection.positionInfoSimilarItemsInGroup = false;
    }
    if (!settings.reportObjectDescriptions) {
        selection.description = false;
    }
    if (!settings.reportKeyboardShortcuts) {
        selection.keyboardShortcut = false;
    }
    if (!settings.reportObjectPositionInformation) {
        selection.positionInfoLevel = false;
        selection.positionInfoIndexInGroup = false;
        selection.positionInfoSimilarItemsInGroup = false;
    }
    if (reason != OutputReason::Query) {
        // The size of a table is said when entering it, not on every move.
        selection.rowCount = false;
        selection.columnCount = false;
    }
    if (!settings.reportTableCellCoords) {
        selection.cellCoordsText = false;
        selection.includeTableCellCoords = false;
    }
    if (settings.reportTableHeaders != TableHeaders::RowsAndColumns &&
        settings.reportTableHeaders != TableHeaders::Rows) {
        selection.rowHeaderText = false;
    }
    if (settings.reportTableHeaders != TableHeaders::RowsAndColumns &&
        settings.reportTableHeaders != TableHeaders::Columns) {
        selection.columnHeaderText = false;
    }
    const bool noRowInfo =
        !settings.reportTables ||
        (!settings.reportTableCellCoords && (settings.reportTableHeaders == TableHeaders::Off ||
                                            settings.reportTableHeaders == TableHeaders::Columns));
    if (noRowInfo) {
        selection.rowNumber = false;
        selection.rowSpan = false;
    }
    const bool noColumnInfo =
        !settings.reportTables ||
        (!settings.reportTableCellCoords && (settings.reportTableHeaders == TableHeaders::Off ||
                                            settings.reportTableHeaders == TableHeaders::Rows));
    if (noColumnInfo) {
        selection.columnNumber = false;
        selection.columnSpan = false;
    }
    if (shouldReportTextContent) {
        // The text will be read; the value would repeat it.
        selection.value = false;
    }
    return selection;
}

speech::SpeechSequence getObjectPropertiesSpeech(Object& object, OutputReason reason,
                                                 const PropertySelection& selection) {
    const Settings& settings = object.registry().settings();
    PropertyValues values;
    values.settings = settings;
    values.realRole = object.role();
    values.realStates = object.states();
    values.includeTableCellCoords = selection.includeTableCellCoords;

    if (selection.name) {
        values.name = object.name();
    }
    if (selection.role) {
        values.role = object.role();
    }
    if (selection.roleText && !object.roleText().empty()) {
        values.roleText = object.roleText();
    }
    if (selection.states) {
        values.states = object.states();
    }
    if (selection.value) {
        values.value = object.value();
    }
    if (selection.description) {
        values.description = object.description();
    }
    if (selection.keyboardShortcut) {
        values.keyboardShortcut = object.keyboardShortcut();
    }
    if (selection.current) {
        values.current = object.isCurrent();
    }
    if (selection.placeholder) {
        values.placeholder = object.placeholder();
    }
    if (selection.errorMessage) {
        // NVDA: only when the entry is actually marked invalid. An error
        // message on a valid field is the author's leftover, not news.
        if (object.states().has(State::InvalidEntry)) {
            values.errorMessage = object.errorMessage();
        }
    }
    if (selection.hasDetails) {
        const std::vector<ObjectPtr>& details = object.details();
        values.hasDetails = !details.empty();
        for (const ObjectPtr& detail : details) {
            if (detail != nullptr) {
                values.detailsRoles.push_back(detail->role());
            }
        }
    }
    const bool wantsPosition = selection.positionInfoLevel ||
                               selection.positionInfoIndexInGroup ||
                               selection.positionInfoSimilarItemsInGroup;
    if (wantsPosition) {
        const PositionInfo& position = object.positionInfo();
        if (selection.positionInfoLevel && position.level > 0) {
            values.level = position.level;
        }
        if (selection.positionInfoIndexInGroup && position.indexInGroup > 0) {
            values.indexInGroup = position.indexInGroup;
        }
        if (selection.positionInfoSimilarItemsInGroup && position.similarItemsInGroup > 0) {
            values.similarItemsInGroup = position.similarItemsInGroup;
        }
    }
    const bool isCell = object.hasInterface(Interface::TableCell) ||
                        object.role() == Role::TableCell ||
                        object.role() == Role::TableColumnHeader ||
                        object.role() == Role::TableRowHeader;
    if (isCell) {
        const CellInfo& cell = object.cellInfo();
        if (selection.cellCoordsText && !object.cellCoordsText().empty()) {
            values.cellCoordsText = object.cellCoordsText();
        }
        if (selection.rowNumber && cell.row >= 0) {
            values.rowNumber = cell.row + 1;
        }
        if (selection.columnNumber && cell.column >= 0) {
            values.columnNumber = cell.column + 1;
        }
        if (selection.rowSpan) {
            values.rowSpan = cell.rowSpan;
        }
        if (selection.columnSpan) {
            values.columnSpan = cell.columnSpan;
        }
        if (selection.rowHeaderText) {
            values.rowHeaderText = object.rowHeaderText();
        }
        if (selection.columnHeaderText) {
            values.columnHeaderText = object.columnHeaderText();
        }
        values.tableId = object.tableId();
    }
    if ((selection.rowCount || selection.columnCount) && object.hasInterface(Interface::Table)) {
        const TableInfo& table = object.tableInfo();
        if (selection.rowCount) {
            values.rowCount = table.rowCount;
        }
        if (selection.columnCount) {
            values.columnCount = table.columnCount;
        }
    }

    // A description identical to the name is the author labelling twice.
    if (values.description.has_value() && !values.description->empty() &&
        values.name.has_value() && *values.name == *values.description &&
        reason != OutputReason::Change) {
        values.description.reset();
    }

    // ---- what was last said about this object ----
    Object::SpokenCache& spoken = object.spokenCache();
    const Object::SpokenCache previous = spoken;
    if (values.name.has_value()) {
        spoken.name = *values.name;
    }
    if (values.value.has_value()) {
        spoken.value = *values.value;
    }
    if (values.description.has_value()) {
        spoken.description = *values.description;
    }
    if (values.role.has_value()) {
        spoken.role = *values.role;
    }
    if (values.states.has_value()) {
        spoken.states = *values.states;
    }
    spoken.valid = true;

    if (reason == OutputReason::OnlyCache) {
        return {};
    }

    if (reason == OutputReason::Change && previous.valid) {
        // Only what changed. The states need their own treatment: what became
        // true and what stopped being true are two different utterances.
        if (values.name.has_value() && *values.name == previous.name) {
            values.name.reset();
        }
        if (values.value.has_value() && *values.value == previous.value) {
            values.value.reset();
        }
        if (values.description.has_value() && *values.description == previous.description) {
            values.description.reset();
        }
        if (values.states.has_value()) {
            const StateSet now = *values.states;
            values.states = now - previous.states;
            values.negativeStates = previous.states - now;
        }
    }

    // On a focus change, "selected" on the focused object is noise: of course
    // the thing the focus landed on is the selected one. It is only worth
    // saying when more than one item is selected.
    if (values.states.has_value() && reason == OutputReason::Focus) {
        const StateSet& states = *values.states;
        if (states.has(State::Selectable) && states.has(State::Focusable) &&
            states.has(State::Selected)) {
            if (const ObjectPtr container = object.selectionContainer()) {
                if (container->selectedItemCount(2) == 1) {
                    StateSet reduced = states;
                    reduced.remove(State::Selected);
                    reduced.remove(State::Selectable);
                    values.states = reduced;
                }
            }
        }
    }

    return getPropertiesSpeech(values, reason);
}

bool shouldReportTextContent(const Object& object, OutputReason reason) {
    // NVDA, with its three exclusions:
    //   * FocusEntered and Mouse never read text content -- an ancestor of the
    //     focus and the thing under the pointer are announced, not read;
    //   * the root of a browse-mode document in browse mode does not either,
    //     because the document is read by the tree interceptor (phase 6);
    //   * an object that reports no navigable text has none to read.
    if (reason == OutputReason::FocusEntered || reason == OutputReason::Mouse) {
        return false;
    }
    if (object.treeInterceptor() != nullptr && object.treeInterceptorReady()) {
        // Phase 6 adds the pass-through test; until a tree interceptor exists
        // at all, its presence alone is the answer.
        return false;
    }
    return object.hasNavigableText();
}

speech::SpeechSequence getObjectSpeech(Object& object, OutputReason reason,
                                       const speech::Presenter* presenter) {
    const Settings& settings = object.registry().settings();
    // Phase 5's text layer closed claude/FAZA4.md O10: an object whose text is
    // navigable reads its TEXT, and the value is dropped from the properties so
    // the two are not both spoken. Without a presenter there is no text layer
    // to read it with, and the value is spoken as in phase 4.
    const bool reportTextContent =
        presenter != nullptr && shouldReportTextContent(object, reason);
    const PropertySelection selection =
        allowedProperties(reason, reportTextContent, object.role(), settings);

    // FocusEntered is spoken LIKE focus, with fewer properties -- the
    // properties were already dropped above.
    const OutputReason effective =
        reason == OutputReason::FocusEntered ? OutputReason::Focus : reason;
    speech::SpeechSequence sequence = getObjectPropertiesSpeech(object, effective, selection);

    if (reason == OutputReason::OnlyCache) {
        return sequence;
    }

    // The placeholder is said only when the field is empty, or the user hears
    // both what they typed and what they were invited to type.
    const auto appendPlaceholder = [&] {
        if (!object.placeholder().empty() && object.isTextEmpty()) {
            sequence.push_back(object.placeholder());
        }
    };

    if (reportTextContent) {
        // NVDA's order here is load-bearing. TEXT THAT IS ALREADY SELECTED IS
        // ANNOUNCED AS SELECTED, before anything else about the content: a
        // field whose contents are selected is a field the user is about to
        // overwrite by typing, and that has to be said first.
        text::TextRangePtr range = object.makeTextRange(text::Position::Selection);
        if (range && !range->isCollapsed()) {
            speech::SpeechSequence selected = getPreselectedTextSpeech(range->text());
            for (speech::SpeechItem& item : selected) {
                sequence.push_back(std::move(item));
            }
        } else {
            if (!range) {
                range = object.makeTextRange(text::Position::First);
            }
            appendPlaceholder();
            if (range) {
                range->expand(text::TextUnit::Line);
                TextRangeSpeechOptions options;
                options.unit = text::TextUnit::Line;
                options.reason = OutputReason::Caret;
                const TextRangeSpeechResult text = getTextRangeSpeech(
                    *range, nullptr, settings.format, options, presenter);
                for (const speech::SpeechSequence& part : text.sequences) {
                    for (const speech::SpeechItem& item : part) {
                        sequence.push_back(item);
                    }
                }
            }
        }
    } else {
        appendPlaceholder();
    }

    // A landmark says what kind it is, after the rest.
    if (settings.reportLandmarks && !object.landmark().empty()) {
        if (const char* label = landmarkLabel(object.landmark())) {
            sequence.push_back(std::string{label});
        }
    }
    return sequence;
}

void installSpeechPresentation(Registry& registry, speech::Presenter& presenter) {
    PresentationHooks& hooks = registry.presentation();

    hooks.speakObject = [&presenter](Object& object, OutputReason reason) {
        speech::SpeechSequence sequence = getObjectSpeech(object, reason, &presenter);
        if (sequence.empty()) {
            return;
        }
        presenter.speak(std::move(sequence));
    };

    hooks.speakPropertyChange = [&presenter](Object& object, const PropertyChange& change,
                                             OutputReason reason) {
        PropertySelection selection;
        selection.name = change.name;
        selection.value = change.value;
        selection.description = change.description;
        selection.states = change.states;
        speech::SpeechSequence sequence = getObjectPropertiesSpeech(object, reason, selection);
        if (sequence.empty()) {
            return;
        }
        presenter.speak(std::move(sequence));
    };

    hooks.message = [](const std::string& text) { ui::message(text); };

    // Phase 5 replaced the stand-in here with the real path (FAZA4.md O11):
    // `speakSpelling` names the punctuation, raises the pitch for a capital,
    // and -- on the THIRD press, where `useCharacterDescriptions` is true --
    // describes each character instead of merely naming it.
    hooks.spell = [&presenter](const std::string& text) {
        if (text.empty()) {
            return;
        }
        presenter.speakSpelling(text);
    };
    hooks.describeCharacters = [&presenter](const std::string& text) {
        if (text.empty()) {
            return;
        }
        presenter.speakSpelling(text, {}, /*useCharacterDescriptions=*/true);
    };

    hooks.cancelSpeech = [&presenter] { presenter.cancelSpeech(); };

    hooks.reportProgress = [&presenter, &registry](double percentage, bool beep, bool speak) {
        if (beep) {
            // NVDA: tones.beep(beepMinHZ * 2 ** (percentage / 25.0), 40).
            // Doubling every 25% is what makes a download audible as a rising
            // pitch rather than a number.
            const double hz =
                registry.settings().beepMinHz * std::pow(2.0, percentage / 25.0);
            audio::beep(hz, 40);
        }
        if (speak) {
            // Translators: a progress bar's value; %s is the number, e.g.
            // "42 percent".
            presenter.speakMessage(i18n::substitute(
                _("%s percent"),
                std::to_string(static_cast<int>(std::lround(percentage)))));
        }
    };
}

}  // namespace alus::a11y
