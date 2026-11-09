// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The gesture dialog's contents, and the two writes it can make.
//
// This is NVDA's `inputCore.getAllGestureMappings` plus its gesture dialog's
// add and remove, with the display layer removed -- so T9.2 (adding and
// removing a binding writes ~/.config/alus/gestures.ini) and T9.3 (a script
// with no description does not appear) are asserted without a display.
//
// WHAT THE FOUR LAYERS MEAN HERE, because the dialog has to show them and
// must not let the user edit the three it does not own (§5.1):
//
//   user       ~/.config/alus/gestures.ini      the only writable layer
//   locale     data/locale/<lang>/gestures.ini  shipped per language
//   app        what app modules and plugins bind
//   built-in   data/gestures-builtin.ini        generated from upstream
//
// A binding the user adds goes into the user layer and wins. REMOVING a
// built-in binding is therefore not a deletion: it is an `None = <gesture>`
// entry in the user layer, which is how NVDA unbinds a key it did not define
// and why `GestureMap::add` accepts an empty script name. A dialog that
// deleted from the built-in map instead would appear to work and then come
// back on the next start.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace alus::config {
class Manager;
}
namespace alus::input {
class GestureManager;
class KeyNames;
}
namespace alus::script {
class Catalog;
}

namespace alus::gui {

// Which of the layers a binding came from. The order IS the priority order,
// so a std::min over this type answers "which binding wins".
enum class Layer { User = 0, Locale = 1, Driver = 2, App = 3, Builtin = 4 };

const char* layerName(Layer layer);
// The user-visible name of the layer, translated.
const char* layerLabel(Layer layer);

struct Binding {
    // The normalized identifier, as it is stored: "kb:numpad6+alus".
    std::string gesture;
    // What the user is shown and what a user who reports a problem will
    // write: "ALUS+numpad right arrow". Built with input::KeyNames, so the key
    // labels are translated.
    std::string display;
    Layer layer = Layer::Builtin;
    // Only a binding in the user layer can be taken out of the file. One in
    // any other layer is removed by SHADOWING it (see the file comment), which
    // is still something the dialog offers -- hence two flags.
    bool removableFromFile = false;
    bool shadowable = false;
};

struct Command {
    // "globalCommands.GlobalCommands" + "toggleInputHelp": together these are
    // the key the gesture map stores and the user writes in gestures.ini.
    std::string location;
    std::string script;
    // Translated, from the catalogue. Never empty: a command with an empty
    // description is not listed at all (T9.3).
    std::string description;
    std::string category;
    std::vector<Binding> bindings;
};

struct Model {
    // Categories in the order the dialog shows them (sorted by the translated
    // name, so the order is the user's alphabet and not English's).
    std::vector<std::string> categories;
    // Commands, sorted by category and then by description.
    std::vector<Command> commands;

    const Command* find(std::string_view location, std::string_view script) const;
    std::vector<const Command*> inCategory(std::string_view category) const;
    std::size_t bindingCount() const;
};

// NVDA: getAllGestureMappings. Walks the catalogue for the commands and all
// five maps for the bindings.
//
// `keyNames` may be null, in which case `Binding::display` is the identifier
// itself -- which is what a test without the keysym tables gets, and is still
// unambiguous.
Model buildModel(const script::Catalog& catalog, input::GestureManager& manager,
                 const input::KeyNames* keyNames = nullptr);

// ---------------------------------------------------------------------------
// The two writes. Both change the live map AND the file, in that order, so the
// new binding takes effect before the user has closed the dialog -- a screen
// reader that needs a restart to use a key the user just bound is one the user
// cannot test.
// ---------------------------------------------------------------------------

struct WriteResult {
    bool ok = false;
    std::string error;
    // Where it was written. Empty when nothing was.
    std::filesystem::path file;
};

// `gesture` may be written in any order ("alus+shift+t"); it is normalized.
WriteResult addBinding(input::GestureManager& manager, const std::filesystem::path& userFile,
                       std::string_view gesture, std::string_view location,
                       std::string_view script);

// Removes a binding. A binding in the user layer is taken out of the file; one
// from any other layer is shadowed with an `None = <gesture>` entry, because
// the file it lives in is not the user's to edit.
WriteResult removeBinding(input::GestureManager& manager,
                          const std::filesystem::path& userFile, std::string_view gesture,
                          std::string_view location, std::string_view script);

// Where the user's map is, given the configuration. One function because the
// dialog, the service and the tests must not each spell the name.
std::filesystem::path userGestureFile(const config::Manager& manager);

}  // namespace alus::gui
