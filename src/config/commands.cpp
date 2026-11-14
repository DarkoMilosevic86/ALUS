// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/config/commands.hpp"

#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/ui/message.hpp"

#include <utility>

namespace alus::config {

namespace {
// The repeat count of the script running now, 0 when there is no dispatcher.
int repeatsOf(const std::function<const script::Dispatcher*()>& dispatcher) {
    const script::Dispatcher* current = dispatcher ? dispatcher() : nullptr;
    return current != nullptr ? current->lastScriptRepeatCount() : 0;
}
}  // namespace


Commands::Commands(Context context, const script::Catalog* catalog)
    // One class name, NVDA's: it is what the generated gesture map addresses.
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      context_(std::move(context)) {
    ALUS_SCRIPT(saveConfiguration);
    ALUS_SCRIPT(revertConfiguration);
}

void Commands::say(const std::string& text) const {
    if (context_.message) {
        context_.message(text);
        return;
    }
    ui::message(text);
}

int Commands::repeatCount() const {
    return repeatsOf(context_.dispatcher);
}

void Commands::saveConfiguration(input::Gesture& /*gesture*/) {
    if (context_.manager == nullptr) {
        // Translators: spoken when the configuration cannot be saved because
        // the command was run somewhere that has none, e.g. a diagnostic tool.
        say(_("cannot save the configuration from here"));
        return;
    }
    std::string error;
    if (!context_.manager->save(&error)) {
        ALUS_ERROR("could not save the configuration: {}", error);
        // Translators: spoken when writing the configuration file failed --
        // a read-only home directory, a full disk. NOT silent: a user who
        // believes a setting was kept and finds it gone after a restart has
        // no way to know which of the two went wrong.
        say(_("Could not save configuration"));
        return;
    }
    ALUS_INFO("configuration saved to {}", context_.manager->paths().configDir.string());
    // Translators: spoken when the configuration has been written to disk.
    say(_("Configuration saved"));
}

void Commands::revertConfiguration(input::Gesture& /*gesture*/) {
    if (!context_.reset) {
        // Translators: spoken when the configuration cannot be reloaded
        // because the command was run outside the screen reader.
        say(_("cannot revert the configuration from here"));
        return;
    }
    // Upstream's counts exactly: one press goes back to what is on disk, THREE
    // go back to the factory defaults. Two presses do nothing at all, which is
    // not an oversight -- it is the gap that keeps a double press from
    // wiping a configuration the user only wanted reloaded.
    const int count = repeatCount();
    if (count == 0) {
        ALUS_INFO("reverting to the saved configuration");
        context_.reset(/*factoryDefaults=*/false);
        // Translators: spoken when the configuration has been reloaded from
        // disk, undoing every change made since it was last saved.
        say(_("Configuration applied"));
        return;
    }
    if (count == 2) {
        ALUS_INFO("reverting to the factory defaults");
        context_.reset(/*factoryDefaults=*/true);
        // Translators: spoken when every setting has been returned to its
        // factory value.
        say(_("Configuration restored to factory defaults"));
    }
}

}  // namespace alus::config
