// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The part of NVDA's `globalCommands` that is about the CONFIGURATION itself:
//
//     alus+control+c    save the configuration
//     alus+control+r    once:  go back to the saved configuration
//                       three times: back to the factory defaults
//
// WHY THESE TWO MATTER MORE THAN THEIR SIZE SUGGESTS. Every other command
// that changes a setting -- the synthesizer settings ring, the symbol level,
// the typing echo, the braille toggles -- writes into the configuration IN
// MEMORY and not to disk, upstream and here. That is deliberate: a user trying
// out voices must be able to get the old one back. It is also what makes
// `alus+control+c` the only way to keep any of it, and what made its absence
// (docs/history/phase-9.md N99's sibling) a screen reader whose settings silently
// reverted at every restart.
//
// `revertConfiguration` is the other half of the same promise: a setting a
// user cannot undo is one they cannot safely try.
#pragma once

#include "alus/script/scriptable.hpp"

#include <functional>
#include <string>

namespace alus::script {
class Dispatcher;
}

namespace alus::config {

class Manager;

// At namespace scope, for the usual reason (a nested class's default member
// initializers cannot be used in a default argument of the enclosing class).
struct CommandsContext {
    Manager* manager = nullptr;
    // NVDA: core.resetConfiguration. Supplied by the daemon, which is the only
    // place that owns every subsystem; null elsewhere, and then
    // `revertConfiguration` SAYS it cannot rather than half-doing it.
    std::function<void(bool factoryDefaults)> reset;
    // Repeat counting (`alus+control+r` three times). Null outside the daemon,
    // and then every press is a first press.
    // Asked for on every press, not held: the input service creates its
    // dispatcher when it starts -- AFTER the daemon builds this context -- and
    // again on every restart. A held pointer was null for the whole life of
    // the daemon, so every second press counted as a first (phase-10.md N122).
    std::function<const script::Dispatcher*()> dispatcher;
    // ui::message by default; injected so the tests can read what was said.
    std::function<void(const std::string&)> message;
};

class Commands final : public script::ScriptableObject {
public:
    using Context = CommandsContext;

    Commands(Context context, const script::Catalog* catalog);

private:
    void saveConfiguration(input::Gesture& gesture);
    void revertConfiguration(input::Gesture& gesture);

    void say(const std::string& text) const;
    int repeatCount() const;

    Context context_;
};

}  // namespace alus::config
