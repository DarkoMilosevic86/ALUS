// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 4 test fixture: a loop, a mock platform, a registry, and a record
// of everything that WOULD have been said.
//
// The recording hooks run the real presentation code (getObjectSpeech,
// getPropertiesSpeech, processAndLabelStates) and keep the result as text, so
// a test can assert on the words a user would hear without a synthesizer, a
// sound device or speech-dispatcher anywhere near it. That is the point of
// Registry::PresentationHooks being a seam at all.
#pragma once

#include "alus/a11y/behaviors.hpp"
#include "alus/a11y/object_speech.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/cached.hpp"
#include "mock_backend.hpp"
#include "support/loop_fixture.hpp"

#include <string>
#include <vector>

namespace alus::testing {

// The text items of a sequence, joined with single spaces -- which is close
// enough to what a user hears for a test to read naturally. (The real pipeline
// joins chunks with TWO spaces, for the reason in presenter.hpp; that is the
// speech layer's business and phase 2 tests it.)
inline std::string joined(const speech::SpeechSequence& sequence) {
    std::string text;
    for (const speech::SpeechItem& item : sequence) {
        if (!item.isText()) {
            continue;
        }
        if (!text.empty()) {
            text += " ";
        }
        text += item.text();
    }
    return text;
}

class A11yFixture : public LoopFixture {
public:
    struct SpokenObject {
        a11y::Ref ref = 0;
        a11y::OutputReason reason = a11y::OutputReason::Query;
        std::string text;
    };

    struct SpokenChange {
        a11y::Ref ref = 0;
        a11y::PropertyChange change;
        std::string text;
    };

    struct Progress {
        double percentage = 0.0;
        bool beep = false;
        bool speak = false;
    };

    A11yFixture() : registry(loop(), backend) {
        a11y::resetPresentationMemory();
        a11y::ProgressBarBehavior::clearCache();
        installHooks();
        installEventRecorder();
        // The same pump stage the daemon installs (a11y::Service), so a test
        // that runs the real loop gets the real path: platform events are
        // converted and dispatched in stage 1 of every cycle (§3.1).
        loop().addStageHandler(core::PumpStage::PlatformEvents, "a11y-test", [this] {
            registry.releaseCycleObjects();
            registry.pumpEvents();
            registry.sweepObjectCache();
        });
        // The registry subscribes to the (mock) platform and takes the
        // desktop object, exactly as the daemon does. Without this the mock's
        // emit() reaches nobody, which is the shape of a test that passes
        // while measuring nothing.
        started = registry.start(&startError);
        // The registry legitimately holds the desktop object from here on, so
        // a leak test compares against this baseline rather than against zero.
        baselineRefs = backend.outstandingRefs();
        // start() read the tree (it looks for the initial focus), and a test
        // builds its tree afterwards. Ending that cycle here means the test
        // starts with empty property caches, the way a pump cycle does --
        // otherwise the desktop would still be remembered as having no
        // applications in it.
        core::invalidateAllCaches();
        clearRecords();
    }

    ~A11yFixture() {
        // The caches hold ObjectPtrs; clearing them here keeps one test's
        // objects out of the next one's reference counts.
        core::invalidateAllCaches();
    }

    void installHooks() {
        a11y::PresentationHooks& hooks = registry.presentation();
        hooks.speakObject = [this](a11y::Object& object, a11y::OutputReason reason) {
            spoken.push_back(SpokenObject{object.id(), reason,
                                          joined(a11y::getObjectSpeech(object, reason))});
        };
        hooks.speakPropertyChange = [this](a11y::Object& object,
                                           const a11y::PropertyChange& change,
                                           a11y::OutputReason reason) {
            a11y::PropertySelection selection;
            selection.name = change.name;
            selection.value = change.value;
            selection.description = change.description;
            selection.states = change.states;
            changes.push_back(SpokenChange{
                object.id(), change,
                joined(a11y::getObjectPropertiesSpeech(object, reason, selection))});
        };
        hooks.message = [this](const std::string& text) { messages.push_back(text); };
        hooks.spell = [this](const std::string& text) { spelled.push_back(text); };
        hooks.cancelSpeech = [this] { ++cancellations; };
        hooks.reportProgress = [this](double percentage, bool beep, bool speak) {
            progress.push_back(Progress{percentage, beep, speak});
        };
    }

    // Records every event that survives the limiter and reaches the dispatch
    // chain. It sits at level 1 (global plugins) and calls on, so the chain
    // runs exactly as it would in the daemon -- which is also a test of the
    // chain itself: a level that forgets to continue stops everything.
    void installEventRecorder() {
        registry.dispatcher().setChainProvider(
            core::EventLevel::GlobalPlugin,
            [this](const core::Event&) {
                std::vector<core::EventDispatcher::ChainHandler> handlers;
                handlers.push_back([this](const core::Event& event,
                                          const core::EventDispatcher::NextHandler& next) {
                    dispatched.push_back(DispatchedEvent{event.name, event.object});
                    next();
                });
                return handlers;
            });
    }

    struct DispatchedEvent {
        std::string name;
        core::ObjectId object = 0;
    };

    std::size_t countDispatched(std::string_view name) const {
        std::size_t count = 0;
        for (const DispatchedEvent& event : dispatched) {
            if (event.name == name) {
                ++count;
            }
        }
        return count;
    }

    bool wasDispatched(std::string_view name, core::ObjectId object) const {
        for (const DispatchedEvent& event : dispatched) {
            if (event.name == name && event.object == object) {
                return true;
            }
        }
        return false;
    }

    // Everything the user would have heard, in order.
    std::string lastSpoken() const { return spoken.empty() ? std::string{} : spoken.back().text; }
    std::string lastMessage() const {
        return messages.empty() ? std::string{} : messages.back();
    }

    void clearRecords() {
        dispatched.clear();
        spoken.clear();
        changes.clear();
        messages.clear();
        spelled.clear();
        progress.clear();
        cancellations = 0;
    }

    // One pump cycle: the limiter's second stage, the dispatch, and then the
    // cache invalidation that ends a cycle (§3.1). Tests call this instead of
    // running the loop, so no behaviour depends on wall-clock luck.
    std::size_t pumpOnce() {
        // The order the daemon uses: let go of the last cycle's objects, then
        // the limiter's second stage and the dispatch, then the cache
        // invalidation that ends a cycle (§3.1).
        registry.releaseCycleObjects();
        const std::size_t emitted = registry.pumpEvents();
        core::invalidateAllCaches();
        return emitted;
    }

    MockBackend backend;
    a11y::Registry registry;
    bool started = false;
    std::string startError;
    int baselineRefs = 0;

    // Platform references held beyond the ones the registry keeps for the
    // whole session (the desktop object). Zero after a cycle boundary.
    int extraRefs() const { return backend.outstandingRefs() - baselineRefs; }

    std::vector<DispatchedEvent> dispatched;
    std::vector<SpokenObject> spoken;
    std::vector<SpokenChange> changes;
    std::vector<std::string> messages;
    std::vector<std::string> spelled;
    std::vector<Progress> progress;
    int cancellations = 0;
};

// The tree the exit criteria talk about: one GTK4 application, one window with
// a "Save" button in it. Built by hand so that every property in T4.1 has a
// known answer.
struct ButtonTree {
    explicit ButtonTree(MockBackend& backend) {
        application = backend.addApplication("Text Editor", "gtk");
        window = backend.add(application, PlatformRole::Frame, "Untitled Document");
        backend.addState(window, PlatformState::Active);
        panel = backend.add(window, PlatformRole::Panel);
        button = backend.add(panel, PlatformRole::Button, "Save");

        MockNode& node = backend.node(button);
        node.description = "Save the current document";
        node.states |= platformStateBit(PlatformState::Focusable);
        node.actionNames = {"click"};
        // ATK's three-part key binding: mnemonic;full path;accelerator.
        node.actionBindings = {"s;;<Control>s"};
        node.extents = Rect{40, 60, 90, 32};
        node.interfaces |= Interface::Action;
    }

    Ref application = 0;
    Ref window = 0;
    Ref panel = 0;
    Ref button = 0;
};

}  // namespace alus::testing
