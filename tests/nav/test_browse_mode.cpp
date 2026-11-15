// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 6 exit criteria, T6.1 to T6.10 (docs/history/phase-plan.md).
//
// Every one of them runs against the mock platform, which means against a
// document shaped the way N47 measured a real one: the root's own text is
// U+FFFC per child, not the body. That is the point -- a fixture that handed
// the root the whole text would pass tests no browser can pass.
#include "nav/browse_support.hpp"

#include "alus/core/cached.hpp"
#include "alus/nav/browse_mode.hpp"
#include "alus/nav/control_field.hpp"
#include "alus/nav/document_range.hpp"
#include "alus/input/gesture.hpp"
#include "alus/nav/quick_nav.hpp"
#include "alus/script/script.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace alus;
using namespace alus::testing;

namespace {

// The fields and text of a range, as strings, so an assertion reads like the
// announcement it is about.
std::vector<std::string> streamOf(const text::TextWithFields& fields) {
    std::vector<std::string> out;
    for (const text::TextOrField& item : fields) {
        if (item.isText()) {
            out.push_back("text:" + *item.text);
            continue;
        }
        switch (item.command->kind()) {
            case text::FieldCommand::Kind::ControlStart: {
                const text::ControlField* field = item.command->control();
                out.push_back(std::string("start:") +
                              a11y::roleName(field != nullptr ? field->role
                                                              : a11y::Role::Unknown));
                break;
            }
            case text::FieldCommand::Kind::ControlEnd:
                out.push_back("end");
                break;
            case text::FieldCommand::Kind::FormatChange:
                break;
        }
    }
    return out;
}

std::size_t countOf(const std::vector<std::string>& stream, const std::string& what) {
    return static_cast<std::size_t>(std::count(stream.begin(), stream.end(), what));
}

}  // namespace

// ---------------------------------------------------------------------------
// T6.1 -- ControlFieldSynth: a GTK/web object gives a field with the right
//         role, states, name and level.
// ---------------------------------------------------------------------------

TEST_CASE("T6.1 control fields are synthesised from objects", "[browsemode][T6.1]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const text::FormatReportSettings settings;
    const nav::ControlFieldSynth synth{settings};

    SECTION("a heading carries its role and its level") {
        const a11y::ObjectPtr heading = fixture.registry.objectFor(tree.heading);
        const std::optional<text::ControlField> field = synth.forObject(*heading);
        REQUIRE(field.has_value());
        // `Heading` WITH A LEVEL, as NVDA's virtual buffers give it -- not
        // the object model's `Heading1`. This asserted `Heading1` and said it
        // was what the presentation layer needed for "heading level 1"; the
        // presentation rules for FIELDS test `Role::Heading` alone, so a
        // `Heading1` field was Layout and silent, and in Chrome neither the
        // down arrow nor `h` ever said "heading" (phase-10.md N116).
        CHECK(field->role == a11y::Role::Heading);
        CHECK(field->getInt("level") == 1);
        CHECK(field->isBlock);
        // And the consequence that matters: it is SPOKEN on a line.
        CHECK(field->getPresentationCategory({}, settings) ==
              text::PresentationCategory::SingleLine);
    }

    SECTION("a list carries its item count, which is what 'with 3 items' needs") {
        const a11y::ObjectPtr list = fixture.registry.objectFor(tree.list);
        const std::optional<text::ControlField> field = synth.forObject(*list);
        REQUIRE(field.has_value());
        CHECK(field->role == a11y::Role::List);
        CHECK(field->getInt("_childcontrolcount") == 3);
    }

    SECTION("a paragraph is a text node and gets NO field at all") {
        const a11y::ObjectPtr paragraph = fixture.registry.objectFor(tree.paragraph);
        CHECK_FALSE(synth.forObject(*paragraph).has_value());
        // ...unless the caller says it is an embedded object.
        CHECK(synth.forObject(*paragraph, /*ignoreEditableText=*/false).has_value());
    }

    SECTION("a named link destination gets no field either") {
        const a11y::Ref anchor =
            addTextLeaf(fixture.backend, tree.document, PlatformRole::Link, "", "top");
        // No action and not focusable: <a name="top">, which cannot be
        // activated. AT-SPI has no LINKED state, so this is the honest test
        // (see nav::isNamedLinkDestination).
        const a11y::ObjectPtr object = fixture.registry.objectFor(anchor);
        CHECK_FALSE(synth.forObject(*object).has_value());
    }

    SECTION("a real link does get one") {
        const a11y::Ref link = tree.addLink(fixture.backend, "the docs");
        const a11y::ObjectPtr object = fixture.registry.objectFor(link);
        const std::optional<text::ControlField> field = synth.forObject(*object);
        REQUIRE(field.has_value());
        CHECK(field->role == a11y::Role::Link);
        CHECK(field->states.has(a11y::State::Linked));
    }

    SECTION("the four obvious states are discarded") {
        const a11y::Ref edit = tree.addEdit(fixture.backend, "Search");
        fixture.backend.addState(edit, PlatformState::Focused);
        fixture.backend.addState(edit, PlatformState::MultiLine);
        const a11y::ObjectPtr object = fixture.registry.objectFor(edit);
        const std::optional<text::ControlField> field =
            synth.forObject(*object, /*ignoreEditableText=*/false);
        REQUIRE(field.has_value());
        CHECK_FALSE(field->states.has(a11y::State::Editable));
        CHECK_FALSE(field->states.has(a11y::State::MultiLine));
        CHECK_FALSE(field->states.has(a11y::State::Focused));
        CHECK_FALSE(field->states.has(a11y::State::Focusable));
    }

    SECTION("a table cell carries 1-based coordinates and its column header") {
        const a11y::ObjectPtr cell = fixture.registry.objectFor(tree.cellA);
        const std::optional<text::ControlField> field = synth.forObject(*cell);
        REQUIRE(field.has_value());
        // AT-SPI counts from 0; every word the user hears counts from 1.
        CHECK(field->getInt("table-rownumber") == 2);
        CHECK(field->getInt("table-columnnumber") == 1);
        CHECK(field->get("table-columnheadertext") == "Name");
        CHECK(field->getInt("table-id").has_value());
    }

    SECTION("a self-referential header is not said twice") {
        // Chrome emits a header cell whose own header is itself.
        fixture.backend.node(tree.nameHeader).columnHeader = "Name";
        const a11y::ObjectPtr header = fixture.registry.objectFor(tree.nameHeader);
        const std::optional<text::ControlField> field = synth.forObject(*header);
        REQUIRE(field.has_value());
        CHECK_FALSE(field->contains("table-columnheadertext"));
    }
}

// ---------------------------------------------------------------------------
// T6.2 -- AncestorChainDiff: a nested list in a table gives the right
//         controlStart/controlEnd stream.
// ---------------------------------------------------------------------------

TEST_CASE("T6.2 the ancestor chain diff emits a balanced nested stream",
          "[browsemode][T6.2]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const text::FormatReportSettings settings;

    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
    text::TextRangePtr whole =
        nav::DocumentTextRange::make(root, settings, text::Position::All);
    REQUIRE(whole != nullptr);

    const std::vector<std::string> stream = streamOf(whole->getTextWithFields(settings));

    SECTION("every start has an end") {
        CHECK(countOf(stream, "end") ==
              static_cast<std::size_t>(std::count_if(
                  stream.begin(), stream.end(), [](const std::string& one) {
                      return one.rfind("start:", 0) == 0;
                  })));
    }

    SECTION("the list opens once, holds its three items, and closes once") {
        const auto listStart = std::find(stream.begin(), stream.end(), "start:LIST");
        REQUIRE(listStart != stream.end());
        CHECK(countOf(stream, "start:LIST") == 1);
        // The three items are inside it, and each is its own control.
        CHECK(countOf(stream, "start:LISTITEM") == 3);
        const auto one = std::find(stream.begin(), stream.end(), "text:One");
        const auto three = std::find(stream.begin(), stream.end(), "text:Three");
        REQUIRE(one != stream.end());
        REQUIRE(three != stream.end());
        CHECK(listStart < one);
        CHECK(one < three);
    }

    SECTION("the whole document's text is there, in reading order") {
        const std::string text = whole->text();
        CHECK(text.find("Interfaces") != std::string::npos);
        CHECK(text.find("The first paragraph.") != std::string::npos);
        CHECK(text.find("One") < text.find("Two"));
        CHECK(text.find("Two") < text.find("Three"));
    }

    SECTION("a table cell's field carries the table, not just the cell") {
        const auto cellStart = std::find(stream.begin(), stream.end(), "start:TABLECELL");
        const auto tableStart = std::find(stream.begin(), stream.end(), "start:TABLE");
        REQUIRE(tableStart != stream.end());
        REQUIRE(cellStart != stream.end());
        CHECK(tableStart < cellStart);
    }

    SECTION("an image resolves to its name instead of the U+FFFC placeholder (N47)") {
        const a11y::Ref image = tree.addImage(fixture.backend, "ALUS logo");
        // The document's child list is cached for the cycle (§3.5), so the
        // cycle has to end before the new child is visible -- which is also
        // true of a real page that has just grown a node.
        fixture.pumpOnce();
        const a11y::ObjectPtr grown = fixture.registry.objectFor(tree.document);
        text::TextRangePtr again =
            nav::DocumentTextRange::make(grown, settings, text::Position::All);
        REQUIRE(again != nullptr);
        bool found = false;
        for (const text::TextOrField& item : again->getTextWithFields(settings)) {
            if (!item.isCommand() ||
                item.command->kind() != text::FieldCommand::Kind::ControlStart) {
                continue;
            }
            const text::ControlField* field = item.command->control();
            if (field != nullptr && field->role == a11y::Role::Graphic) {
                CHECK(field->get("content") == "ALUS logo");
                found = true;
            }
        }
        CHECK(found);
        (void)image;
    }
}

// ---------------------------------------------------------------------------
// T6.3 -- commonFieldCount continuity: moving through many lines does NOT
//         repeat "list with 3 items" on every line.
// ---------------------------------------------------------------------------

TEST_CASE("T6.3 a container is announced once, not once per line",
          "[browsemode][T6.3]") {
    NavFixture fixture;
    MockBackend& backend = fixture.backend;
    const a11y::Ref application = backend.addApplication("Browser", "Chromium");
    const a11y::Ref window = backend.add(application, PlatformRole::Frame, "Long list");
    backend.addState(window, PlatformState::Active);
    const a11y::Ref document = backend.add(window, PlatformRole::DocumentWeb, "Long list");
    backend.addInterface(document, a11y::Interface::Collection);

    // A hundred list items: the shape T6.3 names.
    const a11y::Ref list = backend.add(document, PlatformRole::List);
    backend.addState(list, PlatformState::ReadOnly);
    constexpr int kItems = 100;
    for (int index = 0; index < kItems; ++index) {
        addTextLeaf(backend, list, PlatformRole::ListItem, "item " + std::to_string(index));
    }
    makeContainer(backend, list, kItems);
    makeContainer(backend, document, 1);

    const text::FormatReportSettings settings;
    const a11y::ObjectPtr root = fixture.registry.objectFor(document);

    // The state cache is what makes the diff a diff -- a caller that drops it
    // between lines gets the full announcement every time, which is the bug
    // this test is named after.
    a11y::TextRangeSpeechState state;
    int announcedListEntry = 0;
    std::string firstLine;

    text::TextRangePtr caret =
        nav::DocumentTextRange::make(root, settings, text::Position::First);
    REQUIRE(caret != nullptr);

    for (int line = 0; line < kItems; ++line) {
        text::TextRangePtr toRead = caret->copy();
        REQUIRE(toRead->expand(text::TextUnit::Line));
        a11y::TextRangeSpeechOptions options;
        options.unit = text::TextUnit::Line;
        options.reason = a11y::OutputReason::Caret;
        const a11y::TextRangeSpeechResult result =
            a11y::getTextRangeSpeech(*toRead, &state, settings, options, nullptr);
        std::string said;
        for (const speech::SpeechSequence& sequence : result.sequences) {
            said += joined(sequence) + " ";
        }
        if (line == 0) {
            firstLine = said;
        }
        if (said.find("with 100 items") != std::string::npos) {
            ++announcedListEntry;
        }
        if (caret->move(text::TextUnit::Line, 1) == 0) {
            break;
        }
    }

    // The first line announces the list. No other line does.
    CHECK(firstLine.find("with 100 items") != std::string::npos);
    CHECK(announcedListEntry == 1);
}

// ---------------------------------------------------------------------------
// T6.4 -- the uniqueID is stable across AT-SPI (bus name, object path).
// ---------------------------------------------------------------------------

TEST_CASE("T6.4 the control field's uniqueID is stable and distinguishing",
          "[browsemode][T6.4]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};

    const a11y::ObjectPtr heading = fixture.registry.objectFor(tree.heading);
    const a11y::ObjectPtr list = fixture.registry.objectFor(tree.list);

    const std::string headingId = nav::ControlFieldSynth::uniqueIdFor(*heading);
    const std::string listId = nav::ControlFieldSynth::uniqueIdFor(*list);

    SECTION("it is not empty and the two objects differ") {
        CHECK_FALSE(headingId.empty());
        CHECK(headingId != listId);
    }

    SECTION("it is the same across pump cycles, which is what the diff needs") {
        fixture.pumpOnce();
        const a11y::ObjectPtr again = fixture.registry.objectFor(tree.heading);
        CHECK(nav::ControlFieldSynth::uniqueIdFor(*again) == headingId);
    }

    SECTION("it carries both the bus name and the path") {
        // Neither alone identifies an object: every object in one application
        // shares the bus name, and two applications both have a root path.
        CHECK(headingId.find(heading->path()) != std::string::npos);
        CHECK(headingId.size() > heading->path().size());
    }

    SECTION("the synthesised field carries it, so commonFieldCount can use it") {
        const text::FormatReportSettings settings;
        const nav::ControlFieldSynth synth{settings};
        const std::optional<text::ControlField> field = synth.forObject(*list);
        REQUIRE(field.has_value());
        CHECK(field->uniqueId == listId);
        CHECK(field->get("uniqueID") == listId);
    }
}

// ---------------------------------------------------------------------------
// T6.5 -- QuickNav: each of the 22 types from §9.2 has a rule, and the rule
//         finds the right element.
// ---------------------------------------------------------------------------

TEST_CASE("T6.5 every quick-nav type has a match rule", "[browsemode][T6.5]") {
    // §9.2's table has 22 rows plus the nine heading levels. Every one of
    // them must produce a rule; only `n` (derived) may not.
    std::size_t withRule = 0;
    for (const nav::ElementType type : nav::allElementTypes()) {
        const std::optional<a11y::MatchRule> rule = nav::QuickNav::ruleFor(type);
        if (type == nav::ElementType::NotLinkBlock) {
            CHECK_FALSE(rule.has_value());
            continue;
        }
        INFO("type " << nav::elementTypeScriptName(type));
        REQUIRE(rule.has_value());
        // A rule that constrains nothing would match every object in the
        // document, which is worse than no rule at all.
        const bool constrains = rule->roleMatch != a11y::MatchType::Invalid ||
                                rule->stateMatch != a11y::MatchType::Invalid ||
                                rule->attributeMatch != a11y::MatchType::Invalid ||
                                rule->interfaceMatch != a11y::MatchType::Invalid;
        CHECK(constrains);
        ++withRule;
    }
    CHECK(withRule >= 22);
}

TEST_CASE("T6.5 quick navigation finds the right element", "[browsemode][T6.5]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::Ref visited = tree.addLink(fixture.backend, "seen", /*visited=*/true);
    const a11y::Ref fresh = tree.addLink(fixture.backend, "new", /*visited=*/false);

    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
    const nav::QuickNav quickNav{root};
    const a11y::ObjectPtr first = fixture.registry.objectFor(tree.heading);

    SECTION("h finds the heading") {
        const a11y::ObjectPtr found =
            quickNav.find(nav::ElementType::Heading, root, /*forwards=*/true);
        REQUIRE(found != nullptr);
        CHECK(found->ref() == tree.heading);
    }

    SECTION("1 finds the level 1 heading and 2 finds nothing") {
        CHECK(quickNav.find(nav::ElementType::Heading1, root, true) != nullptr);
        CHECK(quickNav.find(nav::ElementType::Heading2, root, true) == nullptr);
    }

    SECTION("l finds the list and i finds its first item") {
        const a11y::ObjectPtr list = quickNav.find(nav::ElementType::List, root, true);
        REQUIRE(list != nullptr);
        CHECK(list->ref() == tree.list);
        const a11y::ObjectPtr item = quickNav.find(nav::ElementType::ListItem, root, true);
        REQUIRE(item != nullptr);
        CHECK(item->ref() == tree.item1);
    }

    SECTION("N130 e finds an editable combo box, and not a plain drop-down or a read-only entry") {
        // The YouTube search field: ARIA combobox over a text input, exposed
        // by Chromium as COMBO_BOX with EDITABLE.
        const a11y::Ref dropDown =
            fixture.backend.add(tree.document, a11y::PlatformRole::ComboBox, "Sort by");
        const a11y::Ref readOnly =
            fixture.backend.add(tree.document, a11y::PlatformRole::Entry, "Total");
        fixture.backend.addState(readOnly, a11y::PlatformState::ReadOnly);
        const a11y::Ref search =
            fixture.backend.add(tree.document, a11y::PlatformRole::ComboBox, "Search");
        fixture.backend.addState(search, a11y::PlatformState::Editable);
        (void)dropDown;
        const a11y::ObjectPtr found = quickNav.find(nav::ElementType::Edit, root, true);
        REQUIRE(found != nullptr);
        CHECK(found->ref() == search);
    }

    SECTION("t finds the table") {
        const a11y::ObjectPtr found = quickNav.find(nav::ElementType::Table, root, true);
        REQUIRE(found != nullptr);
        CHECK(found->ref() == tree.table);
    }

    SECTION("v and u tell visited links from unvisited ones") {
        const a11y::ObjectPtr seen =
            quickNav.find(nav::ElementType::VisitedLink, root, true);
        REQUIRE(seen != nullptr);
        CHECK(seen->ref() == visited);
        const a11y::ObjectPtr unseen =
            quickNav.find(nav::ElementType::UnvisitedLink, root, true);
        REQUIRE(unseen != nullptr);
        CHECK(unseen->ref() == fresh);
    }

    SECTION("shift+h from the end goes backwards") {
        const a11y::ObjectPtr last = fixture.registry.objectFor(tree.cellB);
        const a11y::ObjectPtr found =
            quickNav.find(nav::ElementType::Heading, last, /*forwards=*/false);
        REQUIRE(found != nullptr);
        CHECK(found->ref() == tree.heading);
    }

    SECTION("an unnamed grouping is not offered") {
        const a11y::Ref unnamed = fixture.backend.add(tree.document, PlatformRole::Grouping);
        CHECK(quickNav.find(nav::ElementType::Grouping, root, true) == nullptr);
        fixture.backend.node(unnamed).name = "Options";
        // The property cache is per pump cycle (§3.5), so a test that changes
        // the platform must end the cycle -- exactly as the daemon does.
        fixture.pumpOnce();
        const a11y::ObjectPtr found =
            quickNav.find(nav::ElementType::Grouping, root, true);
        REQUIRE(found != nullptr);
        CHECK(found->ref() == unnamed);
    }

    SECTION("there is nothing after the last one, and that is not an error") {
        const a11y::ObjectPtr heading = fixture.registry.objectFor(tree.heading);
        CHECK(quickNav.find(nav::ElementType::Heading, heading, true) == nullptr);
    }

    SECTION("the elements list holds every link") {
        const std::vector<a11y::ObjectPtr> links = quickNav.all(nav::ElementType::Link);
        CHECK(links.size() == 2);
    }
    (void)first;
}

// ---------------------------------------------------------------------------
// T6.6 -- sortBy = FLOW gives READING order, not tree order.
// ---------------------------------------------------------------------------

TEST_CASE("T6.6 quick navigation follows reading order, not tree order",
          "[browsemode][T6.6]") {
    NavFixture fixture;
    MockBackend& backend = fixture.backend;
    const a11y::Ref application = backend.addApplication("Browser", "Chromium");
    const a11y::Ref window = backend.add(application, PlatformRole::Frame, "Floats");
    backend.addState(window, PlatformState::Active);
    const a11y::Ref document = backend.add(window, PlatformRole::DocumentWeb, "Floats");
    backend.addInterface(document, a11y::Interface::Collection);

    // A floated sidebar: EARLY in the tree, LATE on the page. This is the
    // real case the criterion is about, and the one that sends a user
    // backwards when the sort order is CANONICAL.
    const a11y::Ref sidebar =
        addTextLeaf(backend, document, PlatformRole::Heading, "Sidebar");
    backend.node(sidebar).attributes["level"] = "2";
    backend.node(sidebar).flowIndex = 900;

    const a11y::Ref body = addTextLeaf(backend, document, PlatformRole::Heading, "Body");
    backend.node(body).attributes["level"] = "2";
    backend.node(body).flowIndex = 10;
    makeContainer(backend, document, 2);

    const a11y::ObjectPtr root = fixture.registry.objectFor(document);
    const nav::QuickNav quickNav{root};

    // In TREE order the sidebar comes first. In READING order the body does,
    // and reading order is what the user is following.
    const a11y::ObjectPtr found =
        quickNav.find(nav::ElementType::Heading, root, /*forwards=*/true);
    REQUIRE(found != nullptr);
    CHECK(found->ref() == body);

    // And the sidebar is still reachable, just second.
    const a11y::ObjectPtr next =
        quickNav.find(nav::ElementType::Heading, found, /*forwards=*/true);
    REQUIRE(next != nullptr);
    CHECK(next->ref() == sidebar);
}

// ---------------------------------------------------------------------------
// T6.7 -- quick-nav latency. The criterion names p95 < 20 ms on the real
//         Firefox and Chromium; what a hermetic test CAN measure is the thing
//         that makes that possible, which is the CALL COUNT: one round trip
//         per press. See docs/history/phase-6.md for the measured wall-clock figures.
// ---------------------------------------------------------------------------

TEST_CASE("T6.7 a quick-nav press costs one collection call", "[browsemode][T6.7]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
    const nav::QuickNav quickNav{root};

    fixture.pumpOnce();
    const std::size_t before = fixture.backend.callsFor("matchesFrom");
    const a11y::ObjectPtr found =
        quickNav.find(nav::ElementType::Heading, root, /*forwards=*/true);
    const std::size_t after = fixture.backend.callsFor("matchesFrom");

    REQUIRE(found != nullptr);
    // ONE. Not one per node walked, which is what the virtual buffers of
    // §9.1 existed to avoid and what AT-SPI's Collection gives for free.
    CHECK(after - before == 1);

    SECTION("and a hundred presses cost a hundred calls, not ten thousand") {
        const std::size_t base = fixture.backend.callsFor("matchesFrom");
        a11y::ObjectPtr at = root;
        int presses = 0;
        for (int step = 0; step < 100; ++step) {
            a11y::ObjectPtr next = quickNav.find(nav::ElementType::ListItem, at, true);
            if (next == nullptr) {
                break;
            }
            at = next;
            ++presses;
        }
        CHECK(presses > 0);
        CHECK(fixture.backend.callsFor("matchesFrom") - base ==
              static_cast<std::size_t>(presses) + 1);
    }
}

// ---------------------------------------------------------------------------
// T6.8 -- auto focus mode: tab into an <input> gives focus mode; escape
//         gives browse mode back.
// ---------------------------------------------------------------------------

TEST_CASE("T6.8 focus mode is entered and left automatically", "[browsemode][T6.8]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::Ref edit = tree.addEdit(fixture.backend, "Search");

    SECTION("the decision is made from the object, one role at a time") {
        const a11y::ObjectPtr editObject = fixture.registry.objectFor(edit);
        CHECK(nav::BrowseMode::shouldPassThroughFor(*editObject));

        const a11y::ObjectPtr heading = fixture.registry.objectFor(tree.heading);
        CHECK_FALSE(nav::BrowseMode::shouldPassThroughFor(*heading));

        const a11y::ObjectPtr paragraph = fixture.registry.objectFor(tree.paragraph);
        CHECK_FALSE(nav::BrowseMode::shouldPassThroughFor(*paragraph));

        // A plain <ul> is content; a focusable listbox is a control.
        const a11y::ObjectPtr list = fixture.registry.objectFor(tree.list);
        CHECK_FALSE(nav::BrowseMode::shouldPassThroughFor(*list));
        fixture.backend.addState(tree.list, PlatformState::Focusable);
        fixture.pumpOnce();
        const a11y::ObjectPtr again = fixture.registry.objectFor(tree.list);
        CHECK(nav::BrowseMode::shouldPassThroughFor(*again));
    }

    SECTION("the interceptor switches when the focus lands in the edit field") {
        nav::TreeInterceptorRegistry* interceptors = fixture.nav().interceptors();
        REQUIRE(interceptors != nullptr);

        const a11y::ObjectPtr heading = fixture.registry.objectFor(tree.heading);
        nav::TreeInterceptor* interceptor = interceptors->update(heading);
        REQUIRE(interceptor != nullptr);
        CHECK_FALSE(interceptor->passThrough());

        const a11y::ObjectPtr editObject = fixture.registry.objectFor(edit);
        interceptor->onInnerFocus(editObject);
        CHECK(interceptor->passThrough());

        // Escape: back to browse mode.
        interceptor->setPassThrough(false);
        CHECK_FALSE(interceptor->passThrough());

        // And moving the focus back out of the control undoes ALUS's own
        // switch without the user pressing anything.
        interceptor->onInnerFocus(editObject);
        CHECK(interceptor->passThrough());
        interceptor->onInnerFocus(heading);
        CHECK_FALSE(interceptor->passThrough());
    }

    SECTION("an editable document is not intercepted at all") {
        // A rich-text compose field is a text editor that happens to be a
        // document; taking its arrow keys away would break writing in it.
        fixture.backend.addState(tree.document, PlatformState::Editable);
        fixture.pumpOnce();
        const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
        CHECK(nav::BrowseMode::chooseForDocument(root, fixture.nav()) == nullptr);
    }
}

// ---------------------------------------------------------------------------
// T6.9 -- single-letter navigation does NOT work in focus mode, and it is the
//         ten-step lookup that filters it.
// ---------------------------------------------------------------------------

TEST_CASE("T6.9 single-letter navigation is filtered by the script lookup",
          "[browsemode][T6.9]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
    nav::TreeInterceptorPtr interceptor =
        nav::BrowseMode::chooseForDocument(root, fixture.nav());
    REQUIRE(interceptor != nullptr);

    SECTION("the interceptor answers the quick-nav scripts by name") {
        CHECK(interceptor->scriptByName("nextHeading") != nullptr);
        CHECK(interceptor->scriptByName("previousHeading") != nullptr);
        CHECK(interceptor->scriptByName("nextCheckBox") != nullptr);
        CHECK(interceptor->scriptByName("moveByLine_forward") != nullptr);
        CHECK(interceptor->scriptByName("nextRow") != nullptr);
        CHECK(interceptor->scriptByName("elementsList") != nullptr);
    }

    SECTION("it answers to the NVDA class names the gesture maps address") {
        CHECK(interceptor->answersTo("browseMode.BrowseModeTreeInterceptor"));
        CHECK(interceptor->answersTo("cursorManager.CursorManager"));
        CHECK(interceptor->answersTo("documentBase.DocumentWithTableNavigation"));
    }

    SECTION("the grab set is rebuilt when the mode changes (N61)") {
        // The grabs are what decide whether the compositor gives ALUS the
        // letter at all, and they are computed by asking the lookup whether
        // the gesture would find a script HERE AND NOW. So the mode change
        // has to rebuild them -- in focus mode the letters must be RELEASED.
        //
        // The fixture has no input service (--no-input), so what can be
        // asserted here is that the call is made and survives a null one;
        // the real grab count was measured live (phase-6.md).
        CHECK_NOTHROW(interceptor->setPassThrough(true, /*announce=*/false));
        CHECK_NOTHROW(interceptor->setPassThrough(false, /*announce=*/false));
        CHECK_NOTHROW(fixture.nav().refreshGrabs());
    }

    SECTION("single letter navigation, off, releases the letters") {
        auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
        REQUIRE(browse != nullptr);
        REQUIRE(browse->singleLetterNavEnabled());
        REQUIRE(browse->scriptByName("toggleSingleLetterNav") != nullptr);

        // The switch is enforced in `alternativeScript`, which is part of the
        // lookup's answer and therefore part of what the grab set is computed
        // from -- so what has to be asserted is the lookup, not a flag.
        const script::Script* heading = browse->scriptByName("nextHeading");
        REQUIRE(heading != nullptr);
        input::KeyboardGesture::Init init;
        init.event.keysym = 'h';
        init.event.unichar = 'h';
        init.baseKeysym = 'h';
        const input::KeyboardGesture letter{init};
        REQUIRE(letter.isCharacter());

        CHECK(browse->alternativeScript(letter, heading) == heading);
        const script::Script* toggle = browse->scriptByName("toggleSingleLetterNav");
        REQUIRE(toggle != nullptr);
        input::KeyboardGesture::Init toggleInit;
        toggleInit.event.keysym = ' ';
        toggleInit.baseKeysym = ' ';
        input::KeyboardGesture toggleGesture{toggleInit};
        toggle->function(toggleGesture);
        CHECK_FALSE(browse->singleLetterNavEnabled());
        // Nothing is found for the letter any more, so the grab is dropped
        // and `h` reaches the application -- the only thing that can happen
        // on Wayland, where a grabbed key cannot be handed on (N18).
        CHECK(browse->alternativeScript(letter, heading) == nullptr);
        // The SCRIPT still exists; it is the lookup that stops reaching it.
        CHECK(browse->scriptByName("nextHeading") != nullptr);
        // A command key is never affected, in either state.
        input::KeyboardGesture::Init command;
        command.event.keysym = 0xff54;  // XK_Down
        command.baseKeysym = 0xff54;
        const input::KeyboardGesture downArrow{command};
        REQUIRE_FALSE(downArrow.isCharacter());
        CHECK(browse->alternativeScript(downArrow, heading) == heading);

        toggle->function(toggleGesture);
        CHECK(browse->singleLetterNavEnabled());
        CHECK(browse->alternativeScript(letter, heading) == heading);
    }

    SECTION("passThrough is what step 6 of the lookup reads") {
        // The filter is the LOOKUP's, not each script's: that is what phase 3
        // built step 6 for, and a script that tested the mode itself would be
        // 74 chances to forget.
        CHECK_FALSE(interceptor->passThrough());
        interceptor->setPassThrough(true, /*announce=*/false);
        CHECK(interceptor->passThrough());
        // The scripts still EXIST -- the lookup is what stops reaching them,
        // which is why this is the honest thing to assert here.
        CHECK(interceptor->scriptByName("nextHeading") != nullptr);
    }
}

// ---------------------------------------------------------------------------
// T6.10 -- table navigation: control+alt+arrows move by cell, and the headers
//          are announced once.
// ---------------------------------------------------------------------------

TEST_CASE("T6.10 table navigation moves by cell and announces headers once",
          "[browsemode][T6.10]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
    nav::TreeInterceptorPtr interceptor =
        nav::BrowseMode::chooseForDocument(root, fixture.nav());
    REQUIRE(interceptor != nullptr);
    auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
    REQUIRE(browse != nullptr);

    SECTION("moving onto a cell announces its coordinates and header") {
        a11y::resetPresentationMemory();
        const a11y::ObjectPtr cell = fixture.registry.objectFor(tree.cellA);
        fixture.clearSpeech();
        REQUIRE(browse->moveTo(cell, a11y::OutputReason::Caret));
        fixture.drainSpeech();
        const std::string said = fixture.spokenJoined();
        INFO("said: " << said);
        CHECK(said.find("alus") != std::string::npos);
    }

    SECTION("a cell's control field says row, column and header") {
        // The coordinate reporting is a DIFF against the last cell spoken
        // (a11y::PresentationMemory), which is process-global because NVDA's
        // is: moving along a row must not repeat the row number. A test that
        // asserts on the first cell has to start from a clean memory.
        a11y::resetPresentationMemory();
        const text::FormatReportSettings settings;
        const nav::ControlFieldSynth synth{settings};
        const a11y::ObjectPtr cell = fixture.registry.objectFor(tree.cellA);
        const std::optional<text::ControlField> field = synth.forObject(*cell);
        REQUIRE(field.has_value());

        // THE ANCESTOR CHAIN IS NOT DECORATION. A cell's presentation
        // category is PRESCAT_CELL only when a non-layout table is ABOVE it
        // in the stack (textInfos/__init__.py:146); with an empty chain the
        // same cell is PRESCAT_LAYOUT and says nothing at all. That is the
        // rule that makes a layout table silent, and it is why the stream of
        // T6.2 has to be nested correctly for any of this to be heard.
        const a11y::ObjectPtr table = fixture.registry.objectFor(tree.table);
        const std::optional<text::ControlField> tableField = synth.forObject(*table);
        REQUIRE(tableField.has_value());
        const std::vector<text::ControlField> ancestors{*tableField};
        CHECK(field->getPresentationCategory(ancestors, settings) ==
              text::PresentationCategory::Cell);

        const a11y::Settings objectSettings;
        const speech::SpeechSequence said = a11y::getControlFieldSpeech(
            *field, ancestors, a11y::ControlFieldType::StartAddedToStack, settings,
            a11y::OutputReason::Caret, /*extraDetail=*/false, objectSettings);
        const std::string text = joined(said);
        INFO("cell said: " << text);
        CHECK(text.find("Name") != std::string::npos);
        CHECK(text.find("row 2") != std::string::npos);
        CHECK(text.find("column 1") != std::string::npos);
    }

    SECTION("entering a table announces its shape") {
        a11y::resetPresentationMemory();
        const text::FormatReportSettings settings;
        const nav::ControlFieldSynth synth{settings};
        const a11y::ObjectPtr table = fixture.registry.objectFor(tree.table);
        const std::optional<text::ControlField> field = synth.forObject(*table);
        REQUIRE(field.has_value());

        const a11y::Settings objectSettings;
        const speech::SpeechSequence said = a11y::getControlFieldSpeech(
            *field, {}, a11y::ControlFieldType::StartAddedToStack, settings,
            a11y::OutputReason::Caret, false, objectSettings);
        const std::string text = joined(said);
        INFO("table said: " << text);
        CHECK(text.find("table") != std::string::npos);
    }

    SECTION("leaving a list says 'out of list', and only on the way out") {
        const text::FormatReportSettings settings;
        const nav::ControlFieldSynth synth{settings};
        const a11y::ObjectPtr list = fixture.registry.objectFor(tree.list);
        const std::optional<text::ControlField> field = synth.forObject(*list);
        REQUIRE(field.has_value());
        const a11y::Settings objectSettings;

        const std::string leaving = joined(a11y::getControlFieldSpeech(
            *field, {}, a11y::ControlFieldType::EndRemovedFromStack, settings,
            a11y::OutputReason::Caret, false, objectSettings));
        CHECK(leaving.find("out of") != std::string::npos);

        // On a FOCUS change nothing is said about leaving: #2591, hearing
        // "out of list" while tabbing makes navigation slower.
        const std::string whileTabbing = joined(a11y::getControlFieldSpeech(
            *field, {}, a11y::ControlFieldType::StartInStack, settings,
            a11y::OutputReason::Focus, false, objectSettings));
        CHECK(whileTabbing.find("out of") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// The document range itself: the crossing of leaves is what makes a document
// a document, and nothing above works if it is wrong.
// ---------------------------------------------------------------------------

TEST_CASE("the document range crosses leaves", "[browsemode]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const text::FormatReportSettings settings;
    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);

    SECTION("the root's own text is placeholders, which is why this is needed") {
        // N47, asserted: the fixture is honest about what a browser answers.
        // FOUR, one per child: AT-SPI counts characters, and each U+FFFC is
        // one. This said 12 -- the placeholders' UTF-8 bytes -- while the mock
        // counted bytes too, which is the agreement N113 was hiding in.
        CHECK(root->characterCount() == 4);
        CHECK(root->textRange(0, -1).find("Interfaces") == std::string::npos);
    }

    SECTION("down arrow at the end of a paragraph reaches the next one") {
        text::TextRangePtr caret =
            nav::DocumentTextRange::make(root, settings, text::Position::First);
        REQUIRE(caret != nullptr);
        text::TextRangePtr line = caret->copy();
        REQUIRE(line->expand(text::TextUnit::Line));
        CHECK(line->text() == "Interfaces");

        REQUIRE(caret->move(text::TextUnit::Line, 1) == 1);
        line = caret->copy();
        REQUIRE(line->expand(text::TextUnit::Line));
        CHECK(line->text() == "The first paragraph.");

        REQUIRE(caret->move(text::TextUnit::Line, 1) == 1);
        line = caret->copy();
        REQUIRE(line->expand(text::TextUnit::Line));
        CHECK(line->text() == "One");
    }

    SECTION("and back up again") {
        text::TextRangePtr caret =
            nav::DocumentTextRange::make(root, settings, text::Position::First);
        REQUIRE(caret != nullptr);
        REQUIRE(caret->move(text::TextUnit::Line, 2) == 2);
        REQUIRE(caret->move(text::TextUnit::Line, -1) == -1);
        text::TextRangePtr line = caret->copy();
        REQUIRE(line->expand(text::TextUnit::Line));
        CHECK(line->text() == "The first paragraph.");
    }

    SECTION("a bookmark survives and comes back to the same place") {
        text::TextRangePtr caret =
            nav::DocumentTextRange::make(root, settings, text::Position::First);
        REQUIRE(caret != nullptr);
        REQUIRE(caret->move(text::TextUnit::Line, 2) == 2);
        const text::Bookmark mark = caret->bookmark();
        REQUIRE(mark.valid());

        text::TextRangePtr again =
            nav::DocumentTextRange::make(root, settings, text::Position::First);
        REQUIRE(again != nullptr);
        REQUIRE(again->moveToBookmark(mark));
        text::TextRangePtr line = again->copy();
        REQUIRE(line->expand(text::TextUnit::Line));
        CHECK(line->text() == "One");
    }

    SECTION("find crosses leaves too") {
        text::TextRangePtr caret =
            nav::DocumentTextRange::make(root, settings, text::Position::First);
        REQUIRE(caret != nullptr);
        REQUIRE(caret->find("Three"));
        text::TextRangePtr line = caret->copy();
        REQUIRE(line->expand(text::TextUnit::Line));
        CHECK(line->text() == "Three");
    }

    SECTION("the line count is the document's, not one leaf's") {
        text::TextRangePtr caret =
            nav::DocumentTextRange::make(root, settings, text::Position::First);
        REQUIRE(caret != nullptr);
        const std::optional<int> count = caret->unitCount(text::TextUnit::Line);
        REQUIRE(count.has_value());
        // heading + paragraph + 3 items + 4 cells = 9 leaves, one line each.
        CHECK(*count == 9);
    }
}

TEST_CASE("the virtual caret's selection anchor does not jump", "[browsemode]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const text::FormatReportSettings settings;
    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);

    nav::CursorManager cursor{
        [&](text::Position position) {
            return nav::DocumentTextRange::make(root, settings, position);
        },
        [&](const text::Bookmark& mark) -> text::TextRangePtr {
            text::TextRangePtr range =
                nav::DocumentTextRange::make(root, settings, text::Position::First);
            if (range == nullptr || !range->moveToBookmark(mark)) {
                return nullptr;
            }
            return range;
        }};

    text::TextRangePtr start =
        nav::DocumentTextRange::make(root, settings, text::Position::First);
    REQUIRE(start != nullptr);
    REQUIRE(cursor.setCaret(*start));
    CHECK_FALSE(cursor.hasSelection());

    // THE RULE: the anchor is planted ONCE, on the first shifted press.
    // Planting it on every press is the "selection that jumps" §8.2 warns
    // about, and the symptom is exactly this: two shift+downs selecting only
    // the second line.
    const nav::CursorMove first = cursor.move(text::TextUnit::Line, 1, /*select=*/true);
    CHECK(first.moved);
    CHECK(cursor.hasSelection());
    const nav::CursorMove second = cursor.move(text::TextUnit::Line, 1, /*select=*/true);
    CHECK(second.moved);

    text::TextRangePtr selection = cursor.selection();
    REQUIRE(selection != nullptr);
    const std::string selected = selection->text();
    INFO("selected: " << selected);
    CHECK(selected.find("Interfaces") != std::string::npos);
    CHECK(selected.find("The first paragraph.") != std::string::npos);

    SECTION("an unshifted press clears the selection") {
        cursor.move(text::TextUnit::Line, 1, /*select=*/false);
        CHECK_FALSE(cursor.hasSelection());
    }

    SECTION("select all takes the whole document") {
        const nav::CursorMove all = cursor.selectAll();
        REQUIRE(all.toRead != nullptr);
        CHECK(all.toRead->text().find("Interfaces") != std::string::npos);
        CHECK(all.toRead->text().find("Three") != std::string::npos);
    }
}

TEST_CASE("the tree interceptor registry attaches and collects", "[browsemode]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    nav::TreeInterceptorRegistry* interceptors = fixture.nav().interceptors();
    REQUIRE(interceptors != nullptr);

    const a11y::ObjectPtr heading = fixture.registry.objectFor(tree.heading);
    nav::TreeInterceptor* interceptor = interceptors->update(heading);
    REQUIRE(interceptor != nullptr);
    CHECK(interceptor->rootObject()->ref() == tree.document);
    CHECK(interceptors->size() == 1);

    SECTION("the object the focus is on points at it, which is what phase 5 asks") {
        CHECK(heading->treeInterceptor() == interceptor);
        CHECK(heading->treeInterceptorReady());
    }

    SECTION("a second focus inside the same document reuses it") {
        const a11y::ObjectPtr item = fixture.registry.objectFor(tree.item2);
        CHECK(interceptors->update(item) == interceptor);
        CHECK(interceptors->size() == 1);
    }

    SECTION("a BUSY document is not ready, and document review falls back") {
        fixture.backend.addState(tree.document, PlatformState::Busy);
        fixture.pumpOnce();
        CHECK_FALSE(interceptor->isReady());
    }

    SECTION("a dead document's interceptor is swept, and the object forgets it") {
        fixture.backend.kill(tree.document);
        fixture.pumpOnce();
        interceptors->sweep();
        CHECK(interceptors->size() == 0);
    }

    SECTION("document review has a position now, which it did not in phase 5") {
        text::TextRangePtr position = fixture.review().documentPosition(heading);
        REQUIRE(position != nullptr);
        CHECK(position->rangeKind() == "root-proxy");
        text::TextRangePtr line = position->copy();
        REQUIRE(line->expand(text::TextUnit::Line));
        CHECK_FALSE(line->text().empty());
    }
}

// ---------------------------------------------------------------------------
// What the second live pass heard in Chrome, written down as tests. Each one
// builds the page the way CHROME answers it -- not the way an earlier test
// assumed a browser would -- because every one of these defects was hidden by
// a mock that was more convenient than the platform (phase-10.md N112-N116).
// ---------------------------------------------------------------------------

namespace {

// <nav><a>Početna</a> <a>Vesti</a> <a>Kontakt</a></nav>, as Chrome exposes it:
// the nav's own text is "U+FFFC U+FFFC U+FFFC" -- a placeholder per link with
// real spaces between them, which makes it a LEAF (DocumentLeaves) -- and the
// links are reached only through its hypertext.
a11y::Ref addNavBar(MockBackend& backend, WebDocumentTree& tree,
                    const std::vector<std::string>& labels) {
    const a11y::Ref nav = backend.add(tree.document, PlatformRole::Landmark, "Glavni meni");
    backend.node(nav).attributes["xml-roles"] = "navigation";
    MockNode& navNode = backend.node(nav);
    navNode.interfaces |= a11y::Interface::Text;
    backend.addState(nav, PlatformState::Showing);
    std::string text;
    int offset = 0;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (index > 0) {
            text += " ";
            ++offset;
        }
        const a11y::Ref link = addTextLeaf(backend, nav, PlatformRole::Link, labels[index],
                                           labels[index]);
        backend.node(link).actionNames.push_back("jump");
        backend.addState(link, PlatformState::Focusable);
        text += kPlaceholder;
        backend.node(nav).links.push_back(a11y::LinkInfo{offset, offset + 1, link, "#"});
        ++offset;
    }
    backend.node(nav).text = text;
    makeContainer(backend, tree.document,
                  static_cast<int>(backend.node(tree.document).children.size()));
    return nav;
}

}  // namespace

TEST_CASE("N114: the links inside a line are read by their text, never as U+FFFC",
          "[browsemode][N114]") {
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::Ref navRef = addNavBar(fixture.backend, tree, {"Početna", "Vesti", "Kontakt"});
    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
    const a11y::ObjectPtr nav = fixture.registry.objectFor(navRef);
    REQUIRE(nav != nullptr);
    REQUIRE(nav::DocumentLeaves::isLeaf(*nav));

    const text::FormatReportSettings settings;
    text::TextRangePtr line = nav::DocumentTextRange::makeAt(
        root, settings, nav::DocumentPosition{nav, 0}, nav::DocumentPosition{nav, 0});
    REQUIRE(line != nullptr);
    REQUIRE(line->expand(text::TextUnit::Line));

    SECTION("the stream: one placeholder per link, each inside the link's own field") {
        std::vector<std::string> contents;
        int placeholdersSeen = 0;
        for (const text::TextOrField& item : line->getTextWithFields(settings)) {
            if (item.isText() && *item.text == kPlaceholder) {
                ++placeholdersSeen;
            }
            if (item.isCommand() &&
                item.command->kind() == text::FieldCommand::Kind::ControlStart &&
                item.command->control()->role == a11y::Role::Link) {
                contents.emplace_back(item.command->control()->get("content"));
            }
        }
        // The placeholder STAYS -- braille's cell-to-offset map needs one
        // character per offset -- and the field says what it stands for.
        CHECK(placeholdersSeen == 3);
        CHECK(contents == std::vector<std::string>{"Početna", "Vesti", "Kontakt"});
    }

    SECTION("what is SPOKEN: the link texts, and no placeholder character") {
        nav::TreeInterceptorPtr interceptor = nav::BrowseMode::chooseForDocument(root, fixture.nav());
        auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
        REQUIRE(browse != nullptr);
        fixture.clearSpeech();
        REQUIRE(browse->moveTo(nav, a11y::OutputReason::Caret));
        fixture.drainSpeech();
        const std::string said = fixture.spokenJoined();
        INFO("said: " << said);
        CHECK(said.find("Početna") != std::string::npos);
        CHECK(said.find("Vesti") != std::string::npos);
        CHECK(said.find("Kontakt") != std::string::npos);
        CHECK(said.find(kPlaceholder) == std::string::npos);
    }
}

TEST_CASE("N115: a list the browser does NOT mark read-only is still a container",
          "[browsemode][N115]") {
    // The fixture's list carries READONLY, which is what NVDA's virtual
    // buffers would give it and what Chrome does not. Without READONLY a list
    // is a list BOX (a single-line control) and "list" was spoken on every
    // line inside it.
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::Ref plain = fixture.backend.add(tree.document, PlatformRole::List);
    addTextLeaf(fixture.backend, plain, PlatformRole::ListItem, "Prva");
    addTextLeaf(fixture.backend, plain, PlatformRole::ListItem, "Druga");
    makeContainer(fixture.backend, plain, 2);
    makeContainer(fixture.backend, tree.document,
                  static_cast<int>(fixture.backend.node(tree.document).children.size()));

    const text::FormatReportSettings settings;
    const nav::ControlFieldSynth synth{settings};
    const std::optional<text::ControlField> field =
        synth.forObject(*fixture.registry.objectFor(plain));
    REQUIRE(field.has_value());
    CHECK(field->states.has(a11y::State::ReadOnly));
    CHECK(field->getPresentationCategory({}, settings) == text::PresentationCategory::Container);

    SECTION("a list box in a form -- focusable -- stays a control") {
        const a11y::Ref box = fixture.backend.add(tree.document, PlatformRole::List, "Izbor");
        fixture.backend.addState(box, PlatformState::Focusable);
        const std::optional<text::ControlField> boxField =
            synth.forObject(*fixture.registry.objectFor(box));
        REQUIRE(boxField.has_value());
        CHECK_FALSE(boxField->states.has(a11y::State::ReadOnly));
        CHECK(boxField->getPresentationCategory({}, settings) ==
              text::PresentationCategory::SingleLine);
    }
}

TEST_CASE("N116: a heading's text is not said twice when its name is its content",
          "[browsemode][N116]") {
    // Chrome names a heading after its text and sets `explicit-name` only on
    // names an author wrote.
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    fixture.backend.node(tree.heading).name = "Interfaces";  // = its text, not explicit
    const text::FormatReportSettings settings;
    const nav::ControlFieldSynth synth{settings};

    const std::optional<text::ControlField> field =
        synth.forObject(*fixture.registry.objectFor(tree.heading));
    REQUIRE(field.has_value());
    CHECK(field->getBool("labelledByContent").value_or(false));

    SECTION("an explicit name the author wrote is kept") {
        fixture.backend.node(tree.heading).attributes["explicit-name"] = "true";
        const nav::ControlFieldSynth fresh{settings};
        // The cache lives for one pump cycle; end it, so the attribute is
        // read again.
        core::invalidateAllCaches();
        const std::optional<text::ControlField> named =
            fresh.forObject(*fixture.registry.objectFor(tree.heading));
        REQUIRE(named.has_value());
        CHECK_FALSE(named->getBool("labelledByContent").value_or(false));
    }
}

TEST_CASE("N117: the arrow keys go into a link's text, as NVDA does", "[browsemode][N117]") {
    // "Pročitaj ovde više." with "ovde" a link: Chrome gives the paragraph the
    // text "Pročitaj U+FFFC više." and the link its own text. The link used
    // to be ONE press of the right arrow; NVDA spells through it.
    NavFixture fixture;
    WebDocumentTree tree{fixture.backend};
    const a11y::Ref paragraph =
        fixture.backend.add(tree.document, PlatformRole::Paragraph);
    {
        MockNode& node = fixture.backend.node(paragraph);
        node.interfaces |= a11y::Interface::Text;
        node.text = std::string("Pročitaj ") + kPlaceholder + " više.";
    }
    fixture.backend.addState(paragraph, PlatformState::Showing);
    const a11y::Ref link =
        addTextLeaf(fixture.backend, paragraph, PlatformRole::Link, "ovde", "ovde");
    fixture.backend.node(link).actionNames.push_back("jump");
    fixture.backend.node(paragraph).links.push_back(a11y::LinkInfo{9, 10, link, "#"});
    makeContainer(fixture.backend, tree.document,
                  static_cast<int>(fixture.backend.node(tree.document).children.size()));

    const a11y::ObjectPtr root = fixture.registry.objectFor(tree.document);
    const a11y::ObjectPtr leaf = fixture.registry.objectFor(paragraph);
    REQUIRE(nav::DocumentLeaves::isLeaf(*leaf));
    const text::FormatReportSettings settings;

    const auto walk = [&](text::TextRangePtr range, int direction, int steps) {
        std::vector<std::string> out;
        for (int step = 0; step < steps; ++step) {
            text::TextRangePtr character = range->copy();
            REQUIRE(character->expand(text::TextUnit::Character));
            out.push_back(character->text());
            if (range->move(text::TextUnit::Character, direction) == 0) {
                break;
            }
        }
        return out;
    };

    SECTION("forwards: through the paragraph, into the link and out again") {
        text::TextRangePtr range = nav::DocumentTextRange::makeAt(
            root, settings, nav::DocumentPosition{leaf, 0}, nav::DocumentPosition{leaf, 0});
        REQUIRE(range != nullptr);
        const std::vector<std::string> letters = walk(std::move(range), 1, 19);
        const std::vector<std::string> expected{"P", "r", "o", "č", "i", "t", "a", "j", " ",
                                                "o", "v", "d", "e", " ", "v", "i", "š", "e",
                                                "."};
        CHECK(letters == expected);
    }

    SECTION("backwards: into the link from its end, and out at its start") {
        text::TextRangePtr range = nav::DocumentTextRange::makeAt(
            root, settings, nav::DocumentPosition{leaf, 10}, nav::DocumentPosition{leaf, 10});
        REQUIRE(range != nullptr);
        // From the space after the link, back: e, d, v, o, then the space
        // before it, then "j".
        const std::vector<std::string> letters = walk(std::move(range), -1, 7);
        const std::vector<std::string> expected{" ", "e", "d", "v", "o", " ", "j"};
        CHECK(letters == expected);
    }

    SECTION("inside the link, the LINE is still the paragraph's") {
        text::TextRangePtr range = nav::DocumentTextRange::makeAt(
            root, settings, nav::DocumentPosition{fixture.registry.objectFor(link), 2},
            nav::DocumentPosition{fixture.registry.objectFor(link), 2});
        REQUIRE(range != nullptr);
        REQUIRE(range->expand(text::TextUnit::Line));
        // The paragraph's whole line, with the link's placeholder in it (its
        // text arrives through the field, N114).
        CHECK(range->text() == std::string("Pročitaj ") + kPlaceholder + " više.");
    }

    SECTION("a position inside the link survives a bookmark round trip") {
        text::TextRangePtr range = nav::DocumentTextRange::makeAt(
            root, settings, nav::DocumentPosition{fixture.registry.objectFor(link), 2},
            nav::DocumentPosition{fixture.registry.objectFor(link), 3});
        REQUIRE(range != nullptr);
        const text::Bookmark mark = range->bookmark();
        text::TextRangePtr restored =
            nav::DocumentTextRange::make(root, settings, text::Position::First);
        REQUIRE(restored != nullptr);
        REQUIRE(restored->moveToBookmark(mark));
        CHECK(restored->text() == "d");
    }
}

TEST_CASE("N129 a focus move does not ask every ancestor whether it is a document",
          "[browsemode][cost]") {
    // Measured live (N129): the tree interceptor walked the whole ancestry of
    // every focus, asking each ancestor its parent, its role, its states and
    // its attributes, on every key. A row deep in a Nautilus window cost about
    // forty calls for that alone. The platform role is now remembered per
    // object and the focus ancestry Api already holds is walked instead.
    NavFixture fixture;
    const Ref application = fixture.backend.addApplication("Files", "gtk");
    const Ref window = fixture.backend.add(application, PlatformRole::Frame, "Home");
    fixture.backend.addState(window, PlatformState::Active);
    Ref container = window;
    for (int depth = 0; depth < 12; ++depth) {
        container = fixture.backend.add(container, PlatformRole::Panel, "");
    }
    const Ref list = fixture.backend.add(container, PlatformRole::List, "Files");
    fixture.backend.addInterface(list, Interface::Selection);
    std::vector<Ref> rows;
    for (int i = 0; i < 4; ++i) {
        const Ref row = fixture.backend.add(list, PlatformRole::ListItem,
                                            "file" + std::to_string(i));
        fixture.backend.addState(row, PlatformState::Focusable);
        rows.push_back(row);
    }
    fixture.backend.addState(rows[0], PlatformState::Focused);
    fixture.backend.emit("object:state-changed:focused", rows[0], 1);
    fixture.pumpOnce();

    for (std::size_t step = 1; step < rows.size(); ++step) {
        fixture.backend.resetCalls();
        fixture.backend.removeState(rows[step - 1], PlatformState::Focused);
        fixture.backend.addState(rows[step], PlatformState::Focused);
        fixture.backend.emit("object:state-changed:focused", rows[step], 1);
        fixture.pumpOnce();
        INFO("step " << step << ": " << fixture.backend.calls() << " calls");
        REQUIRE(fixture.registry.api().focus() != nullptr);
        CHECK(fixture.registry.api().focus()->ref() == rows[step]);
        CHECK(fixture.backend.callsFor("role") <= 1);
        CHECK(fixture.backend.callsFor("parent") <= 2);
        CHECK(fixture.backend.calls() <= 25);
    }

    SECTION("a document above the focus is still found through the chain") {
        const Ref document = fixture.backend.add(container, PlatformRole::DocumentWeb, "Page");
        fixture.backend.addState(document, PlatformState::ReadOnly);
        const Ref link = fixture.backend.add(document, PlatformRole::Link, "Home page");
        fixture.backend.addState(link, PlatformState::Focusable);
        fixture.backend.addState(link, PlatformState::Focused);
        fixture.backend.emit("object:state-changed:focused", link, 1);
        fixture.pumpOnce();
        CHECK(fixture.nav().interceptors()->current() != nullptr);
    }
}

TEST_CASE("N131 empty layout containers are not lines, and an icon button says its name",
          "[browsemode]") {
    // Log 19:35: on YouTube the down arrow said "blank" for every empty <div>
    // between the controls, and the player's buttons said "button, graphic":
    // the only leaf inside each was its unnamed SVG icon, and the button's
    // name ("Pause (k)") was never said.
    NavFixture fixture;
    const Ref application = fixture.backend.addApplication("Browser", "Chromium");
    const Ref window = fixture.backend.add(application, PlatformRole::Frame, "Video");
    fixture.backend.addState(window, PlatformState::Active);
    const Ref document = fixture.backend.add(window, PlatformRole::DocumentWeb, "Video");
    addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "Before the player");
    const Ref player = fixture.backend.add(document, PlatformRole::Section, "");
    // Empty layout divs: AT-SPI sections with a Text interface and no text.
    for (int i = 0; i < 3; ++i) {
        addTextLeaf(fixture.backend, player, PlatformRole::Section, "");
    }
    const Ref pause = fixture.backend.add(player, PlatformRole::Button, "Pause (k)");
    fixture.backend.add(pause, PlatformRole::Image, "");  // the SVG icon
    makeContainer(fixture.backend, pause, 1);
    addTextLeaf(fixture.backend, player, PlatformRole::Section, "");
    addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "After the player");
    makeContainer(fixture.backend, player, 5);
    makeContainer(fixture.backend, document, 3);

    const a11y::ObjectPtr root = fixture.registry.objectFor(document);
    const text::FormatReportSettings settings;
    text::TextRangePtr caret = nav::DocumentTextRange::make(root, settings, text::Position::First);
    REQUIRE(caret != nullptr);

    // Down once: straight to the button, no blank line in between.
    REQUIRE(caret->move(text::TextUnit::Line, 1) == 1);
    text::TextRangePtr line = caret->copy();
    REQUIRE(line->expand(text::TextUnit::Line));
    std::string content;
    bool sawImage = false;
    for (const text::TextOrField& item : line->getTextWithFields(settings)) {
        if (item.isCommand() && item.command->kind() == text::FieldCommand::Kind::ControlStart) {
            const text::ControlField* field = item.command->control();
            if (field->role == a11y::Role::Button) {
                content = field->get("content");
            }
            if (field->role == a11y::Role::Graphic) {
                sawImage = true;
            }
        }
    }
    CHECK(content == "Pause (k)");
    CHECK_FALSE(sawImage);

    // Down again: past the trailing empty div to the next paragraph.
    REQUIRE(caret->move(text::TextUnit::Line, 1) == 1);
    line = caret->copy();
    REQUIRE(line->expand(text::TextUnit::Line));
    CHECK(line->text() == "After the player");

    // And back up: the button, not a blank.
    REQUIRE(caret->move(text::TextUnit::Line, -1) == -1);
    line = caret->copy();
    REQUIRE(line->expand(text::TextUnit::Line));
    CHECK(line->text().find("Before") == std::string::npos);

    // What the user hears, by arrow and by `b`: the name once, then "button".
    nav::TreeInterceptorPtr interceptor = nav::BrowseMode::chooseForDocument(root, fixture.nav());
    auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
    REQUIRE(browse != nullptr);
    const a11y::ObjectPtr button = fixture.registry.objectFor(pause);
    for (const a11y::OutputReason reason : {a11y::OutputReason::Caret, a11y::OutputReason::QuickNav}) {
        fixture.clearSpeech();
        REQUIRE(browse->moveTo(button, reason));
        fixture.drainSpeech();
        const std::string spoken = fixture.spokenJoined();
        INFO("reason " << static_cast<int>(reason) << ": " << spoken);
        const std::size_t first = spoken.find("Pause");
        REQUIRE(first != std::string::npos);
        CHECK(spoken.find("Pause", first + 1) == std::string::npos);
        CHECK(spoken.find("button") != std::string::npos);
        CHECK(spoken.find("graphic") == std::string::npos);
    }
}

TEST_CASE("N132 a focused grid row is focus mode; the focus around the caret leaves it alone",
          "[browsemode]") {
    // Gmail: the message list is an ARIA grid whose rows take the focus. The
    // arrows had to move from message to message and x had to tick one, but
    // browse mode kept both. YouTube: "skip ad" removes itself when pressed
    // and the page puts the focus back on itself -- and the reader landed at
    // the top of the page.
    NavFixture fixture;
    const Ref application = fixture.backend.addApplication("Browser", "Chromium");
    const Ref window = fixture.backend.add(application, PlatformRole::Frame, "Mail");
    fixture.backend.addState(window, PlatformState::Active);
    const Ref document = fixture.backend.add(window, PlatformRole::DocumentWeb, "Inbox");
    addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "First line");
    addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "Second line");
    const Ref skip =
        addTextLeaf(fixture.backend, document, PlatformRole::Button, "Skip ad", "Skip ad");
    addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "Under the ad");
    const Ref grid = fixture.backend.add(document, PlatformRole::Table, "Messages");
    const Ref row = fixture.backend.add(grid, PlatformRole::TableRow, "");
    fixture.backend.addState(row, PlatformState::Focusable);
    const Ref cell = addTextLeaf(fixture.backend, row, PlatformRole::TableCell, "Hello from a friend");
    makeContainer(fixture.backend, row, 1);
    makeContainer(fixture.backend, grid, 1);
    const Ref plainTable = fixture.backend.add(document, PlatformRole::Table, "");
    const Ref plainRow = fixture.backend.add(plainTable, PlatformRole::TableRow, "");
    const Ref plainCell = addTextLeaf(fixture.backend, plainRow, PlatformRole::TableCell, "Price");
    makeContainer(fixture.backend, plainRow, 1);
    makeContainer(fixture.backend, plainTable, 1);
    const Ref listBox = fixture.backend.add(document, PlatformRole::List, "Conversations");
    fixture.backend.addState(listBox, PlatformState::Focusable);
    const Ref item = addTextLeaf(fixture.backend, listBox, PlatformRole::ListItem, "A thread");
    makeContainer(fixture.backend, listBox, 1);
    makeContainer(fixture.backend, document, 7);
    (void)cell;

    SECTION("a row or cell that takes the focus is a widget; a plain table's is content") {
        CHECK(nav::BrowseMode::shouldPassThroughFor(*fixture.registry.objectFor(row)));
        CHECK_FALSE(nav::BrowseMode::shouldPassThroughFor(*fixture.registry.objectFor(plainRow)));
        CHECK_FALSE(nav::BrowseMode::shouldPassThroughFor(*fixture.registry.objectFor(plainCell)));
        // A list item only when it has the focus inside a focusable list.
        CHECK_FALSE(nav::BrowseMode::shouldPassThroughFor(*fixture.registry.objectFor(item)));
        fixture.backend.addState(item, PlatformState::Focused);
        fixture.pumpOnce();
        CHECK(nav::BrowseMode::shouldPassThroughFor(*fixture.registry.objectFor(item)));
    }

    const a11y::ObjectPtr root = fixture.registry.objectFor(document);
    nav::TreeInterceptorPtr interceptor = nav::BrowseMode::chooseForDocument(root, fixture.nav());
    auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
    REQUIRE(browse != nullptr);

    SECTION("arriving in the document on a focused row starts in focus mode") {
        fixture.backend.addState(row, PlatformState::Focused);
        fixture.pumpOnce();
        browse->onGainFocus(fixture.registry.objectFor(row));
        CHECK(browse->passThrough());
    }

    SECTION("moving onto the row from inside the document switches too") {
        browse->onInnerFocus(fixture.registry.objectFor(row));
        CHECK(browse->passThrough());
    }

    const auto caretLine = [&] {
        text::TextRangePtr caret = browse->makeTextRange(text::Position::Caret);
        REQUIRE(caret != nullptr);
        REQUIRE(caret->expand(text::TextUnit::Line));
        return caret->text();
    };

    SECTION("the focus on the document itself does not move the caret") {
        REQUIRE(browse->moveTo(fixture.registry.objectFor(skip), a11y::OutputReason::Caret));
        REQUIRE(caretLine().find("Skip ad") != std::string::npos);
        browse->onInnerFocus(root);
        CHECK(caretLine().find("Skip ad") != std::string::npos);
    }

    SECTION("a vanished leaf leaves the caret where it was, not at the top") {
        REQUIRE(browse->moveTo(fixture.registry.objectFor(skip), a11y::OutputReason::Caret));
        // The button removes itself.
        MockNode& parent = fixture.backend.node(document);
        parent.children.erase(std::find(parent.children.begin(), parent.children.end(), skip));
        fixture.backend.kill(skip);
        makeContainer(fixture.backend, document, 6);
        fixture.pumpOnce();
        browse->onInnerFocus(fixture.registry.objectFor(document));
        CHECK(caretLine() == "Under the ad");
    }
}

TEST_CASE("alus+f7: the elements list's choice is performed when the focus comes back",
          "[browsemode][elements]") {
    // The dialog is another process (alus-elements); what comes back is one
    // line, and the choice waits for the browser to have the focus again.
    SECTION("the protocol") {
        const std::vector<nav::ElementsDialogEntry> entries{
            {"link", 0, "Home\tpage"}, {"heading", 2, "News\nToday", true}};
        CHECK(nav::formatElementsDialogEntries(entries) ==
              "link\t0\t0\tHome page\nheading\t2\t1\tNews Today\n");
        const nav::ElementsDialogChoice move = nav::parseElementsDialogChoice("move\tlink\t3");
        CHECK(move.action == "move");
        CHECK(move.kind == "link");
        CHECK(move.index == 3);
        CHECK(nav::parseElementsDialogChoice("activate\theading\t0").action == "activate");
        CHECK(nav::parseElementsDialogChoice("").action.empty());
        CHECK(nav::parseElementsDialogChoice("delete\tlink\t1").action.empty());
        CHECK(nav::parseElementsDialogChoice("move\tlink\tx").action.empty());
    }

    NavFixture fixture;
    const Ref application = fixture.backend.addApplication("Browser", "Chromium");
    const Ref window = fixture.backend.add(application, PlatformRole::Frame, "Page");
    fixture.backend.addState(window, PlatformState::Active);
    const Ref document = fixture.backend.add(window, PlatformRole::DocumentWeb, "Page");
    addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "Top of the page");
    addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "Some text");
    const Ref link = addTextLeaf(fixture.backend, document, PlatformRole::Link, "Contact us");
    fixture.backend.node(link).actionNames = {"jump"};
    makeContainer(fixture.backend, document, 3);

    const a11y::ObjectPtr root = fixture.registry.objectFor(document);
    nav::TreeInterceptorPtr interceptor = nav::BrowseMode::chooseForDocument(root, fixture.nav());
    auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
    REQUIRE(browse != nullptr);
    browse->setElementObjectsForTest({{"link", {fixture.registry.objectFor(link)}}});
    const auto caretLine = [&] {
        text::TextRangePtr caret = browse->makeTextRange(text::Position::Caret);
        REQUIRE(caret != nullptr);
        REQUIRE(caret->expand(text::TextUnit::Line));
        return caret->text();
    };

    SECTION("Move to: nothing until the focus returns, then the caret is on the link") {
        browse->chooseElement(nav::parseElementsDialogChoice("move\tlink\t0"));
        CHECK(caretLine() == "Top of the page");
        fixture.clearSpeech();
        browse->onGainFocus(root);
        CHECK(caretLine() == "Contact us");
        fixture.drainSpeech();
        CHECK(fixture.spokenJoined().find("Contact us") != std::string::npos);
        CHECK(fixture.backend.performed.empty());
    }

    SECTION("Activate: the link is activated, and the caret is on it") {
        browse->chooseElement(nav::parseElementsDialogChoice("activate\tlink\t0"));
        browse->onInnerFocus(root);
        REQUIRE(fixture.backend.performed.size() == 1);
        CHECK(fixture.backend.performed.front().ref == link);
        CHECK(caretLine() == "Contact us");
    }

    SECTION("with no focus event, a moment later") {
        browse->chooseElement(nav::parseElementsDialogChoice("move\tlink\t0"));
        CHECK(fixture.pumpUntil([&] { return caretLine() == "Contact us"; },
                                std::chrono::milliseconds{2000}));
    }

    SECTION("an index the lists do not have does nothing") {
        browse->chooseElement(nav::parseElementsDialogChoice("move\tlink\t5"));
        browse->onGainFocus(root);
        CHECK(caretLine() == "Top of the page");
    }
}

TEST_CASE("N134 a link nested in a link is one entry, in the list and for k",
          "[browsemode][elements]") {
    // Log 22:49: YouTube's menu is <a role=link title="Home"> holding a
    // <tp-yt-paper-item role=link>, so alus+f7 listed "Home" twice, "Shorts"
    // twice, and so on.
    NavFixture fixture;
    const Ref application = fixture.backend.addApplication("Browser", "Chromium");
    const Ref window = fixture.backend.add(application, PlatformRole::Frame, "Video");
    fixture.backend.addState(window, PlatformState::Active);
    const Ref document = fixture.backend.add(window, PlatformRole::DocumentWeb, "Video");
    fixture.backend.addInterface(document, a11y::Interface::Collection);
    const Ref home = fixture.backend.add(document, PlatformRole::Link, "Home");
    const Ref homeItem = addTextLeaf(fixture.backend, home, PlatformRole::Link, "Home", "Home");
    makeContainer(fixture.backend, home, 1);
    const Ref shorts = fixture.backend.add(document, PlatformRole::Link, "Shorts");
    addTextLeaf(fixture.backend, shorts, PlatformRole::Link, "Shorts", "Shorts");
    makeContainer(fixture.backend, shorts, 1);
    // A list inside a list item stays two lists.
    const Ref outer = fixture.backend.add(document, PlatformRole::List, "");
    const Ref item = fixture.backend.add(outer, PlatformRole::ListItem, "");
    const Ref inner = fixture.backend.add(item, PlatformRole::List, "");
    addTextLeaf(fixture.backend, inner, PlatformRole::ListItem, "Nested");
    makeContainer(fixture.backend, inner, 1);
    makeContainer(fixture.backend, item, 1);
    makeContainer(fixture.backend, outer, 1);
    makeContainer(fixture.backend, document, 3);
    (void)homeItem;

    const a11y::ObjectPtr root = fixture.registry.objectFor(document);
    nav::TreeInterceptorPtr interceptor = nav::BrowseMode::chooseForDocument(root, fixture.nav());
    auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
    REQUIRE(browse != nullptr);

    const std::vector<nav::ElementListItem> links = browse->elementsList(nav::ElementType::Link);
    REQUIRE(links.size() == 2);
    CHECK(links[0].label == "Home");
    CHECK(links[1].label == "Shorts");
    CHECK(browse->elementsList(nav::ElementType::List).size() == 2);

    // k from Home goes to Shorts, not to the Home inside Home.
    REQUIRE(browse->moveTo(fixture.registry.objectFor(home), a11y::OutputReason::QuickNav));
    fixture.clearSpeech();
    REQUIRE(browse->moveToElement(nav::ElementType::Link, true));
    fixture.drainSpeech();
    CHECK(fixture.spokenJoined().find("Shorts") != std::string::npos);
    // And shift+k back lands on Home once, not on its outer half first.
    fixture.clearSpeech();
    REQUIRE(browse->moveToElement(nav::ElementType::Link, false));
    fixture.drainSpeech();
    CHECK(fixture.spokenJoined().find("Home") != std::string::npos);
    fixture.clearSpeech();
    CHECK_FALSE(browse->moveToElement(nav::ElementType::Link, false));
}

TEST_CASE("N135 the elements list opens on the element at the caret", "[browsemode][elements]") {
    // NVDA's dialog selects the element nearest the caret; ALUS's opened on
    // the first of the page.
    NavFixture fixture;
    const Ref application = fixture.backend.addApplication("Browser", "Chromium");
    const Ref window = fixture.backend.add(application, PlatformRole::Frame, "Page");
    fixture.backend.addState(window, PlatformState::Active);
    const Ref document = fixture.backend.add(window, PlatformRole::DocumentWeb, "Page");
    fixture.backend.addInterface(document, a11y::Interface::Collection);
    const Ref intro = addTextLeaf(fixture.backend, document, PlatformRole::Heading, "Intro");
    fixture.backend.node(intro).attributes["level"] = "1";
    const Ref start = addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "Start.");
    const Ref home = fixture.backend.add(document, PlatformRole::Link, "Home");
    const Ref homeItem = addTextLeaf(fixture.backend, home, PlatformRole::Link, "Home", "Home");
    makeContainer(fixture.backend, home, 1);
    const Ref between = addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "Between.");
    const Ref shorts = addTextLeaf(fixture.backend, document, PlatformRole::Link, "Shorts", "Shorts");
    const Ref news = addTextLeaf(fixture.backend, document, PlatformRole::Heading, "News");
    fixture.backend.node(news).attributes["level"] = "2";
    const Ref end = addTextLeaf(fixture.backend, document, PlatformRole::Paragraph, "End.");
    makeContainer(fixture.backend, document, 7);

    const a11y::ObjectPtr root = fixture.registry.objectFor(document);
    nav::TreeInterceptorPtr interceptor = nav::BrowseMode::chooseForDocument(root, fixture.nav());
    auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
    REQUIRE(browse != nullptr);

    const auto objects = [&](nav::ElementType type) {
        std::vector<a11y::ObjectPtr> out;
        for (nav::ElementListItem& item : browse->elementsList(type)) {
            out.push_back(std::move(item.object));
        }
        return out;
    };
    const std::vector<a11y::ObjectPtr> links = objects(nav::ElementType::Link);
    const std::vector<a11y::ObjectPtr> headings = objects(nav::ElementType::Heading);
    REQUIRE(links.size() == 2);
    REQUIRE(headings.size() == 2);
    const auto caretOn = [&](Ref ref) {
        REQUIRE(browse->moveTo(fixture.registry.objectFor(ref), a11y::OutputReason::QuickNav));
    };

    SECTION("before every link: the first") {
        caretOn(start);
        CHECK(browse->initialElementIndex(nav::ElementType::Link, links) == 0);
        // And in a heading: that heading.
        caretOn(intro);
        CHECK(browse->initialElementIndex(nav::ElementType::Heading, headings) == 0);
    }
    SECTION("on a link, or inside the link inside it: that link") {
        caretOn(shorts);
        CHECK(browse->initialElementIndex(nav::ElementType::Link, links) == 1);
        caretOn(homeItem);
        CHECK(browse->initialElementIndex(nav::ElementType::Link, links) == 0);
    }
    SECTION("between two: the one before the caret") {
        caretOn(between);
        CHECK(browse->initialElementIndex(nav::ElementType::Link, links) == 0);
        CHECK(browse->initialElementIndex(nav::ElementType::Heading, headings) == 0);
    }
    SECTION("after the last: the last") {
        caretOn(end);
        CHECK(browse->initialElementIndex(nav::ElementType::Link, links) == 1);
        CHECK(browse->initialElementIndex(nav::ElementType::Heading, headings) == 1);
    }
    SECTION("an empty list has none") {
        CHECK(browse->initialElementIndex(nav::ElementType::Link, {}) == -1);
    }
}
