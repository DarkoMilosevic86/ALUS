// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The input subsystem as one restartable unit.
//
// Rule 3 of the phase plan: every subsystem is restartable in process. For
// input that is not a nicety — the keyboard layout, the ALUS key and the
// gesture maps all change at run time, and each change means a different grab
// set in the compositor. A user who edits `gestures.ini` must get the new keys
// without restarting the screen reader.
//
// It is also where the two safety measures phase 0 demanded are wired up:
//
//   * the watchdog releases every grab when it detects a freeze, BEFORE it
//     tries to recover (N19 measure 2);
//   * the backend's health is reported out loud at start-up, because the
//     failure mode this interface has is a screen reader that looks healthy
//     and answers no key at all (N7, N8).
#pragma once

#include "alus/config/config.hpp"
#include "alus/input/a11y_manager_backend.hpp"
#include "alus/input/commands.hpp"
#include "alus/input/keymap.hpp"
#include "alus/input/manager.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/dispatch.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace alus::core {
class Loop;
class Watchdog;
}  // namespace alus::core

namespace alus::input {

struct InputServiceOptions {
    // `--no-input`: the whole pipeline runs with no keyboard attached. The
    // same idea as `--no-speech`, and what the daemon smoke test uses.
    bool noInput = false;
    // Empty means the usual search (core::findDataDirectory).
    std::filesystem::path dataDir;
    // Overrides [input] appId. The D-Bus name decides whether the compositor
    // lets ALUS near the keyboard at all (R1).
    std::string appId;
    // Where the user's own gestures.ini lives; empty means the config dir.
    std::filesystem::path userGestureMap;
};

class Service final : public config::Subsystem {
public:
    using Options = InputServiceOptions;

    Service(core::Loop& loop, config::Manager& config, GestureManager::Hooks hooks,
            Options options = {});
    ~Service() override;

    std::string_view name() const override { return "input"; }
    void initialize() override;
    void terminate() override;

    bool isRunning() const noexcept { return running_; }

    // Null before initialize() and after terminate().
    GestureManager* manager() noexcept { return manager_.get(); }
    script::Dispatcher* dispatcher() noexcept { return dispatcher_.get(); }
    IInputBackend* backend() noexcept { return backend_.get(); }
    InputCommands* commands() noexcept { return commands_.get(); }
    script::Catalog& catalog() noexcept { return catalog_; }
    const KeyMap& keymap() const noexcept { return keymap_; }

    BackendHealth health() const;
    // One line, for the start-up log and for `alus-cli input-info`. Not const:
    // it reports the grab set, and computing that asks the dispatcher what
    // would resolve right now.
    std::string describe();

    // Lets a later subsystem fill in the parts of the dispatch context it
    // owns: phase 4 supplies the focus, the focus ancestors, the app module
    // and its own global commands (phase 3's handover, item 2). Called on
    // every gesture, so it must be cheap: it reads already-cached state.
    // The parts of the dispatch context other subsystems own (phase 3's
    // handover, item 2).
    //
    // A LIST, not one function, and phase 5 is why: the object model fills in
    // the focus and its own command group, and the text layer adds the review
    // commands. With a single slot the second registration silently removed
    // the first, and the symptom would have been a numpad that works until
    // the first SIGHUP.
    using ContextContributorId = std::uint64_t;
    ContextContributorId addContextContributor(
        std::function<void(script::DispatchContext&)> contributor);
    bool removeContextContributor(ContextContributorId id);
    // Replaces every contributor with this one (or none). Kept because the
    // tests build a context by hand.
    void setContextContributor(std::function<void(script::DispatchContext&)> contributor);

    // Lets the watchdog drop every grab on a freeze (N19). Safe to call with
    // null, and safe to call from the watchdog's own thread.
    void attachWatchdog(core::Watchdog* watchdog);

    static void registerDefaults(config::Manager& config);

    // Reads the [keyboard] section into a KeyboardConfig.
    static KeyboardConfig keyboardConfigFrom(const config::Manager& config);

    // Re-reads this subsystem's own configuration section, in place.
    //
    // Public as of phase 9: the settings window changes one setting at a time
    // and must not call resetConfiguration for it -- changing the speech rate
    // would otherwise take the braille display and every app module down and
    // bring them back, which the user hears as the screen reader stuttering.
    // Rule 3 of 02-FAZE.md read narrowly: restartable in place, and the
    // smallest place that will do.
    void applyConfiguration();

private:
    void loadMaps();

    core::Loop& loop_;
    config::Manager& config_;
    GestureManager::Hooks hooks_;
    Options options_;
    std::filesystem::path dataDir_;

    script::Catalog catalog_;
    KeyMap keymap_;
    std::unique_ptr<script::Dispatcher> dispatcher_;
    std::unique_ptr<GestureManager> manager_;
    std::unique_ptr<IInputBackend> backend_;
    std::unique_ptr<InputCommands> commands_;
    // The commands ALUS has not written yet, bound to an announcement rather
    // than left silent (script/command_status.hpp). Consulted LAST in step
    // 10, after every subsystem's real commands, so it can shadow nothing.
    std::unique_ptr<script::ScriptableObject> missing_;
    struct ContextContributor {
        ContextContributorId id = 0;
        std::function<void(script::DispatchContext&)> function;
    };
    std::vector<ContextContributor> contextContributors_;
    ContextContributorId nextContributorId_ = 0;
    core::Watchdog* watchdog_ = nullptr;
    bool running_ = false;
};

}  // namespace alus::input
