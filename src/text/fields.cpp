// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/text/fields.hpp.

#include "alus/text/fields.hpp"

#include <algorithm>
#include <charconv>

namespace alus::text {
namespace {

using a11y::Role;
using a11y::State;
using a11y::StateSet;

bool isOneOf(Role role, std::initializer_list<Role> roles) {
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

}  // namespace

std::string_view Field::get(std::string_view key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return {};
    }
    return it->second;
}

std::optional<bool> Field::getBool(std::string_view key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    const std::string& value = it->second;
    // Every spelling a toolkit has been seen to use. The normaliser already
    // reduces them, but an app module (phase 8) writes fields by hand.
    if (value == "1" || value == "true" || value == "True" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "False" || value == "no" ||
        value.empty()) {
        return false;
    }
    return std::nullopt;
}

std::optional<int> Field::getInt(std::string_view key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    int result = 0;
    const char* begin = it->second.data();
    const char* end = begin + it->second.size();
    const auto [stop, error] = std::from_chars(begin, end, result);
    if (error != std::errc{} || stop == begin) {
        return std::nullopt;
    }
    return result;
}

std::optional<double> Field::getDouble(std::string_view key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const double result = std::stod(it->second, &consumed);
        if (consumed == 0) {
            return std::nullopt;
        }
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void Field::set(std::string key, std::string value) {
    values_[std::move(key)] = std::move(value);
}

void Field::set(std::string key, bool value) {
    values_[std::move(key)] = value ? "1" : "0";
}

void Field::set(std::string key, int value) {
    values_[std::move(key)] = std::to_string(value);
}

void Field::erase(std::string_view key) {
    const auto it = values_.find(key);
    if (it != values_.end()) {
        values_.erase(it);
    }
}

void Field::update(const Field& other) {
    for (const auto& [key, value] : other.values_) {
        values_[key] = value;
    }
}

const char* presentationCategoryName(PresentationCategory category) {
    switch (category) {
        case PresentationCategory::Layout:
            return "layout";
        case PresentationCategory::SingleLine:
            return "singleLine";
        case PresentationCategory::Marker:
            return "marker";
        case PresentationCategory::Container:
            return "container";
        case PresentationCategory::Cell:
            return "cell";
    }
    return "layout";
}

bool ControlField::operator==(const ControlField& other) const noexcept {
    // NVDA #2199: a uniqueID, when either side has one, decides on its own.
    // Two fields with the same id ARE the same field, whatever else differs,
    // and that is what keeps a long document's field stack cheap to diff.
    if (!uniqueId.empty() || !other.uniqueId.empty()) {
        return uniqueId == other.uniqueId;
    }
    return role == other.role && states == other.states && name == other.name &&
           landmark == other.landmark && roleText == other.roleText &&
           isBlock == other.isBlock && tableLayout == other.tableLayout &&
           isHidden == other.isHidden && Field::operator==(other);
}

PresentationCategory ControlField::getPresentationCategory(
    const std::vector<ControlField>& ancestors, const FormatReportSettings& settings,
    bool extraDetail) const {
    // Ported rule for rule from `textInfos/__init__.py:71-243`. The order of
    // the tests is the contract: the first answer wins, and several roles
    // appear in more than one list with different states.

    // ---- verbosity: a layout table and everything in it is nothing ----
    static constexpr std::initializer_list<Role> kTableRoles{
        Role::Table, Role::TableCell, Role::TableRowHeader, Role::TableColumnHeader};
    if (isOneOf(role, kTableRoles)) {
        const ControlField* table = nullptr;
        if (role == Role::Table) {
            table = this;
        } else {
            for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
                if (it->role == Role::Table) {
                    table = &*it;
                    break;
                }
            }
        }
        if (table == nullptr || (!settings.includeLayoutTables && table->tableLayout) ||
            table->isHidden) {
            return PresentationCategory::Layout;
        }
    }

    // ---- verbosity: what the user turned off is layout, not silence ----
    // NVDA applies this only for the reasons where the user is reading text;
    // the caller passes those reasons by asking at all (phase 6's control
    // field speech is the only caller that does not).
    const bool off =
        (role == Role::Link && !settings.reportLinks) ||
        (role == Role::Graphic && !settings.reportGraphics) ||
        (role == Role::Heading && !settings.reportHeadings) ||
        (role == Role::BlockQuote && !settings.reportBlockQuotes) ||
        (role == Role::Grouping && (name.empty() || !settings.reportGroupings)) ||
        (isOneOf(role, kTableRoles) && !settings.reportTables) ||
        (isOneOf(role, {Role::List, Role::ListItem}) && states.has(State::ReadOnly) &&
         !settings.reportLists) ||
        (role == Role::Article && !settings.reportArticles) ||
        (role == Role::MarkedContent && !settings.reportHighlight) ||
        (isOneOf(role, {Role::Frame, Role::InternalFrame}) && !settings.reportFrames) ||
        (isOneOf(role, {Role::DeletedContent, Role::InsertedContent}) &&
         !settings.reportRevisions) ||
        ((role == Role::Landmark || !landmark.empty()) && !settings.reportLandmarks) ||
        (role == Role::Region && (name.empty() || !settings.reportLandmarks)) ||
        (isOneOf(role, {Role::Figure, Role::Caption}) && !settings.reportFigures);
    if (off) {
        return PresentationCategory::Layout;
    }

    // ---- single line ----
    static constexpr std::initializer_list<Role> kSingleLineRoles{
        Role::DeletedContent, Role::InsertedContent, Role::Link,     Role::Heading,
        Role::Button,         Role::RadioButton,     Role::CheckBox, Role::Switch,
        Role::Graphic,        Role::Chart,           Role::MenuItem, Role::Tab,
        Role::ComboBox,       Role::Slider,          Role::SpinButton,
        Role::ProgressBar,    Role::BusyIndicator,   Role::ToggleButton,
        Role::MenuButton,     Role::TreeView,        Role::CheckMenuItem,
        Role::RadioMenuItem};
    const bool singleLineEditable =
        role == Role::EditableText && !states.has(State::MultiLine) &&
        (!states.has(State::ReadOnly) || states.has(State::Focusable));
    if (isOneOf(role, kSingleLineRoles) || singleLineEditable ||
        (role == Role::List && !states.has(State::ReadOnly))) {
        return PresentationCategory::SingleLine;
    }

    // ---- marker ----
    if (isOneOf(role, {Role::Separator, Role::Footnote, Role::Endnote, Role::EmbeddedObject,
                       Role::Math}) ||
        (extraDetail && role == Role::ListItem)) {
        return PresentationCategory::Marker;
    }
    if (isOneOf(role, {Role::Application, Role::Dialog})) {
        // Embedded in content they are markers; as the root of what is being
        // read they are layout. That is what keeps a browse-mode document from
        // starting with the word "dialog".
        return ancestors.empty() ? PresentationCategory::Layout : PresentationCategory::Marker;
    }

    // ---- cell ----
    if (isOneOf(role, {Role::TableCell, Role::TableColumnHeader, Role::TableRowHeader})) {
        return PresentationCategory::Cell;
    }

    // ---- container ----
    static constexpr std::initializer_list<Role> kContainerRoles{
        Role::BlockQuote, Role::Grouping,      Role::Figure,   Role::Caption,
        Role::Region,     Role::Frame,         Role::InternalFrame, Role::ToolBar,
        Role::MenuBar,    Role::PopupMenu,     Role::Table,    Role::Article,
        Role::MarkedContent};
    const bool multiLineEditable =
        role == Role::EditableText &&
        (!states.has(State::ReadOnly) || states.has(State::Focusable)) &&
        states.has(State::MultiLine);
    if (isOneOf(role, kContainerRoles) || multiLineEditable ||
        (role == Role::List && states.has(State::ReadOnly)) || role == Role::Landmark ||
        !landmark.empty() ||
        (states.has(State::Focusable) && states.has(State::Editable))) {
        return PresentationCategory::Container;
    }

    // ---- the author's own role name ----
    if (!roleText.empty()) {
        return isBlock ? PresentationCategory::Container : PresentationCategory::SingleLine;
    }

    return PresentationCategory::Layout;
}

FieldCommand FieldCommand::controlStart(ControlField field) {
    FieldCommand command;
    command.kind_ = Kind::ControlStart;
    command.control_ = std::make_shared<ControlField>(std::move(field));
    return command;
}

FieldCommand FieldCommand::controlEnd() {
    FieldCommand command;
    command.kind_ = Kind::ControlEnd;
    return command;
}

FieldCommand FieldCommand::formatChange(FormatField field) {
    FieldCommand command;
    command.kind_ = Kind::FormatChange;
    command.format_ = std::make_shared<FormatField>(std::move(field));
    return command;
}

std::string FieldCommand::repr() const {
    switch (kind_) {
        case Kind::ControlStart:
            return "FieldCommand controlStart with " +
                   std::to_string(control_ ? control_->size() : 0) + " values";
        case Kind::ControlEnd:
            return "FieldCommand controlEnd";
        case Kind::FormatChange:
            return "FieldCommand formatChange with " +
                   std::to_string(format_ ? format_->size() : 0) + " values";
    }
    return "FieldCommand";
}

}  // namespace alus::text
