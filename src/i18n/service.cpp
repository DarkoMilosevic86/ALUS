// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/i18n/service.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"

namespace alus::i18n {

Service::Service(config::Manager& manager, std::string commandLineLanguage)
    : manager_(manager), commandLineLanguage_(std::move(commandLineLanguage)) {}

void Service::registerDefaults(config::Manager& manager) {
    // "" and not "en": an empty value means "follow the session", which is
    // what a user who never opens the language setting wants. "en" as the
    // default would make a Serbian desktop speak English until the user found
    // the setting.
    manager.setDefault("general", "language", "");
}

void Service::initialize() {
    Options options;
    options.language = !commandLineLanguage_.empty()
                           ? commandLineLanguage_
                           : manager_.getString("general", "language", "");
    const std::string& effective = i18n::initialize(options);
    ALUS_DEBUG("i18n: language {}, catalogue {}", effective,
               haveCatalog() ? "loaded" : "not needed or not found");
}

void initializeFromEnvironment() { i18n::initialize(Options{}); }

}  // namespace alus::i18n
