// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The app modules ALUS ships with, and the one function that registers them.
//
// phase-plan.md's phase 8 table. Each one exists because a MEASURED behaviour of
// that application needs it, and each file in src/app/modules/ says which; a
// module that cannot name what it fixes does not belong here, because every
// module is a layer between the user and what the application really said.
//
// The browsers are deliberately absent. §9.4 and the phase 6 measurements say
// the overlay in the object layer is enough for Firefox and Chromium -- the
// role map folds their document roles together, `Collection` answers quick
// navigation in one call, and `BrowseMode::isDocumentRoot` recognises both
// without knowing their names. A Firefox module would be a file with nothing
// in it.
#pragma once

#include "alus/app/registry.hpp"

namespace alus::app {

// Registers every built-in rule, in the order "most specific last" so that the
// registry's newest-wins tie-break keeps the more specific one. Returns how
// many rules were added.
std::size_t registerBuiltinModules(Registry& registry);

// The individual registrations, exposed so that a test can install exactly
// one rule and nothing else -- which is what makes a test of the LibreOffice
// module a test of that module.
void registerLibreOfficeModule(Registry& registry);
void registerTerminalModules(Registry& registry);
void registerElectronModule(Registry& registry);
void registerNautilusModule(Registry& registry);
void registerGnomeShellModule(Registry& registry);

}  // namespace alus::app
