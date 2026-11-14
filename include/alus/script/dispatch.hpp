// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Script dispatch: the ten-step lookup, and script execution.
//
// Port of NVDA's `scriptHandler` (architecture.md §5.1, §5.3). The ten steps
// are not a heuristic; the ORDER is the behaviour users know:
//
//    1. the gesture's own scriptable object (a braille display for `br:`)
//    2. every global plugin
//    3. the focus object's app module
//    4. the braille display
//    5. the active vision providers
//    6. the focus object's tree interceptor   [filter: pass-through off, OR
//                                              the script ignores pass-through]
//    7. the focus object
//    8. the focus ancestors, closest first    [filter: script.canPropagate]
//    9. the configuration profile activation commands
//   10. the global commands                   (last resort)
//
// Step 6 and its filter are exactly what makes single-letter quick navigation
// work in browse mode and fall through to the application in focus mode.
//
// Within each object: THE GLOBAL MAPS BEAT THE BUILT-IN CLASS BINDINGS, and
// the global maps are consulted in priority order (user, locale, braille
// driver). That is how a user rebinding one key does not lose the rest.
#pragma once

#include "alus/input/gesture_map.hpp"
#include "alus/script/scriptable.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace alus::core {
class Loop;
}

namespace alus::script {

class Catalog;

// Who may hold a script for the current gesture. Phase 3 fills in what phase 3
// has (global commands, the braille display later); phases 4 and 6 fill in the
// focus object, the app module and the tree interceptor through the provider.
struct DispatchContext {
    ScriptableObject* gestureScriptable = nullptr;
    std::vector<ScriptableObject*> globalPlugins;
    ScriptableObject* appModule = nullptr;
    ScriptableObject* brailleDisplay = nullptr;
    std::vector<ScriptableObject*> visionProviders;
    ScriptableObject* treeInterceptor = nullptr;
    // NVDA consults the tree interceptor only when `isReady`; an interceptor
    // still building its buffer must not swallow keys.
    bool treeInterceptorReady = false;
    ScriptableObject* focus = nullptr;
    // Outermost first, as NVDA's `api.getFocusAncestors()` returns them; the
    // lookup walks them in reverse, closest ancestor first.
    std::vector<ScriptableObject*> focusAncestors;
    ScriptableObject* profileActivation = nullptr;
    // NVDA has one GlobalCommands object; ALUS assembles the same set from
    // the subsystem that owns each command -- input in phase 3, the object
    // model in phase 4 -- and every one of them answers to
    // `globalCommands.GlobalCommands`, so NVDA's own key assignments reach
    // them. They are consulted in order, as step 10.
    std::vector<ScriptableObject*> globalCommands;
    // NVDA: `focus.sleepMode`. In full sleep mode nothing runs; in app sleep
    // mode only scripts marked `allowInSleepMode` do.
    enum class Sleep { Awake, App, Full } sleepMode = Sleep::Awake;
};

class Dispatcher {
public:
    // `loop` may be null in tests that execute scripts synchronously.
    explicit Dispatcher(core::Loop* loop = nullptr, const Catalog* catalog = nullptr);
    ~Dispatcher();

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    using ContextProvider = std::function<void(DispatchContext&)>;
    // Emulates a system key press for a `kb:<keyname>` map entry. Returns
    // false when the platform cannot do it, which on Wayland is always
    // (finding N18) — the caller then reports it instead of failing silently.
    using KeyEmulator = std::function<bool(std::string_view keyName, input::Gesture& gesture)>;

    void setContextProvider(ContextProvider provider);
    // The global gesture maps in priority order: user, locale, braille driver.
    void setGlobalMaps(std::vector<const input::GestureMap*> maps);
    // The built-in class bindings; consulted after the global maps, per object.
    void setBuiltinMap(const input::GestureMap* map);
    void setKeyEmulator(KeyEmulator emulator);
    void setMultiPressTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds multiPressTimeout() const noexcept { return multiPressTimeout_; }

    // NVDA: `findScript`. Null when the gesture is unbound (or deliberately
    // unbound by a `None =` entry).
    std::optional<Script> findScript(const input::Gesture& gesture);

    // NVDA: `queueScript`. The script runs on the main thread, in a pump
    // cycle, with the gesture kept alive until it does.
    void queueScript(Script script, std::shared_ptr<input::Gesture> gesture);
    // NVDA: `executeScript`. Runs now; also the path tests use.
    void executeScript(const Script& script, input::Gesture& gesture);

    // NVDA: `isScriptWaiting`. Checked throughout the speech code so that a
    // long utterance is cut short when a key is already waiting (T3.9).
    bool isScriptWaiting() const noexcept { return queuedScripts_ > 0; }
    // NVDA: `getLastScriptRepeatCount`. 0 for a first press, 1 for the second
    // within the multi-press timeout, and so on.
    int lastScriptRepeatCount() const;
    // NVDA: `clearLastScript`. Called when an unbound gesture is executed, so
    // that two identical bound gestures either side of it are not a repeat.
    void clearLastScript();
    const Script* currentScript() const;

    std::size_t executedCount() const noexcept { return executed_; }
    std::size_t failedCount() const noexcept { return failed_; }

    // Which of the ten steps produced the last found script. For the log, for
    // the tests, and for the gesture dialog's "where does this key go".
    struct Trace {
        int step = 0;
        std::string location;
        std::string script;
        bool fromGlobalMap = false;
        bool unboundByMap = false;
    };
    const Trace& lastTrace() const noexcept { return trace_; }

private:
    struct MapHit {
        std::string location;
        std::string script;  // empty = unbind
    };

    std::vector<MapHit> globalMapHits(const input::Gesture& gesture) const;
    const Script* objectScript(ScriptableObject& object, const input::Gesture& gesture,
                               const std::vector<MapHit>& hits, bool& unbound);
    const Script* keyEmulationScript(std::string_view keyName);

    core::Loop* loop_;
    const Catalog* catalog_;
    ContextProvider provider_;
    std::vector<const input::GestureMap*> globalMaps_;
    const input::GestureMap* builtinMap_ = nullptr;
    KeyEmulator keyEmulator_;
    std::chrono::milliseconds multiPressTimeout_{500};

    // Key emulation scripts, cached so that their ScriptInfo addresses stay
    // stable and repeat counting keeps working for them.
    std::map<std::string, std::unique_ptr<ScriptInfo>, std::less<>> emulationInfos_;
    std::map<std::string, Script, std::less<>> emulationScripts_;

    std::size_t queuedScripts_ = 0;
    std::size_t executed_ = 0;
    std::size_t failed_ = 0;
    bool scriptRunning_ = false;
    const void* lastScriptIdentity_ = nullptr;
    const Script* currentScript_ = nullptr;
    std::chrono::steady_clock::time_point lastScriptTime_{};
    int lastScriptCount_ = 0;
    Trace trace_;
};

}  // namespace alus::script
