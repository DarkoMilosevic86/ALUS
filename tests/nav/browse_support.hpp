// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 6 fixture: a WEB DOCUMENT on the mock platform.
//
// It is built to the shape the exit criteria talk about, and to the shape N47
// measured on the real Chrome: THE DOCUMENT ROOT'S OWN TEXT IS U+FFFC PER
// CHILD, not the body text. A fixture that gave the root the whole text would
// test a document no browser produces, and would hide the one thing phase 6
// exists to solve.
//
//     document (Collection, Text = "￼￼￼￼")
//       heading level 1   "Interfaces"
//       paragraph         "The first paragraph."
//       list (3 items)
//         list item       "One"
//         list item       "Two"
//         list item       "Three"
//       table 2x2
//         column header   "Name"      column header "Size"
//         cell            "alus"      cell          "9 MB"
//
// plus a link, an image, an edit field and a check box where the tests that
// need them put them.
#pragma once

#include "nav/support.hpp"

#include "a11y/mock_backend.hpp"

#include <string>
#include <vector>

namespace alus::testing {

// U+FFFC. One CHARACTER in AT-SPI offsets, three UTF-8 bytes in a std::string.
inline constexpr const char* kPlaceholder = "\xEF\xBF\xBC";

inline std::string placeholders(int count) {
    std::string out;
    for (int index = 0; index < count; ++index) {
        out += kPlaceholder;
    }
    return out;
}

// A leaf that holds real characters: a paragraph, a heading, a list item.
inline a11y::Ref addTextLeaf(MockBackend& backend, a11y::Ref parent, PlatformRole role,
                             const std::string& text, std::string name = {}) {
    const a11y::Ref ref = backend.add(parent, role, std::move(name));
    MockNode& node = backend.node(ref);
    node.text = text;
    node.interfaces |= a11y::Interface::Text;
    backend.addState(ref, PlatformState::Showing);
    return ref;
}

// A container whose own text is one placeholder per child -- what Gecko and
// Chromium really answer (N47).
inline void makeContainer(MockBackend& backend, a11y::Ref ref, int childCount) {
    MockNode& node = backend.node(ref);
    node.text = placeholders(childCount);
    node.interfaces |= a11y::Interface::Text;
}

struct WebDocumentTree {
    explicit WebDocumentTree(MockBackend& backend, const char* toolkit = "Chromium") {
        application = backend.addApplication("Browser", toolkit);
        window = backend.add(application, PlatformRole::Frame, "Interfaces - Browser");
        backend.addState(window, PlatformState::Active);

        document = backend.add(window, PlatformRole::DocumentWeb, "Interfaces");
        backend.addInterface(document, a11y::Interface::Collection);
        backend.addState(document, PlatformState::Focusable);

        heading = addTextLeaf(backend, document, PlatformRole::Heading, "Interfaces");
        backend.node(heading).attributes["level"] = "1";

        paragraph =
            addTextLeaf(backend, document, PlatformRole::Paragraph, "The first paragraph.");

        list = backend.add(document, PlatformRole::List);
        backend.addState(list, PlatformState::ReadOnly);
        item1 = addTextLeaf(backend, list, PlatformRole::ListItem, "One");
        item2 = addTextLeaf(backend, list, PlatformRole::ListItem, "Two");
        item3 = addTextLeaf(backend, list, PlatformRole::ListItem, "Three");
        makeContainer(backend, list, 3);

        table = backend.add(document, PlatformRole::Table, "Downloads");
        backend.addInterface(table, a11y::Interface::Table);
        backend.node(table).table.rowCount = 2;
        backend.node(table).table.columnCount = 2;
        nameHeader =
            addTextLeaf(backend, table, PlatformRole::ColumnHeader, "Name", "Name");
        sizeHeader =
            addTextLeaf(backend, table, PlatformRole::ColumnHeader, "Size", "Size");
        cellA = addTextLeaf(backend, table, PlatformRole::TableCell, "alus");
        cellB = addTextLeaf(backend, table, PlatformRole::TableCell, "9 MB");
        for (const a11y::Ref cell : {nameHeader, sizeHeader, cellA, cellB}) {
            backend.addInterface(cell, a11y::Interface::TableCell);
            backend.node(cell).table_ = table;
        }
        setCell(backend, nameHeader, 0, 0);
        setCell(backend, sizeHeader, 0, 1);
        setCell(backend, cellA, 1, 0);
        setCell(backend, cellB, 1, 1);
        backend.node(cellA).columnHeader = "Name";
        backend.node(cellB).columnHeader = "Size";
        makeContainer(backend, table, 4);

        makeContainer(backend, document, 4);
    }

    static void setCell(MockBackend& backend, a11y::Ref ref, int row, int column) {
        MockNode& node = backend.node(ref);
        node.cell.row = row;
        node.cell.column = column;
        node.cell.rowSpan = 1;
        node.cell.columnSpan = 1;
    }

    // Adds a link inside the paragraph's parent, after it.
    a11y::Ref addLink(MockBackend& backend, const std::string& text, bool visited = false) {
        const a11y::Ref ref = addTextLeaf(backend, document, PlatformRole::Link, text, text);
        // On AT-SPI a link is activatable when it has an ACTION; there is no
        // LINKED state (see nav::isNamedLinkDestination).
        backend.node(ref).actionNames.push_back("jump");
        backend.addState(ref, PlatformState::Focusable);
        if (visited) {
            backend.addState(ref, PlatformState::Visited);
        }
        makeContainer(backend, document,
                      static_cast<int>(backend.node(document).children.size()));
        return ref;
    }

    // Adds an edit field: what T6.8 tabs into.
    a11y::Ref addEdit(MockBackend& backend, const std::string& name) {
        const a11y::Ref ref = backend.add(document, PlatformRole::Entry, name);
        MockNode& node = backend.node(ref);
        node.interfaces |= a11y::Interface::Text;
        node.text = "";
        backend.addState(ref, PlatformState::Editable);
        backend.addState(ref, PlatformState::Focusable);
        makeContainer(backend, document,
                      static_cast<int>(backend.node(document).children.size()));
        return ref;
    }

    // An image: a leaf with NO text, which is the embedded-leaf case and the
    // one N47 said the user hears as a placeholder until phase 6.
    a11y::Ref addImage(MockBackend& backend, const std::string& name) {
        const a11y::Ref ref = backend.add(document, PlatformRole::Image, name);
        backend.addState(ref, PlatformState::Showing);
        makeContainer(backend, document,
                      static_cast<int>(backend.node(document).children.size()));
        return ref;
    }

    a11y::Ref application = 0;
    a11y::Ref window = 0;
    a11y::Ref document = 0;
    a11y::Ref heading = 0;
    a11y::Ref paragraph = 0;
    a11y::Ref list = 0;
    a11y::Ref item1 = 0;
    a11y::Ref item2 = 0;
    a11y::Ref item3 = 0;
    a11y::Ref table = 0;
    a11y::Ref nameHeader = 0;
    a11y::Ref sizeHeader = 0;
    a11y::Ref cellA = 0;
    a11y::Ref cellB = 0;
};

}  // namespace alus::testing
