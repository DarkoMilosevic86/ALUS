// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T4.1 — Object properties: name, role, states, value, description are right
//        for a GTK4 button.
// T4.2 — Cached<T> over AT-SPI: 30 property reads in one cycle = 1 batch of
//        platform calls.
#include "alus/a11y/object.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/core/cached.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace alus::testing;
using namespace alus::a11y;

TEST_CASE("T4.1 the property set of a GTK4 button", "[a11y][object]") {
    A11yFixture fixture;
    const ButtonTree tree{fixture.backend};
    const ObjectPtr button = fixture.registry.objectFor(tree.button);
    REQUIRE(button != nullptr);

    CHECK(button->name() == "Save");
    CHECK(button->role() == Role::Button);
    CHECK(std::string{roleLabel(button->role())} == "button");
    CHECK(button->description() == "Save the current document");
    CHECK(button->isAlive());
    CHECK(button->isFocusable());
    CHECK_FALSE(button->hasFocus());
    CHECK_FALSE(button->states().has(State::Unavailable));
    CHECK_FALSE(button->states().has(State::Invisible));
    CHECK_FALSE(button->states().has(State::Offscreen));
    CHECK(button->states().has(State::Focusable));
    // A button has no Value interface, so it has no value -- and saying one
    // would be inventing it.
    CHECK(button->value().empty());
    // ATK's "mnemonic;path;accelerator": the accelerator is what works from
    // here, so it is what the user hears.
    CHECK(button->keyboardShortcut() == "<Control>s");
    CHECK(button->location() == Rect{40, 60, 90, 32});
    CHECK(button->actionCount() == 1);
    CHECK(button->actionName(0) == "click");
    CHECK(button->appInfo().toolkitName == "gtk");
    CHECK(button->appInfo().name == "Text Editor");
    CHECK(button->processId() == 4242);
    CHECK(button->presentationType() == PresentationType::Content);
}

TEST_CASE("the tree: parent, children, siblings, index", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref application = backend.addApplication("Files");
    const Ref window = backend.add(application, PlatformRole::Frame, "Home");
    const Ref list = backend.add(window, PlatformRole::List, "Files");
    const Ref first = backend.add(list, PlatformRole::ListItem, "Documents");
    const Ref second = backend.add(list, PlatformRole::ListItem, "Downloads");
    const Ref third = backend.add(list, PlatformRole::ListItem, "Pictures");

    const ObjectPtr middle = fixture.registry.objectFor(second);
    REQUIRE(middle != nullptr);
    CHECK(middle->indexInParent() == 1);
    CHECK(middle->parent()->id() == list);
    CHECK(middle->container()->id() == list);
    CHECK(middle->next()->name() == "Pictures");
    CHECK(middle->previous()->name() == "Documents");
    CHECK(middle->childCount() == 0);
    CHECK(middle->firstChild() == nullptr);

    const ObjectPtr container = fixture.registry.objectFor(list);
    CHECK(container->childCount() == 3);
    CHECK(container->children().size() == 3);
    CHECK(container->firstChild()->id() == first);
    CHECK(container->lastChild()->id() == third);
    CHECK(container->previous() == nullptr);

    // "2 of 3": the position information a list item is useless without.
    CHECK(middle->positionInfo().indexInGroup == 2);
    CHECK(middle->positionInfo().similarItemsInGroup == 3);
}

TEST_CASE("posinset and setsize win over the child count", "[a11y][object]") {
    // A virtual list of 10000 rows has 30 children and knows it. Counting the
    // children would tell the user "3 of 30" in a list of ten thousand.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref list = backend.add(backend.desktopRef(), PlatformRole::List, "Mail");
    const Ref row = backend.add(list, PlatformRole::ListItem, "A message");
    backend.node(row).attributes = {{"posinset", "4211"}, {"setsize", "10000"}};

    const PositionInfo info = fixture.registry.objectFor(row)->positionInfo();
    CHECK(info.indexInGroup == 4211);
    CHECK(info.similarItemsInGroup == 10000);
}

TEST_CASE("object identity is stable", "[a11y][object]") {
    A11yFixture fixture;
    const ButtonTree tree{fixture.backend};
    const ObjectPtr first = fixture.registry.objectFor(tree.button);
    const ObjectPtr again = fixture.registry.objectFor(tree.button);
    // The same accessible must be the same object: the event limiter and the
    // event dispatcher key on id().
    CHECK(first.get() == again.get());
    CHECK(first->id() == static_cast<alus::core::ObjectId>(tree.button));
    CHECK(first->sameAs(*again));
    CHECK(fixture.registry.cachedObjectCount() >= 1);
}

TEST_CASE("the registry is a cache, not an owner", "[a11y][object]") {
    A11yFixture fixture;
    const ButtonTree tree{fixture.backend};
    {
        const ObjectPtr button = fixture.registry.objectFor(tree.button);
        CHECK(fixture.backend.node(tree.button).refs == 1);
    }
    // The registry still holds it: an object lives for the cycle that
    // mentioned it (Registry::releaseCycleObjects explains why).
    CHECK(fixture.backend.node(tree.button).refs == 1);
    CHECK(fixture.registry.cycleObjectCount() >= 1);

    // At the start of the next cycle it is let go, and the platform reference
    // goes back with it.
    fixture.registry.releaseCycleObjects();
    CHECK(fixture.backend.node(tree.button).refs == 0);
    fixture.registry.sweepObjectCache();
    // The desktop object is still there: the registry holds that one for the
    // whole session, because api::getDesktopObject is where every tree walk
    // starts.
    CHECK(fixture.registry.cachedObjectCount() == 1);
    CHECK(fixture.extraRefs() == 0);
}

TEST_CASE("T4.2 thirty property reads in one cycle are one batch", "[a11y][object][cached]") {
    A11yFixture fixture;
    const ButtonTree tree{fixture.backend};
    const ObjectPtr button = fixture.registry.objectFor(tree.button);
    REQUIRE(button != nullptr);

    // A fresh cycle, with an empty cache. The invalidation matters: creating
    // an object costs one read of its role, attributes and states, because
    // that is what the overlay chooser asks about (§6.2). Measuring from here
    // measures a cycle that works with an object it already has, which is
    // every cycle but the first.
    alus::core::invalidateAllCaches();
    fixture.backend.resetCalls();

    for (int round = 0; round < 10; ++round) {
        (void)button->name();
        (void)button->role();
        (void)button->states();
    }
    // Thirty reads. Each of the three properties cost exactly one platform
    // call, and `states` needs the attributes too (the refinement of §6.5), so
    // the batch is four calls -- not thirty.
    CHECK(fixture.backend.callsFor("name") == 1);
    CHECK(fixture.backend.callsFor("role") == 1);
    CHECK(fixture.backend.callsFor("states") == 1);
    CHECK(fixture.backend.callsFor("attributes") == 1);
    CHECK(fixture.backend.calls() == 4);

    // The next cycle starts with an empty cache, which is the whole point of
    // the one-cycle lifetime: an object that changed is read again.
    alus::core::invalidateAllCaches();
    (void)button->name();
    CHECK(fixture.backend.callsFor("name") == 2);
}

TEST_CASE("the whole presentation of an object is one batch", "[a11y][object][cached]") {
    // The realistic version of T4.2: speech, braille and review all ask the
    // same object for everything they need, in one cycle.
    A11yFixture fixture;
    const ButtonTree tree{fixture.backend};
    const ObjectPtr button = fixture.registry.objectFor(tree.button);
    alus::core::invalidateAllCaches();
    fixture.backend.resetCalls();

    for (int consumer = 0; consumer < 3; ++consumer) {
        (void)button->name();
        (void)button->role();
        (void)button->states();
        (void)button->value();
        (void)button->description();
        (void)button->keyboardShortcut();
        (void)button->location();
        (void)button->positionInfo();
        (void)button->isCurrent();
        (void)button->level();
    }
    // Every property, three times over, and no platform call for THIS object
    // is made twice. (The count is per object on purpose: positionInfo asks
    // the parent for its child count, and creating that parent costs a read
    // of its own role and attributes -- work that belongs to the parent, not
    // to a cache miss here.)
    for (const char* property : {"name", "role", "states", "attributes", "description",
                                 "extents", "value", "indexInParent", "actionCount",
                                 "actionKeyBinding"}) {
        CHECK(fixture.backend.callsFor(tree.button, property) <= 1);
    }
    // Fifteen calls for a whole object and its parent, against the thirty
    // reads of three properties above.
    CHECK(fixture.backend.calls() < 20);
}

TEST_CASE("a dead object answers emptily and says so", "[a11y][object]") {
    A11yFixture fixture;
    const ButtonTree tree{fixture.backend};
    const ObjectPtr button = fixture.registry.objectFor(tree.button);
    REQUIRE(button->name() == "Save");

    fixture.backend.kill(tree.button);
    alus::core::invalidateAllCaches();

    CHECK_FALSE(button->isAlive());
    CHECK(button->name().empty());
    CHECK(button->states().has(State::Defunct));
    // The setters refuse, which is the contract of §6.4 -- and they must not
    // throw, because an application closing a window is an ordinary event.
    CHECK_FALSE(button->setFocus());
    CHECK_FALSE(button->doAction(0));
    CHECK_FALSE(button->scrollIntoView());
}

TEST_CASE("the name comes from the LABELLED_BY relation", "[a11y][object]") {
    // A GTK entry with a GtkLabel in front of it has no name of its own. This
    // is the difference between "edit" and "File name edit".
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref box = backend.add(backend.desktopRef(), PlatformRole::Panel);
    const Ref label = backend.add(box, PlatformRole::Label, "File name");
    const Ref entry = backend.add(box, PlatformRole::Entry);
    backend.node(entry).relations[PlatformRelation::LabelledBy] = {label};

    CHECK(fixture.registry.objectFor(entry)->name() == "File name");
}

TEST_CASE("the description comes from DESCRIBED_BY when there is none", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref box = backend.add(backend.desktopRef(), PlatformRole::Panel);
    const Ref hint = backend.add(box, PlatformRole::Label, "Must be unique");
    const Ref entry = backend.add(box, PlatformRole::Entry, "User name");
    backend.node(entry).relations[PlatformRelation::DescribedBy] = {hint};

    CHECK(fixture.registry.objectFor(entry)->description() == "Must be unique");
}

TEST_CASE("an error message is only read when the entry is invalid", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref box = backend.add(backend.desktopRef(), PlatformRole::Panel);
    const Ref message = backend.add(box, PlatformRole::Label, "That is not an email address");
    const Ref entry = backend.add(box, PlatformRole::Entry, "Email");
    backend.node(entry).relations[PlatformRelation::ErrorMessage] = {message};

    const ObjectPtr object = fixture.registry.objectFor(entry);
    // The relation is there either way; whether it is SPOKEN is the
    // presentation layer's decision, and it asks for the invalid state.
    CHECK(object->errorMessage() == "That is not an email address");
    CHECK_FALSE(object->states().has(State::InvalidEntry));
}

TEST_CASE("a value is a percentage when the range says so", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref slider = backend.add(backend.desktopRef(), PlatformRole::Slider, "Volume");
    backend.addInterface(slider, Interface::Value);
    backend.node(slider).value = ValueInfo{30.0, 0.0, 120.0, 1.0, ""};

    CHECK(fixture.registry.objectFor(slider)->value() == "25%");

    SECTION("the toolkit's own wording wins") {
        backend.node(slider).value = ValueInfo{30.0, 0.0, 120.0, 1.0, "Monday"};
        alus::core::invalidateAllCaches();
        CHECK(fixture.registry.objectFor(slider)->value() == "Monday");
    }

    SECTION("a value with no range is the number") {
        backend.node(slider).value = ValueInfo{7.0, 0.0, 0.0, 0.0, ""};
        alus::core::invalidateAllCaches();
        CHECK(fixture.registry.objectFor(slider)->value() == "7");
    }
}

TEST_CASE("a combo box with no Value interface says its selection", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref combo = backend.add(backend.desktopRef(), PlatformRole::ComboBox, "Language");
    backend.addInterface(combo, Interface::Selection);
    const Ref serbian = backend.add(combo, PlatformRole::ListItem, "Serbian");
    backend.add(combo, PlatformRole::ListItem, "English");
    backend.node(combo).selected = {serbian};

    CHECK(fixture.registry.objectFor(combo)->value() == "Serbian");
}

TEST_CASE("presentationType keeps the layout objects quiet", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref window = backend.add(backend.desktopRef(), PlatformRole::Frame, "Settings");
    const Ref pane = backend.add(window, PlatformRole::Panel);
    const Ref named = backend.add(window, PlatformRole::Panel, "Appearance");
    const Ref hidden = backend.add(window, PlatformRole::Button, "Apply");
    backend.removeState(hidden, PlatformState::Visible);

    // An unnamed panel is scaffolding; a named one is a group the user is in.
    CHECK(fixture.registry.objectFor(pane)->presentationType() == PresentationType::Layout);
    CHECK(fixture.registry.objectFor(named)->presentationType() == PresentationType::Content);
    CHECK(fixture.registry.objectFor(hidden)->presentationType() ==
          PresentationType::Unavailable);

    // And that is what decides whether an ancestor of the focus is announced.
    CHECK_FALSE(fixture.registry.objectFor(pane)->isPresentableFocusAncestor());
    CHECK(fixture.registry.objectFor(named)->isPresentableFocusAncestor());
}

TEST_CASE("a list item is not announced as a focus ancestor", "[a11y][object]") {
    // NVDA's list exactly: an ancestor of these kinds says nothing the focus
    // itself will not say.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref item = backend.add(backend.desktopRef(), PlatformRole::ListItem, "Downloads");
    const Ref bar = backend.add(backend.desktopRef(), PlatformRole::ProgressBar, "Copying");
    CHECK_FALSE(fixture.registry.objectFor(item)->isPresentableFocusAncestor());
    CHECK_FALSE(fixture.registry.objectFor(bar)->isPresentableFocusAncestor());
}

TEST_CASE("the first focusable descendant search is bounded", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref root = backend.add(backend.desktopRef(), PlatformRole::Panel, "Application");
    // A chain far deeper than the budget, with the focusable control at the
    // very bottom: the search must give up rather than walk it.
    Ref current = root;
    for (int depth = 0; depth < 400; ++depth) {
        current = backend.add(current, PlatformRole::Panel);
    }
    const Ref deep = backend.add(current, PlatformRole::Button, "Too deep");
    backend.addState(deep, PlatformState::Focusable);

    const QuietLog quiet;
    CHECK(fixture.registry.objectFor(root)->firstFocusableDescendant() == nullptr);

    SECTION("and finds what is actually within reach") {
        const Ref shallow = backend.add(root, PlatformRole::Button, "Right here");
        backend.addState(shallow, PlatformState::Focusable);
        alus::core::invalidateAllCaches();
        const ObjectPtr found = fixture.registry.objectFor(root)->firstFocusableDescendant();
        REQUIRE(found != nullptr);
        CHECK(found->name() == "Right here");
    }
}

TEST_CASE("table cell properties", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref table = backend.add(backend.desktopRef(), PlatformRole::Table, "Budget");
    backend.addInterface(table, Interface::Table);
    backend.node(table).table = TableInfo{12, 4, 0, 0};
    const Ref cell = backend.add(table, PlatformRole::TableCell, "1200");
    backend.addInterface(cell, Interface::TableCell);
    backend.node(cell).cell = CellInfo{2, 1, 1, 1};
    backend.node(cell).table_ = table;
    backend.node(cell).rowHeader = "March";
    backend.node(cell).columnHeader = "Income";

    const ObjectPtr object = fixture.registry.objectFor(cell);
    CHECK(object->cellInfo().row == 2);
    CHECK(object->cellInfo().column == 1);
    CHECK(object->rowHeaderText() == "March");
    CHECK(object->columnHeaderText() == "Income");
    CHECK(object->tableId() == table);
    REQUIRE(object->table() != nullptr);
    CHECK(object->table()->name() == "Budget");
    CHECK(fixture.registry.objectFor(table)->tableInfo().rowCount == 12);
    CHECK(fixture.registry.objectFor(table)->tableInfo().columnCount == 4);
}

TEST_CASE("a table cell hands its table reference back", "[a11y][object]") {
    // Every reference the platform gives out has to come back, and the one
    // that is easiest to lose is the one that arrives inside a cached struct:
    // nobody owns it, and a user moving over a hundred cells loses a hundred
    // accessibles. The cell's table is therefore an OBJECT, which owns the
    // reference for as long as the cycle lasts.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref table = backend.add(backend.desktopRef(), PlatformRole::Table, "Budget");
    backend.addInterface(table, Interface::Table);
    const Ref cell = backend.add(table, PlatformRole::TableCell, "1200");
    backend.addInterface(cell, Interface::TableCell);
    backend.node(cell).cell = CellInfo{2, 1, 1, 1};
    backend.node(cell).table_ = table;

    {
        const ObjectPtr object = fixture.registry.objectFor(cell);
        CHECK(object->tableId() == table);
        // Asked twice in one cycle: still one reference, because the answer
        // is cached like every other property.
        CHECK(object->tableId() == table);
    }
    fixture.registry.releaseCycleObjects();
    alus::core::invalidateAllCaches();
    fixture.registry.sweepObjectCache();
    CHECK(fixture.extraRefs() == 0);
}

TEST_CASE("setFocus and doAction reach the platform", "[a11y][object]") {
    A11yFixture fixture;
    const ButtonTree tree{fixture.backend};
    const ObjectPtr button = fixture.registry.objectFor(tree.button);

    CHECK(button->setFocus());
    REQUIRE(fixture.backend.focused.size() == 1);
    CHECK(fixture.backend.focused.front() == tree.button);

    CHECK(button->doAction(0));
    REQUIRE(fixture.backend.performed.size() == 1);
    CHECK(fixture.backend.performed.front().ref == tree.button);
    // An action index that does not exist is refused, not guessed.
    CHECK_FALSE(button->doAction(3));

    SECTION("a control that refuses the focus answers false") {
        fixture.backend.node(tree.button).focusWorks = false;
        CHECK_FALSE(button->setFocus());
    }
}

TEST_CASE("a password field is protected", "[a11y][object]") {
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref field = backend.add(backend.desktopRef(), PlatformRole::PasswordText, "Password");
    const ObjectPtr object = fixture.registry.objectFor(field);
    CHECK(object->role() == Role::PasswordEdit);
    CHECK(object->isProtected());
}

TEST_CASE("a toolkit that claims two billion children is capped", "[a11y][object]") {
    // MEASURED, and the reason this test exists: a LibreOffice Calc sheet
    // reports its child count as 2147483647 -- INT_MAX, the whole addressable
    // grid rather than the cells that have anything in them. Without a cap,
    // one children() on that object is a reserve() of 32 GB followed by two
    // billion D-Bus round trips: the screen reader does not slow down, it
    // stops. Found in phase 8 while measuring the LibreOffice app module.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref sheet = backend.add(backend.desktopRef(), PlatformRole::Table, "Sheet Sheet1");
    for (int index = 0; index < 20; ++index) {
        backend.add(sheet, PlatformRole::TableCell, "A" + std::to_string(index + 1));
    }
    backend.node(sheet).claimedChildCount = 2147483647;

    const ObjectPtr object = fixture.registry.objectFor(sheet);
    REQUIRE(object != nullptr);
    // The claim is reported as it stands: it is what the toolkit said, and the
    // table commands use the Table interface, which answers by coordinate.
    CHECK(object->childCount() == 2147483647);
    // The LIST is bounded, and it did not try to fetch two billion of them.
    const std::size_t fetched = object->children().size();
    CHECK(fetched == 20);
    CHECK(backend.callsFor("childAt") <= 8192);

    SECTION("and an ordinary object is not truncated") {
        const Ref list = backend.add(backend.desktopRef(), PlatformRole::List, "Files");
        for (int index = 0; index < 50; ++index) {
            backend.add(list, PlatformRole::ListItem, "item");
        }
        const ObjectPtr listObject = fixture.registry.objectFor(list);
        REQUIRE(listObject != nullptr);
        CHECK(listObject->children().size() == 50);
    }
}

TEST_CASE("a filler is scaffolding: never an announced ancestor", "[a11y][object]") {
    // The desktop-icons extension wraps every column of icons in an unnamed
    // FILLER; moving to an icon in another column announced "filler" (in
    // Serbian "popunjavanje") as a new focus ancestor, which a user reported
    // as noise. AT-SPI has the role and MSAA does not, so NVDA's own list
    // never needed it. Orca never speaks it.
    A11yFixture fixture;
    MockBackend& backend = fixture.backend;
    const Ref window = backend.add(backend.desktopRef(), PlatformRole::Frame, "Desktop icons");
    const Ref filler = backend.add(window, PlatformRole::Filler);
    const Ref namedFiller = backend.add(window, PlatformRole::Filler, "Recent");

    CHECK(fixture.registry.objectFor(filler)->presentationType() == PresentationType::Layout);
    CHECK_FALSE(fixture.registry.objectFor(filler)->isPresentableFocusAncestor());
    // A filler somebody NAMED is a group with a name, and the name is said.
    CHECK(fixture.registry.objectFor(namedFiller)->presentationType() ==
          PresentationType::Content);
}
