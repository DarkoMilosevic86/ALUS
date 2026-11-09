// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The LibreOffice app module.
//
// WHAT WAS MEASURED, because every line below follows from it. LibreOffice
// 25.8 on the target system (Debian 13, GNOME 48.7), through the real AT-SPI
// bus, reports:
//
//     application "soffice"   toolkit gtk 3.24.49   /proc/<pid>/exe ->
//         /usr/lib/libreoffice/program/soffice.bin
//
//     frame/panel/panel/root pane/status bar            13 children, depth 4
//     frame/.../document text                           Writer
//     frame/.../document spreadsheet/table              Calc, children=2147483647
//     frame/.../document spreadsheet/table/table cell   name "A1"
//
// Four things in that come as a surprise, and each one is a line of code here:
//
//  1. THE STATUS BAR IS FOUR LEVELS BELOW THE WINDOW. `Module::statusBar`
//     looks at the window's own children, which is where GTK puts it; here it
//     is frame -> panel -> panel -> root pane -> status bar, so the default
//     finds nothing in the one application whose status line matters most
//     (Writer's page and word count, Calc's selection sum).
//
//  2. THE STATUS FIELDS HAVE NO NAMES. All 13 are `label` with an empty name,
//     an empty value and an empty description -- the content is in their TEXT
//     interface. `Module::statusBarText` joins names and values, so it would
//     answer the empty string even once the bar is found. Measured contents of
//     a freshly opened Writer document:
//
//         [1] "Page 1 of 1"   [2] "0 words, 0 characters"
//         [4] "Default Page Style"   [5] "Serbian Cyrillic (Serbia)"
//         [12] "100%"        and seven empty fields between them
//
//     So the override reads the text AND skips the empty fields, or the user
//     hears the silence between them as a pause and the zoom as the first
//     thing on the line.
//
//  3. CALC HAS NO TABLE ROWS. The hierarchy is table -> table cell, with no
//     row in between, so the `container` override §12.2 describes for a cell
//     is NOT needed for Calc -- the parent already is the table. It is kept
//     for the shape where a row does appear (Writer's own tables), and it says
//     nothing when the parent is already a table.
//
//  4. A CALC SHEET REPORTS 2147483647 CHILDREN. That is INT_MAX, the whole
//     addressable grid. It is not this module's problem to solve and it is not
//     solved here -- it is capped in the object model, where every caller is
//     protected at once (src/a11y/object.cpp, kMaxChildren). It is recorded
//     here because this module is where it was found, and because it is the
//     reason the status-bar search below is BOUNDED: an unbounded walk of a
//     Calc window would never return.
//
// What this module does NOT do, deliberately: it does not create a tree
// interceptor for Writer. Phase 6 measured that the one `BrowseMode` covers
// Writer as well as the browsers, because the role map folds all six AT-SPI
// document roles onto `Role::Document` and the compound range works the same
// way for both. A `CompoundDocument` class here would be a second copy of code
// that is already right.
#include "alus/a11y/registry.hpp"
#include "alus/app/modules.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"

#include <format>
#include <memory>
#include <string>
#include <utility>

namespace alus::app {
namespace {

// How far below the window the status bar may be. The measured answer is 4;
// one more than that is slack for a LibreOffice that adds a container, and it
// is still bounded, which is what matters next to a sheet of a billion cells.
constexpr int kStatusBarDepth = 5;

class LibreOfficeModule final : public Module {
public:
    LibreOfficeModule(AppKey key, Services services)
        : Module("libreoffice", std::move(key), std::move(services),
                 {"appModules.soffice.AppModule"}) {}

    void initialize() override {
        // Mechanism 4: the module's own keys. The gestures are declared here
        // and the app layer puts them into GestureManager::appMap(), so they
        // are rebindable in gestures.ini under
        // `[appModules.soffice.AppModule]` like any other command.
        ALUS_SCRIPT(reportColumnHeader,
                    .description = "Reports the column header of the current cell.",
                    .category = "Table navigation",
                    .gestures = {"kb:alus+shift+c"});
        ALUS_SCRIPT(reportRowHeader,
                    .description = "Reports the row header of the current cell.",
                    .category = "Table navigation",
                    .gestures = {"kb:alus+shift+r"});
    }

    // ---- 1. the status bar ----
    a11y::ObjectPtr statusBar() const override {
        // The window's own children first: cheap, and right if a future
        // LibreOffice flattens its hierarchy.
        if (a11y::ObjectPtr found = Module::statusBar()) {
            return found;
        }
        return findDescendant(activeWindow(), a11y::Role::StatusBar, kStatusBarDepth);
    }

    // ---- 2. the status fields, whose content is their text ----
    std::string statusBarText() const override {
        const a11y::ObjectPtr bar = statusBar();
        if (bar == nullptr) {
            return {};
        }
        std::string text;
        for (const a11y::ObjectPtr& field : bar->children()) {
            if (field == nullptr) {
                continue;
            }
            // The name first, in case a LibreOffice build sets it; the text is
            // what this one actually uses.
            std::string piece = field->name();
            if (piece.empty() && field->hasInterface(a11y::Interface::Text)) {
                const int count = field->characterCount();
                if (count > 0) {
                    piece = field->textRange(0, count);
                }
            }
            if (piece.empty()) {
                // Seven of the thirteen fields are empty. Skipped, not joined
                // as blanks: a status line read as "Page 1 of 1, , , 100%" is
                // worse than one with the empty fields left out.
                continue;
            }
            if (!text.empty()) {
                text += ", ";
            }
            text += piece;
        }
        return text;
    }

    // ---- 3. the cell's container ----
    a11y::ObjectPtr container(const a11y::Object& object) const override {
        if (object.role() != a11y::Role::TableCell) {
            return nullptr;
        }
        const a11y::ObjectPtr parent = object.parent();
        if (parent == nullptr || parent->role() != a11y::Role::TableRow) {
            // Calc, measured: the parent IS the table, so the default is
            // right and this override says nothing.
            return nullptr;
        }
        a11y::ObjectPtr table = parent->parent();
        return table != nullptr && table->role() == a11y::Role::Table ? table : nullptr;
    }

    std::string productName() const override { return "LibreOffice"; }

private:
    // ---- the two header commands ----
    void reportColumnHeader(input::Gesture& /*gesture*/) { reportHeader(true); }
    void reportRowHeader(input::Gesture& /*gesture*/) { reportHeader(false); }

    void reportHeader(bool column) {
        a11y::Registry* registry = services().a11y;
        const a11y::ObjectPtr cell =
            registry != nullptr ? registry->api().focus() : nullptr;
        if (cell == nullptr) {
            return;
        }
        if (cell->role() != a11y::Role::TableCell) {
            // Translators: reported when a table command is used outside a table.
            say(_("Not in a table"));
            return;
        }
        const std::string text = column ? cell->columnHeaderText() : cell->rowHeaderText();
        if (!text.empty()) {
            say(text);
            return;
        }
        // No header is a real answer and it is said, not swallowed: a
        // spreadsheet whose first row is data has no column headers, and
        // silence here looks exactly like a broken key.
        say(column
                // Translators: reported when the current cell's column has no header.
                ? _("No column header")
                // Translators: reported when the current cell's row has no header.
                : _("No row header"));
    }
};

}  // namespace

void registerLibreOfficeModule(Registry& registry) {
    // Only the executable, not the toolkit: the measured toolkit is `gtk`
    // (3.24.49) and not the `gail` the plan assumed, and the Qt (kf5) build of
    // the same suite behaves the same way at this level. Keying on the
    // toolkit here would have matched every GTK application on the desktop.
    registry.add(Registry::Rule{
        "libreoffice", "soffice.bin", "",
        [](const AppKey& key, const Services& services) -> ModulePtr {
            return std::make_unique<LibreOfficeModule>(key, services);
        }});
}

}  // namespace alus::app
