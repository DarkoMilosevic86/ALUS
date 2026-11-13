// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// AT-SPI state -> ALUS state. NOT generated: every row is a decision, and four
// of them are inversions.
//
// THE INVERSIONS ARE THE POINT OF THIS FILE. Windows' IAccessible reports the
// bad news (UNAVAILABLE, INVISIBLE, OFFSCREEN); AT-SPI reports the good news
// (ENABLED, SENSITIVE, VISIBLE, SHOWING). NVDA's whole presentation layer, and
// everything ported from it, asks "is UNAVAILABLE in the states" — so the
// absence of the AT-SPI state has to become the presence of the NVDA one, here
// and nowhere else. Get this wrong and every single control in every
// application is announced as unavailable, or none ever is.
//
// Four AT-SPI states also mean different things depending on the role, which
// is why this takes the role:
//   * CHECKED on a toggle button is "pressed", and on a switch it is "on";
//   * the role Link is what NVDA calls the LINKED state;
//   * the role PasswordText is what NVDA calls the PROTECTED state, and that
//     is what stops the typed-character echo from reading a password out loud.
#include "alus/a11y/mapping.hpp"

#include <array>

namespace alus::a11y {
namespace {

struct StateMapRow {
    PlatformState platform;
    State state;
};

// The direct rows. The AT-SPI states NOT in this table are listed under it,
// each with the reason it is not mapped -- an unmapped state is a decision too.
constexpr std::array<StateMapRow, 22> kDirect{{
    {PlatformState::Busy, State::Busy},
    {PlatformState::Checked, State::Checked},
    {PlatformState::Collapsed, State::Collapsed},
    {PlatformState::Defunct, State::Defunct},
    {PlatformState::Editable, State::Editable},
    {PlatformState::Expanded, State::Expanded},
    {PlatformState::Focusable, State::Focusable},
    {PlatformState::Focused, State::Focused},
    {PlatformState::Iconified, State::Iconified},
    {PlatformState::Modal, State::Modal},
    {PlatformState::MultiLine, State::MultiLine},
    {PlatformState::Multiselectable, State::MultiSelectable},
    {PlatformState::Pressed, State::Pressed},
    {PlatformState::Selectable, State::Selectable},
    {PlatformState::Selected, State::Selected},
    // oleacc's MIXED and INDETERMINATE are the same bit, and NVDA maps it to
    // HALFCHECKED (state.py's own comment says so). transformRoleStates then
    // turns an indeterminate progress bar into a busy indicator.
    {PlatformState::Indeterminate, State::HalfChecked},
    {PlatformState::Required, State::Required},
    // NVDA has no TRUNCATED; its word for text that does not fit its control
    // is CROPPED, and that is what the user hears.
    {PlatformState::Truncated, State::Cropped},
    {PlatformState::InvalidEntry, State::InvalidEntry},
    {PlatformState::SupportsAutocompletion, State::Autocomplete},
    {PlatformState::Visited, State::Visited},
    {PlatformState::Checkable, State::Checkable},
}};

// NOT MAPPED, and why:
//   Invalid              not a state
//   Active               a window is the active one; ALUS uses it to find the
//                        foreground window, and no screen reader says it
//   Armed                "about to be activated": transient, not announced
//   Enabled, Sensitive   inverted below into State::Unavailable
//   Expandable           synthesised below into State::Collapsed
//   HasTooltip           NVDA has no such state; the tooltip is announced when
//                        it shows (the ToolTip behaviour)
//   Horizontal, Vertical orientation; NVDA does not announce it
//   Opaque, Resizable    painting and window management, not content
//   Showing              inverted below into State::Offscreen
//   SingleLine           the absence of MULTILINE already says it
//   Stale                NVDA deliberately does NOT treat this as defunct
//                        (the mapping is commented out in NVDA's own source):
//                        a stale object is usually still usable
//   Transient            an object not worth caching; phase 5 uses it
//   Visible              inverted below into State::Invisible
//   ManagesDescendants   ALUS uses it for ActiveDescendantChanged, not speech
//   Animated             NVDA has no such state
//   SelectableText       the Text interface already says it
//   IsDefault            NVDA has no such state (the default button's name is
//                        what the user needs, and the dialog says it)
//   HasPopup, ReadOnly   mapped below, but through the attribute refinement
//                        for HASPOPUP's four ARIA flavours

bool hasBit(std::uint64_t bits, PlatformState state) {
    return (bits & platformStateBit(state)) != 0;
}

// Whether "disabled" means anything for this role.
//
// The roles a toolkit can grey out: everything the user can operate, plus the
// few containers whose own sensitivity is meaningful (a menu, a page tab, a
// split pane). Everything else -- frames, dialogs, panels, groupings, labels,
// documents, sections -- is furniture: it is never greyed out, and some
// toolkits (gnome-shell's St) do not report SENSITIVE on it at all.
//
// Kept in one place rather than spread over the presentation, because the
// STATE is what is wrong when this list is wrong: `alus-cli a11y-tree` and
// the braille display read the same state set as speech does.
bool roleReportsSensitivity(PlatformRole role) {
    switch (role) {
        case PlatformRole::Button:
        case PlatformRole::PushButtonMenu:
        case PlatformRole::ToggleButton:
        case PlatformRole::CheckBox:
        case PlatformRole::CheckMenuItem:
        case PlatformRole::RadioButton:
        case PlatformRole::RadioMenuItem:
        case PlatformRole::MenuItem:
        case PlatformRole::TearoffMenuItem:
        case PlatformRole::Menu:
        case PlatformRole::ComboBox:
        case PlatformRole::Entry:
        case PlatformRole::Text:
        case PlatformRole::PasswordText:
        case PlatformRole::Editbar:
        case PlatformRole::Slider:
        case PlatformRole::SpinButton:
        case PlatformRole::Dial:
        case PlatformRole::ScrollBar:
        case PlatformRole::PageTab:
        case PlatformRole::TableCell:
        case PlatformRole::ListItem:
        case PlatformRole::TreeItem:
        case PlatformRole::Link:
        case PlatformRole::Switch:
        case PlatformRole::Separator:
        case PlatformRole::SplitPane:
        case PlatformRole::LayeredPane:
        case PlatformRole::LevelBar:
        case PlatformRole::Embedded:
        case PlatformRole::Calendar:
        case PlatformRole::ColorChooser:
        case PlatformRole::DateEditor:
        case PlatformRole::FileChooser:
        case PlatformRole::FontChooser:
        case PlatformRole::Autocomplete:
        case PlatformRole::Rating:
            return true;
        default:
            return false;
    }
}

}  // namespace

StateSet statesFromPlatform(std::uint64_t bits, PlatformRole platform) {
    StateSet states;
    for (const StateMapRow& row : kDirect) {
        if (hasBit(bits, row.platform)) {
            states.add(row.state);
        }
    }

    if (hasBit(bits, PlatformState::HasPopup)) {
        states.add(State::HasPopup);
    }
    if (hasBit(bits, PlatformState::ReadOnly)) {
        states.add(State::ReadOnly);
    }

    // ---- the three inversions ----
    // AN EMPTY STATE SET MEANS "THIS OBJECT DOES NOT REPORT STATES", NOT
    // "EVERYTHING IS FALSE". Measured on AT-SPI 2.56.2: the desktop object
    // and every application object report an empty set, while every real
    // widget reports the full one (enabled, sensitive, showing, visible, ...).
    // Without this guard the whole desktop is announced as unavailable,
    // invisible and off-screen -- which is how this was found.
    if (bits != 0) {
        // A control is unavailable when the toolkit reports NEITHER ENABLED
        // NOR SENSITIVE -- both absent, and not either one.
        //
        // Each half of that was learned the hard way (FAZA10.md N111):
        //   * GTK 4 reports SENSITIVE on every widget and ENABLED on none --
        //     measured with an independent binding; even a dialog's Close
        //     button answers enabled=False, sensitive=True. The original
        //     `!ENABLED || !SENSITIVE` made every GTK 4 program "unavailable",
        //     ALUS's own settings window included.
        //   * GTK 3's notebook tab does the OPPOSITE: enabled=True,
        //     sensitive=False (measured on gnome-terminal's tab). The first
        //     fix, SENSITIVE alone (Orca's test), made that tab
        //     "unavailable, selected".
        // A widget that is really greyed out has neither, in GTK 3 (ATK sets
        // both from the one sensitivity), in GTK 4 and in the browsers.
        //
        // AND ONLY FOR A ROLE THAT CAN BE DISABLED, the other half of Orca's
        // rule (its `_generate_state_sensitive` is called from the generators
        // of controls and of no container): gnome-shell's panels report
        // neither bit and are not things that can be greyed out. The list is
        // in roleReportsSensitivity().
        if (roleReportsSensitivity(platform) && !hasBit(bits, PlatformState::Enabled) &&
            !hasBit(bits, PlatformState::Sensitive)) {
            states.add(State::Unavailable);
        }
        if (!hasBit(bits, PlatformState::Visible)) {
            states.add(State::Invisible);
        }
        // SHOWING is "inside its parent's visible area, were the parent
        // visible". Its absence is exactly NVDA's OFFSCREEN: scrolled out of
        // view, in a collapsed section, behind a tab that is not current.
        if (!hasBit(bits, PlatformState::Showing)) {
            states.add(State::Offscreen);
        }
    }
    // Some toolkits set EXPANDABLE and EXPANDED and never COLLAPSED (GTK
    // does this for tree rows). NVDA's presentation says "collapsed", so an
    // expandable object that is not expanded is collapsed.
    if (hasBit(bits, PlatformState::Expandable) && !hasBit(bits, PlatformState::Expanded)) {
        states.add(State::Collapsed);
    }

    // ---- the role-dependent rows ----
    const Role role = roleFromPlatform(platform);
    if (role == Role::ToggleButton && states.has(State::Checked)) {
        // ATK sets CHECKED on a toggle button; the user hears "pressed".
        states.remove(State::Checked);
        states.add(State::Pressed);
    }
    if (role == Role::Switch) {
        // NVDA's State::On, with its own negative label ("off").
        if (states.has(State::Checked)) {
            states.remove(State::Checked);
            states.add(State::On);
        }
        states.remove(State::Checkable);
    }
    if (role == Role::Link) {
        // AT-SPI has no LINKED state: the role is the statement. NVDA's
        // presentation layer needs LINKED to decide what to say about a link.
        states.add(State::Linked);
    }
    if (role == Role::PasswordEdit) {
        // The state that keeps api::isTypingProtected() honest, and with it
        // the typed-character echo.
        states.add(State::Protected);
    }
    return states;
}

void transformRoleStates(Role& role, StateSet& states) {
    // NVDA: controlTypes.transformRoleStates. A progress bar that can only
    // convey activity, not progress, is a busy indicator -- and must not be
    // announced as "half checked".
    if ((role == Role::ProgressBar || role == Role::BusyIndicator) &&
        (states.has(State::Indeterminate) || states.has(State::HalfChecked))) {
        states.remove(State::HalfChecked);
        states.add(State::Indeterminate);
        role = Role::BusyIndicator;
    }
}

}  // namespace alus::a11y
