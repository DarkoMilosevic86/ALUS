// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What an object sounds like.
//
// The ORDER of getPropertiesSpeech is a contract, not a style (see
// object_speech.hpp), and so are the ~30 rules in processAndLabelStates that
// decide which states are worth a word. These tests are written as sentences a
// user would hear, because that is the only form in which they can be checked
// against NVDA by listening.
#include "alus/a11y/object_speech.hpp"

#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace alus::testing;
using namespace alus::a11y;

namespace {

std::string say(Object& object, OutputReason reason = OutputReason::Query) {
    return joined(getObjectSpeech(object, reason));
}

}  // namespace

TEST_CASE("the order of the properties", "[a11y][speech]") {
    PropertyValues values;
    values.name = "Volume";
    values.role = Role::Slider;
    values.value = "40%";
    values.states = StateSet{State::Focused};
    values.realStates = StateSet{State::Focused};
    values.realRole = Role::Slider;
    values.description = "Output volume";
    values.keyboardShortcut = "alt+v";

    // name, role, value, states, description, shortcut.
    CHECK(joined(getPropertiesSpeech(values, OutputReason::Query)) ==
          "Volume slider 40% focused Output volume alt+v");
}

TEST_CASE("the role is not always said", "[a11y][speech]") {
    PropertyValues values;
    values.realRole = Role::ListItem;
    values.role = Role::ListItem;
    values.name = "Documents";

    SECTION("a list item on focus says its text, not the word list item") {
        // Role::ListItem is in NVDA's silentRolesOnFocus, and the object has
        // a name, so the role adds nothing.
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) == "Documents");
    }

    SECTION("but on a query the role is what was asked about") {
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Query)) ==
              "Documents list item");
    }

    SECTION("and a nameless one says the role even on focus") {
        values.name.reset();
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) == "list item");
    }

    SECTION("aria-roledescription replaces the role label") {
        values.roleText = "slide";
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) == "Documents slide");
    }
}

TEST_CASE("a value is not said for the roles where it is noise", "[a11y][speech]") {
    PropertyValues values;
    values.realRole = Role::CheckBox;
    values.role = Role::CheckBox;
    values.name = "Dark mode";
    values.value = "true";
    // silentValuesForRoles: a check box says "checked", never "true".
    CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) == "Dark mode check box");
}

TEST_CASE("states: what is said and what is not", "[a11y][speech]") {
    const Settings settings;

    SECTION("an unchecked check box says so") {
        const std::vector<std::string> labels = processAndLabelStates(
            Role::CheckBox, StateSet{State::Focusable, State::Focused}, OutputReason::Focus,
            StateSet{State::Focusable, State::Focused}, StateSet{}, settings);
        REQUIRE(labels.size() == 1);
        CHECK(labels.front() == "not checked");
    }

    SECTION("a checked one says checked") {
        const StateSet states{State::Focusable, State::Checked};
        const std::vector<std::string> labels = processAndLabelStates(
            Role::CheckBox, states, OutputReason::Focus, states, StateSet{}, settings);
        REQUIRE(labels.size() == 1);
        CHECK(labels.front() == "checked");
    }

    SECTION("focusable and focused are never said") {
        const StateSet states{State::Focusable, State::Focused};
        CHECK(processAndLabelStates(Role::Button, states, OutputReason::Focus, states,
                                    StateSet{}, settings)
                  .empty());
    }

    SECTION("an edit field does not announce that it is editable") {
        const StateSet states{State::Focusable, State::Editable};
        CHECK(processAndLabelStates(Role::EditableText, states, OutputReason::Focus, states,
                                    StateSet{}, settings)
                  .empty());
    }

    SECTION("a switch says on or off, not checked") {
        const StateSet on{State::Focusable, State::On};
        std::vector<std::string> labels =
            processAndLabelStates(Role::Switch, on, OutputReason::Focus, on, StateSet{},
                                  settings);
        REQUIRE(labels.size() == 1);
        CHECK(labels.front() == "on");

        const StateSet off{State::Focusable};
        labels = processAndLabelStates(Role::Switch, off, OutputReason::Focus, off, StateSet{},
                                       settings);
        REQUIRE(labels.size() == 1);
        CHECK(labels.front() == "off");
    }

    SECTION("a toggle button says pressed or not pressed") {
        const StateSet up{State::Focusable};
        const std::vector<std::string> labels = processAndLabelStates(
            Role::ToggleButton, up, OutputReason::Focus, up, StateSet{}, settings);
        REQUIRE(labels.size() == 1);
        CHECK(labels.front() == "not pressed");
    }

    SECTION("unavailable is said, and said first") {
        const StateSet states{State::Unavailable, State::Focusable};
        const std::vector<std::string> labels = processAndLabelStates(
            Role::Button, states, OutputReason::Focus, states, StateSet{}, settings);
        REQUIRE(labels.size() == 1);
        CHECK(labels.front() == "unavailable");
    }

    SECTION("a collapsed tree item says collapsed, and that it is not selected") {
        const StateSet states{State::Focusable, State::Collapsed, State::Selectable};
        const std::vector<std::string> labels = processAndLabelStates(
            Role::TreeViewItem, states, OutputReason::Focus, states, StateSet{}, settings);
        // In bit order: Selected is bit 2, Collapsed is bit 9. "Not selected"
        // is worth saying here because the item IS selectable and focusable,
        // which is NVDA's rule for restricting that word to where it means
        // something.
        REQUIRE(labels.size() == 2);
        CHECK(labels[0] == "not selected");
        CHECK(labels[1] == "collapsed");
    }

    SECTION("a clickable link does not say clickable") {
        const StateSet states{State::Focusable, State::Clickable, State::Linked};
        CHECK(processAndLabelStates(Role::Link, states, OutputReason::Focus, states, StateSet{},
                                    settings)
                  .empty());
    }
}

TEST_CASE("a focus change says the object, not its states", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref window = backend.add(backend.desktopRef(), PlatformRole::Frame, "Settings");
    const Ref checkbox = backend.add(window, PlatformRole::CheckBox, "Dark mode");
    backend.addState(checkbox, PlatformState::Focusable);
    backend.addState(checkbox, PlatformState::Focused);

    const ObjectPtr object = fixture.registry.objectFor(checkbox);
    CHECK(say(*object, OutputReason::Focus) == "Dark mode check box not checked");

    SECTION("and a checked one") {
        backend.addState(checkbox, PlatformState::Checked);
        alus::core::invalidateAllCaches();
        CHECK(say(*object, OutputReason::Focus) == "Dark mode check box checked");
    }
}

TEST_CASE("only what changed is said again", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref window = backend.add(backend.desktopRef(), PlatformRole::Frame, "Settings");
    const Ref checkbox = backend.add(window, PlatformRole::CheckBox, "Dark mode");
    backend.addState(checkbox, PlatformState::Focusable);
    backend.addState(checkbox, PlatformState::Focused);
    const ObjectPtr object = fixture.registry.objectFor(checkbox);

    // The focus announcement is what fills the "what was said" record.
    CHECK(say(*object, OutputReason::Focus) == "Dark mode check box not checked");

    backend.addState(checkbox, PlatformState::Checked);
    alus::core::invalidateAllCaches();

    PropertySelection selection;
    selection.states = true;
    // Pressing space on it says one word, not the whole object again.
    CHECK(joined(getObjectPropertiesSpeech(*object, OutputReason::Change, selection)) ==
          "checked");
}

TEST_CASE("the position in a group, and the level", "[a11y][speech]") {
    // The row, column and level memory is process-wide, as NVDA's is: it is
    // what makes "row 3" silent when the row did not change. Catch2 runs a
    // test case once per section, so a test that uses that memory has to
    // start from a known state.
    resetPresentationMemory();
    PropertyValues values;
    values.realRole = Role::TreeViewItem;
    values.role = Role::TreeViewItem;
    values.name = "Pictures";
    values.indexInGroup = 3;
    values.similarItemsInGroup = 7;
    values.level = 2;

    // The level of a tree item that CHANGED goes first; everything else is
    // appended. NVDA does this because the level is the thing that changed.
    CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
          "level 2 Pictures 3 of 7");

    SECTION("the same level again is not repeated first") {
        // The memory now holds level 2, so the next item at level 2 says it
        // at the end, where it does not interrupt the name.
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
              "Pictures 3 of 7 level 2");
    }
}

TEST_CASE("a table cell says where it is, once", "[a11y][speech]") {
    resetPresentationMemory();
    PropertyValues values;
    values.realRole = Role::TableCell;
    values.role = Role::TableCell;
    values.name = "1200";
    values.rowNumber = 3;
    values.columnNumber = 2;
    values.rowHeaderText = "March";
    values.columnHeaderText = "Income";
    values.tableId = 99;

    CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
          "1200 March row 3 Income column 2");

    SECTION("moving along the row says only the column") {
        values.columnNumber = 3;
        values.columnHeaderText = "Expenses";
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
              "1200 Expenses column 3");
    }

    SECTION("a cell in another table says everything again") {
        values.tableId = 100;
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
              "1200 March row 3 Income column 2");
    }

    SECTION("a spanning cell says what it spans") {
        // The column was said by the check above (Catch2 runs it again for
        // this section), and it has not changed -- so only the row, now with
        // its span, is worth saying.
        values.rowSpan = 3;
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
              "1200 March row 3 through 5");
    }
}

TEST_CASE("entering a table says its size", "[a11y][speech]") {
    resetPresentationMemory();
    PropertyValues values;
    values.realRole = Role::Table;
    values.role = Role::Table;
    values.name = "Budget";
    values.rowCount = 12;
    values.columnCount = 4;
    CHECK(joined(getPropertiesSpeech(values, OutputReason::Query)) ==
          "Budget table with 12 rows and 4 columns");

    SECTION("one row and one column are singular") {
        values.rowCount = 1;
        values.columnCount = 1;
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Query)) ==
              "Budget table with 1 row and 1 column");
    }
}

TEST_CASE("aria-current, details and the placeholder", "[a11y][speech]") {
    PropertyValues values;
    values.realRole = Role::Link;
    values.role = Role::Link;
    values.name = "Products";
    values.current = IsCurrent::Page;
    CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
          "Products link current page");

    SECTION("details with known roles") {
        values.current = IsCurrent::No;
        values.hasDetails = true;
        values.detailsRoles = {Role::Comment};
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
              "Products link has comment");
    }

    SECTION("details with no role") {
        values.current = IsCurrent::No;
        values.hasDetails = true;
        CHECK(joined(getPropertiesSpeech(values, OutputReason::Focus)) ==
              "Products link has details");
    }
}

TEST_CASE("the placeholder is said only for an empty field", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref entry = backend.add(backend.desktopRef(), PlatformRole::Entry);
    backend.addInterface(entry, Interface::Text);
    backend.node(entry).attributes = {{"placeholder-text", "Search"}};
    const ObjectPtr object = fixture.registry.objectFor(entry);

    CHECK(say(*object, OutputReason::Focus) == "edit Search");

    SECTION("and not once the user has typed") {
        backend.node(entry).text = "mesecina";
        alus::core::invalidateAllCaches();
        CHECK(say(*object, OutputReason::Focus) == "edit");
    }
}

TEST_CASE("a landmark says what kind it is", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref region = backend.add(backend.desktopRef(), PlatformRole::Section, "Site header");
    backend.node(region).attributes = {{"xml-roles", "banner"}};

    const ObjectPtr object = fixture.registry.objectFor(region);
    CHECK(object->role() == Role::Landmark);
    CHECK(say(*object, OutputReason::Focus) == "Site header landmark banner");

    SECTION("unless the user turned landmarks off") {
        fixture.registry.settings().reportLandmarks = false;
        CHECK(say(*object, OutputReason::Focus) == "Site header landmark");
    }
}

TEST_CASE("a heading says its level", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref heading = backend.add(backend.desktopRef(), PlatformRole::Heading, "Chapter two");
    backend.node(heading).attributes = {{"level", "2"}};

    const ObjectPtr object = fixture.registry.objectFor(heading);
    CHECK(object->role() == Role::Heading2);
    // NVDA's label for Role.HEADING2 is "heading 2", and the level is said
    // again from positionInfo -- which is what NVDA does too, because Gecko
    // reports the level through groupPosition as well as through the
    // attribute. Phase 6 presents headings through the text layer, where
    // NVDA says "heading level 2" once.
    CHECK(say(*object, OutputReason::Query) == "Chapter two heading 2 level 2");
}

TEST_CASE("an error message is said only when the entry is invalid",
          "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref box = backend.add(backend.desktopRef(), PlatformRole::Panel);
    const Ref message = backend.add(box, PlatformRole::Label, "That is not an email address");
    const Ref entry = backend.add(box, PlatformRole::Entry, "Email");
    backend.node(entry).relations[PlatformRelation::ErrorMessage] = {message};
    const ObjectPtr object = fixture.registry.objectFor(entry);

    CHECK(say(*object, OutputReason::Focus) == "Email edit");

    SECTION("and said when it is") {
        backend.addState(entry, PlatformState::InvalidEntry);
        alus::core::invalidateAllCaches();
        CHECK(say(*object, OutputReason::Focus) ==
              "Email edit invalid entry That is not an email address");
    }
}

TEST_CASE("a description equal to the name is not said twice", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref button = backend.add(backend.desktopRef(), PlatformRole::Button, "Save");
    backend.node(button).description = "Save";
    CHECK(say(*fixture.registry.objectFor(button), OutputReason::Focus) == "Save button");
}

TEST_CASE("focus entered says less than focus", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref group = backend.add(backend.desktopRef(), PlatformRole::Grouping, "Appearance");
    backend.node(group).actionNames = {"click"};
    backend.node(group).actionBindings = {";;<Alt>a"};
    backend.addInterface(group, Interface::Action);
    const ObjectPtr object = fixture.registry.objectFor(group);

    // NVDA #15826: a container's shortcut key is often defined and does not
    // work, so it is not said when merely entering the container.
    CHECK(say(*object, OutputReason::FocusEntered) == "Appearance grouping");
    CHECK(say(*object, OutputReason::Focus) == "Appearance grouping <Alt>a");
}

TEST_CASE("the settings switch properties off", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref button = backend.add(backend.desktopRef(), PlatformRole::Button, "Save");
    backend.node(button).description = "Writes the file";
    backend.node(button).actionNames = {"click"};
    backend.node(button).actionBindings = {";;<Control>s"};
    backend.addInterface(button, Interface::Action);
    const ObjectPtr object = fixture.registry.objectFor(button);

    CHECK(say(*object, OutputReason::Focus) == "Save button Writes the file <Control>s");

    fixture.registry.settings().reportObjectDescriptions = false;
    CHECK(say(*object, OutputReason::Focus) == "Save button <Control>s");

    fixture.registry.settings().reportKeyboardShortcuts = false;
    CHECK(say(*object, OutputReason::Focus) == "Save button");
}

TEST_CASE("OnlyCache says nothing and still remembers", "[a11y][speech]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref checkbox = backend.add(backend.desktopRef(), PlatformRole::CheckBox, "Dark mode");
    const ObjectPtr object = fixture.registry.objectFor(checkbox);

    CHECK(say(*object, OutputReason::OnlyCache).empty());

    backend.addState(checkbox, PlatformState::Checked);
    alus::core::invalidateAllCaches();
    PropertySelection selection;
    selection.states = true;
    // It knows what it did not say, so the change is one word.
    CHECK(joined(getObjectPropertiesSpeech(*object, OutputReason::Change, selection)) ==
          "checked");
}
