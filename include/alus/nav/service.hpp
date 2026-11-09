// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The text and review layer as one restartable unit.
//
// Rule 3 of the phase plan again (see a11y/service.hpp): every subsystem is
// restartable in process. For this layer that means a SIGHUP re-reads the
// document formatting settings, the review cursor settings and the caret
// timeout, and drops the review position -- a reconfigured ALUS must not keep
// reviewing by the old rules.
//
// It is also the seam where phase 5 reaches everything it needs without
// anything reaching back:
//
//   * the object model -- the focus, the navigator object, the events;
//   * speech          -- `Presenter`, for the text, the spelling and say-all;
//   * input           -- the review commands as another step-10 command group,
//                        and the say-all stop-on-key-press hook, which is the
//                        last empty hook phase 3 left behind
//                        (`isSayAllRunning`, FAZA4.md handover point 5);
//   * core            -- the task registry, where the caret wait and say-all
//                        live, one step per pump cycle.
//
// WHAT IT REGISTERS WITH THE OBJECT MODEL: the `EditableText`, `LiveText` and
// `Terminal` behaviours, through an overlay chooser. Phase 4 built the chooser
// mechanism; this is its first user from outside the object model, which is
// what §6.2's "composition" was for.
#pragma once

#include "alus/a11y/behavior.hpp"
#include "alus/a11y/object.hpp"
#include "alus/a11y/output_reason.hpp"
#include "alus/a11y/settings.hpp"
#include "alus/a11y/text_speech.hpp"
#include "alus/config/config.hpp"
#include "alus/core/event_dispatcher.hpp"
#include "alus/core/extension_points.hpp"
#include "alus/nav/editable_text.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/tree_interceptor.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace alus::core {
class Loop;
class TaskRegistry;
}

namespace alus::a11y {
class Registry;
}

namespace alus::input {
class Service;
}

namespace alus::speech {
class Presenter;
}

namespace alus::nav {

class SayAll;
class ScreenExplorer;
class TextCommands;

struct NavServiceOptions {
    // `--no-nav`: no review cursor, no say-all, no caret reporting. Used by
    // the daemon smoke test and by anyone bisecting a problem.
    bool noNav = false;
};

class Service final : public config::Subsystem {
public:
    using Options = NavServiceOptions;

    // BOTH the registry and the presenter are PROVIDERS, not pointers, and
    // for the same reason a11y::Service takes a presenter provider: this
    // service is constructed before the object model has started, and both are
    // re-created on SIGHUP. A provider that returns null is the --no-a11y and
    // --no-speech case, and the text layer then does nothing at all.
    using PresenterProvider = std::function<speech::Presenter*()>;
    using RegistryProvider = std::function<a11y::Registry*()>;

    Service(core::Loop& loop, config::Manager& config, RegistryProvider registry,
            input::Service* input, PresenterProvider presenter, Options options = {});
    ~Service() override;

    std::string_view name() const override { return "nav"; }
    void initialize() override;
    void terminate() override;

    bool isRunning() const noexcept { return running_; }
    std::string describe() const;

    // ---- what the behaviours and the commands use ----
    core::Loop& loop() noexcept { return loop_; }
    // May be null (--no-input): the text layer then has no keys, which the
    // daemon smoke test relies on.
    input::Service* inputService();
    core::TaskRegistry& tasks();
    a11y::Registry* registry();
    speech::Presenter* presenter();
    Review* review() noexcept { return review_.get(); }
    // Phase 6. Null with --no-nav, like everything else here.
    TreeInterceptorRegistry* interceptors() noexcept { return interceptors_.get(); }
    SayAll* sayAll() noexcept { return sayAll_.get(); }
    ScreenExplorer* explorer() noexcept { return explorer_.get(); }
    TextCommands* commands() noexcept { return commands_.get(); }
    // The command catalogue, or null with --no-input. Whatever this layer
    // creates that owns scripts has to be given it, or those scripts have no
    // description -- and a script with no description is hidden from the
    // gesture dialog and nameless in input help (T9.3, and N101).
    const script::Catalog* catalog() const;

    const a11y::Settings& settings() const;
    const text::FormatReportSettings& formatSettings() const;

    // The three things every caret and review command does, in one place so
    // that they are done the same way everywhere.
    //
    // NVDA: speech.speakTextInfo with the state cached on the object.
    bool speakRange(const text::TextRange& range, std::optional<text::TextUnit> unit,
                    a11y::OutputReason reason, const a11y::ObjectPtr& object = nullptr,
                    bool suppressBlanks = false);
    // NVDA: ui.message.
    void message(const std::string& text);

    // Rebuilds the compositor key grabs. PHASE 6 NEEDS THIS AND PHASE 4's
    // HOOK IS NOT ENOUGH: `Registry::handleGainFocus` refreshes the grabs and
    // THEN dispatches `gainFocus`, which is the event that creates the tree
    // interceptor -- so the grab set computed there does not yet know the
    // document's scripts exist. Without this call the FIRST web page a user
    // focuses answers none of the quick-navigation letters, and the second one
    // does, which is the most confusing failure a screen reader can have.
    //
    // It is also what makes focus mode real: `computeGrabs` asks the ten-step
    // lookup whether each gesture would find a script here and now, and in
    // focus mode `passThrough` makes the answer no -- so the letters are
    // RELEASED back to the application rather than tested for inside each
    // script (T6.9).
    void refreshGrabs();
    // NVDA's "once says it, twice spells it, three times describes it" for a
    // piece of text rather than for an object name.
    void sayOrSpell(const std::string& text, int repeatCount,
                    const std::string& locale = {});

    // The two settings the user cycles with a key (NVDA:
    // `cycleParagraphStyle`, `toggleReportLineIndentation`). They live here
    // rather than in the command group because they change the SERVICE's own
    // settings and write them back to the configuration.
    void cycleParagraphStyle();
    void cycleLineIndentationReporting();

    // The per-object speech state (NVDA's `obj._speakTextInfoState`), kept
    // here rather than on the object because it must survive the one-cycle
    // property cache and because an object does not know what speech is.
    a11y::TextRangeSpeechState& speechStateFor(const a11y::Object& object);
    void forgetSpeechState(const a11y::Object& object);

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
    void installBehaviors();
    void installEventHandlers();
    // Phase 6: level 3 of the event chain (core::EventLevel::TreeInterceptor),
    // which is the level NVDA runs a tree interceptor's handlers at and the
    // one phase 1 left empty for exactly this.
    void installInterceptorHandlers();
    void handleInterceptorEvent(const core::Event& event);

    core::Loop& loop_;
    config::Manager& config_;
    RegistryProvider registryProvider_;
    // Resolved at initialize() and held for the service's run, so that every
    // command and behaviour sees the same registry it was installed into.
    a11y::Registry* registry_ = nullptr;
    input::Service* input_;
    PresenterProvider presenter_;
    Options options_;

    std::unique_ptr<Review> review_;
    std::unique_ptr<TreeInterceptorRegistry> interceptors_;
    std::unique_ptr<SayAll> sayAll_;
    std::unique_ptr<ScreenExplorer> explorer_;
    std::unique_ptr<TextCommands> commands_;
    // Keyed by the platform reference, which is object identity (§6.2).
    std::map<a11y::Ref, a11y::TextRangeSpeechState> speechStates_;
    core::HandlerHandle overlayChooser_;
    core::HandlerHandle keyHandler_;
    std::uint64_t contextContributor_ = 0;
    a11y::Settings settings_;
    CaretDetectionSettings caretSettings_;
    bool running_ = false;
};

}  // namespace alus::nav
