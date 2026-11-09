// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Refinement through `Accessible::get_attributes()` -- §6.5, and the reason
// web content is usable at all.
//
// The finding that makes this cheap: the attribute keys Gecko and Chromium put
// on an AT-SPI accessible are THE SAME KEYS NVDA ALREADY READS as IA2
// attributes -- `xml-roles`, `level`, `tag`, `id`, `placeholder-text`,
// `current`, `haspopup`, `sort`. So the logic ports over from
// NVDAObjects/IAccessible/ia2Web.py and aria.py rather than being invented.
//
// WHEN THE ARIA ROLE WINS. Not always, and this is the one judgement call in
// the file. A toolkit that already mapped `role="button"` to
// ATSPI_ROLE_PUSH_BUTTON needs no help, and overriding a specific role with an
// ARIA token would lose information. So the ARIA role is applied only when the
// platform role is one of the GENERIC ones -- the roles a toolkit falls back
// to when it has nothing better to say. Everything else (level, landmark, tag,
// aria-current, placeholder) is additive and always applied.
#include "alus/a11y/mapping.hpp"
#include "alus/core/log.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace alus::a11y {
namespace {

struct AriaRoleRow {
    const char* aria;
    Role role;
};

// NVDA: aria.ariaRolesToNVDARoles, plus the landmark tokens it folds in.
constexpr std::array<AriaRoleRow, 57> kAriaRoles{{
    {"alert", Role::Alert},
    {"alertdialog", Role::Dialog},
    {"application", Role::Application},
    {"article", Role::Article},
    {"button", Role::Button},
    {"checkbox", Role::CheckBox},
    {"columnheader", Role::TableColumnHeader},
    {"combobox", Role::ComboBox},
    {"comment", Role::Comment},
    {"definition", Role::Definition},
    {"deletion", Role::DeletedContent},
    // Not in the ARIA specification; NVDA keeps it because Google uses it.
    {"description", Role::StaticText},
    {"dialog", Role::Dialog},
    {"directory", Role::List},
    {"document", Role::Document},
    {"figure", Role::Figure},
    {"form", Role::Form},
    {"grid", Role::Table},
    {"gridcell", Role::TableCell},
    {"group", Role::Grouping},
    {"heading", Role::Heading},
    {"img", Role::Graphic},
    {"insertion", Role::InsertedContent},
    {"link", Role::Link},
    {"list", Role::List},
    {"listbox", Role::List},
    {"listitem", Role::ListItem},
    {"mark", Role::MarkedContent},
    {"menu", Role::PopupMenu},
    {"menubar", Role::MenuBar},
    {"menuitem", Role::MenuItem},
    {"menuitemcheckbox", Role::MenuItem},
    {"menuitemradio", Role::MenuItem},
    {"option", Role::ListItem},
    {"progressbar", Role::ProgressBar},
    {"radio", Role::RadioButton},
    {"radiogroup", Role::Grouping},
    {"region", Role::Region},
    {"row", Role::TableRow},
    {"rowgroup", Role::Grouping},
    {"rowheader", Role::TableRowHeader},
    {"scrollbar", Role::ScrollBar},
    {"search", Role::Landmark},
    {"separator", Role::Separator},
    {"slider", Role::Slider},
    {"spinbutton", Role::SpinButton},
    {"status", Role::StatusBar},
    {"suggestion", Role::Suggestion},
    {"tab", Role::Tab},
    {"tablist", Role::TabControl},
    {"tabpanel", Role::PropertyPage},
    {"textbox", Role::EditableText},
    {"toolbar", Role::ToolBar},
    {"tooltip", Role::ToolTip},
    {"tree", Role::TreeView},
    {"treegrid", Role::TreeView},
    {"treeitem", Role::TreeViewItem},
}};

struct LandmarkRow {
    const char* token;
    const char* label;
};

// NVDA: aria.landmarkRoles. The labels are translatable and are what the user
// hears, so they are NVDA's own strings word for word.
constexpr std::array<LandmarkRow, 7> kLandmarks{{
    {"banner", "banner"},
    {"complementary", "complementary"},
    {"contentinfo", "content info"},
    {"main", "main"},
    {"navigation", "navigation"},
    {"search", "search"},
    {"form", "form"},
}};

// The roles a toolkit uses when it has nothing specific to say. Only these are
// overridden by an ARIA role.
bool isGenericRole(Role role) {
    switch (role) {
        case Role::Unknown:
        case Role::Section:
        case Role::Panel:
        case Role::Grouping:
        case Role::Pane:
        case Role::StaticText:
        case Role::Label:
        case Role::Paragraph:
        case Role::Filler:
        case Role::Canvas:
        case Role::RedundantObject:
            return true;
        default:
            return false;
    }
}

struct TagRow {
    const char* tag;
    Role role;
};

// `tag` is the HTML element name. These are the cases where the element says
// more than the AT-SPI role does -- NVDA reads exactly these
// (IAccessibleRolesToNVDARoles' string rows and ia2Web.py's blockquote rule).
constexpr std::array<TagRow, 11> kTags{{
    {"blockquote", Role::BlockQuote},
    {"figure", Role::Figure},
    {"embed", Role::EmbeddedObject},
    {"object", Role::EmbeddedObject},
    {"applet", Role::EmbeddedObject},
    {"fieldset", Role::Grouping},
    {"math", Role::Math},
    {"tbody", Role::TableBody},
    {"thead", Role::TableHeader},
    {"tfoot", Role::TableFooter},
    {"p", Role::Paragraph},
}};

const std::string* find(const Attributes& attributes, std::string_view key) {
    const auto it = attributes.find(key);
    return it == attributes.end() ? nullptr : &it->second;
}

std::vector<std::string_view> split(std::string_view value) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(' ', start);
        const std::string_view part = value.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!part.empty()) {
            parts.push_back(part);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

int parseInt(std::string_view value) {
    int result = 0;
    const auto* first = value.data();
    const auto* last = first + value.size();
    const auto answer = std::from_chars(first, last, result);
    if (answer.ec != std::errc{} || answer.ptr != last) {
        return 0;
    }
    return result;
}

Role headingForLevel(int level) {
    switch (level) {
        case 1:
            return Role::Heading1;
        case 2:
            return Role::Heading2;
        case 3:
            return Role::Heading3;
        case 4:
            return Role::Heading4;
        case 5:
            return Role::Heading5;
        case 6:
            return Role::Heading6;
        default:
            // A heading deeper than 6 has no role of its own; the level is
            // still spoken, from Refined::level.
            return Role::Heading;
    }
}

}  // namespace

std::optional<Role> roleFromAriaRole(std::string_view ariaRole) {
    const auto it = std::find_if(kAriaRoles.begin(), kAriaRoles.end(),
                                 [ariaRole](const AriaRoleRow& row) {
                                     return ariaRole == row.aria;
                                 });
    if (it != kAriaRoles.end()) {
        return it->role;
    }
    // The landmark tokens NVDA folds into the same table.
    if (isLandmarkToken(ariaRole)) {
        return Role::Landmark;
    }
    return std::nullopt;
}

const char* landmarkLabel(std::string_view landmark) {
    const auto it = std::find_if(kLandmarks.begin(), kLandmarks.end(),
                                 [landmark](const LandmarkRow& row) {
                                     return landmark == row.token;
                                 });
    return it == kLandmarks.end() ? nullptr : it->label;
}

bool isLandmarkToken(std::string_view token) { return landmarkLabel(token) != nullptr; }

IsCurrent isCurrentFromString(std::string_view value) {
    if (value.empty() || value == "false") {
        return IsCurrent::No;
    }
    if (value == "page") {
        return IsCurrent::Page;
    }
    if (value == "step") {
        return IsCurrent::Step;
    }
    if (value == "location") {
        return IsCurrent::Location;
    }
    if (value == "date") {
        return IsCurrent::Date;
    }
    if (value == "time") {
        return IsCurrent::Time;
    }
    // NVDA: an unrecognised value still means "current" (IsCurrent.YES's
    // displayString is the fallback).
    return IsCurrent::Yes;
}

const char* isCurrentLabel(IsCurrent value) {
    switch (value) {
        case IsCurrent::No:
            // Nothing extra to say about something that is not current.
            return "";
        case IsCurrent::Yes:
            return "current";
        case IsCurrent::Page:
            return "current page";
        case IsCurrent::Step:
            return "current step";
        case IsCurrent::Location:
            return "current location";
        case IsCurrent::Date:
            return "current date";
        case IsCurrent::Time:
            return "current time";
    }
    return "";
}

Refined refine(Role role, const StateSet& states, const Attributes& attributes) {
    Refined refined;
    refined.role = role;
    refined.states = states;

    const std::vector<std::string_view> xmlRoles =
        find(attributes, "xml-roles") != nullptr ? split(*find(attributes, "xml-roles"))
                                                 : std::vector<std::string_view>{};

    // ---- landmark (NVDA: ia2Web.Ia2Web._get_landmark) ----
    // NVDA only accepts a landmark token that is FIRST in xml-roles, unless
    // the platform role is already a landmark: `<div role="search button">`
    // is a button, not a search landmark.
    for (const std::string_view token : xmlRoles) {
        if (!isLandmarkToken(token)) {
            continue;
        }
        if (token == xmlRoles.front() || role == Role::Landmark) {
            refined.landmark = std::string{token};
        }
        break;
    }

    // ---- the ARIA role, for generic platform roles only ----
    if (!xmlRoles.empty() && isGenericRole(role)) {
        if (const std::optional<Role> ariaRole = roleFromAriaRole(xmlRoles.front())) {
            refined.role = *ariaRole;
        }
    }
    if (!refined.landmark.empty() && isGenericRole(refined.role)) {
        refined.role = Role::Landmark;
    }

    // ---- the HTML element ----
    if (const std::string* tag = find(attributes, "tag")) {
        std::string lowered = *tag;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto it = std::find_if(kTags.begin(), kTags.end(), [&lowered](const TagRow& row) {
            return lowered == row.tag;
        });
        if (it != kTags.end() && isGenericRole(refined.role)) {
            refined.role = it->role;
        }
    }

    // ---- level: a heading's number, or a tree item's depth ----
    if (const std::string* level = find(attributes, "level")) {
        refined.level = parseInt(*level);
        if (refined.level > 0 && refined.role == Role::Heading) {
            refined.role = headingForLevel(refined.level);
        }
    }

    // ---- aria-current ----
    if (const std::string* current = find(attributes, "current")) {
        refined.current = isCurrentFromString(*current);
    }

    // ---- placeholder, spoken only when the field is empty ----
    if (const std::string* placeholder = find(attributes, "placeholder-text")) {
        refined.placeholder = *placeholder;
    } else if (const std::string* aria = find(attributes, "placeholder")) {
        refined.placeholder = *aria;
    }

    // ---- states from attributes (NVDA: ia2Web.Ia2Web._get_states) ----
    // aria-haspopup's four flavours replace the plain HASPOPUP state, so the
    // user hears "has menu" against "has dialog".
    if (refined.states.has(State::HasPopup)) {
        if (const std::string* popup = find(attributes, "haspopup")) {
            const std::string& value = *popup;
            if (value == "dialog") {
                refined.states.remove(State::HasPopup);
                refined.states.add(State::HasPopupDialog);
            } else if (value == "grid") {
                refined.states.remove(State::HasPopup);
                refined.states.add(State::HasPopupGrid);
            } else if (value == "listbox") {
                refined.states.remove(State::HasPopup);
                refined.states.add(State::HasPopupList);
            } else if (value == "tree") {
                refined.states.remove(State::HasPopup);
                refined.states.add(State::HasPopupTree);
            }
        }
    }
    // aria-sort.
    if (const std::string* sort = find(attributes, "sort")) {
        if (*sort == "ascending") {
            refined.states.add(State::SortedAscending);
        } else if (*sort == "descending") {
            refined.states.add(State::SortedDescending);
        } else if (*sort == "other") {
            refined.states.add(State::Sorted);
        }
    }
    // An ARIA grid cell is focusable even when the browser forgets to say so;
    // the selection logic in the presentation layer depends on it.
    if (std::find(xmlRoles.begin(), xmlRoles.end(), "gridcell") != xmlRoles.end()) {
        refined.states.add(State::Focusable);
    }
    // Google Docs turns the editable state off with its own attribute.
    if (const std::string* editable = find(attributes, "goog-editable")) {
        if (*editable == "false") {
            refined.states.remove(State::Editable);
        }
    }

    transformRoleStates(refined.role, refined.states);
    return refined;
}

}  // namespace alus::a11y
