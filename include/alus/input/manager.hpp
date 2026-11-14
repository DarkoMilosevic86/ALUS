// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The gesture manager: from a key event to a script.
//
// Port of NVDA's `inputCore.InputManager` and the state machine in
// `keyboardHandler.internal_keyDownEvent` / `internal_keyUpEvent`
// (architecture.md §5.1, §5.2). Four behaviours here are the ones users
// would notice immediately if they were missing:
//
//   trapped keys        a consumed key down must be matched by a consumed key
//                       up, or applications are left believing the key is
//                       still held (T3.5);
//   the ALUS key        is never passed to the system, EXCEPT when pressed
//                       twice within the multi-press timeout, which is how a
//                       user types a real Insert (T3.6, H2);
//   repeat by SCRIPT    "press twice to spell" counts repeats of the script,
//                       not of the key, so two different keys bound to the
//                       same script still count as a repeat (T3.7);
//   pass next key       `nvda+f2` sends the next key straight to the
//                       application.
//
// The grab set is computed here too, out of the gesture maps, because on
// Wayland consumption is declared in advance (see IInputBackend).
#pragma once

#include "alus/core/extension_points.hpp"
#include "alus/input/backend.hpp"
#include "alus/input/gesture.hpp"
#include "alus/input/gesture_map.hpp"
#include "alus/input/input_help.hpp"
#include "alus/input/keymap.hpp"
#include "alus/input/typed_chars.hpp"
#include "alus/script/dispatch.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace alus::core {
class Loop;
}

namespace alus::input {

// The [keyboard] configuration section. Defaults are NVDA's defaults, because
// a user coming from NVDA must find the same screen reader (H1).
struct KeyboardConfig {
    Layout layout = Layout::Desktop;
    // Which physical keys act as the ALUS key (NVDA key). Insert and numpad
    // insert by default; caps lock off, as in NVDA.
    bool alusKeyInsert = true;
    bool alusKeyNumpadInsert = true;
    bool alusKeyCapsLock = false;
    std::chrono::milliseconds multiPressTimeout{500};
    bool speakCommandKeys = false;
    TypingEcho speakTypedCharacters = TypingEcho::EditControls;
    TypingEcho speakTypedWords = TypingEcho::Off;
    bool speechInterruptForCharacters = true;
    bool speechInterruptForEnter = true;
    bool allowSkimReadingInSayAll = false;
};

class GestureManager {
public:
    // Everything the manager needs from the rest of ALUS, as callables, so
    // that the input layer can be tested without speech, braille or AT-SPI.
    struct Hooks {
        std::function<void()> cancelSpeech;
        std::function<void(bool pause)> pauseSpeech;
        std::function<void(const std::string& text, bool symbolLevelAll)> speakText;
        std::function<void(const std::string& text)> speakSpelling;
        std::function<void(const std::string& text)> brailleMessage;
        std::function<bool()> isSayAllRunning;
        std::function<bool()> isSpeechPaused;
        // NVDA: watchdog.isAttemptingRecovery. While the core is wedged,
        // gestures must pass through untouched rather than pile up.
        std::function<bool()> isRecovering;
        std::function<bool()> isFocusEditable;
        std::function<bool()> isTypingProtected;
        // NVDA: `script_quit`. Supplied by the daemon, which is the only thing
        // that owns the loop. Without it `alus+q` has nothing to call, which
        // is why the command went unimplemented for a whole phase
        // (docs/history/phase-9.md N99) -- a screen reader a blind user cannot stop
        // from the keyboard is stopped from a terminal they have to find.
        std::function<void()> quit;
    };

    enum class Result {
        NoAction,       // nothing bound: the application gets the key
        Executed,       // a script was queued
        Captured,       // input help (or another captor) took it
        PassedThrough,  // the user asked for this key to go to the application
        Modifier,       // a modifier key: no script is looked up
    };

    GestureManager(core::Loop& loop, script::Dispatcher& dispatcher, Hooks hooks = {});
    ~GestureManager();

    GestureManager(const GestureManager&) = delete;
    GestureManager& operator=(const GestureManager&) = delete;

    void setConfig(KeyboardConfig config);
    const KeyboardConfig& config() const noexcept { return config_; }

    // Attaching a backend installs the key handler, maps the ALUS key to a
    // virtual modifier and pushes the grab set.
    void setBackend(IInputBackend* backend);
    IInputBackend* backend() const noexcept { return backend_; }
    void setKeyMap(const KeyMap* keymap) noexcept { keymap_ = keymap; }

    // The global layers, in priority order, plus the generated built-in layer
    // (§5.1). `refreshMaps()` republishes them to the dispatcher and
    // recomputes the grab set; it is also what a configuration reload calls.
    GestureMap& userMap() noexcept { return userMap_; }
    GestureMap& localeMap() noexcept { return localeMap_; }
    GestureMap& driverMap() noexcept { return driverMap_; }
    // Phase 8: what app modules and plugins bind.
    //
    // WHY A FOURTH LAYER AND NOT THE BUILT-IN ONE. NVDA's app modules and
    // global plugins bind their keys as CLASS bindings (`__gestures` on the
    // class), which puts them at the lowest priority -- the user's own
    // `gestures.ini` must be able to override an app module exactly as it
    // overrides a built-in command. That is this layer's position: last of the
    // global maps, still above the generated built-in map, because a module
    // written for one application is more specific than a global default.
    //
    // It is a LAYER and not a per-module map because the lookup already keys
    // on the script class name: a binding for `appModules.soffice.AppModule`
    // can only resolve when the focused application's module answers to that
    // name, so one map serves every module without any of them leaking keys
    // into another application.
    GestureMap& appMap() noexcept { return appMap_; }
    void setBuiltinMap(const GestureMap* map);
    void refreshMaps();

    InputHelp& inputHelp() noexcept { return inputHelp_; }
    TypedChars& typedChars() noexcept { return typedChars_; }

    // The platform entry point. Returns true when ALUS consumed the event.
    bool onKeyEvent(const KeyEvent& event);

    // NVDA: `executeGesture`. Also the entry point for emulated gestures and
    // for tests, which is why it takes a gesture rather than a key event.
    Result executeGesture(const std::shared_ptr<Gesture>& gesture);

    // NVDA: `passNextKeyThrough` (`nvda+f2`).
    void passNextKeyThrough();
    bool isPassingKeysThrough() const noexcept { return passKeyThroughCount_ >= 0; }

    // NVDA: `decide_executeGesture`. A handler returning false cancels the
    // gesture without reporting it as unbound.
    core::Decider<Gesture&>& decideExecuteGesture() noexcept { return decideExecuteGesture_; }
    // Notified for every key event, before anything else happens. Phase 5's
    // say-all uses it to stop on the first key press.
    core::Action<const KeyEvent&>& keyEvent() noexcept { return keyEvent_; }

    // The grabs the maps currently imply.
    //
    // THIS IS WHERE WAYLAND DIFFERS FROM NVDA, and the difference is not a
    // detail. NVDA decides per key press, after the script lookup, whether to
    // swallow the key. The compositor wants that decision IN ADVANCE, so the
    // grab set is exactly the set of gestures that WOULD find a script right
    // now — which is why this asks the dispatcher rather than reading the maps
    // literally. Taking every binding in the maps instead would grab `h`, `k`
    // and the digits globally (browse mode binds them) and make typing
    // impossible in every application, with the key going nowhere at all.
    //
    // The consequence is that the grab set is CONTEXT DEPENDENT: it has to be
    // recomputed whenever the set of live scriptable objects changes. Phase 3
    // has only the global commands, so it is computed at start-up and whenever
    // the maps change; phases 4 and 6 call refreshGrabs() on focus and
    // browse-mode changes.
    std::vector<KeyDefinition> computeGrabs();

    // What is installed in the compositor right now, so that an unchanged set
    // is not sent again (see pushGrabs).
    std::vector<KeyDefinition> lastGrabs_;
    bool grabsInstalled_ = false;
    // Recomputes and pushes the grab set. Cheap: a few hundred map lookups.
    void refreshGrabs() { pushGrabs(); }
    // Sends the grab set again even if it did not change. The watchdog's
    // emergency release (N19) drops every grab in the compositor behind this
    // manager's back; without this, `lastGrabs_` still matched and the grabs
    // stayed gone until the set happened to change, so the ALUS key went to
    // the application (log 16:14:44: numpad Insert opened LibreOffice's
    // "Confirm overwrite mode" dialog). N128.
    void reinstallGrabs() {
        grabsInstalled_ = false;
        pushGrabs();
    }

    // Every configured ALUS key as a virtual modifier mask, OR-ed together:
    // the test "is the ALUS key held" is "is ANY of these bits set". A MASK,
    // never an index (N9).
    std::uint32_t alusModifierMask() const noexcept { return alusModifierMask_; }
    // The masks one by one. A gesture written `nvda+t` has to be grabbed ONCE
    // PER ALUS KEY, because insert and numpad insert are separate virtual
    // modifiers and a grab naming both would require both to be held.
    const std::vector<std::uint32_t>& alusModifierMasks() const noexcept {
        return alusModifierMasks_;
    }

    struct Stats {
        std::uint64_t keysSeen = 0;
        std::uint64_t keysConsumed = 0;
        std::uint64_t scriptsQueued = 0;
        std::uint64_t unboundGestures = 0;
        std::uint64_t charactersEchoed = 0;
        // How often the user asked for a real ALUS key by double-pressing it,
        // and how often ALUS could actually deliver one. On Wayland the second
        // number stays at zero (N18) and that difference is the measurement.
        std::uint64_t alusKeyPassThroughRequested = 0;
        std::uint64_t alusKeyPassThroughDelivered = 0;
    };
    const Stats& stats() const noexcept { return stats_; }

private:
    bool onKeyDown(const KeyEvent& event);
    bool onKeyUp(const KeyEvent& event);

    bool isAlusKeyName(const std::string& keyName) const;
    std::uint32_t baseKeysymFor(const KeyEvent& event) const;
    Modifiers currentModifiers(const KeyEvent& event) const;
    void applySpeechEffect(const Gesture& gesture, SpeechEffect effect);
    void reportCommandKey(const std::shared_ptr<Gesture>& gesture);
    void echoTypedCharacter(const KeyboardGesture& gesture, bool consumed);
    void pushGrabs();
    void mapAlusModifier();

    core::Loop& loop_;
    script::Dispatcher& dispatcher_;
    Hooks hooks_;
    KeyboardConfig config_;
    IInputBackend* backend_ = nullptr;
    const KeyMap* keymap_ = nullptr;

    GestureMap userMap_{"user"};
    GestureMap localeMap_{"locale"};
    GestureMap driverMap_{"braille driver"};
    GestureMap appMap_{"app modules and plugins"};
    const GestureMap* builtinMap_ = nullptr;

    InputHelp inputHelp_;
    TypedChars typedChars_;

    core::Decider<Gesture&> decideExecuteGesture_;
    core::Action<const KeyEvent&> keyEvent_;

    std::uint32_t alusModifierMask_ = 0;
    std::vector<std::uint32_t> alusModifierMasks_;

    // Keys ALUS consumed on the way down: their key up must be consumed too.
    std::set<std::uint32_t> trappedKeys_;
    // Modifier keys currently held, by keycode. The compositor's state mask
    // covers the real modifiers; this covers the ALUS key, which is not a
    // modifier anywhere but in ALUS.
    std::unordered_map<std::uint32_t, Modifiers> heldModifiers_;

    // The ALUS key pass-through state machine (NVDA: lastNVDAModifier,
    // lastNVDAModifierReleaseTime, bypassNVDAModifier).
    std::optional<std::uint32_t> lastAlusKeyCode_;
    std::chrono::steady_clock::time_point lastAlusKeyRelease_{};
    bool bypassAlusKey_ = false;
    // NVDA: lastModifierWasInSayAll.
    bool lastModifierWasInSayAll_ = false;

    // NVDA: passKeyThroughCount. -1 means off.
    int passKeyThroughCount_ = -1;
    std::optional<std::uint32_t> lastPassThroughKeyDown_;

    Stats stats_;
};

}  // namespace alus::input
