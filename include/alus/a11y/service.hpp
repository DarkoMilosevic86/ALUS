// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The object model as one restartable unit.
//
// Rule 3 of the phase plan: every subsystem is restartable in process. For the
// object model that means a SIGHUP re-reads what the user wants announced, and
// drops every object it was holding -- including the focus -- so that a
// reconfigured ALUS does not keep presenting objects by the old rules.
//
// It is also the one place where the four subsystems are tied together:
//
//   * speech  -- through PresentationHooks, which is the only way the object
//                model speaks (see object_speech.hpp);
//   * input   -- the object model supplies the focus, the focus ancestors and
//                the app module to the script dispatch context (phase 3's
//                handover, item 2), contributes its own global commands as
//                step 10, and calls refreshGrabs() on every focus change,
//                which phase 3 asked for explicitly (item 1, O6);
//   * core    -- the pump calls pumpEvents() once per cycle, which is where
//                the limiter's second stage runs.
#pragma once

#include "alus/a11y/atspi_backend.hpp"
#include "alus/a11y/commands.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/config/config.hpp"
#include "alus/core/loop.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace alus::core {
class Loop;
}

namespace alus::input {
class Service;
}

namespace alus::speech {
class Presenter;
}

namespace alus::a11y {

struct A11yServiceOptions {
    // `--no-a11y`: run with no object model at all. The same idea as
    // `--no-speech` and `--no-input`, and what the daemon smoke test uses so
    // that it never touches the session's accessibility bus.
    bool noA11y = false;
    // A backend other than libatspi. Tests pass a mock here; the daemon never
    // does.
    Backend* backend = nullptr;
};

// How well the object model is working, for the start-up announcement. A
// screen reader that cannot read the desktop must say so (the lesson of N7,
// N8 and phase 3's start-up diagnosis).
enum class A11yHealth {
    Working,
    Disabled,
    // No accessibility bus: at-spi2-registryd is not running, or
    // org.gnome.desktop.interface toolkit-accessibility is false.
    NoBus,
    // The bus is there but the event subscription was refused.
    NoEvents,
};

const char* a11yHealthName(A11yHealth health);

class Service final : public config::Subsystem {
public:
    using Options = A11yServiceOptions;

    // How the object model finds its voice. A FUNCTION, not a pointer:
    // speech is initialised before the object model and re-initialised on
    // SIGHUP, so the presenter that exists at construction time is not
    // necessarily the one that exists at initialize() time. Returning null
    // (or passing an empty function) is the --no-speech case, and the object
    // model then runs silently, which is what the tests do.
    using PresenterProvider = std::function<speech::Presenter*()>;

    // `input` may be null (--no-input): the object model then runs without
    // keys, which is what the daemon smoke test does.
    Service(core::Loop& loop, config::Manager& config, input::Service* input,
            PresenterProvider presenter, Options options = {});
    ~Service() override;

    std::string_view name() const override { return "a11y"; }
    void initialize() override;
    void terminate() override;

    bool isRunning() const noexcept { return running_; }
    A11yHealth health() const noexcept { return health_; }
    // One line for the start-up log and for `alus-cli a11y-info`.
    std::string describe() const;

    // Null before initialize() and after terminate().
    Registry* registry() noexcept { return registry_.get(); }
    Api* api() noexcept { return registry_ != nullptr ? &registry_->api() : nullptr; }
    ObjectCommands* commands() noexcept { return commands_.get(); }

    static void registerDefaults(config::Manager& config);

    // Re-reads this subsystem's own configuration section, in place.
    //
    // Public as of phase 9: the settings window changes one setting at a time
    // and must not call resetConfiguration for it -- changing the speech rate
    // would otherwise take the braille display and every app module down and
    // bring them back, which the user hears as the screen reader stuttering.
    // Rule 3 of phase-plan.md read narrowly: restartable in place, and the
    // smallest place that will do.
    void applyConfiguration();

private:

    core::Loop& loop_;
    config::Manager& config_;
    input::Service* input_;
    PresenterProvider presenter_;
    Options options_;

    std::unique_ptr<AtspiBackend> ownedBackend_;
    std::unique_ptr<Registry> registry_;
    std::unique_ptr<ObjectCommands> commands_;
    core::Loop::HandlerId pumpHandler_ = 0;
    // The dispatch-context contribution, so terminate() removes exactly its
    // own and leaves the text layer's alone (phase 5 made this a list).
    std::uint64_t contextContributor_ = 0;
    A11yHealth health_ = A11yHealth::Disabled;
    bool running_ = false;
};

}  // namespace alus::a11y
