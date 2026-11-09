// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What a control reads as in braille, and what a line of text reads as.
//
// These are not numbered exit criteria; they are the reason the numbered ones
// matter. §14's port is only correct if the abbreviations, the field rules and
// the position maps are NVDA's, and each rule below is one assertion -- which
// is what `braille::properties` being pure functions buys.
#include "alus/braille/properties.hpp"

#include "alus/braille/labels.hpp"
#include "alus/braille/region.hpp"
#include "alus/braille/text_region.hpp"
#include "braille/support.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::a11y::IsCurrent;
using alus::a11y::Role;
using alus::a11y::State;
using alus::a11y::StateSet;
using alus::braille::ObjectRegion;
using alus::braille::ObjectTextSource;
using alus::braille::PropertyValues;
using alus::braille::TextInfoRegion;
using alus::braille::getControlFieldBraille;
using alus::braille::getFormatFieldBraille;
using alus::braille::getPropertiesBraille;
using alus::braille::roleBrailleLabel;
using alus::testing::BrailleFixture;
using alus::testing::ButtonTree;
using alus::testing::DocumentTree;
using alus::testing::TestHost;
using alus::text::ControlField;
using alus::text::FormatField;
using alus::text::FormatReportSettings;
using alus::text::PresentationCategory;

namespace {

PropertyValues forRole(Role role, StateSet states = {}) {
    PropertyValues values;
    values.role = role;
    values.realRole = role;
    values.states = states;
    values.realStates = states;
    return values;
}

}  // namespace

TEST_CASE("the abbreviations are NVDA's, and a missing one falls back",
          "[braille][labels]") {
    CHECK(roleBrailleLabel(Role::CheckBox) == "chk");
    CHECK(roleBrailleLabel(Role::Button) == "btn");
    CHECK(roleBrailleLabel(Role::EditableText) == "edt");
    CHECK(roleBrailleLabel(Role::Link) == "lnk");
    CHECK(roleBrailleLabel(Role::Terminal) == "term");
    // A separator is five dot-36 cells: a picture, not a word, and the same
    // in every language.
    CHECK(roleBrailleLabel(Role::Separator) == "⠤⠤⠤⠤⠤");
    // No abbreviation exists for a slider, so the spoken label is used --
    // NVDA's own rule, and better than inventing one nobody knows.
    CHECK(alus::braille::roleAbbreviation(Role::Slider).empty());
    CHECK(roleBrailleLabel(Role::Slider) == "slider");
}

TEST_CASE("getPropertiesBraille builds the line NVDA builds",
          "[braille][properties]") {
    SECTION("a checked check box is its name, the shape and the abbreviation") {
        PropertyValues values = forRole(Role::CheckBox, {State::Checked, State::Focusable});
        values.name = "Remember me";
        CHECK(getPropertiesBraille(values) == "Remember me ⣏⣿⣹ chk");
    }

    SECTION("an unchecked check box says so, because that is information") {
        PropertyValues values =
            forRole(Role::CheckBox, {State::Checkable, State::Focused, State::Focusable});
        values.name = "Remember me";
        CHECK(getPropertiesBraille(values) == "Remember me ⣏⣀⣹ chk");
    }

    SECTION("a heading collapses its level into the role text") {
        PropertyValues values = forRole(Role::Heading);
        values.name = "Introduction";
        values.level = 2;
        // "h2", and the level is NOT then repeated as "lv 2".
        CHECK(getPropertiesBraille(values) == "Introduction h2");
    }

    SECTION("a visited link collapses the state into the role text") {
        PropertyValues values = forRole(Role::Link, {State::Visited});
        values.name = "Downloads";
        CHECK(getPropertiesBraille(values) == "Downloads vlnk");
        // An unvisited one is a plain link. N53 again: `State::Linked` is
        // never the test, because ALUS synthesises it for every link.
        PropertyValues plain = forRole(Role::Link);
        plain.name = "Downloads";
        CHECK(getPropertiesBraille(plain) == "Downloads lnk");
    }

    SECTION("a list appends its item count to its own abbreviation") {
        PropertyValues values = forRole(Role::List);
        values.name = "Messages";
        values.childControlCount = 5;
        CHECK(getPropertiesBraille(values) == "Messages lst5");
    }

    SECTION("a table cell is its coordinates, in two cells each") {
        PropertyValues values = forRole(Role::TableCell);
        values.rowNumber = 3;
        values.columnNumber = 2;
        CHECK(getPropertiesBraille(values) == "r3 c2");
        SECTION("and a span is a range") {
            values.rowSpan = 2;
            values.columnSpan = 3;
            CHECK(getPropertiesBraille(values) == "r3-4 c2-4");
        }
    }

    SECTION("a role that says nothing on focus is dropped when a name is there") {
        PropertyValues values = forRole(Role::StaticText);
        values.name = "Ready";
        CHECK(getPropertiesBraille(values) == "Ready");
    }

    SECTION("the position in a group is said short") {
        PropertyValues values = forRole(Role::ListItem);
        values.name = "Inbox";
        values.indexInGroup = 2;
        values.similarItemsInGroup = 7;
        CHECK(getPropertiesBraille(values) == "Inbox 2 of 7");
    }

    SECTION("aria-current and a placeholder come last") {
        PropertyValues values = forRole(Role::Link);
        values.name = "Home";
        values.current = IsCurrent::Page;
        CHECK(getPropertiesBraille(values) == "Home lnk current page");
    }

    SECTION("a state with no abbreviation is SILENT, where speech says a word") {
        // This is the one place braille differs from speech in kind and not
        // only in length: the braille state dictionary is a filter.
        PropertyValues values = forRole(Role::Button, {State::Focusable, State::Visited});
        values.name = "Save";
        CHECK(getPropertiesBraille(values) == "Save btn");
    }
}

TEST_CASE("getControlFieldBraille reports a container once", "[braille][properties]") {
    FormatReportSettings format;
    alus::a11y::Settings objectSettings;
    alus::braille::Settings brailleSettings;

    ControlField list;
    list.role = Role::List;
    list.uniqueId = "list-1";
    // READ-ONLY IS WHAT MAKES A LIST A CONTAINER. `getPresentationCategory`
    // (phase 5, textInfos/__init__.py:204) sorts a writable list -- a form's
    // list box -- as a single line, and only a read-only one, which is what a
    // list in a document is, as a container. Without the state the three
    // sections below would be asserting about a combo box.
    list.states.add(State::ReadOnly);
    list.set("_childcontrolcount", 5);
    list.set("_startOfNode", true);

    SECTION("at the start of the node it is announced") {
        PresentationCategory category = PresentationCategory::Layout;
        const auto text = getControlFieldBraille(list, {}, /*reportStart=*/true, format,
                                                 objectSettings, brailleSettings, &category);
        REQUIRE(text.has_value());
        CHECK(category == PresentationCategory::Container);
        CHECK(*text == "lst5");
    }

    SECTION("in the middle of the node it is not") {
        // The third item of a list must not re-announce the list. This is the
        // flag `nav::AncestorChainDiff` sets, and the reason it was added.
        list.set("_startOfNode", false);
        const auto text = getControlFieldBraille(list, {}, /*reportStart=*/true, format,
                                                 objectSettings, brailleSettings);
        CHECK_FALSE(text.has_value());
    }

    SECTION("its end is announced as `<role> end`") {
        const auto text = getControlFieldBraille(list, {}, /*reportStart=*/false, format,
                                                 objectSettings, brailleSettings);
        REQUIRE(text.has_value());
        CHECK(*text == "lst end");
    }

    SECTION("a landmark's end is NOT announced") {
        ControlField landmark;
        landmark.role = Role::Landmark;
        landmark.landmark = "navigation";
        landmark.set("_startOfNode", true);
        const auto end = getControlFieldBraille(landmark, {}, /*reportStart=*/false, format,
                                                objectSettings, brailleSettings);
        CHECK_FALSE(end.has_value());
        // Its start is, with the four-letter landmark abbreviation.
        const auto start = getControlFieldBraille(landmark, {}, /*reportStart=*/true, format,
                                                  objectSettings, brailleSettings);
        REQUIRE(start.has_value());
        CHECK(*start == "lmk navi");
    }

    SECTION("a layout container is worth no cells at all") {
        ControlField box;
        box.role = Role::Section;
        box.set("_startOfNode", true);
        PresentationCategory category = PresentationCategory::Container;
        const auto text = getControlFieldBraille(box, {}, /*reportStart=*/true, format,
                                                 objectSettings, brailleSettings, &category);
        CHECK(category == PresentationCategory::Layout);
        CHECK_FALSE(text.has_value());
    }

    SECTION("a table cell is its coordinates and its column header") {
        ControlField cell;
        cell.role = Role::TableCell;
        cell.set("table-id", "t1");
        cell.set("table-rownumber", 2);
        cell.set("table-columnnumber", 3);
        cell.set("table-columnheadertext", "Price");
        // THE ANCESTOR CHAIN IS NOT DECORATION, in braille either: with an
        // empty chain the same cell is PRESCAT_LAYOUT and worth no cells at
        // all, which is what keeps a layout table off the display.
        ControlField table;
        table.role = Role::Table;
        const std::vector<ControlField> ancestors{table};
        const auto text = getControlFieldBraille(cell, ancestors, /*reportStart=*/true, format,
                                                 objectSettings, brailleSettings);
        REQUIRE(text.has_value());
        CHECK(*text == "r2 Price c3");
    }

    SECTION("a link's content follows its role") {
        ControlField link;
        link.role = Role::Link;
        link.set("_startOfNode", true);
        link.set("content", "Downloads");
        const auto text = getControlFieldBraille(link, {}, /*reportStart=*/true, format,
                                                 objectSettings, brailleSettings);
        REQUIRE(text.has_value());
        CHECK(*text == "lnk Downloads");
    }
}

TEST_CASE("getFormatFieldBraille reports only what changed", "[braille][properties]") {
    FormatReportSettings format;
    alus::braille::Settings brailleSettings;
    FormatField cache;

    SECTION("a link is reported when it appears and not again") {
        FormatField field;
        field.set("link", std::string{"https://example.com"});
        CHECK(getFormatFieldBraille(field, cache, /*isAtStart=*/false, format,
                                    brailleSettings) == "lnk");
        // The same link in the next run is not news.
        CHECK(getFormatFieldBraille(field, cache, false, format, brailleSettings).empty());
    }

    SECTION("a heading level is reported only at the start of the line") {
        FormatField field;
        field.set("heading-level", 3);
        CHECK(getFormatFieldBraille(field, cache, /*isAtStart=*/true, format,
                                    brailleSettings) == "h3");
        cache = FormatField{};
        CHECK(getFormatFieldBraille(field, cache, /*isAtStart=*/false, format,
                                    brailleSettings).empty());
    }

    SECTION("the paragraph marker appears when reading by paragraph") {
        brailleSettings.readByParagraph = true;
        FormatField field;
        CHECK(getFormatFieldBraille(field, cache, /*isAtStart=*/true, format,
                                    brailleSettings) == "¶");
    }

    SECTION("the formatting tags bracket a bold run, once each") {
        brailleSettings.fontFormattingDisplay =
            alus::braille::FontFormattingDisplay::Tags;
        brailleSettings.reportFontAttributes = true;
        FormatField bold;
        bold.set("bold", true);
        // ⣋ opens the tag, ⠃ is the braille letter b, ⣙ closes it.
        CHECK(getFormatFieldBraille(bold, cache, false, format, brailleSettings) == "⣋⠃⣙");
        // Still bold: no tag.
        CHECK(getFormatFieldBraille(bold, cache, false, format, brailleSettings).empty());
        // Bold ended: the closing marker is b plus dot 7.
        FormatField plain;
        CHECK(getFormatFieldBraille(plain, cache, false, format, brailleSettings) == "⣋⡃⣙");
    }

    SECTION("a spelling error has its own setting, separate from font attributes") {
        brailleSettings.fontFormattingDisplay =
            alus::braille::FontFormattingDisplay::Tags;
        brailleSettings.reportFontAttributes = false;
        brailleSettings.reportSpellingErrorsInBraille = true;
        FormatField misspelt;
        misspelt.set("invalid-spelling", true);
        CHECK(getFormatFieldBraille(misspelt, cache, false, format, brailleSettings) ==
              "⣋⠑⣙");
    }
}

TEST_CASE("an object region shows the control, and a routing key activates it",
          "[braille][region]") {
    BrailleFixture fixture;
    ButtonTree tree{fixture.backend};
    const alus::a11y::ObjectPtr button = fixture.registry.objectFor(tree.button);
    REQUIRE(button != nullptr);

    ObjectRegion region{fixture.handler(), button};
    region.update();
    // The keyboard shortcut is the ATK accelerator verbatim, as
    // Object::keyboardShortcut answers it (phase 4); braille does not
    // prettify it, and NVDA does not either.
    CHECK(region.rawText == "Save btn Save the current document <Control>s");
    CHECK_FALSE(region.brailleCells.empty());
    CHECK(region.object() == button);

    SECTION("a routing key performs the default action") {
        region.routeTo(0);
        CHECK(fixture.backend.performed.size() == 1);
    }

    SECTION("a dead object leaves the region empty rather than throwing") {
        fixture.backend.kill(tree.button);
        alus::core::invalidateAllCaches();
        CHECK_FALSE(region.isAlive());
        region.update();
        CHECK(region.rawText.empty());
    }
}

TEST_CASE("a text region shows a line, maps every cell, and routes the caret",
          "[braille][region]") {
    BrailleFixture fixture;
    DocumentTree tree{fixture.backend, "first line\nsecond line\n"};
    const alus::a11y::ObjectPtr document = fixture.registry.objectFor(tree.document);
    REQUIRE(document != nullptr);

    TextInfoRegion region{fixture.handler(), std::make_unique<ObjectTextSource>(document)};
    region.update();

    SECTION("the reading unit is the line the caret is on") {
        // The trailing newline is stripped and one blank cell is added for the
        // cursor to sit on at the end of the line.
        CHECK(region.rawText == "first line ");
        CHECK(region.cursorPos.has_value());
        CHECK(*region.cursorPos == 0);
        CHECK(region.brailleCells.size() == 11);
    }

    SECTION("a multi-line control is pinned hard left when focused") {
        CHECK(region.focusToHardLeft);
    }

    SECTION("every cell maps back to a position in the document") {
        const alus::text::TextRangePtr atStart = region.rangeForBraillePos(0);
        REQUIRE(atStart != nullptr);
        const alus::text::TextRangePtr atSix = region.rangeForBraillePos(6);
        REQUIRE(atSix != nullptr);
        alus::text::TextRangePtr word = atSix->copy();
        word->expand(alus::text::TextUnit::Word);
        // A word expansion takes the whitespace that FOLLOWS the word
        // (phase 5: "one ", "two "), and at the end of a line that
        // whitespace is the line break.
        CHECK(word->text() == "line\n");
    }

    SECTION("a routing key moves the caret there") {
        region.routeTo(6);
        REQUIRE(fixture.backend.caretsSet.size() == 1);
        CHECK(fixture.backend.caretsSet.back().offset == 6);
    }

    SECTION("the caret further down the document hides the context in front") {
        fixture.backend.node(tree.document).caret = 14;
        alus::core::invalidateAllCaches();
        region.update();
        CHECK(region.rawText == "second line ");
        // Not the start of the object, so the focus context no longer applies.
        CHECK(region.hidePreviousRegions);
    }

    SECTION("a selection is marked with dots 7 and 8") {
        fixture.backend.node(tree.document).selectionStart = 0;
        fixture.backend.node(tree.document).selectionEnd = 5;
        alus::core::invalidateAllCaches();
        region.update();
        REQUIRE(region.brailleSelectionStart.has_value());
        REQUIRE(region.brailleSelectionEnd.has_value());
        CHECK(*region.brailleSelectionStart == 0);
        CHECK(*region.brailleSelectionEnd == 5);
        for (int cell = 0; cell < 5; ++cell) {
            CHECK((region.brailleCells[static_cast<std::size_t>(cell)] & 0xC0) == 0xC0);
        }
    }
}
