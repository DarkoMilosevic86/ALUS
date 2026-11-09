// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/keymap.hpp"

#include "alus/core/log.hpp"

#include <gio/gio.h>
#include <glib.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>

namespace alus::input {
namespace {

constexpr const char* kInputSourcesSchema = "org.gnome.desktop.input-sources";

const char* environmentOr(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? value : fallback;
}

}  // namespace

KeyMap::KeyMap() = default;

KeyMap::~KeyMap() {
    if (keymap_ != nullptr) {
        xkb_keymap_unref(keymap_);
    }
    if (context_ != nullptr) {
        xkb_context_unref(context_);
    }
}

std::optional<std::pair<std::string, std::string>> KeyMap::layoutFromGSettings() {
    // GSettings schemas are not guaranteed to exist (KDE, a bare session, a
    // build container), and asking for a missing schema ABORTS the process in
    // GIO, so the schema source has to be interrogated first.
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (source == nullptr) {
        return std::nullopt;
    }
    GSettingsSchema* schema =
        g_settings_schema_source_lookup(source, kInputSourcesSchema, TRUE);
    if (schema == nullptr) {
        return std::nullopt;
    }
    g_settings_schema_unref(schema);

    GSettings* settings = g_settings_new(kInputSourcesSchema);
    GVariant* sources = g_settings_get_value(settings, "sources");
    std::optional<std::pair<std::string, std::string>> result;
    GVariantIter iter;
    g_variant_iter_init(&iter, sources);
    const gchar* type = nullptr;
    const gchar* id = nullptr;
    while (g_variant_iter_next(&iter, "(&s&s)", &type, &id) != 0) {
        if (type == nullptr || id == nullptr || g_strcmp0(type, "xkb") != 0) {
            continue;
        }
        // The id is "rs" or "rs+latin": layout, optionally layout+variant.
        std::string value(id);
        const std::size_t plus = value.find('+');
        if (plus == std::string::npos) {
            result = std::make_pair(value, std::string());
        } else {
            result = std::make_pair(value.substr(0, plus), value.substr(plus + 1));
        }
        break;  // the first source is the one in effect at start-up
    }
    g_variant_unref(sources);
    g_object_unref(settings);
    return result;
}

bool KeyMap::load(KeyMapOptions options, std::string* error) {
    keycodeOffset_ = options.keycodeOffset;
    if (options.layout.empty()) {
        if (const char* override = std::getenv("ALUS_XKB_LAYOUT");
            override != nullptr && *override != '\0') {
            options.layout = override;
        } else if (auto fromSettings = layoutFromGSettings()) {
            options.layout = fromSettings->first;
            if (options.variant.empty()) {
                options.variant = fromSettings->second;
            }
        } else {
            options.layout = environmentOr("XKB_DEFAULT_LAYOUT", "us");
        }
    }

    if (context_ == nullptr) {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }
    if (context_ == nullptr) {
        if (error != nullptr) {
            *error = "xkb_context_new failed";
        }
        return false;
    }

    const xkb_rule_names names{
        .rules = nullptr,
        .model = options.model.empty() ? nullptr : options.model.c_str(),
        .layout = options.layout.c_str(),
        .variant = options.variant.empty() ? nullptr : options.variant.c_str(),
        .options = options.options.empty() ? nullptr : options.options.c_str(),
    };
    xkb_keymap* keymap =
        xkb_keymap_new_from_names(context_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == nullptr) {
        if (error != nullptr) {
            *error = "cannot compile keymap for layout '" + options.layout + "'";
        }
        return false;
    }
    if (keymap_ != nullptr) {
        xkb_keymap_unref(keymap_);
    }
    keymap_ = keymap;
    layout_ = options.layout;
    if (!options.variant.empty()) {
        layout_ += "+" + options.variant;
    }
    index();
    ALUS_INFO("keymap: layout '{}', {} keys, keycode offset {}", layout_, base_.size(),
              keycodeOffset_);
    return true;
}

void KeyMap::index() {
    base_.clear();
    shifted_.clear();
    baseToShifted_.clear();
    if (keymap_ == nullptr) {
        return;
    }
    const xkb_keycode_t min = xkb_keymap_min_keycode(keymap_);
    const xkb_keycode_t max = xkb_keymap_max_keycode(keymap_);
    for (xkb_keycode_t keycode = min; keycode <= max; ++keycode) {
        const xkb_keysym_t* syms = nullptr;
        // Layout 0 is the first (active at start-up) group; shift is level 1.
        const int levelOneCount =
            xkb_keymap_key_get_syms_by_level(keymap_, keycode, 0, 0, &syms);
        if (levelOneCount <= 0 || syms == nullptr) {
            continue;
        }
        const std::uint32_t baseSym = syms[0];
        base_[keycode] = baseSym;

        const xkb_keysym_t* shiftSyms = nullptr;
        const int levelTwoCount =
            xkb_keymap_key_get_syms_by_level(keymap_, keycode, 0, 1, &shiftSyms);
        if (levelTwoCount > 0 && shiftSyms != nullptr && shiftSyms[0] != baseSym) {
            shifted_[keycode] = shiftSyms[0];
            baseToShifted_.emplace(baseSym, shiftSyms[0]);
        }
    }
}

std::optional<std::uint32_t> KeyMap::baseKeysym(std::uint32_t keycode) const {
    const auto it = base_.find(static_cast<std::uint32_t>(
        static_cast<int>(keycode) + keycodeOffset_));
    if (it == base_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::uint32_t> KeyMap::shiftedKeysym(std::uint32_t keycode) const {
    const auto it = shifted_.find(static_cast<std::uint32_t>(
        static_cast<int>(keycode) + keycodeOffset_));
    if (it == shifted_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::uint32_t> KeyMap::shiftedKeysymFor(std::uint32_t baseKeysymValue) const {
    const auto it = baseToShifted_.find(baseKeysymValue);
    if (it == baseToShifted_.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace alus::input
