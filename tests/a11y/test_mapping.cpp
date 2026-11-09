// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T4.6 — role mapping: every AtspiRole has an ALUS Role (none lands on
//        UNKNOWN except on purpose).
// T4.7 — xml-roles="banner" => landmark; level=2 => heading level 2.
//
// Plus the four state INVERSIONS, which are the part of this layer most likely
// to be silently wrong: AT-SPI reports the good news (ENABLED, SENSITIVE,
// VISIBLE, SHOWING) where NVDA's presentation asks for the bad news
// (UNAVAILABLE, INVISIBLE, OFFSCREEN).
#include "alus/a11y/mapping.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace alus::a11y;

TEST_CASE("T4.6 every AT-SPI role maps to an ALUS role", "[a11y][mapping]") {
    std::set<std::string> unknownOnPurpose;
    std::set<std::string> unknownByAccident;

    for (std::size_t index = 0; index < kPlatformRoleCount; ++index) {
        const auto platform = static_cast<PlatformRole>(static_cast<int>(index));
        const Role role = roleFromPlatform(platform);
        if (role != Role::Unknown) {
            continue;
        }
        if (isDeliberatelyUnknownRole(platform)) {
            unknownOnPurpose.emplace(platformRoleName(platform));
        } else {
            unknownByAccident.emplace(platformRoleName(platform));
        }
    }

    CHECK(unknownByAccident.empty());
    // The four holes, each with its reason in src/a11y/role_map.cpp. If this
    // number changes, the reason has to be written down with it.
    CHECK(unknownOnPurpose ==
          std::set<std::string>{"ATSPI_ROLE_EXTENDED", "ATSPI_ROLE_FOCUS_TRAVERSABLE",
                                "ATSPI_ROLE_INVALID", "ATSPI_ROLE_UNKNOWN"});
}

TEST_CASE("the measured sizes of the two vocabularies", "[a11y][mapping]") {
    // 01-ARHITEKTURA.md §6.5 said 133 roles and 45 states; those counts
    // include ATSPI_ROLE_LAST_DEFINED and the PUSH_BUTTON alias. Measured on
    // AT-SPI 2.56.2:
    CHECK(kPlatformRoleCount == 131);
    CHECK(kPlatformStateCount == 44);
    CHECK(kPlatformRelationCount == 23);
    // NVDA's own, from source/controlTypes:
    CHECK(kRoleCount == 156);
    CHECK(kStateCount == 53);
}

TEST_CASE("the roles a screen reader says every minute", "[a11y][mapping]") {
    CHECK(roleFromPlatform(PlatformRole::Button) == Role::Button);
    CHECK(roleFromPlatform(PlatformRole::CheckBox) == Role::CheckBox);
    CHECK(roleFromPlatform(PlatformRole::Entry) == Role::EditableText);
    CHECK(roleFromPlatform(PlatformRole::Text) == Role::EditableText);
    CHECK(roleFromPlatform(PlatformRole::Static) == Role::StaticText);
    CHECK(roleFromPlatform(PlatformRole::PageTab) == Role::Tab);
    CHECK(roleFromPlatform(PlatformRole::PageTabList) == Role::TabControl);
    CHECK(roleFromPlatform(PlatformRole::PasswordText) == Role::PasswordEdit);
    CHECK(roleFromPlatform(PlatformRole::TreeItem) == Role::TreeViewItem);
    // The five document kinds all become one role; the kind is not a control
    // type (rule 2 in role_map.cpp).
    CHECK(roleFromPlatform(PlatformRole::DocumentWeb) == Role::Document);
    CHECK(roleFromPlatform(PlatformRole::DocumentSpreadsheet) == Role::Document);
    CHECK(roleFromPlatform(PlatformRole::DocumentText) == Role::Document);
    // A role from a newer AT-SPI than this build knows about must not crash.
    CHECK(roleFromPlatform(static_cast<PlatformRole>(9999)) == Role::Unknown);
}

TEST_CASE("role labels are NVDA's words", "[a11y][mapping]") {
    CHECK(std::string{roleLabel(Role::CheckBox)} == "check box");
    CHECK(std::string{roleLabel(Role::EditableText)} == "edit");
    CHECK(std::string{roleLabel(Role::StaticText)} == "text");
    CHECK(std::string{roleLabel(Role::BusyIndicator)} == "busy indicator");
    CHECK(std::string{roleName(Role::CheckBox)} == "CHECKBOX");
    CHECK(roleFromName("TREEVIEWITEM") == Role::TreeViewItem);
    CHECK_FALSE(roleFromName("NOT_A_ROLE").has_value());
}

TEST_CASE("the four state inversions", "[a11y][mapping]") {
    SECTION("a healthy control has none of the negative states") {
        const std::uint64_t healthy = platformStateBit(PlatformState::Enabled) |
                                      platformStateBit(PlatformState::Sensitive) |
                                      platformStateBit(PlatformState::Visible) |
                                      platformStateBit(PlatformState::Showing);
        const StateSet states = statesFromPlatform(healthy, PlatformRole::Button);
        CHECK_FALSE(states.has(State::Unavailable));
        CHECK_FALSE(states.has(State::Invisible));
        CHECK_FALSE(states.has(State::Offscreen));
    }

    SECTION("an empty state set reports NOTHING, not everything negative") {
        // Measured on AT-SPI 2.56.2: the desktop object and every application
        // object report an empty state set. Inverting that would announce the
        // whole desktop as unavailable.
        const StateSet states = statesFromPlatform(0, PlatformRole::Application);
        CHECK_FALSE(states.has(State::Unavailable));
        CHECK_FALSE(states.has(State::Invisible));
        CHECK_FALSE(states.has(State::Offscreen));
        CHECK(states.empty());
    }

    SECTION("a widget that reports states and is not enabled IS unavailable") {
        const std::uint64_t bits = platformStateBit(PlatformState::Focusable) |
                                   platformStateBit(PlatformState::Visible) |
                                   platformStateBit(PlatformState::Showing);
        const StateSet states = statesFromPlatform(bits, PlatformRole::Button);
        CHECK(states.has(State::Unavailable));
        CHECK_FALSE(states.has(State::Invisible));
        CHECK_FALSE(states.has(State::Offscreen));
    }

    SECTION("a defunct object is unavailable, invisible and off-screen") {
        const std::uint64_t bits = platformStateBit(PlatformState::Defunct);
        const StateSet states = statesFromPlatform(bits, PlatformRole::Button);
        CHECK(states.has(State::Defunct));
        CHECK(states.has(State::Unavailable));
        CHECK(states.has(State::Invisible));
        CHECK(states.has(State::Offscreen));
    }

    SECTION("not sensitive is unavailable even when enabled") {
        const std::uint64_t bits = platformStateBit(PlatformState::Enabled) |
                                   platformStateBit(PlatformState::Visible) |
                                   platformStateBit(PlatformState::Showing);
        CHECK(statesFromPlatform(bits, PlatformRole::Button).has(State::Unavailable));
    }

    SECTION("expandable and not expanded is collapsed") {
        const std::uint64_t bits =
            platformStateBit(PlatformState::Expandable) | platformStateBit(PlatformState::Enabled) |
            platformStateBit(PlatformState::Sensitive) |
            platformStateBit(PlatformState::Visible) | platformStateBit(PlatformState::Showing);
        const StateSet states = statesFromPlatform(bits, PlatformRole::TreeItem);
        CHECK(states.has(State::Collapsed));
        CHECK_FALSE(states.has(State::Expanded));
    }
}

TEST_CASE("the role-dependent states", "[a11y][mapping]") {
    const std::uint64_t checked = platformStateBit(PlatformState::Checked) |
                                  platformStateBit(PlatformState::Enabled) |
                                  platformStateBit(PlatformState::Sensitive) |
                                  platformStateBit(PlatformState::Visible) |
                                  platformStateBit(PlatformState::Showing);

    SECTION("checked on a toggle button is pressed") {
        const StateSet states = statesFromPlatform(checked, PlatformRole::ToggleButton);
        CHECK(states.has(State::Pressed));
        CHECK_FALSE(states.has(State::Checked));
    }

    SECTION("checked on a switch is on") {
        const StateSet states = statesFromPlatform(checked, PlatformRole::Switch);
        CHECK(states.has(State::On));
        CHECK_FALSE(states.has(State::Checked));
    }

    SECTION("a link is linked, which AT-SPI never says") {
        CHECK(statesFromPlatform(0, PlatformRole::Link).has(State::Linked));
    }

    SECTION("a password field is protected, which is what silences the echo") {
        CHECK(statesFromPlatform(0, PlatformRole::PasswordText).has(State::Protected));
    }

    SECTION("indeterminate is NVDA's half-checked") {
        const std::uint64_t bits = platformStateBit(PlatformState::Indeterminate);
        CHECK(statesFromPlatform(bits, PlatformRole::CheckBox).has(State::HalfChecked));
    }
}

TEST_CASE("an indeterminate progress bar is a busy indicator", "[a11y][mapping]") {
    Role role = Role::ProgressBar;
    StateSet states{State::HalfChecked};
    transformRoleStates(role, states);
    CHECK(role == Role::BusyIndicator);
    CHECK(states.has(State::Indeterminate));
    // "half checked" on a progress bar is NVDA's own bug report (#6946's
    // neighbour); it must not survive.
    CHECK_FALSE(states.has(State::HalfChecked));
}

TEST_CASE("T4.7 refinement through get_attributes()", "[a11y][mapping]") {
    SECTION("xml-roles=banner on a section is a landmark") {
        Attributes attributes{{"xml-roles", "banner"}};
        const Refined refined = refine(Role::Section, StateSet{}, attributes);
        CHECK(refined.role == Role::Landmark);
        CHECK(refined.landmark == "banner");
        CHECK(std::string{landmarkLabel("banner")} == "banner");
        CHECK(std::string{landmarkLabel("contentinfo")} == "content info");
    }

    SECTION("a landmark token that is not first is ignored") {
        // <div role="button search"> is a button, not a search landmark.
        Attributes attributes{{"xml-roles", "button search"}};
        const Refined refined = refine(Role::Section, StateSet{}, attributes);
        CHECK(refined.landmark.empty());
        CHECK(refined.role == Role::Button);
    }

    SECTION("level=2 on a heading is heading level 2") {
        Attributes attributes{{"level", "2"}};
        const Refined refined = refine(Role::Heading, StateSet{}, attributes);
        CHECK(refined.role == Role::Heading2);
        CHECK(refined.level == 2);
    }

    SECTION("level=7 keeps the plain heading role and the level") {
        Attributes attributes{{"level", "7"}};
        const Refined refined = refine(Role::Heading, StateSet{}, attributes);
        CHECK(refined.role == Role::Heading);
        CHECK(refined.level == 7);
    }

    SECTION("level on a tree item is a depth, not a role") {
        Attributes attributes{{"level", "3"}};
        const Refined refined = refine(Role::TreeViewItem, StateSet{}, attributes);
        CHECK(refined.role == Role::TreeViewItem);
        CHECK(refined.level == 3);
    }

    SECTION("a specific platform role is NOT overridden by an ARIA role") {
        // The toolkit already mapped role="button" to a push button; an ARIA
        // token must not turn it into something else.
        Attributes attributes{{"xml-roles", "combobox"}};
        const Refined refined = refine(Role::Button, StateSet{}, attributes);
        CHECK(refined.role == Role::Button);
    }

    SECTION("tag=blockquote on a section is a block quote") {
        Attributes attributes{{"tag", "blockquote"}};
        CHECK(refine(Role::Section, StateSet{}, attributes).role == Role::BlockQuote);
    }

    SECTION("aria-haspopup picks the flavour of popup") {
        Attributes attributes{{"haspopup", "dialog"}};
        const Refined refined = refine(Role::Button, StateSet{State::HasPopup}, attributes);
        CHECK(refined.states.has(State::HasPopupDialog));
        CHECK_FALSE(refined.states.has(State::HasPopup));
    }

    SECTION("aria-sort becomes a sort state") {
        Attributes attributes{{"sort", "descending"}};
        CHECK(refine(Role::TableColumnHeader, StateSet{}, attributes)
                  .states.has(State::SortedDescending));
    }

    SECTION("an ARIA grid cell is focusable even when the browser forgets") {
        Attributes attributes{{"xml-roles", "gridcell"}};
        CHECK(refine(Role::TableCell, StateSet{}, attributes).states.has(State::Focusable));
    }

    SECTION("aria-current") {
        CHECK(refine(Role::Link, StateSet{}, Attributes{{"current", "page"}}).current ==
              IsCurrent::Page);
        CHECK(std::string{isCurrentLabel(IsCurrent::Page)} == "current page");
        // An unrecognised value still means "current".
        CHECK(isCurrentFromString("something") == IsCurrent::Yes);
        CHECK(isCurrentFromString("false") == IsCurrent::No);
        CHECK(std::string{isCurrentLabel(IsCurrent::No)}.empty());
    }

    SECTION("placeholder text") {
        CHECK(refine(Role::EditableText, StateSet{},
                     Attributes{{"placeholder-text", "Search"}})
                  .placeholder == "Search");
    }
}

TEST_CASE("state labels, including the negative ones", "[a11y][mapping]") {
    CHECK(std::string{stateLabel(State::Checked)} == "checked");
    CHECK(std::string{stateLabel(State::HalfChecked)} == "half checked");
    // NVDA keeps separate strings for the commonly negated states, because
    // "not checked" is not "not" + "checked" in every language.
    CHECK(stateHasOwnNegativeLabel(State::Checked));
    CHECK(std::string{stateNegativeLabel(State::Checked)} == "not checked");
    CHECK(stateHasOwnNegativeLabel(State::Selected));
    // And none for the rest, which the presentation layer composes.
    CHECK_FALSE(stateHasOwnNegativeLabel(State::Busy));
    // The two states NVDA deliberately leaves unlabelled.
    CHECK(std::string{stateLabel(State::Indeterminate)}.empty());
    CHECK(std::string{stateLabel(State::HasAriaDetails)}.empty());
}

TEST_CASE("a state set keeps NVDA's bit order", "[a11y][mapping]") {
    StateSet states{State::Focusable, State::Checked, State::Unavailable};
    const std::vector<State> order = states.toVector();
    REQUIRE(order.size() == 3);
    // Unavailable is bit 0, Checked bit 5, Focusable bit 24.
    CHECK(order[0] == State::Unavailable);
    CHECK(order[1] == State::Checked);
    CHECK(order[2] == State::Focusable);
    CHECK(states.size() == 3);
    states.remove(State::Checked);
    CHECK_FALSE(states.has(State::Checked));
    CHECK((states & StateSet{State::Focusable}).size() == 1);
    CHECK((states - StateSet{State::Focusable}).size() == 1);
}
