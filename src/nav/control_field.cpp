// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/control_field.hpp.
#include "alus/nav/control_field.hpp"

#include "alus/core/log.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace alus::nav {
namespace {

using a11y::Role;
using a11y::State;

// The four states a control field never reports. The first three are NVDA's
// own `states.discard` calls; FOCUSABLE is the measured addition, because
// every single block of a Gecko document carries it and it says nothing about
// the control the user just entered.
constexpr std::array<State, 4> kDiscardedStates{
    State::Editable,
    State::MultiLine,
    State::Focused,
    State::Focusable,
};

// Whether a control occupies its own block. There is no "display: block"
// attribute on AT-SPI to ask for, so the role is the honest answer -- and it
// is the answer the two callers need: a roleText field reads as a container
// rather than a single line, and say-all breaks an utterance at a block edge.
bool isBlockRole(Role role) {
    switch (role) {
        case Role::Paragraph:
        case Role::Heading:
        case Role::Heading1:
        case Role::Heading2:
        case Role::Heading3:
        case Role::Heading4:
        case Role::Heading5:
        case Role::Heading6:
        case Role::BlockQuote:
        case Role::List:
        case Role::ListItem:
        case Role::Table:
        case Role::TableCell:
        case Role::TableColumnHeader:
        case Role::TableRowHeader:
        case Role::Section:
        case Role::Landmark:
        case Role::Region:
        case Role::Article:
        case Role::Figure:
        case Role::Grouping:
        case Role::Separator:
        case Role::InternalFrame:
        case Role::Dialog:
        case Role::Document:
            return true;
        default:
            return false;
    }
}

bool isTableCellRole(Role role) {
    return role == Role::TableCell || role == Role::TableColumnHeader ||
           role == Role::TableRowHeader;
}

// The attribute spellings that mean "this text exists but is not drawn".
// `hidden` is what NVDA's backends emit; `invisible` is what ATK actually
// uses on this desktop and what N46 was about. Both are accepted because an
// app module may bring either.
bool attributeSaysHidden(const text::Field& field) {
    for (const std::string_view key : {"isHidden", "hidden", "invisible"}) {
        const std::string_view value = field.get(key);
        if (value == "1" || value == "true") {
            return true;
        }
    }
    return false;
}

void setIntIfPositive(text::ControlField& field, const char* key, int value) {
    if (value > 0) {
        field.set(key, value);
    }
}

}  // namespace

bool hasText(const a11y::Object& object) {
    return object.hasInterface(a11y::Interface::Text) && object.characterCount() > 0;
}

bool isObjectEditableText(const a11y::Object& object) {
    const Role role = object.role();
    return role == Role::Paragraph || role == Role::EditableText || role == Role::PasswordEdit;
}

bool isNamedLinkDestination(const a11y::Object& object) {
    if (object.role() != Role::Link) {
        return false;
    }
    // DEVIATION FROM NVDA, AND A FORCED ONE. NVDA tests `State.LINKED`, which
    // comes from IA2. AT-SPI HAS NO SUCH STATE -- ALUS synthesises
    // `State::Linked` from the Link role itself (src/a11y/state_map.cpp:160),
    // so testing it here would always answer "it is a link" and this function
    // would be dead code.
    //
    // What distinguishes `<a name="x">` from `<a href="...">` on AT-SPI is
    // that the first has NOTHING TO ACTIVATE: no action, and no URI on its
    // hyperlink. That is the test, and it is the one the user can hear the
    // difference of -- a link announced for an anchor sends them hunting for
    // something that cannot be pressed.
    if (object.actionCount() > 0) {
        return false;
    }
    return !object.states().has(State::Focusable);
}

// ---------------------------------------------------------------------------
// The normaliser
// ---------------------------------------------------------------------------

void normaliseControlField(text::ControlField& field) {
    // `table-layout` arrives as "1"/"0" from an attribute and as a typed
    // member from the synthesiser. One of the two wins and it is the attribute,
    // because a caller that set the string meant it.
    if (field.contains("table-layout")) {
        field.tableLayout = field.get("table-layout") == "1" ||
                            field.get("table-layout") == "true";
        field.erase("table-layout");
    }

    // The table keys are integers. A key that is present but not a number is
    // REMOVED rather than kept as text: the presentation layer formats these
    // into "row 3 of 12", and "row  of 12" is worse than saying nothing.
    for (const std::string_view key : {"table-id", "table-rownumber", "table-columnnumber",
                                       "table-rowsspanned", "table-columnsspanned",
                                       "table-rowcount", "table-columncount"}) {
        if (!field.contains(key)) {
            continue;
        }
        if (const std::optional<int> value = field.getInt(key)) {
            field.set(std::string(key), *value);
        } else {
            ALUS_DEBUG("control field: dropping non-numeric {} = '{}'", key, field.get(key));
            field.erase(key);
        }
    }

    if (attributeSaysHidden(field)) {
        field.isHidden = true;
    }
    field.erase("hidden");
    field.erase("invisible");
    field.erase("isHidden");

    // A self-referential header. Chrome emits a column header cell whose own
    // column header is itself; said as it stands, the user hears the heading
    // twice for every cell in that row.
    const std::string_view uniqueId = field.uniqueId;
    for (const std::string_view axis : {"table-rowheadercell", "table-columnheadercell"}) {
        if (field.contains(axis) && !uniqueId.empty() && field.get(axis) == uniqueId) {
            const std::string textKey =
                axis == std::string_view("table-rowheadercell") ? "table-rowheadertext"
                                                                : "table-columnheadertext";
            field.erase(textKey);
        }
        field.erase(axis);
    }
    // The same rule for the already-flattened text: a header cell whose header
    // text is its own name has nothing to add.
    if (isTableCellRole(field.role) && !field.name.empty()) {
        for (const std::string_view key : {"table-rowheadertext", "table-columnheadertext"}) {
            if (field.get(key) == field.name) {
                field.erase(key);
            }
        }
    }

    // NVDA: an unnamed landmark or region is not worth offering, and a named
    // one IS its name -- so the name is reported whatever the output reason.
    if (field.role == Role::Landmark || field.role == Role::Region) {
        field.set("alwaysReportName", true);
    }

    if (!field.uniqueId.empty()) {
        field.set("uniqueID", field.uniqueId);
    }
}

// ---------------------------------------------------------------------------
// The synthesiser
// ---------------------------------------------------------------------------

ControlFieldSynth::ControlFieldSynth(const text::FormatReportSettings& settings)
    : settings_(settings) {}

std::string ControlFieldSynth::uniqueIdFor(const a11y::Object& object) {
    // §9.1: (bus name, object path). The bus name alone is not enough (every
    // object in one application shares it) and the path alone is not enough
    // either (two applications both have /org/a11y/atspi/accessible/root).
    const std::string& path = object.path();
    if (path.empty()) {
        return {};
    }
    return object.appInfo().busName + path;
}

std::optional<text::ControlField> ControlFieldSynth::forObject(const a11y::Object& object,
                                                              bool ignoreEditableText) const {
    if (!object.isAlive()) {
        return std::nullopt;
    }
    if (ignoreEditableText && isObjectEditableText(object)) {
        // Decision: this is a text node, not a control. Its text is the
        // document's text and nothing is announced about it.
        return std::nullopt;
    }
    if (isNamedLinkDestination(object)) {
        return std::nullopt;
    }

    text::ControlField field;
    field.role = object.role();
    field.roleText = object.roleText();
    field.name = object.name();
    // A NAME THAT IS ONLY THE OBJECT'S OWN TEXT IS NOT SAID TWICE. Chrome
    // names a heading or a link after its content and marks a name the AUTHOR
    // wrote with `explicit-name=true` (measured: toolbar buttons have it, the
    // page's headings and links do not). Said as a name and then again as the
    // text, `h` read "Prvi naslov stranice, Prvi naslov stranice, heading
    // level 1" (phase-10.md N116). NVDA keys the same decision on the same
    // attribute; the text comparison is there for a toolkit that does not send
    // it, so that a real label is never dropped on a guess.
    if (!field.name.empty() && hasText(object)) {
        const auto& attributes = object.attributes();
        const auto explicitName = attributes.find("explicit-name");
        // A button whose author-given name IS its text says it once either
        // way: Chrome marks <input type=reset value=Clear> explicit, and f
        // read "Clear Clear button" (N142).
        const bool isButton = field.role == Role::Button || field.role == Role::ToggleButton;
        if (isButton || explicitName == attributes.end() || explicitName->second != "true") {
            const auto trimmed = [](std::string_view value) {
                const std::size_t first = value.find_first_not_of(" \t\n\r");
                if (first == std::string_view::npos) {
                    return std::string_view{};
                }
                const std::size_t last = value.find_last_not_of(" \t\n\r");
                return value.substr(first, last - first + 1);
            };
            const std::string own = object.textRange(0, -1);
            if (trimmed(own) == trimmed(field.name)) {
                field.set("labelledByContent", true);
            }
        }
    }
    field.landmark = object.landmark();
    field.uniqueId = uniqueIdFor(object);

    a11y::StateSet states = object.states();
    for (const State state : kDiscardedStates) {
        states.remove(state);
    }
    // DOCUMENT CONTENT IS READ-ONLY UNLESS IT IS EDITABLE, which is what
    // NVDA's virtual buffers make it: their backends add READONLY to every
    // node that is not editable, and the presentation rules are written
    // against that. ALUS reads the live tree, where Chrome and Firefox set no
    // READ_ONLY on a <ul> -- so a list in a page fell into the "list box"
    // branch of text::getPresentationCategory (a list without READONLY is a
    // single-line control) and "list" was spoken on EVERY line inside it
    // instead of once on entry. The same state is what the "report lists"
    // setting is tested against, so that switch did nothing on the web either.
    // phase-10.md N115.
    //
    // An editable object keeps its states untouched: a list box in a form is
    // focusable, not editable, so the FOCUSABLE test is what keeps it a
    // control, and that is the distinction the category rule draws.
    //
    // Asked of the OBJECT's states, not of `states`: FOCUSABLE is one of the
    // discarded states above (no screen reader says "focusable"), so testing
    // the filtered set made every list box read-only too.
    if (!object.states().has(State::Editable) && !object.states().has(State::Focusable)) {
        states.add(State::ReadOnly);
    }
    field.states = states;

    if (!object.description().empty()) {
        field.set("description", object.description());
    }
    if (!object.value().empty()) {
        field.set("value", object.value());
    }
    if (!object.keyboardShortcut().empty()) {
        field.set("keyboardShortcut", object.keyboardShortcut());
    }
    if (!object.placeholder().empty()) {
        field.set("placeholder", object.placeholder());
    }
    if (object.isCurrent() != a11y::IsCurrent::No) {
        // The RAW aria-current token, not the label: the presentation layer
        // parses it back with `isCurrentFromString`, so the field round-trips
        // through an app module and through braille (phase 7) unchanged.
        const auto& attributes = object.attributes();
        const auto current = attributes.find("current");
        field.set("current", current != attributes.end() ? current->second : std::string("true"));
    }
    if (!object.details().empty()) {
        field.set("hasDetails", true);
    }
    if (states.has(State::InvalidEntry) && !object.errorMessage().empty()) {
        field.set("errorMessage", object.errorMessage());
    }

    // `_childcount` is what the "list with 5 items" announcement counts, and
    // NVDA spells the key `_childcontrolcount` where the presentation reads it.
    const int childCount = object.childCount();
    setIntIfPositive(field, "_childcontrolcount", childCount);

    const int level = object.positionInfo().level;
    setIntIfPositive(field, "level", level);

    // A HEADING IS `Heading` WITH A LEVEL in a document field, as it is in
    // NVDA's virtual buffers -- not `Heading1`..`Heading6`, which is how the
    // object model names it (AT-SPI's heading plus its `level` attribute, the
    // way NVDA presents a heading OBJECT). The presentation rules for fields
    // test `Role::Heading` alone: a `Heading2` field matched no category,
    // fell through to Layout and was silent, so neither the down arrow nor `h`
    // ever said "heading level 2" -- only the heading's text (phase-10.md N116).
    if (field.role >= Role::Heading1 && field.role <= Role::Heading6) {
        const int fromRole = static_cast<int>(field.role) - static_cast<int>(Role::Heading1) + 1;
        field.role = Role::Heading;
        if (!field.contains("level")) {
            field.set("level", fromRole);
        }
    }

    // Whether the control is its own block. The presentation layer needs it
    // for a roleText field and say-all needs it to break an utterance, and the
    // honest answer on AT-SPI is the role: there is no "display: block"
    // attribute to ask for.
    field.isBlock = isBlockRole(field.role);

    if (field.role == Role::Table) {
        const a11y::TableInfo& info = object.tableInfo();
        field.set("table-id", static_cast<int>(object.ref()));
        setIntIfPositive(field, "table-rowcount", info.rowCount);
        setIntIfPositive(field, "table-columncount", info.columnCount);
        // A layout table: one with no headers, used for positioning. The
        // attribute is the author's own declaration where there is one.
        const auto& attributes = object.attributes();
        const auto layout = attributes.find("layout-guess");
        if (layout != attributes.end()) {
            field.set("table-layout", layout->second);
        }
    }

    if (isTableCellRole(field.role)) {
        const a11y::CellInfo& cell = object.cellInfo();
        if (const a11y::ObjectPtr table = object.table()) {
            field.set("table-id", static_cast<int>(table->ref()));
        } else {
            field.set("table-id", static_cast<int>(object.ref()));
        }
        // AT-SPI counts rows and columns from 0; everything above this line
        // and every word the user hears counts from 1.
        if (cell.row >= 0) {
            field.set("table-rownumber", cell.row + 1);
        }
        if (cell.column >= 0) {
            field.set("table-columnnumber", cell.column + 1);
        }
        if (cell.rowSpan > 1) {
            field.set("table-rowsspanned", cell.rowSpan);
        }
        if (cell.columnSpan > 1) {
            field.set("table-columnsspanned", cell.columnSpan);
        }
        if (!object.rowHeaderText().empty()) {
            field.set("table-rowheadertext", object.rowHeaderText());
        }
        if (!object.columnHeaderText().empty()) {
            field.set("table-columnheadertext", object.columnHeaderText());
        }
    }

    normaliseControlField(field);
    return field;
}

}  // namespace alus::nav
