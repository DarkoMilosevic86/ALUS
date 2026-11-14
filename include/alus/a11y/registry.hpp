// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The object model's hub: the event stream in, objects out.
//
// What belongs here (01-ARHITEKTURA.md §6.1, §3.4, §3.3):
//
//   * the subscription to the platform's events, with `properties`;
//   * the two-stage event limiter, which is NOT optional -- Chromium loading a
//     page and LibreOffice scrolling a table flood exactly as winEvents do;
//   * the translation from AT-SPI event names to ALUS ones;
//   * the object cache, which is what makes object identity stable;
//   * the four levels of event dispatch, with the object itself as level 4;
//   * NVDA's `doPreGainFocus`: the focus, its ancestor chain, the foreground
//     window, and the focusEntered/focusExited events that follow from them.
//
// What deliberately does NOT belong here: speech. The object model reaches the
// presentation layer only through `PresentationHooks`, the way phase 3's input
// layer reaches speech. That is what lets every test in tests/a11y assert on
// what WOULD be said without a synthesizer, and it keeps the dependency
// one-way.
#pragma once

#include "alus/a11y/api.hpp"
#include "alus/a11y/backend.hpp"
#include "alus/a11y/events.hpp"
#include "alus/a11y/object.hpp"
#include "alus/a11y/output_reason.hpp"
#include "alus/a11y/overlay_registry.hpp"
#include "alus/a11y/settings.hpp"
#include "alus/core/event_dispatcher.hpp"
#include "alus/core/event_limiter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace alus::core {
class Loop;
}

namespace alus::a11y {

// Which properties changed, for OutputReason::Change.
struct PropertyChange {
    bool name = false;
    bool value = false;
    bool description = false;
    bool states = false;
};

// How the object model speaks, without knowing what speech is.
struct PresentationHooks {
    // NVDA: speech.speakObject.
    std::function<void(Object&, OutputReason)> speakObject;
    // NVDA: speech.speakObjectProperties with reason CHANGE.
    std::function<void(Object&, const PropertyChange&, OutputReason)> speakPropertyChange;
    // NVDA: ui.message.
    std::function<void(const std::string&)> message;
    // NVDA: speech.speakSpelling. "Press twice to spell" is the one thing
    // every object-query command does, so it is a hook of its own. Phase 5
    // made it the real path: punctuation named, pitch raised for a capital.
    std::function<void(const std::string&)> spell;
    // The THIRD press: each character described ("a, Avala") rather than
    // merely spelled. NVDA: speakSpelling(useCharacterDescriptions=True).
    std::function<void(const std::string&)> describeCharacters;
    // A progress bar's update: a beep, a spoken percentage, or both. The
    // behaviour decides which, from the configuration.
    std::function<void(double percentage, bool beep, bool speak)> reportProgress;
    // NVDA: speech.cancelSpeech.
    std::function<void()> cancelSpeech;
};

// At namespace scope for the usual reason: default member initializers in a
// default argument (the same reason input::InputCommandsContext is out here).
struct RegistryOptions {
    // The per-application cap of the first limiter stage (NVDA:
    // MAX_WINEVENTS_PER_THREAD).
    std::size_t maxEventsPerApplication = 10;
    std::size_t maxFocusEvents = 4;
    // Follow the mouse. Off by default: it costs an accessibleAtPoint call per
    // mouse move, and nobody has asked for it yet.
    bool trackMouse = false;
};

class Registry {
public:
    using Options = RegistryOptions;

    Registry(core::Loop& loop, Backend& backend, Options options = {});
    ~Registry();

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    // Subscribes to the platform and takes the desktop object. Returns false
    // with `error` filled when the platform refuses -- which the daemon SAYS
    // OUT LOUD, for the same reason phase 3 announces a refused keyboard.
    bool start(std::string* error);
    void stop();
    bool isRunning() const noexcept { return running_; }

    Backend& backend() noexcept { return backend_; }
    OverlayRegistry& overlays() noexcept { return overlays_; }
    Api& api() noexcept { return api_; }
    const Api& api() const noexcept { return api_; }
    core::EventDispatcher& dispatcher() noexcept { return dispatcher_; }
    core::EventLimiter& limiter() noexcept { return limiter_; }
    // What the user wants announced. Shared by the behaviours and by the
    // presentation path, and re-read on resetConfiguration().
    Settings& settings() noexcept { return settings_; }
    const Settings& settings() const noexcept { return settings_; }

    PresentationHooks& presentation() noexcept { return presentation_; }
    const PresentationHooks& presentation() const noexcept { return presentation_; }

    // ---- objects ----
    // The object for this accessible, created once and then reused, so that
    // object identity is stable for as long as anyone holds the object.
    // `objectFor` adds its own reference; `adoptObject` takes the caller's.
    ObjectPtr objectFor(Ref ref);
    ObjectPtr adoptObject(Ref ref);
    ObjectPtr desktop();
    std::size_t cachedObjectCount() const;
    // Drops the cache entries whose objects are gone. Called at the end of
    // each pump cycle; public for the tests.
    void sweepObjectCache();
    // Lets go of the objects this cycle touched.
    //
    // WHY THE REGISTRY HOLDS THEM AT ALL, and why this is called at the START
    // of a cycle rather than the end: an object's tree properties are
    // `Cached<ObjectPtr>`, so invalidating them drops references. If one of
    // those was the last reference, the object -- and with it its CacheScope
    // -- would be destroyed in the middle of core::invalidateAllCaches(),
    // which §3.1 forbids outright (see the comment on that function: "neither
    // create nor destroy scopes"). Holding every object touched in a cycle
    // and letting go at the start of the next one keeps that contract, and
    // gives the object model NVDA's own lifetime: an object stays alive for as
    // long as the cycle that mentioned it.
    void releaseCycleObjects();
    std::size_t cycleObjectCount() const noexcept { return cycleObjects_.size(); }
    // Lets go of EVERY object, breaking the parent/children reference rings
    // first. Called by stop() and by the destructor; see the comment on the
    // definition, which is the one piece of lifetime bookkeeping in the object
    // model that cannot be left to shared_ptr.
    void releaseObjects();

    // ---- events ----
    // What the platform calls, directly or through the backend's sink. Returns
    // false when the limiter discarded the event outright.
    bool onPlatformEvent(PlatformEvent event);
    // Queues an already-translated event. For tests and for the events ALUS
    // generates itself (focusEntered).
    void queueEvent(std::string name, const ObjectPtr& object,
                    std::shared_ptr<EventPayload> payload = nullptr);
    // Second limiter stage plus dispatch. Called once per pump cycle; returns
    // how many events were dispatched.
    std::size_t pumpEvents();
    // An object whose events are exempt from the per-application budget, as
    // the focus and its ancestors are: the one an app module is speaking for
    // while the focus is elsewhere (gnome-shell's selected search result,
    // N121). One at a time; null clears it.
    void setWatchedObject(ObjectPtr object) { watched_ = std::move(object); }
    // Notified AFTER an event has been through the whole chain, with the
    // object it was about.
    //
    // WHY A SEPARATE EXTENSION POINT AND NOT A FIFTH CHAIN LEVEL. The four
    // levels of `core::EventLevel` are a chain: a handler may stop the ones
    // below it, and that is what makes an app module able to override the
    // object model. A consumer that only WATCHES -- braille (phase 7), the
    // vision layer, a diagnostic log -- must not be able to do that, and must
    // run after the chain rather than inside it, so that the display shows
    // what speech has just said and not what it is about to.
    //
    // NVDA has no equivalent: there, `NVDAObject.event_gainFocus` calls
    // `braille.handler.handleGainFocus` itself, which is why braille, vision
    // and speech are tangled together in `NVDAObjects/__init__.py`. One
    // extension point in one place is the same behaviour without the tangle.
    core::Action<const core::Event&, const ObjectPtr&>& eventDispatched() noexcept {
        return eventDispatched_;
    }
    std::uint64_t dispatchedCount() const noexcept { return dispatched_; }

    // ---- focus ----
    // NVDA: eventHandler.doPreGainFocus. Everything that must happen before a
    // focus event is announced: the focus and its ancestor chain, the
    // foreground window when the chain changed at the top, and a focusEntered
    // for every new ancestor. Returns false when the object turned out to be
    // unusable, which makes the limiter fall back to an older focus event.
    bool handleGainFocus(const ObjectPtr& object);
    // A best-effort scan for the object that has the focus right now. AT-SPI
    // has no "who has the focus" call -- focus arrives as an event -- so at
    // start-up the tree is walked for the active window and its focused
    // descendant. Without this the first thing a user does after starting ALUS
    // (alus+tab) answers nothing.
    ObjectPtr findInitialFocus();
    // The active top-level window of the desktop, or null.
    ObjectPtr activeWindow();

    // Phase 8: the app module for an application. Null when no app layer is
    // running, which is the --no-app case and what every phase 4-7 test does.
    using AppModuleProvider = std::function<script::ScriptableObject*(const AppInfo&)>;
    void setAppModuleProvider(AppModuleProvider provider);
    script::ScriptableObject* appModuleFor(const AppInfo& info) const;

    // ---- the hooks phase 8's five customisation mechanisms need (§12.2) ----
    //
    // All four are single-slot and all four are OPTIONAL: with none of them
    // installed the object model behaves exactly as phase 4 left it. The app
    // layer installs them in one place (app::Service) and routes each to the
    // module that owns the object, which is why there is one slot and not a
    // chain -- an object belongs to exactly one application.

    // Mechanism 2: `event_NVDAObject_init`. Called once per object, right
    // after its overlay behaviours are chosen and its script class chain is
    // built, which is the moment NVDA calls it (and the only moment at which
    // an app module can still change what the object IS rather than what it
    // says).
    using ObjectInitHook = std::function<void(Object&)>;
    void setObjectInitHook(ObjectInitHook hook);

    // Generalised hook 1: `shouldAllowFocusEvent`. Consulted before a focus
    // event is acted on; false drops it, and the limiter then falls back to
    // an older focus event exactly as it does for a dead object. This is how
    // an app module suppresses the spurious focus a toolkit sends (the LibreOffice
    // and Electron cases are both in src/app/modules/).
    using FocusEventFilter = std::function<bool(const ObjectPtr&)>;
    void setFocusEventFilter(FocusEventFilter filter);

    // Generalised hook 2: `lastQueuedFocusObject`. The object of the most
    // recently QUEUED focus event -- not the current focus, which is the
    // object of the most recently DISPATCHED one. The difference is the whole
    // point: an app module that sends two focus events for one move detects
    // the duplicate by comparing against this, before the second one is
    // announced over the first.
    const ObjectPtr& lastQueuedFocusObject() const noexcept { return lastQueuedFocus_; }

    // Generalised hook 3: `container` override. Consulted by
    // Object::container(); a null answer leaves the parent in place. Reshapes
    // the focus ancestry without touching the platform's tree, which is what
    // a toolkit that puts a cell's text inside two layout panes needs.
    using ContainerOverride = std::function<ObjectPtr(const Object&)>;
    void setContainerOverride(ContainerOverride override);
    ObjectPtr containerFor(const Object& object) const;

    // Called after every focus change, with the keyboard grabs in mind: phase
    // 3's handover asks for refreshGrabs() here, because an object's scripts
    // exist but their keys are not grabbed until something asks for them (O6).
    void setFocusChangedHook(std::function<void()> hook);

    // Phase 5's review cursor: the review position is rebuilt from the
    // navigator object, so it has to know when that object changes. The
    // `becomeNavigatorObject` event carries the same news, but the event chain
    // levels 1-3 belong to plugins, app modules and tree interceptors
    // (phases 6 and 8), and the text layer is none of those -- so it gets a
    // hook, exactly as the input layer got one for the grabs (O6).
    using NavigatorChangedHook = std::function<void(const ObjectPtr&, bool isFocus)>;
    void setNavigatorChangedHook(NavigatorChangedHook hook);
    void notifyNavigatorChanged(const ObjectPtr& object, bool isFocus) const;

    const Options& options() const noexcept { return options_; }

private:
    void installDispatchProviders();
    // Primes an object's property cache from the values AT-SPI delivered with
    // the event. See the comment in registry.cpp: the window in which those
    // values exist is the callback itself.
    void primeFromEvent(const ObjectPtr& object);

    core::Loop& loop_;
    Backend& backend_;
    Options options_;

    OverlayRegistry overlays_;
    Settings settings_;
    core::EventLimiter limiter_;
    core::EventDispatcher dispatcher_;
    Api api_;
    PresentationHooks presentation_;
    AppModuleProvider appModuleProvider_;
    ObjectInitHook objectInit_;
    FocusEventFilter focusEventFilter_;
    ObjectPtr watched_;
    ContainerOverride containerOverride_;
    // Strong, and deliberately: it is compared against by identity, and an
    // object that died between the queue and the dispatch is exactly the case
    // the comparison has to answer -- a weak pointer would have expired by
    // then and the duplicate would get through. Held like api_'s focus, for as
    // long as it is the answer, and let go in releaseObjects().
    ObjectPtr lastQueuedFocus_;
    std::function<void()> focusChanged_;
    NavigatorChangedHook navigatorChanged_;
    core::Action<const core::Event&, const ObjectPtr&> eventDispatched_;

    // Ref -> object. Weak, so that an object nobody holds goes away with its
    // platform reference; the registry is a cache, not an owner.
    std::unordered_map<Ref, std::weak_ptr<Object>> objects_;
    // The objects this cycle touched; see releaseCycleObjects().
    std::vector<ObjectPtr> cycleObjects_;

    bool running_ = false;
    std::uint64_t dispatched_ = 0;
};

}  // namespace alus::a11y
