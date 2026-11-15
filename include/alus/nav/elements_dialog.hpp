// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Starting `alus-elements` (src/gui/elements_main.cpp) and hearing back.
//
// The daemon never waits for the window: the labels go out in a file, the
// child is spawned, and its answer arrives on a pipe watched by the main loop
// -- a single line, or nothing when the user cancelled. See the protocol in
// the child's file comment.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace alus::nav {

struct ElementsDialogEntry {
    // "link", "heading", "formField", "button", "landmark".
    std::string kind;
    int level = 0;
    std::string label;
    // The entry the list opens on for its kind: the last one that starts at
    // or before the browse mode caret, as NVDA's dialog opens.
    bool atCaret = false;
};

struct ElementsDialogChoice {
    // "activate" or "move".
    std::string action;
    std::string kind;
    // Among that kind's entries, in the order they were given.
    int index = -1;
};

// The line the child prints, parsed; an action of "" when it is not one.
ElementsDialogChoice parseElementsDialogChoice(const std::string& line);

// What the file holds. Tabs and line breaks inside a label become spaces.
std::string formatElementsDialogEntries(const std::vector<ElementsDialogEntry>& entries);

// Where `alus-elements` is: beside this executable first (an uninstalled
// build opens its own), then $PATH. Empty when it is not there.
std::string elementsDialogProgram();

// Spawns the dialog. `done` runs on the main loop when the child answers, and
// only if it chose something. False when it could not be started.
bool openElementsDialog(const std::vector<ElementsDialogEntry>& entries,
                        const std::string& firstKind,
                        std::function<void(const ElementsDialogChoice&)> done);

}  // namespace alus::nav
