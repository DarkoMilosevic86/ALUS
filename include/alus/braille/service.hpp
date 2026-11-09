// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The braille subsystem as one restartable unit.
//
// Rule 3 of the phase plan, for the fifth time: EVERY SUBSYSTEM IS RESTARTABLE
// IN PROCESS. For braille that means a SIGHUP re-reads the table, the display
// driver, the tether and the word-wrap mode, reconnects to brltty and
// re-translates every region -- and a user who changes their braille table
// must not have to restart the screen reader to read in it.
//
// This is the composition root for braille: it owns `Louis`, the display, the
// `Handler`, the `BrailleInput` and the command group, and it is the ONLY file
// in `braille/` that knows the rest of ALUS exists. Everything below it takes
// what it needs as an interface or a hook, which is why `tests/braille` runs
// the whole layer with no display, no speech and no desktop.
//
// WHAT IT CONNECTS TO, and where each connection was left for it:
//
//   the object model   `Registry::eventDispatched` -- the extension point
//                      phase 7 added, fired after the chain so the display
//                      shows what speech has already said;
//   the text layer     `nav::Service` for the review cursor, the settings and
//                      the browse mode whose caret the display follows;
//   input              the driver gesture map (`GestureManager::driverMap`,
//                      the third global layer phase 3 built and left empty)
//                      and a step-10 command group, the fourth;
//   speech             `Presenter`, for `speakOnRouting`, for
//                      `speakOnNavigatingByUnit` and for braille's
//                      speech-output mode.
#pragma once

#include "alus/braille/commands.hpp"
#include "alus/braille/handler.hpp"
#include "alus/braille/input.hpp"
#include "alus/braille/louis.hpp"
#include "alus/braille/settings.hpp"
#include "alus/config/config.hpp"
#include "alus/core/extension_points.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace alus::a11y {
class Registry;
}

namespace alus::core {
class Loop;
}

namespace alus::input {
class Service;
}

namespace alus::nav {
class Service;
}

namespace alus::speech {
class Presenter;
}

namespace alus::braille {

struct ServiceOptions {
    // `--no-braille`: no translation, no display, no cost. Used by the daemon
    // smoke test and by anyone bisecting a problem.
    bool noBraille = false;
};

class Service final : public config::Subsystem {
public:
    using Options = ServiceOptions;
    using RegistryProvider = std::function<a11y::Registry*()>;
    using NavProvider = std::function<nav::Service*()>;
    using PresenterProvider = std::function<speech::Presenter*()>;

    // Providers rather than pointers, for the reason nav::Service takes them:
    // this service is constructed before the object model has started, and
    // every one of them is re-created on SIGHUP. A provider that answers null
    // is the --no-a11y / --no-nav / --no-speech case, and braille then shows
    // what it can -- which for the object model is nothing, and that is a
    // supported way to run.
    Service(core::Loop& loop, config::Manager& config, RegistryProvider registry,
            NavProvider nav, input::Service* input, PresenterProvider presenter,
            Options options = {});
    ~Service() override;

    std::string_view name() const override { return "braille"; }
    void initialize() override;
    void terminate() override;

    bool isRunning() const noexcept { return running_; }
    std::string describe() const;

    Handler* handler() noexcept { return handler_.get(); }
    BrailleInput* input() noexcept { return input_.get(); }
    Louis& louis() const noexcept { return *louis_; }
    const Settings& settings() const noexcept { return settings_; }
    // Writes a changed setting back to the configuration and re-applies it.
    void updateSettings(const Settings& settings);

    a11y::Registry* registry() const;
    nav::Service* nav() const;
    input::Service* inputService() const noexcept { return inputService_; }
    speech::Presenter* presenter() const;

    // NVDA: `braille.handler.message`, which `ui.message` also feeds.
    void message(const std::string& text);

    static void registerDefaults(config::Manager& config);

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
    void installEventHandlers();
    void installDisplayGestures();
    void onEvent(const core::Event& event, const a11y::ObjectPtr& object);
    // Drains the display's own keys and turns them into gestures. Runs in the
    // pump, before the braille stage, so a routing key is acted on in the same
    // cycle it arrived in.
    void pumpDisplayKeys();

    core::Loop& loop_;
    config::Manager& config_;
    RegistryProvider registryProvider_;
    NavProvider navProvider_;
    input::Service* inputService_;
    PresenterProvider presenterProvider_;
    Options options_;

    Louis* louis_ = nullptr;
    Settings settings_;
    std::unique_ptr<Handler> handler_;
    std::unique_ptr<BrailleInput> input_;
    std::unique_ptr<Commands> commands_;

    core::HandlerHandle eventHandle_;
    core::HandlerHandle messageHandle_;
    std::uint64_t contextContributor_ = 0;
    core::Loop::HandlerId keyPumpHandler_ = 0;
    bool running_ = false;
};

}  // namespace alus::braille
