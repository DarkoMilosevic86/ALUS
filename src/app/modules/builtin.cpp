// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// One list, in one place: the app modules a stock ALUS has.
//
// Separate from the modules themselves so that the question "what does ALUS
// adjust, and for which applications" has a single answer a reader can check
// against the phase 8 table of 02-FAZE.md -- and so that a test can register
// one module and nothing else.
#include "alus/app/modules.hpp"

namespace alus::app {

std::size_t registerBuiltinModules(Registry& registry) {
    const std::size_t before = registry.ruleCount();
    // Order is "least specific first", so that the registry's newest-wins
    // tie-break leaves the more specific rules on top. The toolkit-only
    // Electron rule therefore goes FIRST: an executable rule for a program
    // that happens to be Electron (a future `code` rule) must beat it.
    registerElectronModule(registry);
    registerTerminalModules(registry);
    registerNautilusModule(registry);
    registerGnomeShellModule(registry);
    registerLibreOfficeModule(registry);
    return registry.ruleCount() - before;
}

}  // namespace alus::app
