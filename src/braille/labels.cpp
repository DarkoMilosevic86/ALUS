// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/labels.hpp.

#include "alus/braille/labels.hpp"

#include "alus/i18n/i18n.hpp"

namespace alus::braille {

// Translators: EVERY string returned below is a braille abbreviation -- what a
// role, a state or a landmark looks like in the few cells a braille display
// has to spare. They are NOT the spoken labels and must not be translated as
// if they were: "btn" is three cells, "dugme" is five, and a 40-cell display
// that spends five cells on the word "button" has 35 left for the text the
// user is reading. The convention, which NVDA's braille/labels.py follows in
// every language, is to shorten the TRANSLATED word the same way English
// shortens the English one, and to keep every abbreviation distinct from the
// others in the same language.

std::string_view roleAbbreviation(a11y::Role role) {
    using a11y::Role;
    switch (role) {
        case Role::Window:
            return _("wnd");
        case Role::Dialog:
            return _("dlg");
        case Role::CheckBox:
            return _("chk");
        case Role::RadioButton:
            return _("rbtn");
        case Role::EditableText:
            return _("edt");
        case Role::Button:
            return _("btn");
        case Role::MenuBar:
            return _("mnubar");
        case Role::MenuItem:
            return _("mnuitem");
        case Role::PopupMenu:
        case Role::Menu:
            return _("mnu");
        case Role::ComboBox:
            return _("cbo");
        case Role::List:
            return _("lst");
        case Role::Graphic:
            return _("gra");
        case Role::HelpBalloon:
            return _("hlp");
        case Role::ToolTip:
            return _("tltip");
        case Role::Link:
            return _("lnk");
        case Role::TreeView:
            return _("tv");
        case Role::TreeViewItem:
            return _("tvitem");
        case Role::TabControl:
            return _("tabctl");
        case Role::ProgressBar:
            return _("prgbar");
        case Role::BusyIndicator:
            return _("bsyind");
        case Role::ScrollBar:
            return _("scrlbar");
        case Role::StatusBar:
            return _("stbar");
        case Role::Table:
            return _("tbl");
        case Role::ToolBar:
            return _("tlbar");
        case Role::DropDownButton:
            return _("drbtn");
        // Not a word in any language: five dot-36 cells, the braille dash, so
        // a separator looks like a separator.
        case Role::Separator:
            return _("⠤⠤⠤⠤⠤");
        case Role::BlockQuote:
            return _("bqt");
        case Role::Document:
            return _("doc");
        case Role::Application:
            return _("app");
        case Role::Grouping:
            return _("grp");
        case Role::Caption:
            return _("cap");
        case Role::EmbeddedObject:
            return _("embedded");
        case Role::Endnote:
            return _("enote");
        case Role::Footnote:
            return _("fnote");
        case Role::Terminal:
            return _("term");
        case Role::Section:
            return _("sect");
        case Role::ToggleButton:
            return _("tgbtn");
        case Role::SplitButton:
            return _("splbtn");
        case Role::MenuButton:
            return _("mnubtn");
        case Role::SpinButton:
            return _("spnbtn");
        case Role::TreeViewButton:
            return _("tvbtn");
        case Role::Panel:
            return _("pnl");
        case Role::PasswordEdit:
            return _("pwdedt");
        case Role::DeletedContent:
            return _("del");
        case Role::InsertedContent:
            return _("ins");
        case Role::Landmark:
            return _("lmk");
        case Role::Article:
            return _("art");
        case Role::Region:
            return _("rgn");
        case Role::Figure:
            return _("fig");
        case Role::MarkedContent:
            return _("hlght");
        case Role::Comment:
            return _("cmnt");
        case Role::Suggestion:
            return _("sggstn");
        case Role::Definition:
            return _("definition");
        case Role::Switch:
            return _("swtch");
        default:
            return {};
    }
}

std::string roleBrailleLabel(a11y::Role role) {
    const std::string_view abbreviation = roleAbbreviation(role);
    if (!abbreviation.empty()) {
        return std::string{abbreviation};
    }
    const char* label = a11y::roleLabel(role);
    return label != nullptr ? std::string{label} : std::string{};
}

std::string_view statePositiveAbbreviation(a11y::State state) {
    using a11y::State;
    switch (state) {
        case State::Selected:
            return _("sel");
        // The three-cell shapes: a frame around a filled, half filled or empty
        // middle. They are pictures, not abbreviations, and they are the one
        // thing braille users read faster than speech users hear.
        case State::Pressed:
            return _("⢎⣿⡱");
        case State::HalfPressed:
            return _("⢎⣸⡱");
        case State::Checked:
            return _("⣏⣿⣹");
        case State::HalfChecked:
            return _("⣏⣸⣹");
        case State::ReadOnly:
            return _("ro");
        case State::Expanded:
            return _("-");
        case State::Collapsed:
            return _("+");
        case State::HasPopup:
            return _("submnu");
        case State::Protected:
            return _("***");
        case State::Required:
            return _("req");
        case State::InvalidEntry:
            return _("invalid");
        case State::Autocomplete:
            return _("...");
        case State::MultiLine:
            return _("mln");
        case State::Clickable:
            return _("clk");
        case State::SortedAscending:
            return _("sorted asc");
        case State::SortedDescending:
            return _("sorted desc");
        case State::HasLongDescription:
            return _("ldesc");
        case State::HasFormula:
            return _("frml");
        case State::HasComment:
            return _("cmnt");
        case State::On:
            return _("⣏⣿⣹");
        case State::InternalLink:
            return _("smp");
        case State::MultiSelectable:
            return _("msel");
        default:
            return {};
    }
}

std::string_view stateNegativeAbbreviation(a11y::State state) {
    using a11y::State;
    switch (state) {
        case State::Selected:
            return _("nsel");
        case State::Pressed:
            return _("⢎⣀⡱");
        case State::Checked:
            return _("⣏⣀⣹");
        case State::On:
            return _("⣏⣀⣹");
        default:
            return {};
    }
}

std::string_view landmarkAbbreviation(std::string_view token) {
    if (token == "banner") {
        return _("bnnr");
    }
    if (token == "complementary") {
        return _("cmpl");
    }
    if (token == "contentinfo") {
        return _("cinf");
    }
    if (token == "main") {
        return _("main");
    }
    if (token == "navigation") {
        return _("navi");
    }
    if (token == "search") {
        return _("srch");
    }
    if (token == "form") {
        return _("form");
    }
    return {};
}

std::string_view isCurrentAbbreviation(a11y::IsCurrent current) {
    if (current == a11y::IsCurrent::No) {
        return {};
    }
    const char* label = a11y::isCurrentLabel(current);
    return label != nullptr ? std::string_view{label} : std::string_view{};
}

}  // namespace alus::braille
