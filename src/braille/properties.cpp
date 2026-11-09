// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/properties.hpp.

#include "alus/braille/properties.hpp"

#include "alus/a11y/object_speech.hpp"
#include "alus/braille/formatting.hpp"
#include "alus/braille/labels.hpp"
#include "alus/i18n/i18n.hpp"

#include <algorithm>
#include <string>

namespace alus::braille {
namespace {

void append(std::vector<std::string>& into, std::string piece) {
    if (!piece.empty()) {
        into.push_back(std::move(piece));
    }
}

std::string join(const std::vector<std::string>& pieces) {
    std::string out;
    for (const std::string& piece : pieces) {
        if (piece.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += kTextSeparator;
        }
        out += piece;
    }
    return out;
}

// NVDA's braille state labelling: the same ~30 rules speech uses
// (`a11y::processStates`), then the ABBREVIATIONS -- and a state with no
// abbreviation says nothing at all, which is what keeps a focus change inside
// a tree from filling the display with words.
std::vector<std::string> stateLabels(const PropertyValues& values,
                                     const a11y::StateSet& states) {
    const a11y::ProcessedStates processed =
        a11y::processStates(values.realRole != a11y::Role::Unknown ? values.realRole
                                                                   : values.role.value_or(
                                                                         a11y::Role::Unknown),
                            values.realStates, values.reason, states, a11y::StateSet{},
                            values.settings);
    std::vector<std::string> labels;
    for (const a11y::State state : (processed.positive | processed.negative).toVector()) {
        const std::string_view label = processed.positive.has(state)
                                           ? statePositiveAbbreviation(state)
                                           : stateNegativeAbbreviation(state);
        if (!label.empty()) {
            labels.emplace_back(label);
        }
    }
    return labels;
}

std::string detailsText(const PropertyValues& values) {
    std::vector<std::string> pieces;
    if (values.hasGenericDetails || values.detailsRoles.empty()) {
        // Translators: brailled when there are further details that can be
        // fetched manually, and the kind is unknown.
        pieces.emplace_back(_("details"));
    }
    for (const a11y::Role role : values.detailsRoles) {
        // Translators: brailled when there are details of a known kind; %s is
        // the abbreviation of the kind, e.g. "cmnt".
        pieces.push_back(i18n::substitute(_("has %s"), roleBrailleLabel(role)));
    }
    // No comma: every separator is a cell the content needed.
    return join(pieces);
}

// `pattern` is an already-translated one-placeholder pattern ("r%s"); the
// placeholder takes either a number or a range of numbers. The pattern and not
// a bare prefix, because a language may put the letter after the number.
std::string numberRange(const char* pattern, int first, int span) {
    std::string value = std::to_string(first);
    if (span > 1) {
        value += "-";
        value += std::to_string(first + span - 1);
    }
    return i18n::substitute(pattern, value);
}

}  // namespace

// ---------------------------------------------------------------------------
// getPropertiesBraille
// ---------------------------------------------------------------------------

std::string getPropertiesBraille(const PropertyValues& values) {
    std::vector<std::string> pieces;
    if (values.name.has_value()) {
        append(pieces, *values.name);
    }

    // The role text is computed first, because three roles COLLAPSE a state
    // into the role abbreviation and the state must then not be reported
    // again. NVDA does the same thing in the same order.
    std::string roleText = values.roleText;
    a11y::StateSet states = values.states.value_or(a11y::StateSet{});
    std::optional<int> level = values.level;
    std::optional<int> childControlCount = values.childControlCount;
    const a11y::Role role = values.role.value_or(values.realRole);

    if (values.role.has_value() && roleText.empty()) {
        if (role == a11y::Role::Heading && level.has_value() && *level > 0) {
            // Translators: brailled for a heading with a level; %s is the
            // level, so the result reads "h2".
            roleText = i18n::substitute(_("h%s"), std::to_string(*level));
            level.reset();
        } else if (role == a11y::Role::Link && states.has(a11y::State::Visited)) {
            states.remove(a11y::State::Visited);
            // Translators: brailled for a link which has been visited.
            roleText = _("vlnk");
        } else if (role == a11y::Role::List) {
            if (states.has(a11y::State::MultiSelectable) &&
                values.settings.reportMultiSelect) {
                states.remove(a11y::State::MultiSelectable);
                // Translators: brailled for a multi select list.
                roleText = _("mslst");
            } else {
                roleText = roleBrailleLabel(role);
            }
            if (childControlCount.has_value() && *childControlCount > 0) {
                roleText += std::to_string(*childControlCount);
                childControlCount.reset();
            }
        } else if ((values.name.has_value() || values.cellCoordsText.has_value() ||
                    values.rowNumber.has_value() || values.columnNumber.has_value()) &&
                   a11y::isSilentOnFocus(role)) {
            // Something more specific is already being shown; the role would
            // only cost cells.
            roleText.clear();
        } else {
            roleText = roleBrailleLabel(role);
        }
    }

    if (values.value.has_value() && !a11y::isSilentValue(role)) {
        append(pieces, *values.value);
    }
    if (values.states.has_value()) {
        PropertyValues forStates = values;
        forStates.states = states;
        for (std::string& label : stateLabels(forStates, states)) {
            append(pieces, std::move(label));
        }
    }
    append(pieces, roleText);

    if (values.errorMessage.has_value()) {
        append(pieces, *values.errorMessage);
    }
    if (values.description.has_value()) {
        append(pieces, *values.description);
    }
    if (values.hasDetails) {
        append(pieces, detailsText(values));
    }
    if (values.keyboardShortcut.has_value()) {
        append(pieces, *values.keyboardShortcut);
    }

    if (values.indexInGroup.has_value() && values.similarItemsInGroup.has_value() &&
        *values.indexInGroup > 0 && *values.similarItemsInGroup > 0) {
        // Translators: the position of an item in a group, brailled; the
        // first %s is the item's number and the second how many there are, so
        // the result reads "3 of 7".
        append(pieces, i18n::substitute(_("%s of %s"), std::to_string(*values.indexInGroup),
                                        std::to_string(*values.similarItemsInGroup)));
    }
    if (level.has_value() && *level > 0) {
        // Translators: brailled when an object has a hierarchical level; %s
        // is the level.
        append(pieces, i18n::substitute(_("lv %s"), std::to_string(*level)));
    }

    if (values.rowNumber.has_value() && *values.rowNumber > 0 &&
        values.includeTableCellCoords && !values.cellCoordsText.has_value()) {
        // Translators: a table cell's row number, brailled; %s is the number
        // or the range of numbers, so the result reads "r3" or "r3-4".
        append(pieces, numberRange(_("r%s"), *values.rowNumber, values.rowSpan));
    }
    if (values.columnNumber.has_value() && *values.columnNumber > 0) {
        if (values.columnHeaderText.has_value()) {
            append(pieces, *values.columnHeaderText);
        }
        if (values.includeTableCellCoords && !values.cellCoordsText.has_value()) {
            // Translators: a table cell's column number, brailled; %s is the
            // number or the range of numbers, so the result reads "c2".
            append(pieces, numberRange(_("c%s"), *values.columnNumber, values.columnSpan));
        }
    }

    if (values.current != a11y::IsCurrent::No) {
        append(pieces, std::string{isCurrentAbbreviation(values.current)});
    }
    if (values.placeholder.has_value()) {
        append(pieces, *values.placeholder);
    }
    if (values.includeTableCellCoords && values.cellCoordsText.has_value()) {
        append(pieces, *values.cellCoordsText);
    }
    return join(pieces);
}

// ---------------------------------------------------------------------------
// getControlFieldBraille
// ---------------------------------------------------------------------------

namespace {

// NVDA: `_getControlFieldForLayoutPresentation`. A layout container is worth
// nothing EXCEPT the three things that are news whatever the container is.
std::optional<std::string> layoutPresentation(const text::ControlField& field,
                                              const PropertyValues& base) {
    std::vector<std::string> pieces;
    if (base.description.has_value()) {
        PropertyValues values;
        values.settings = base.settings;
        values.description = base.description;
        append(pieces, getPropertiesBraille(values));
    }
    if (base.current != a11y::IsCurrent::No) {
        PropertyValues values;
        values.settings = base.settings;
        values.current = base.current;
        append(pieces, getPropertiesBraille(values));
    }
    if (base.hasDetails) {
        PropertyValues values;
        values.settings = base.settings;
        values.hasDetails = true;
        values.detailsRoles = base.detailsRoles;
        values.hasGenericDetails = base.hasGenericDetails;
        append(pieces, getPropertiesBraille(values));
    }
    if (field.role == a11y::Role::Graphic) {
        append(pieces, std::string{field.get("content")});
    }
    if (pieces.empty()) {
        return std::nullopt;
    }
    return join(pieces);
}

std::optional<int> fieldInt(const text::ControlField& field, std::string_view key) {
    if (const std::optional<int> value = field.getInt(key); value.has_value()) {
        return value;
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::string> getControlFieldBraille(
    const text::ControlField& field, const std::vector<text::ControlField>& ancestors,
    bool reportStart, const text::FormatReportSettings& format,
    const a11y::Settings& objectSettings, const Settings& brailleSettings,
    text::PresentationCategory* category) {
    using text::PresentationCategory;

    const PresentationCategory presCat = field.getPresentationCategory(ancestors, format);
    if (category != nullptr) {
        *category = presCat;
    }
    const a11y::Role role = field.role;

    if (reportStart) {
        // A container is announced when the user ENTERS it, which in this
        // stream means the node begins here (`_startOfNode`, set by
        // nav::AncestorChainDiff). Without that test, the third item of a
        // list would re-announce the list on every line.
        if (presCat == PresentationCategory::Container &&
            !field.getBool("_startOfNode").value_or(false)) {
            return std::nullopt;
        }
    } else {
        // Ends are reported for containers only, and never for a landmark:
        // "lmk end" is noise, the landmark's own text already moved on.
        if (presCat != PresentationCategory::Container || role == a11y::Role::Landmark) {
            return std::nullopt;
        }
    }

    PropertyValues values;
    values.settings = objectSettings;
    values.reason = a11y::OutputReason::Focus;
    values.realRole = role;
    values.realStates = field.states;

    // NVDA shows only an ARIA description here: an ordinary `title`
    // attribute would fill the display with what is visually obvious, and
    // `reportObjectDescriptions` is about focus and object navigation, not
    // about a document being read.
    //
    // DEVIATION (claude/FAZA7.md O3): ALUS has no `descriptionFrom` -- AT-SPI
    // reports a description without saying where it came from, and phases 4
    // to 6 never needed to know. So `reportAriaDescription`, whose entire
    // purpose is this one decision, governs ALL document descriptions in
    // braille. The failure mode is a `title` attribute reaching the display
    // for a user who turned ARIA descriptions on; the alternative is losing
    // the ARIA descriptions that setting exists for.
    if (!field.getBool("descriptionIsContent").value_or(false) &&
        objectSettings.reportAriaDescription) {
        if (const std::string_view description = field.get("description");
            !description.empty()) {
            values.description = std::string{description};
        }
    }
    values.current = a11y::isCurrentFromString(field.get("current"));
    values.hasDetails =
        field.getBool("hasDetails").value_or(false) && objectSettings.reportDetails;
    if (values.hasDetails) {
        values.hasGenericDetails = true;
    }

    if (presCat == PresentationCategory::Layout) {
        return layoutPresentation(field, values);
    }

    const bool isCell = role == a11y::Role::TableCell ||
                        role == a11y::Role::TableColumnHeader ||
                        role == a11y::Role::TableRowHeader;
    if (isCell && field.contains("table-id")) {
        // NVDA: `_getControlFieldForTableCell`.
        values.states = field.states;
        values.rowNumber = fieldInt(field, "table-rownumber-presentational")
                               .value_or(fieldInt(field, "table-rownumber").value_or(0));
        values.columnNumber = fieldInt(field, "table-columnnumber-presentational")
                                  .value_or(fieldInt(field, "table-columnnumber").value_or(0));
        values.rowSpan = std::max(1, fieldInt(field, "table-rowsspanned").value_or(1));
        values.columnSpan = std::max(1, fieldInt(field, "table-columnsspanned").value_or(1));
        values.includeTableCellCoords = format.reportTableCellCoords;
        if (objectSettings.reportTableHeaders == a11y::TableHeaders::RowsAndColumns ||
            objectSettings.reportTableHeaders == a11y::TableHeaders::Columns) {
            if (const std::string_view header = field.get("table-columnheadertext");
                !header.empty()) {
                values.columnHeaderText = std::string{header};
            }
        }
        return getPropertiesBraille(values);
    }

    std::string roleText = field.roleText;
    if (roleText.empty() && role == a11y::Role::Landmark && !field.landmark.empty()) {
        const std::string_view abbreviation = landmarkAbbreviation(field.landmark);
        roleText = roleBrailleLabel(a11y::Role::Landmark);
        roleText += kTextSeparator;
        roleText += abbreviation.empty() ? field.landmark : std::string{abbreviation};
    }

    if (!reportStart) {
        // Translators: brailled at the end of a control field such as a list
        // or a table; "%s end".
        PropertyValues endValues;
        endValues.settings = objectSettings;
        endValues.realRole = role;
        endValues.role = role;
        endValues.roleText = roleText;
        return getPropertiesBraille(endValues) + " end";
    }

    // NVDA: `_getControlFieldForReportStart`.
    values.states = field.states;
    if (const std::string_view value = field.get("value"); !value.empty()) {
        values.value = std::string{value};
    }
    if (const std::string_view placeholder = field.get("placeholder"); !placeholder.empty()) {
        values.placeholder = std::string{placeholder};
    }
    values.roleText = roleText;
    if (field.states.has(a11y::State::InvalidEntry)) {
        if (const std::string_view message = field.get("errorMessage"); !message.empty()) {
            values.errorMessage = std::string{message};
        }
    }
    // Math gets no role: the braille of the formula IS the content, and "math"
    // before it costs four cells to say what the next ten already say.
    if (role != a11y::Role::Math) {
        values.role = role;
    }
    if (field.getBool("alwaysReportName").value_or(false) && !field.name.empty()) {
        values.name = field.name;
    }
    if (objectSettings.reportKeyboardShortcuts) {
        if (const std::string_view shortcut = field.get("keyboardShortcut");
            !shortcut.empty()) {
            values.keyboardShortcut = std::string{shortcut};
        }
    }
    if (const std::optional<int> level = fieldInt(field, "level");
        level.has_value() && *level > 0) {
        values.level = level;
    }
    if (role == a11y::Role::List) {
        if (const std::optional<int> count = fieldInt(field, "_childcontrolcount");
            count.has_value() && *count > 0) {
            values.childControlCount = count;
        }
    }

    std::string text = getPropertiesBraille(values);
    if (const std::string_view content = field.get("content"); !content.empty()) {
        if (!text.empty()) {
            text += kTextSeparator;
        }
        text += content;
    }
    (void)brailleSettings;
    return text;
}

// ---------------------------------------------------------------------------
// getFormatFieldBraille
// ---------------------------------------------------------------------------

std::string getFormatFieldBraille(const text::FormatField& field, text::FormatField& cache,
                                  bool isAtStart, const text::FormatReportSettings& format,
                                  const Settings& brailleSettings) {
    std::vector<std::string> pieces;
    if (isAtStart) {
        append(pieces, paragraphStartMarker(brailleSettings));
        if (format.reportLineNumber) {
            append(pieces, std::string{field.get("line-number")});
        }
        append(pieces, std::string{field.get("line-prefix")});
        if (format.reportHeadings) {
            if (const std::optional<int> level = field.getInt("heading-level");
                level.has_value() && *level > 0) {
                // Translators: brailled for a heading with a level; %s is
                // the level, so the result reads "h2".
                append(pieces, i18n::substitute(_("h%s"), std::to_string(*level)));
            }
        }
        if (field.getBool("collapsed").value_or(false)) {
            append(pieces, std::string{statePositiveAbbreviation(a11y::State::Collapsed)});
        }
    }
    if (format.reportLinks) {
        const std::string_view link = field.get("link");
        const std::string_view wasLink = cache.get("link");
        if (!link.empty() && link != wasLink) {
            append(pieces, roleBrailleLabel(a11y::Role::Link));
        }
    }
    if (format.reportComments) {
        const std::string_view comment = field.get("comment");
        const std::string_view wasComment = cache.get("comment");
        if (comment != wasComment && !comment.empty()) {
            if (comment == "draft") {
                // Translators: brailled when text contains a draft comment.
                append(pieces, _("drft cmnt"));
            } else if (comment == "resolved") {
                // Translators: brailled when text contains a resolved comment.
                append(pieces, _("rslvd cmnt"));
            } else {
                // Translators: brailled when text contains a comment.
                append(pieces, _("cmnt"));
            }
        }
    }
    if (format.reportBookmarks) {
        const std::string_view bookmark = field.get("bookmark");
        const std::string_view wasBookmark = cache.get("bookmark");
        if (bookmark != wasBookmark && !bookmark.empty()) {
            // Translators: brailled when text contains a bookmark.
            append(pieces, _("bkmk"));
        }
    }
    if (brailleSettings.fontFormattingDisplay == FontFormattingDisplay::Tags) {
        if (const std::optional<std::string> tags = formattingTags(field, &cache,
                                                                   brailleSettings);
            tags.has_value()) {
            append(pieces, *tags);
        }
    }
    // The cache is in/out: NVDA clears and refills the same dict here, and the
    // caller keeps it across region updates so a bold run reports once.
    cache = field;
    return join(pieces);
}

// ---------------------------------------------------------------------------
// An object rather than a field
// ---------------------------------------------------------------------------

bool hasUsefulText(const a11y::Object& object) {
    if (!object.isAlive()) {
        return false;
    }
    // NVDA also returns true for anything reviewed through the display model;
    // ALUS's equivalent is screen review, whose ranges are not over an object
    // at all, so the test reduces to the two real cases.
    return object.hasNavigableText() || object.states().has(a11y::State::Editable);
}

PropertyValues propertiesForObject(const a11y::Object& object, const a11y::Settings& settings,
                                   const Settings& brailleSettings, bool objectHasUsefulText) {
    PropertyValues values;
    values.settings = settings;
    values.reason = a11y::OutputReason::Focus;
    values.realRole = object.role();
    values.realStates = object.states();

    values.name = object.name();
    values.role = object.role();
    values.roleText = object.roleText();
    values.states = object.states();
    values.current = object.isCurrent();

    if (!object.placeholder().empty() && object.isTextEmpty()) {
        values.placeholder = object.placeholder();
    }
    if (!object.errorMessage().empty() && object.states().has(a11y::State::InvalidEntry)) {
        values.errorMessage = object.errorMessage();
    }

    // NVDA's rule, verbatim: a description that merely repeats the name is two
    // dozen wasted cells, and an ARIA description is worth showing even when
    // ordinary descriptions are off.
    const std::string& description = object.description();
    if (!description.empty() && description != object.name() &&
        settings.reportObjectDescriptions) {
        values.description = description;
    }

    if (!objectHasUsefulText && !object.value().empty()) {
        values.value = object.value();
    }
    if (settings.reportKeyboardShortcuts && !object.keyboardShortcut().empty()) {
        values.keyboardShortcut = object.keyboardShortcut();
    }
    if (settings.reportObjectPositionInformation) {
        const a11y::PositionInfo position = object.positionInfo();
        if (position.indexInGroup > 0) {
            values.indexInGroup = position.indexInGroup;
        }
        if (position.similarItemsInGroup > 0) {
            values.similarItemsInGroup = position.similarItemsInGroup;
        }
        if (position.level > 0) {
            values.level = position.level;
        }
    } else if (object.level() > 0) {
        values.level = object.level();
    }
    if (settings.reportTableCellCoords && !object.cellCoordsText().empty()) {
        values.cellCoordsText = object.cellCoordsText();
    }
    if (settings.reportDetails && !object.details().empty()) {
        values.hasDetails = true;
        for (const a11y::ObjectPtr& detail : object.details()) {
            if (detail != nullptr) {
                values.detailsRoles.push_back(detail->role());
            }
        }
        if (values.detailsRoles.empty()) {
            values.hasGenericDetails = true;
        }
    }
    (void)brailleSettings;
    return values;
}

}  // namespace alus::braille
