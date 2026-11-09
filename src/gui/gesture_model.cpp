// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/gui/gesture_model.hpp.

#include "alus/gui/gesture_model.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/gesture.hpp"
#include "alus/input/gesture_map.hpp"
#include "alus/input/key_names.hpp"
#include "alus/input/manager.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/script.hpp"

#include <algorithm>
#include <map>

namespace alus::gui {
namespace {

// "kb:numpad6+alus" -> "ALUS+numpad right arrow".
//
// NOT KeyboardGesture::displayName(): that needs a live KeyEvent, and a STORED
// identifier has no event behind it. So the identifier is taken apart and each
// piece is named through KeyNames, which is also what makes the names
// translated -- the key labels go through gettext (data/key-labels.ini).
std::string displayFor(std::string_view identifier, const input::KeyNames* keyNames) {
    if (keyNames == nullptr) {
        return std::string{identifier};
    }
    const std::optional<input::ParsedKeyboardGesture> parsed =
        input::parseKeyboardGesture(identifier, *keyNames);
    if (!parsed.has_value()) {
        // A braille gesture (`br(alva):t1`, `bk:dot7`): shown as itself, which
        // is how the user writes it in gestures.ini.
        return std::string{identifier};
    }

    std::vector<std::string> pieces;
    // The ALUS key first, then the real modifiers in NVDA's order, then the
    // key. Fixed order and not the order the user typed, so the same binding
    // always reads the same way.
    if (input::has(parsed->modifiers, input::Modifier::Alus)) {
        // Not keyNames->label("alus"): the generated key label table comes
        // from upstream and has no entry for ALUS's own modifier, so the label
        // would fall through to the identifier's lowercase spelling. The user
        // calls this key by the product's name, so the dialog does too.
        //
        // Translators: the name of the ALUS key -- Insert, numpad Insert
        // and/or Caps Lock -- as it appears in a key combination in the
        // gesture dialog. "ALUS" is the product name; translate this only if
        // the name is written differently in your script.
        pieces.emplace_back(_("ALUS"));
    }
    for (const auto& [modifier, name] : {
             std::pair{input::Modifier::Control, "control"},
             std::pair{input::Modifier::Shift, "shift"},
             std::pair{input::Modifier::Alt, "alt"},
             std::pair{input::Modifier::Windows, "windows"},
         }) {
        if (input::has(parsed->modifiers, modifier)) {
            pieces.emplace_back(keyNames->label(name));
        }
    }
    pieces.push_back(keyNames->label(parsed->mainKeyName));

    std::string out;
    for (const std::string& piece : pieces) {
        if (!out.empty()) {
            out += "+";
        }
        out += piece;
    }
    if (parsed->layout.has_value()) {
        out += *parsed->layout == input::Layout::Laptop
                   // Translators: a gesture that applies only to one keyboard
                   // layout, shown after the keys, e.g. "ALUS+h (laptop)".
                   ? std::string{" ("} + _("laptop") + ")"
                   // Translators: a gesture that applies only to one keyboard
                   // layout.
                   : std::string{" ("} + _("desktop") + ")";
    }
    return out;
}

void collect(const input::GestureMap& map, Layer layer,
             std::map<std::string, std::vector<Binding>>& into,
             const input::KeyNames* keyNames) {
    for (const auto& [gesture, entry] : map.allEntries()) {
        if (entry.script.empty()) {
            // An unbind (`None = kb:h`). It is not a binding to show; it is
            // the ABSENCE of the lower layers' bindings, and those are already
            // filtered out below.
            continue;
        }
        Binding binding;
        binding.gesture = gesture;
        binding.display = displayFor(gesture, keyNames);
        binding.layer = layer;
        binding.removableFromFile = layer == Layer::User;
        binding.shadowable = layer != Layer::User;
        into[entry.location + "\x1f" + entry.script].push_back(std::move(binding));
    }
}

// Which gestures the user layer has unbound with `None = <gesture>`. A lower
// layer's binding for such a gesture must not be shown as active, because it
// is not: that is what the entry means.
std::vector<std::string> unboundGestures(const input::GestureMap& userMap) {
    std::vector<std::string> out;
    for (const auto& [gesture, entry] : userMap.allEntries()) {
        if (entry.script.empty()) {
            out.push_back(gesture);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

const char* layerName(Layer layer) {
    switch (layer) {
        case Layer::User:
            return "user";
        case Layer::Locale:
            return "locale";
        case Layer::Driver:
            return "driver";
        case Layer::App:
            return "app";
        case Layer::Builtin:
            return "built-in";
    }
    return "built-in";
}

const char* layerLabel(Layer layer) {
    switch (layer) {
        // Translators: the gesture layer a key binding comes from; this one
        // is the user's own gestures.ini and is the only editable layer.
        case Layer::User:
            return _("yours");
        // Translators: the gesture layer a key binding comes from; shipped
        // with the language.
        case Layer::Locale:
            return _("from the language");
        // Translators: the gesture layer a key binding comes from; defined by
        // a braille display driver.
        case Layer::Driver:
            return _("from the braille driver");
        // Translators: the gesture layer a key binding comes from; defined by
        // an application module or an add-on.
        case Layer::App:
            return _("from an application module");
        // Translators: the gesture layer a key binding comes from; ALUS's own
        // default, which matches NVDA's.
        case Layer::Builtin:
            return _("built in");
    }
    return _("built in");
}

const Command* Model::find(std::string_view location, std::string_view script) const {
    const auto it = std::find_if(commands.begin(), commands.end(),
                                 [&](const Command& command) {
                                     return command.location == location &&
                                            command.script == script;
                                 });
    return it == commands.end() ? nullptr : &*it;
}

std::vector<const Command*> Model::inCategory(std::string_view category) const {
    std::vector<const Command*> out;
    for (const Command& command : commands) {
        if (command.category == category) {
            out.push_back(&command);
        }
    }
    return out;
}

std::size_t Model::bindingCount() const {
    std::size_t count = 0;
    for (const Command& command : commands) {
        count += command.bindings.size();
    }
    return count;
}

Model buildModel(const script::Catalog& catalog, input::GestureManager& manager,
                 const input::KeyNames* keyNames) {
    // All five maps, in priority order. The fourth (app) carries what app
    // modules and plugins bound, with the class name the user writes in
    // gestures.ini, so nothing extra is needed for add-ons to appear here.
    std::map<std::string, std::vector<Binding>> byCommand;
    collect(manager.userMap(), Layer::User, byCommand, keyNames);
    collect(manager.localeMap(), Layer::Locale, byCommand, keyNames);
    collect(manager.driverMap(), Layer::Driver, byCommand, keyNames);
    collect(manager.appMap(), Layer::App, byCommand, keyNames);
    const input::GestureMap& builtin = catalog.builtinGestures();
    collect(builtin, Layer::Builtin, byCommand, keyNames);

    const std::vector<std::string> unbound = unboundGestures(manager.userMap());

    Model model;
    for (const script::ScriptInfo* info : catalog.all()) {
        // T9.3, and NVDA's own rule: a command with no description is not
        // shown. The reason is not tidiness -- a command that cannot be
        // explained cannot be rebound sensibly, and the description IS the
        // explanation.
        if (!info->isVisibleInGestureDialog()) {
            continue;
        }
        Command command;
        command.location = info->location;
        command.script = info->name;
        // Already translated: the catalogue translates each row as it loads it.
        command.description = info->description;
        command.category = info->category;

        const auto it = byCommand.find(info->location + "\x1f" + info->name);
        if (it != byCommand.end()) {
            for (Binding& binding : it->second) {
                // Shadowed by an `None =` entry in the user layer: the key does
                // not run this command any more, so showing it would be a lie.
                if (binding.layer != Layer::User &&
                    std::binary_search(unbound.begin(), unbound.end(), binding.gesture)) {
                    continue;
                }
                command.bindings.push_back(std::move(binding));
            }
            std::sort(command.bindings.begin(), command.bindings.end(),
                      [](const Binding& a, const Binding& b) {
                          if (a.layer != b.layer) {
                              return a.layer < b.layer;
                          }
                          return a.gesture < b.gesture;
                      });
        }
        model.commands.push_back(std::move(command));
    }

    // Sorted by the TRANSLATED category and description, so the order is the
    // user's alphabet. std::string's < is byte order, which for UTF-8 is
    // codepoint order -- not a locale collation, but stable and the same every
    // time, which is what a list the user navigates with a braille display
    // needs. A real collation would need ICU and would reorder the dialog
    // depending on whether libicu-dev was installed.
    std::sort(model.commands.begin(), model.commands.end(),
              [](const Command& a, const Command& b) {
                  if (a.category != b.category) {
                      return a.category < b.category;
                  }
                  if (a.description != b.description) {
                      return a.description < b.description;
                  }
                  return a.script < b.script;
              });
    for (const Command& command : model.commands) {
        if (model.categories.empty() || model.categories.back() != command.category) {
            if (std::find(model.categories.begin(), model.categories.end(),
                          command.category) == model.categories.end()) {
                model.categories.push_back(command.category);
            }
        }
    }
    return model;
}

std::filesystem::path userGestureFile(const config::Manager& manager) {
    return manager.paths().configDir / "gestures.ini";
}

WriteResult addBinding(input::GestureManager& manager,
                       const std::filesystem::path& userFile, std::string_view gesture,
                       std::string_view location, std::string_view script) {
    WriteResult result;
    if (gesture.empty() || location.empty() || script.empty()) {
        result.error = "a binding needs a gesture, a class and a script";
        return result;
    }
    // The LIVE map first: the binding has to work before the user closes the
    // dialog, or they cannot tell whether it took.
    manager.userMap().add(gesture, location, script);
    manager.refreshMaps();

    std::string error;
    if (!manager.userMap().save(userFile, &error)) {
        // The live map keeps the binding: losing it as well would mean the
        // user's key stops working AND the file is unchanged, which is the
        // worst of the two outcomes.
        result.error = error.empty() ? "could not write the gesture map" : error;
        ALUS_ERROR("gesture dialog: {}", result.error);
        return result;
    }
    result.ok = true;
    result.file = userFile;
    ALUS_INFO("bound {} to {}.{}", gesture, location, script);
    return result;
}

WriteResult removeBinding(input::GestureManager& manager,
                          const std::filesystem::path& userFile, std::string_view gesture,
                          std::string_view location, std::string_view script) {
    WriteResult result;
    input::GestureMap& userMap = manager.userMap();
    if (!userMap.remove(gesture, location, script)) {
        // Not the user's binding, so the file that holds it is not the user's
        // to edit. NVDA's answer, and the only one that survives a restart: an
        // `None = <gesture>` entry in the user layer, which shadows every
        // lower layer (see the header).
        userMap.add(gesture, location, /*script=*/{});
    }
    manager.refreshMaps();

    std::string error;
    if (!userMap.save(userFile, &error)) {
        result.error = error.empty() ? "could not write the gesture map" : error;
        ALUS_ERROR("gesture dialog: {}", result.error);
        return result;
    }
    result.ok = true;
    result.file = userFile;
    ALUS_INFO("unbound {} from {}.{}", gesture, location, script);
    return result;
}

}  // namespace alus::gui
