// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `alus::app::Module` -- one application's adjustments.
//
// Port of NVDA's `appModuleHandler.AppModule` (01-ARHITEKTURA.md §12.1, §12.2).
// All five customisation mechanisms are here, with NVDA's own names, because
// the names are what someone porting an app module knows:
//
//   1. `chooseOverlayClasses(obj, clsList)`   -- what the object behaves like
//   2. `objectInit(obj)`                      -- NVDA's event_NVDAObject_init
//   3. `event_<name>(obj, nextHandler)`       -- intercept an event
//   4. gestures and scripts                   -- a Module IS a ScriptableObject
//   5. property overrides                     -- statusBar, productName, ...
//
// Plus the three generalised hooks §12.2 names as the ones real app modules
// cannot do without: `shouldAllowFocusEvent`, `lastQueuedFocusObject` (which
// lives on a11y::Registry, since it is a fact about the event stream rather
// than about one application) and an override of `container`.
//
// WHY A MODULE IS A ScriptableObject AND NOT A SET OF CALLBACKS. Step 3 of the
// ten-step script lookup is "the focus object's app module", and the lookup
// finds a script by asking a `ScriptableObject` what it answers to. A module
// that was not one would need a parallel mechanism for its keys, and a gesture
// map section written as
//
//     [appModules.soffice.AppModule]
//         reportCellHeaders = kb:alus+shift+c
//
// would reach nothing. Being one means the user can rebind a module's key in
// `gestures.ini` exactly like any other, and that is mechanism 4 in full.
//
// LIFETIME. A module is created when its application is first seen and lives
// until the application exits (`Registry::cleanup`). It therefore must NOT
// hold an `a11y::ObjectPtr`: that is the N49 rule, and an app module holding
// the object it was created for would keep a dead application's accessible
// alive for the session. It holds the process id and asks for objects.
//
// THREADING: main thread only (§13).
#pragma once

#include "alus/a11y/behavior.hpp"
#include "alus/a11y/object.hpp"
#include "alus/app/key.hpp"
#include "alus/config/config.hpp"
#include "alus/core/event_dispatcher.hpp"
#include "alus/script/scriptable.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace alus::a11y {
class Registry;
}

namespace alus::input {
class Service;
}

namespace alus::nav {
class Service;
}

namespace alus::app {

// What a module (or a plugin) may reach. Pointers the app layer is given and
// does not own, plus the two things that are functions because they belong to
// subsystems the app layer deliberately does not link against.
struct Services {
    a11y::Registry* a11y = nullptr;
    nav::Service* nav = nullptr;
    input::Service* input = nullptr;
    // A module's own settings live in the ordinary configuration, under
    // `[app]`, so that a profile can override them per application -- which is
    // the whole point of the profile trigger this layer activates.
    config::Manager* config = nullptr;
    // NVDA: ui.message. A function rather than a Presenter*, for the same
    // reason a11y::Service takes a PresenterProvider: speech is restarted
    // independently of this layer.
    std::function<void(const std::string&)> message;
    // Phase 7's handover, item 1: `TableRegistry::addSource`, so a plugin that
    // ships a braille table wins over the system table of the same name. A
    // function, so that the app layer does not link liblouis.
    std::function<bool(const std::filesystem::path&)> addBrailleTableSource;
};

class Module : public script::ScriptableObject {
public:
    // What an event handler decides, spelled out rather than a bool: at the
    // call site `Disposition::Consume` says what not calling nextHandler means,
    // and `true`/`false` does not.
    enum class Disposition {
        // Call the rest of the chain: the tree interceptor, then the object.
        Continue,
        // Do NOT (NVDA: not calling nextHandler). The event stops here, and
        // the object layer never announces it.
        Consume,
    };

    // The object is passed because an event is about an object; it may be null
    // for a global event.
    using EventHandler = std::function<Disposition(const a11y::ObjectPtr&)>;

    // `moduleName` is the name the module is known by: the key of its gesture
    // map sections, of its profile trigger (`app:<moduleName>`) and of what
    // the log and `alus-cli app-info` print. `classChain` is NVDA's own
    // spelling, most derived first, and a module always answers to
    // `appModuleHandler.AppModule` last so that a binding written for every
    // app module reaches it.
    Module(std::string moduleName, AppKey key, Services services,
           std::vector<std::string> classChain = {},
           const script::Catalog* catalog = nullptr);
    ~Module() override;

    const std::string& moduleName() const noexcept { return moduleName_; }
    const AppKey& key() const noexcept { return key_; }
    const Services& services() const noexcept { return services_; }
    // True for the module the registry made because no rule matched. It is a
    // real module (it carries the profile trigger and the sleep mode); it just
    // has no application-specific code.
    bool isGeneric() const noexcept { return generic_; }
    void setGeneric(bool generic) noexcept { generic_ = generic; }

    // ---- lifecycle (§12.1) ----
    // Called once, by the registry, after construction. Bind scripts and
    // event handlers here rather than in the constructor: `this` is fully
    // built, and a module that fails to initialise can be refused.
    virtual void initialize() {}
    virtual void terminate() {}
    // NVDA: `event_appModule_gainFocus` / `event_appModule_loseFocus`. Both
    // run INSIDE the atomic profile switch of §12.1, so a module may change
    // configuration here and pay for one rebuild, not two.
    virtual void appGainFocus() {}
    virtual void appLoseFocus() {}

    // ---- mechanism 1: overlay behaviours ----
    // The list is IN/OUT, exactly as NVDA's `clsList`: prepend to win, append
    // to lose to the platform, `removeBehavior` to take one away.
    virtual void chooseOverlayClasses(a11y::Object& object, a11y::BehaviorList& list) {
        (void)object;
        (void)list;
    }

    // ---- mechanism 2: object init ----
    // NVDA: `event_NVDAObject_init`. Runs once per object, after its
    // behaviours are chosen.
    virtual void objectInit(a11y::Object& object) { (void)object; }

    // ---- mechanism 3: event interception ----
    // Binds a handler for one ALUS event name (a11y::events::*). Later
    // bindings for the same name replace earlier ones, which is what lets a
    // subclass override its base's handler.
    void bindEvent(std::string name, EventHandler handler);
    bool hasEventHandler(std::string_view name) const;
    // What the dispatcher's level-2 provider calls. A module with no handler
    // for this event answers Continue, which costs one map lookup.
    virtual Disposition handleEvent(std::string_view name, const a11y::ObjectPtr& object);
    std::size_t eventHandlerCount() const noexcept { return eventHandlers_.size(); }

    // ---- mechanism 5: property overrides ----
    // NVDA: `AppModule.statusBar`. The default is NVDA's own rule, which is
    // worth having rather than returning null: the last child of the
    // application's active window whose role is StatusBar. A module overrides
    // it where the toolkit hides the status bar somewhere else, which is
    // exactly what the LibreOffice module does.
    virtual a11y::ObjectPtr statusBar() const;
    // NVDA: `getStatusBarText`. The names (or values) of the status bar's
    // children, joined. Empty when there is no status bar -- which the command
    // reports as "no status line found", never as silence.
    virtual std::string statusBarText() const;
    virtual std::string productName() const;
    virtual std::string productVersion() const;
    // NVDA: `appModule.devInfo`. What `alus-cli app-info` prints and what a
    // bug report needs: the key, the toolkit, the module, and whatever the
    // module itself wants to add.
    virtual std::vector<std::string> devInfo() const;

    // ---- the three generalised hooks (§12.2) ----
    // NVDA: `shouldAllowIAccessibleFocusEvent` and its relatives. False means
    // the event is spurious and NOTHING happens for it.
    virtual bool shouldAllowFocusEvent(const a11y::ObjectPtr& object) const {
        (void)object;
        return true;
    }
    // An override of `Object::container`; null leaves the parent in place.
    // It must not call `object.container()`, which would come straight back
    // here.
    virtual a11y::ObjectPtr container(const a11y::Object& object) const {
        (void)object;
        return nullptr;
    }

    // NVDA: an app module answering None to `treeInterceptorClass`. True means
    // this object must NOT be given a tree interceptor -- the Electron case,
    // where a document is a user interface and browse mode would take the
    // arrow keys away from a text editor.
    //
    // Reached through ONE veto installed by the app service, rather than one
    // per module: a veto that outlived the module that installed it would be
    // a dangling pointer the first time its application was closed.
    virtual bool blocksTreeInterceptor(const a11y::Object& object) const {
        (void)object;
        return false;
    }

    // ---- sleep mode ----
    // NVDA: `appModule.sleepMode`. In sleep mode ALUS says nothing about this
    // application and runs only the scripts marked as allowed -- which is how
    // a self-voicing application (a screen reader demo, a game, a speech
    // toolkit) is used at all.
    bool sleepMode() const noexcept { return sleepMode_; }
    void setSleepMode(bool sleeping) noexcept { sleepMode_ = sleeping; }

    // ---- the application ----
    int processId() const noexcept { return key_.processId; }
    // The Application accessible, found through the object model, or null when
    // the process is gone. NOT cached: see the lifetime note in the file
    // comment.
    a11y::ObjectPtr applicationObject() const;
    // True while the process still exists. `Registry::cleanup` uses it; so
    // does every property override, because an application that exited answers
    // nothing.
    bool isAlive() const;

    // The script class chain every module ends with, so that a gesture map
    // section written for app modules in general reaches every one of them.
    static constexpr std::string_view kBaseClass = "appModuleHandler.AppModule";

protected:
    // Helpers for the modules in src/app/modules/, which all need the same
    // two or three things.
    //
    // The active top-level window of THIS application, or null.
    a11y::ObjectPtr activeWindow() const;
    // Depth-first search below `root` for the first object with this role,
    // bounded: a status bar is near the top and an unbounded search over a
    // spreadsheet is a freeze.
    a11y::ObjectPtr findDescendant(const a11y::ObjectPtr& root, a11y::Role role,
                                   int maxDepth = 3) const;
    void say(const std::string& text) const;

private:
    std::string moduleName_;
    AppKey key_;
    Services services_;
    std::map<std::string, EventHandler, std::less<>> eventHandlers_;
    bool sleepMode_ = false;
    bool generic_ = false;
};

using ModulePtr = std::unique_ptr<Module>;

}  // namespace alus::app
