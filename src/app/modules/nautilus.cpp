// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The Nautilus (GNOME Files) app module.
//
// WHAT WAS MEASURED. Nautilus 48 on the target system reports toolkit
// `GTK 4.18.6`, and its file view -- in list mode, through the real AT-SPI bus
// -- looks like this:
//
//     scroll pane/tree table  "Преглед садржаја"
//       [0] table row  "Назив Величина Датум измене"   (the header row)
//       [1] list                                        (the rows container)
//           table row  "ALUS"                           <- a FILE
//             table cell "ALUS ALUS"       the name column
//             table cell "14 ставки"       the size column
//             table cell "Данас, у 9∶05"   the modified column
//             table cell "Звезда"          the starred column
//
// Four facts in that, and each one is a line here:
//
//  1. A FILE IS A `table row`, not a list item and not an icon. A module that
//     keyed on ListItem (which is what the GTK3 Nautilus used, and what the
//     plan assumed) would do nothing at all here.
//
//  2. THE DETAIL IS IN THE CELLS' NAMES. Nothing in the file view exposes the
//     Text interface and no cell has a description; the size and the date are
//     the cells' accessible names. So a screen reader that announces only the
//     row says "ALUS" where the user asked "what is this", and one that reads
//     every cell says the name, the size, the date and the star on every arrow
//     key.
//
//  3. THE NAME COLUMN REPEATS THE NAME, DOUBLED: the row is "ALUS" and its
//     first cell is "ALUS ALUS". Skipping a cell whose name EQUALS the row's
//     would therefore not skip it, and the user would hear "ALUS, ALUS ALUS,
//     14 items". The first cell is the name column -- the header row says so
//     -- so it is skipped by position.
//
//  4. THE ROW'S PARENT IS AN ANONYMOUS `list` INSIDE THE `tree table`. The
//     focus ancestry therefore says "list" where it should say which view the
//     user is in, and the thing with a name ("Преглед садржаја") is one level
//     further up. §12.2's third hook is for exactly this.
//
// What this module does with all that: it puts the detail in the item's
// DESCRIPTION, which is how NVDA's own file-list modules do it, so that it
// follows the user's own "report object description" setting instead of being
// forced on them.
#include "alus/a11y/registry.hpp"
#include "alus/app/modules.hpp"
#include "alus/core/log.hpp"

#include <memory>
#include <string>
#include <utility>

namespace alus::app {
namespace {

// A file row's parent is a `list`, whose parent is the `tree table` that has
// the name. Two steps, and no more: an unbounded walk ends at the desktop.
constexpr int kContainerDepth = 2;

class NautilusModule final : public Module {
public:
    NautilusModule(AppKey key, Services services)
        : Module("nautilus", std::move(key), std::move(services),
                 {"appModules.nautilus.AppModule"}) {}

    void chooseOverlayClasses(a11y::Object& object, a11y::BehaviorList& list) override {
        if (!isFileItem(object)) {
            return;
        }
        // The front: this behaviour answers `description`, and the platform's
        // own empty answer must not win over it.
        a11y::prependBehavior(list,
                              a11y::behaviorEntry<FileItem>("appModules.nautilus.FileItem"));
    }

    a11y::ObjectPtr container(const a11y::Object& object) const override {
        if (!isFileItem(object)) {
            return nullptr;
        }
        // Past the anonymous `list` to the view that has a name.
        a11y::ObjectPtr candidate = object.parent();
        for (int depth = 0; depth < kContainerDepth && candidate != nullptr; ++depth) {
            switch (candidate->role()) {
                case a11y::Role::Table:
                // ALUS folds ATSPI_ROLE_TREE_TABLE onto TreeView, because
                // NVDA has no tree table (src/a11y/role_map.cpp), so this is
                // what the measured `tree table` arrives as.
                case a11y::Role::TreeView:
                    return candidate;
                default:
                    break;
            }
            candidate = candidate->parent();
        }
        return nullptr;
    }

    std::string productName() const override { return "Files"; }

private:
    // A behaviour and not a property override on the module, because
    // `description` is asked of the OBJECT: the presentation layer reads
    // `object.description()`, and only a behaviour sits in that path (§6.2).
    class FileItem final : public a11y::Behavior {
    public:
        std::string_view className() const override {
            return "appModules.nautilus.FileItem";
        }

        std::optional<std::string> description(const a11y::Object& object) const override {
            // The platform's own description wins when there is one: Nautilus
            // setting a description means it had something to say, and
            // replacing that would be this module overruling the application
            // about its own content.
            if (!object.platformDescription().empty()) {
                return std::nullopt;
            }
            std::string text;
            bool first = true;
            for (const a11y::ObjectPtr& cell : object.children()) {
                if (cell == nullptr) {
                    continue;
                }
                if (first) {
                    // The name column, measured as the row's name repeated
                    // twice. Skipped by position, because comparing it with
                    // the row's name does not match it.
                    first = false;
                    continue;
                }
                const std::string& piece = cell->name();
                if (piece.empty()) {
                    continue;
                }
                if (!text.empty()) {
                    text += ", ";
                }
                text += piece;
            }
            if (text.empty()) {
                return std::nullopt;
            }
            return text;
        }
    };

    static bool isFileItem(const a11y::Object& object) {
        // The measured shape: a row whose children are cells, inside a view.
        // The role alone is not enough -- the header row is a `table row` too,
        // and so is every row of every table in every GTK application.
        if (object.platformRole() != a11y::Role::TableRow) {
            return false;
        }
        if (object.childCount() <= 0) {
            return false;
        }
        const a11y::ObjectPtr firstCell = object.firstChild();
        if (firstCell == nullptr || firstCell->role() != a11y::Role::TableCell) {
            // The header row's children are `filler`, which is how it is told
            // apart from a file (measured).
            return false;
        }
        return true;
    }
};

}  // namespace

void registerNautilusModule(Registry& registry) {
    registry.add(Registry::Rule{
        "nautilus", "nautilus", "",
        [](const AppKey& key, const Services& services) -> ModulePtr {
            return std::make_unique<NautilusModule>(key, services);
        }});
}

}  // namespace alus::app
