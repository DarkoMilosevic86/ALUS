// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// AT-SPI role -> ALUS role. NOT generated: every row is a decision.
//
// Where the two vocabularies agree the row is obvious. Where they do not, the
// row carries its reason. Three rules decided the hard ones:
//
//  1. NVDA's own IAccessible2 mapping wins (`IAccessibleHandler.
//     IAccessibleRolesToNVDARoles`). IA2 and AT-SPI/ATK both descend from the
//     Java Accessibility API, so most AT-SPI roles have an IA2 twin and NVDA
//     has already decided what to call it. A user moving between the two
//     screen readers then hears the same word for the same widget.
//  2. Where AT-SPI is more specific than NVDA, the information is kept
//     elsewhere rather than dropped: the five document roles all become
//     Role::Document, and ATSPI_ROLE_HEADING becomes Role::Heading, which the
//     refinement then turns into Heading1..Heading6 from the `level`
//     attribute.
//  3. Where AT-SPI has a role NVDA has never had, the nearest role THE USER
//     WILL RECOGNISE is chosen, and never Role::Unknown. "Unknown" is a dead
//     end for the user; "graphic" for a GtkArrow is at least true enough to
//     navigate by. The four deliberate exceptions are at the top of the
//     table.
#include "alus/a11y/mapping.hpp"

#include <algorithm>
#include <array>

namespace alus::a11y {
namespace {

struct RoleMapRow {
    PlatformRole platform;
    Role role;
};

// Every value of PlatformRole, in enumeration order. T4.6 checks that.
constexpr std::array<RoleMapRow, kPlatformRoleCount> kRoleMap{{
    // ---- the four deliberate holes ----
    // Not a role: the zero value AT-SPI uses for "no answer".
    {PlatformRole::Invalid, Role::Unknown},
    // Deprecated in ATK itself; no toolkit sets it.
    {PlatformRole::FocusTraversable, Role::Unknown},
    // The toolkit says it does not know either. Honest, and the refinement
    // through xml-roles/tag is what rescues these in web content.
    {PlatformRole::Unknown, Role::Unknown},
    // "A role not in this enumeration": the real role, if any, is in
    // get_attributes()["xml-roles"] and the refinement reads it.
    {PlatformRole::Extended, Role::Unknown},

    // ---- widgets ----
    {PlatformRole::AcceleratorLabel, Role::Label},
    {PlatformRole::Alert, Role::Alert},
    {PlatformRole::Animation, Role::Animation},
    // No NVDA role; a GtkArrow is a directional glyph, so the user hears
    // "graphic" rather than "unknown".
    {PlatformRole::Arrow, Role::Graphic},
    {PlatformRole::Calendar, Role::Calendar},
    {PlatformRole::Canvas, Role::Canvas},
    {PlatformRole::CheckBox, Role::CheckBox},
    {PlatformRole::CheckMenuItem, Role::CheckMenuItem},
    {PlatformRole::ColorChooser, Role::ColorChooser},
    {PlatformRole::ColumnHeader, Role::TableColumnHeader},
    {PlatformRole::ComboBox, Role::ComboBox},
    {PlatformRole::DateEditor, Role::DateEditor},
    {PlatformRole::DesktopIcon, Role::DesktopIcon},
    {PlatformRole::DesktopFrame, Role::DesktopPane},
    {PlatformRole::Dial, Role::Dial},
    {PlatformRole::Dialog, Role::Dialog},
    {PlatformRole::DirectoryPane, Role::DirectoryPane},
    // ATK's drawing area is NVDA's canvas; both mean "pixels, no structure".
    {PlatformRole::DrawingArea, Role::Canvas},
    {PlatformRole::FileChooser, Role::FileChooser},
    {PlatformRole::Filler, Role::Filler},
    {PlatformRole::FontChooser, Role::FontChooser},
    {PlatformRole::Frame, Role::Frame},
    {PlatformRole::GlassPane, Role::GlassPane},
    // "A document container for HTML": the document, not a container widget.
    {PlatformRole::HtmlContainer, Role::Document},
    {PlatformRole::Icon, Role::Icon},
    {PlatformRole::Image, Role::Graphic},
    {PlatformRole::InternalFrame, Role::InternalFrame},
    {PlatformRole::Label, Role::Label},
    {PlatformRole::LayeredPane, Role::LayeredPane},
    {PlatformRole::List, Role::List},
    {PlatformRole::ListItem, Role::ListItem},
    {PlatformRole::Menu, Role::Menu},
    {PlatformRole::MenuBar, Role::MenuBar},
    {PlatformRole::MenuItem, Role::MenuItem},
    {PlatformRole::OptionPane, Role::OptionPane},
    {PlatformRole::PageTab, Role::Tab},
    {PlatformRole::PageTabList, Role::TabControl},
    {PlatformRole::Panel, Role::Panel},
    // The role itself is what makes typing protected; see statesFromPlatform.
    {PlatformRole::PasswordText, Role::PasswordEdit},
    {PlatformRole::PopupMenu, Role::PopupMenu},
    {PlatformRole::ProgressBar, Role::ProgressBar},
    {PlatformRole::Button, Role::Button},
    {PlatformRole::RadioButton, Role::RadioButton},
    {PlatformRole::RadioMenuItem, Role::RadioMenuItem},
    {PlatformRole::RootPane, Role::RootPane},
    {PlatformRole::RowHeader, Role::TableRowHeader},
    {PlatformRole::ScrollBar, Role::ScrollBar},
    {PlatformRole::ScrollPane, Role::ScrollPane},
    {PlatformRole::Separator, Role::Separator},
    {PlatformRole::Slider, Role::Slider},
    {PlatformRole::SpinButton, Role::SpinButton},
    {PlatformRole::SplitPane, Role::SplitPane},
    {PlatformRole::StatusBar, Role::StatusBar},
    {PlatformRole::Table, Role::Table},
    {PlatformRole::TableCell, Role::TableCell},
    {PlatformRole::TableColumnHeader, Role::TableColumnHeader},
    {PlatformRole::TableRowHeader, Role::TableRowHeader},
    {PlatformRole::TearoffMenuItem, Role::TearoffMenu},
    {PlatformRole::Terminal, Role::Terminal},
    // ATK's TEXT is an entry; read-only text carries ATSPI_STATE_READ_ONLY and
    // static text has its own role (PlatformRole::Static).
    {PlatformRole::Text, Role::EditableText},
    {PlatformRole::ToggleButton, Role::ToggleButton},
    {PlatformRole::ToolBar, Role::ToolBar},
    {PlatformRole::ToolTip, Role::ToolTip},
    {PlatformRole::Tree, Role::TreeView},
    // NVDA has no "tree table"; a tree grid is navigated as a tree, and the
    // Table interface is still there for the table commands of phase 5.
    {PlatformRole::TreeTable, Role::TreeView},
    {PlatformRole::Viewport, Role::Viewport},
    {PlatformRole::Window, Role::Window},
    {PlatformRole::Header, Role::Header},
    {PlatformRole::Footer, Role::Footer},
    {PlatformRole::Paragraph, Role::Paragraph},
    {PlatformRole::Ruler, Role::Ruler},
    {PlatformRole::Application, Role::Application},
    // No NVDA role. An autocomplete is a text entry with a list attached, and
    // "combo box" is the control users already know with that behaviour.
    {PlatformRole::Autocomplete, Role::ComboBox},
    {PlatformRole::Editbar, Role::EditBar},
    {PlatformRole::Embedded, Role::EmbeddedObject},
    {PlatformRole::Entry, Role::EditableText},
    {PlatformRole::Chart, Role::Chart},
    {PlatformRole::Caption, Role::Caption},
    {PlatformRole::DocumentFrame, Role::Document},
    // The refinement turns this into Heading1..6 from the `level` attribute.
    {PlatformRole::Heading, Role::Heading},
    {PlatformRole::Page, Role::Page},
    {PlatformRole::Section, Role::Section},
    {PlatformRole::RedundantObject, Role::RedundantObject},
    {PlatformRole::Form, Role::Form},
    {PlatformRole::Link, Role::Link},
    {PlatformRole::InputMethodWindow, Role::InputWindow},
    {PlatformRole::TableRow, Role::TableRow},
    {PlatformRole::TreeItem, Role::TreeViewItem},
    // Rule 2: the kind of document is not a control type. It reaches the user
    // through the application name, and phase 6 reads it for browse mode.
    {PlatformRole::DocumentSpreadsheet, Role::Document},
    {PlatformRole::DocumentPresentation, Role::Document},
    {PlatformRole::DocumentText, Role::Document},
    {PlatformRole::DocumentWeb, Role::Document},
    {PlatformRole::DocumentEmail, Role::Document},
    {PlatformRole::Comment, Role::Comment},
    {PlatformRole::ListBox, Role::List},
    {PlatformRole::Grouping, Role::Grouping},
    {PlatformRole::ImageMap, Role::ImageMap},
    // NVDA announces toasts and notification popups as alerts, and the
    // Notification behaviour is what speaks them (§6.3).
    {PlatformRole::Notification, Role::Alert},
    // A GtkInfoBar is an in-window alert strip.
    {PlatformRole::InfoBar, Role::Alert},
    // ARIA meter. NVDA has no meter role and reads meters as progress bars.
    {PlatformRole::LevelBar, Role::ProgressBar},
    {PlatformRole::TitleBar, Role::TitleBar},
    {PlatformRole::BlockQuote, Role::BlockQuote},
    {PlatformRole::Audio, Role::Audio},
    {PlatformRole::Video, Role::Video},
    {PlatformRole::Definition, Role::Definition},
    {PlatformRole::Article, Role::Article},
    {PlatformRole::Landmark, Role::Landmark},
    // ARIA live region kinds. There is no NVDA role for either; what matters
    // about them is the live-region announcement, not the word.
    {PlatformRole::Log, Role::Section},
    {PlatformRole::Marquee, Role::Animation},
    {PlatformRole::Math, Role::Math},
    // A rating is a value in a range, like a slider, and NVDA has no rating.
    {PlatformRole::Rating, Role::Slider},
    {PlatformRole::Timer, Role::Clock},
    {PlatformRole::Static, Role::StaticText},
    // Phase 5 presents the structure; the role is the equation either way.
    {PlatformRole::MathFraction, Role::Math},
    {PlatformRole::MathRoot, Role::Math},
    {PlatformRole::Subscript, Role::Subscript},
    {PlatformRole::Superscript, Role::Superscript},
    // A description list is a list of terms and values; NVDA's web support
    // reads <dl>/<dt>/<dd> exactly this way (IAccessibleRolesToNVDARoles
    // maps the "dl" tag to LIST).
    {PlatformRole::DescriptionList, Role::List},
    {PlatformRole::DescriptionTerm, Role::ListItem},
    {PlatformRole::DescriptionValue, Role::ListItem},
    {PlatformRole::Footnote, Role::Footnote},
    {PlatformRole::ContentDeletion, Role::DeletedContent},
    {PlatformRole::ContentInsertion, Role::InsertedContent},
    {PlatformRole::Mark, Role::MarkedContent},
    {PlatformRole::Suggestion, Role::Suggestion},
    {PlatformRole::PushButtonMenu, Role::MenuButton},
    {PlatformRole::Switch, Role::Switch},
}};

// The holes of rule 3, as a list so that T4.6 can tell them from an omission.
constexpr std::array<PlatformRole, 4> kDeliberateUnknown{{
    PlatformRole::Invalid,
    PlatformRole::FocusTraversable,
    PlatformRole::Unknown,
    PlatformRole::Extended,
}};

}  // namespace

Role roleFromPlatform(PlatformRole platform) {
    const auto it = std::find_if(kRoleMap.begin(), kRoleMap.end(),
                                 [platform](const RoleMapRow& row) {
                                     return row.platform == platform;
                                 });
    // A role from a newer AT-SPI than this build knows about. Unknown is the
    // only honest answer, and it must not be a crash.
    return it == kRoleMap.end() ? Role::Unknown : it->role;
}

bool isDeliberatelyUnknownRole(PlatformRole platform) {
    return std::find(kDeliberateUnknown.begin(), kDeliberateUnknown.end(), platform) !=
           kDeliberateUnknown.end();
}

}  // namespace alus::a11y
