// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/manager.hpp"

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <algorithm>
#include <utility>

namespace alus::input {
namespace {

// The X11 state bit each ALUS modifier corresponds to, for building grabs.
// `alusModifierMask` is ONE ALUS key's mask here, not the combined one.
std::uint32_t stateMaskFor(Modifiers modifiers, std::uint32_t alusModifierMask) {
    std::uint32_t mask = 0;
    if (has(modifiers, Modifier::Shift)) {
        mask |= keysym::kStateShift;
    }
    if (has(modifiers, Modifier::Control)) {
        mask |= keysym::kStateControl;
    }
    if (has(modifiers, Modifier::Alt)) {
        mask |= keysym::kStateMod1;
    }
    if (has(modifiers, Modifier::Windows)) {
        mask |= keysym::kStateMod4;
    }
    if (has(modifiers, Modifier::NumLock)) {
        mask |= keysym::kStateMod2;
    }
    if (has(modifiers, Modifier::Alus)) {
        mask |= alusModifierMask;
    }
    return mask;
}

}  // namespace

GestureManager::GestureManager(core::Loop& loop, script::Dispatcher& dispatcher, Hooks hooks)
    : loop_(loop), dispatcher_(dispatcher), hooks_(std::move(hooks)) {
    InputHelp::Hooks helpHooks;
    helpHooks.speakText = hooks_.speakText;
    helpHooks.speakSpelling = hooks_.speakSpelling;
    helpHooks.brailleMessage = hooks_.brailleMessage;
    inputHelp_ = InputHelp(std::move(helpHooks));

    TypedChars::Hooks echoHooks;
    if (hooks_.speakText) {
        echoHooks.speakText = [this](const std::string& text) { hooks_.speakText(text, false); };
    }
    echoHooks.speakSpelling = hooks_.speakSpelling;
    echoHooks.isFocusEditable = hooks_.isFocusEditable;
    echoHooks.isTypingProtected = hooks_.isTypingProtected;
    typedChars_ = TypedChars(std::move(echoHooks));

    dispatcher_.setMultiPressTimeout(config_.multiPressTimeout);
    typedChars_.setCharacterEcho(config_.speakTypedCharacters);
    typedChars_.setWordEcho(config_.speakTypedWords);
    refreshMaps();
}

GestureManager::~GestureManager() = default;

void GestureManager::setConfig(KeyboardConfig config) {
    const bool layoutChanged = config.layout != config_.layout;
    const bool alusKeysChanged = config.alusKeyInsert != config_.alusKeyInsert ||
                                 config.alusKeyNumpadInsert != config_.alusKeyNumpadInsert ||
                                 config.alusKeyCapsLock != config_.alusKeyCapsLock;
    config_ = config;
    dispatcher_.setMultiPressTimeout(config_.multiPressTimeout);
    typedChars_.setCharacterEcho(config_.speakTypedCharacters);
    typedChars_.setWordEcho(config_.speakTypedWords);
    if (layoutChanged || alusKeysChanged) {
        // Both change which keys have to be grabbed: the layout decides which
        // `kb(...)` bindings are live, and the ALUS keys are grabbed as keys
        // in their own right.
        if (alusKeysChanged) {
            mapAlusModifier();
        }
        pushGrabs();
    }
}

void GestureManager::setBackend(IInputBackend* backend) {
    if (backend_ == backend) {
        return;
    }
    if (backend_ != nullptr) {
        backend_->setKeyHandler(nullptr);
    }
    backend_ = backend;
    if (backend_ == nullptr) {
        alusModifierMask_ = 0;
        return;
    }
    backend_->setKeyHandler([this](const KeyEvent& event) { return onKeyEvent(event); });
    mapAlusModifier();
    pushGrabs();
}

void GestureManager::mapAlusModifier() {
    if (backend_ == nullptr) {
        return;
    }
    // Each ALUS key gets its OWN virtual modifier from the backend — insert
    // and numpad insert are different modifiers as far as the compositor is
    // concerned. The combined mask answers "is the ALUS key held" (any bit);
    // the individual masks are what grabs are built from (one grab per key).
    alusModifierMask_ = 0;
    alusModifierMasks_.clear();
    const std::uint32_t candidates[] = {
        config_.alusKeyInsert ? keysym::kInsert : 0u,
        config_.alusKeyNumpadInsert ? keysym::kKpInsert : 0u,
        config_.alusKeyCapsLock ? keysym::kCapsLock : 0u,
    };
    for (const std::uint32_t candidate : candidates) {
        if (candidate == 0) {
            continue;
        }
        const std::uint32_t mask = backend_->mapModifier(candidate);
        if (mask == 0) {
            ALUS_WARNING("could not map keysym 0x{:04x} as the ALUS modifier", candidate);
            continue;
        }
        if (mask < (1u << keysym::kFirstVirtualModifierIndex)) {
            // N9 again: a mask below the virtual modifier range means someone
            // returned an INDEX. Shifting it again would be the silent bug.
            ALUS_WARNING("keysym 0x{:04x} mapped to 0x{:x}, below the virtual modifier range "
                         "— refusing it (see N9)",
                         candidate, mask);
            continue;
        }
        alusModifierMask_ |= mask;
        if (std::find(alusModifierMasks_.begin(), alusModifierMasks_.end(), mask) ==
            alusModifierMasks_.end()) {
            alusModifierMasks_.push_back(mask);
        }
    }
    if (alusModifierMask_ != 0) {
        ALUS_INFO("ALUS key: {} virtual modifier(s), combined mask 0x{:x}",
                  alusModifierMasks_.size(), alusModifierMask_);
    }
}

void GestureManager::setBuiltinMap(const GestureMap* map) {
    builtinMap_ = map;
    refreshMaps();
}

void GestureManager::refreshMaps() {
    dispatcher_.setGlobalMaps({&userMap_, &localeMap_, &driverMap_, &appMap_});
    dispatcher_.setBuiltinMap(builtinMap_);
    pushGrabs();
}

void GestureManager::pushGrabs() {
    if (backend_ == nullptr) {
        return;
    }
    std::vector<KeyDefinition> grabs = computeGrabs();
    // Unchanged? Then say nothing to the compositor. This is not a
    // micro-optimisation: phase 4 calls refreshGrabs() on EVERY focus change
    // (its handover asked for it, O6), and tabbing through a dialog would
    // otherwise be one SetKeyGrabs round trip and one log line per control,
    // for a set of keys that did not move.
    if (grabsInstalled_ && grabs == lastGrabs_) {
        return;
    }
    std::string error;
    if (!backend_->setGrabs(grabs, &error)) {
        ALUS_WARNING("could not install {} key grabs: {}", grabs.size(), error);
        return;
    }
    lastGrabs_ = std::move(grabs);
    grabsInstalled_ = true;
    ALUS_INFO("{} key grabs installed ({} backend)", lastGrabs_.size(), backend_->name());
}

std::vector<KeyDefinition> GestureManager::computeGrabs() {
    std::vector<KeyDefinition> grabs;
    const KeyNames& names = KeyNames::shared();
    const auto addGrab = [&](std::uint32_t keysymValue, std::uint32_t stateMask,
                             const std::string& origin) {
        if (keysymValue == 0) {
            return;
        }
        KeyDefinition definition{keysymValue, stateMask, origin};
        if (std::find(grabs.begin(), grabs.end(), definition) == grabs.end()) {
            grabs.push_back(std::move(definition));
        }
    };

    // The ALUS keys themselves. They are grabbed as keys, not only mapped as a
    // modifier, so that ALUS sees every press (it has to, for the
    // double-press-for-a-real-Insert rule) and the application sees none.
    if (config_.alusKeyInsert) {
        addGrab(keysym::kInsert, 0, "ALUS key");
    }
    if (config_.alusKeyNumpadInsert) {
        addGrab(keysym::kKpInsert, 0, "ALUS key");
    }
    if (config_.alusKeyCapsLock) {
        addGrab(keysym::kCapsLock, 0, "ALUS key");
    }

    const GestureMap* maps[] = {&userMap_, &localeMap_, &driverMap_, &appMap_, builtinMap_};
    for (const GestureMap* map : maps) {
        if (map == nullptr) {
            continue;
        }
        for (const auto& [identifier, entry] : map->allEntries()) {
            if (entry.script.empty()) {
                // An unbind creates no grab of its own; if another class binds
                // the same gesture, that binding adds the grab.
                continue;
            }
            const std::optional<ParsedKeyboardGesture> parsed =
                parseKeyboardGesture(identifier, names);
            if (!parsed.has_value()) {
                // Not a keyboard gesture (a braille one, or malformed).
                continue;
            }
            if (parsed->layout.has_value() && *parsed->layout != config_.layout) {
                // A gesture for the other keyboard layout. A numpad command
                // keeps working in the laptop layout because NVDA writes it as
                // `kb:`, not `kb(desktop):` (§5.4).
                continue;
            }
            if (parsed->keysym == 0) {
                ALUS_DEBUG_WARNING("gesture '{}': no keysym for key '{}'; not grabbed",
                                   identifier, parsed->mainKeyName);
                continue;
            }
            const Modifiers modifiers = parsed->modifiers;

            // Would this gesture find a script, here and now? Only then is it
            // ALUS's to consume. See the header: this is the Wayland model,
            // and without it every browse-mode letter would be swallowed
            // system-wide.
            KeyboardGesture::Init probe;
            probe.event.keysym = parsed->keysym;
            probe.baseKeysym = parsed->keysym;
            probe.modifiers = modifiers;
            probe.isAlusKey = parsed->mainKeyName == KeyNames::kAlusKeyName;
            probe.config.layout = parsed->layout.value_or(config_.layout);
            const KeyboardGesture probeGesture(probe, names);
            const std::optional<script::Script> script = dispatcher_.findScript(probeGesture);
            if (!script.has_value()) {
                continue;
            }
            if (script->info != nullptr && script->info->needsKeyPassThrough &&
                (backend_ == nullptr || !backend_->canSendKeys())) {
                // The script only WATCHES this key; the application has to act
                // on it. On a platform that cannot re-send a swallowed key
                // (Wayland, N18) grabbing it would stop the caret from moving
                // at all, so the key is left alone -- ALUS still sees it,
                // because `WatchKeyboard` is not a grab.
                //
                // This is phase 5's addition to the Wayland input model, and
                // the whole reason `nav::EditableText` needs no gesture.send().
                ALUS_DEBUG("gesture '{}' is observed, not consumed; not grabbed", identifier);
                continue;
            }
            if (script->info != nullptr && script->info->location == "inputCore.KeyboardEmulation" &&
                (backend_ == nullptr || !backend_->canSendKeys())) {
                // A `kb:` map entry asks ALUS to send a key on. Where it
                // cannot (Wayland, N18), grabbing the key would swallow it for
                // nothing; letting it through is the closest thing to what the
                // user asked for.
                ALUS_DEBUG_WARNING("gesture '{}' emulates a key press, which this platform "
                                   "cannot do; not grabbed",
                                   identifier);
                continue;
            }

            // One grab per ALUS key, because each one is its own virtual
            // modifier; a gesture without the ALUS key needs exactly one.
            std::vector<std::uint32_t> alusMasks{0};
            if (has(modifiers, Modifier::Alus)) {
                alusMasks = alusModifierMasks_;
                if (alusMasks.empty()) {
                    // No ALUS key could be mapped: the gesture cannot be
                    // grabbed at all, and saying so beats grabbing the bare key
                    // and swallowing it from the application.
                    ALUS_DEBUG_WARNING("gesture '{}' needs the ALUS key, which is not mapped",
                                       identifier);
                    continue;
                }
            }
            for (const std::uint32_t alusMask : alusMasks) {
                const std::uint32_t stateMask = stateMaskFor(modifiers, alusMask);
                addGrab(parsed->keysym, stateMask, identifier);
                // With shift in the gesture, the compositor may report the
                // SHIFTED keysym for the same physical key. Which level it
                // compares a grab against is not yet measured on this system,
                // so both are registered: one extra entry in the grab set
                // against a binding that silently never fires.
                if (has(modifiers, Modifier::Shift) && keymap_ != nullptr) {
                    if (const std::optional<std::uint32_t> shifted =
                            keymap_->shiftedKeysymFor(parsed->keysym)) {
                        addGrab(*shifted, stateMask, identifier + " (shifted level)");
                    }
                }
            }
        }
    }
    return grabs;
}

bool GestureManager::isAlusKeyName(const std::string& keyName) const {
    if (config_.alusKeyInsert && keyName == "insert") {
        return true;
    }
    if (config_.alusKeyNumpadInsert && keyName == "numpadInsert") {
        return true;
    }
    if (config_.alusKeyCapsLock && keyName == "capsLock") {
        return true;
    }
    return false;
}

std::uint32_t GestureManager::baseKeysymFor(const KeyEvent& event) const {
    if (keymap_ != nullptr) {
        if (const std::optional<std::uint32_t> base = keymap_->baseKeysym(event.keycode)) {
            return *base;
        }
    }
    return event.keysym;
}

Modifiers GestureManager::currentModifiers(const KeyEvent& event) const {
    Modifiers modifiers = KeyNames::modifiersFromState(event.state, alusModifierMask_);
    // The compositor's state mask is authoritative for the real modifiers. The
    // ALUS key is not a real modifier, so if the virtual modifier did not make
    // it into the mask, ALUS's own bookkeeping supplies it.
    for (const auto& [keycode, held] : heldModifiers_) {
        if (keycode == event.keycode) {
            continue;  // a key does not modify itself
        }
        modifiers |= held;
    }
    return modifiers;
}

bool GestureManager::onKeyEvent(const KeyEvent& event) {
    keyEvent_.notify(event);
    return event.pressed ? onKeyDown(event) : onKeyUp(event);
}

bool GestureManager::onKeyDown(const KeyEvent& event) {
    ++stats_.keysSeen;

    if (passKeyThroughCount_ >= 0) {
        // The user asked for the next key to go straight to the application
        // (`nvda+f2`). Repeats of the same key down do not count twice,
        // because no key up arrives between them.
        if (lastPassThroughKeyDown_ != event.keycode) {
            ++passKeyThroughCount_;
            lastPassThroughKeyDown_ = event.keycode;
        }
        return false;
    }

    if (hooks_.isRecovering && hooks_.isRecovering()) {
        // The core is wedged. Gestures pass through unhindered rather than
        // queue up for the moment it revives (NVDA does the same), and no
        // grab-dependent work is attempted.
        return false;
    }

    const std::uint32_t baseKeysym = baseKeysymFor(event);
    const KeyNames& names = KeyNames::shared();
    const std::string keyName = names.name(baseKeysym, event.unichar);
    const bool isAlusKey = isAlusKeyName(keyName);

    bool treatAsAlusKey = isAlusKey;
    if (isAlusKey) {
        const auto sinceRelease = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - lastAlusKeyRelease_);
        const bool repeatedQuickly = lastAlusKeyCode_ == event.keycode &&
                                     lastAlusKeyRelease_.time_since_epoch().count() != 0 &&
                                     sinceRelease < config_.multiPressTimeout;
        if (bypassAlusKey_ || repeatedQuickly) {
            // Pressed twice inside the multi-press timeout: the user wants the
            // key to do its ordinary job (H2). The flag stays set until the
            // key is released, so auto-repeat does not flip it back.
            bypassAlusKey_ = true;
            treatAsAlusKey = false;
        }
    }
    lastAlusKeyRelease_ = {};
    if (treatAsAlusKey) {
        lastAlusKeyCode_ = event.keycode;
    } else {
        lastAlusKeyCode_.reset();
    }

    KeyboardGesture::Init init;
    init.event = event;
    init.isAlusKey = treatAsAlusKey;
    init.baseKeysym = baseKeysym;
    init.modifiers = currentModifiers(event);
    if (isAlusKey) {
        // A key does not modify itself. The backend has to OR the virtual
        // modifier bit into the ALUS key's OWN event (the compositor does not
        // track virtual modifiers for us, N27), so that bit has to come back
        // out here — otherwise the key reads as `alus+alus`, and the second
        // press of a double press read as `alus+numpadInsert` (measured in the
        // interactive run: the daemon announced "alus plus numpad insert").
        //
        // Another ALUS key genuinely held down is a different matter: insert
        // held plus numpad insert pressed really is an ALUS gesture, so the
        // bit is only cleared when no OTHER ALUS key is down.
        bool anotherAlusKeyHeld = false;
        for (const auto& [keycode, held] : heldModifiers_) {
            if (keycode != event.keycode && has(held, Modifier::Alus)) {
                anotherAlusKeyHeld = true;
                break;
            }
        }
        if (!anotherAlusKeyHeld) {
            init.modifiers &= ~static_cast<Modifiers>(Modifier::Alus);
        }
    }
    init.config.layout = config_.layout;
    init.config.speechInterruptForCharacters = config_.speechInterruptForCharacters;
    init.config.speechInterruptForEnter = config_.speechInterruptForEnter;
    init.config.speechIsPaused = hooks_.isSpeechPaused && hooks_.isSpeechPaused();
    init.config.inputHelpActive = inputHelp_.active();

    auto gesture = std::make_shared<KeyboardGesture>(std::move(init), names);
    if (gesture->isModifier()) {
        Modifiers held = static_cast<Modifiers>(Modifier::None);
        if (treatAsAlusKey) {
            held = static_cast<Modifiers>(Modifier::Alus);
        } else if (const std::optional<Modifier> modifier = names.modifierFor(keyName)) {
            held = static_cast<Modifiers>(*modifier);
        }
        heldModifiers_[event.keycode] = held;
    }

    const Result result = executeGesture(gesture);

    bool consumed = result == Result::Executed || result == Result::Captured;
    if (!consumed && treatAsAlusKey) {
        // The ALUS key is never given to the system (H2), even when nothing
        // was bound to it.
        consumed = true;
    }
    if (isAlusKey && !treatAsAlusKey) {
        // The double press: the application should now receive a real key.
        //
        // On the KeyboardMonitor backend THE COMPOSITOR DOES THIS ITSELF
        // (finding N34). Its interface contract is explicit: a grabbed
        // modifier pressed alone is ignored unless a second press follows
        // within the key repeat delay, and "this second event is processed
        // normally" — so the application gets a real key press without ALUS
        // injecting anything, which Wayland could not do anyway (N18). All
        // ALUS has to do is not act on it and not consume it.
        ++stats_.alusKeyPassThroughRequested;
        if (backend_ != nullptr && backend_->modifierDoublePressReachesApplication()) {
            ++stats_.alusKeyPassThroughDelivered;
            ALUS_DEBUG("the ALUS key was pressed twice; the compositor passes it on");
        } else if (backend_ != nullptr && backend_->canSendKeys()) {
            KeyDefinition key{baseKeysym, 0, "ALUS key pass-through"};
            std::string error;
            if (backend_->sendKey(key, &error)) {
                ++stats_.alusKeyPassThroughDelivered;
            } else {
                ALUS_DEBUG_WARNING("could not pass the ALUS key through: {}", error);
            }
        } else {
            ALUS_DEBUG_WARNING(
                "the ALUS key was pressed twice, but this platform neither passes it on nor "
                "allows a key press to be injected; the application will not see it");
        }
        consumed = false;
    }

    if (consumed) {
        // A consumed key down must be matched by a consumed key up (T3.5).
        trappedKeys_.insert(event.keycode);
        ++stats_.keysConsumed;
    }
    echoTypedCharacter(*gesture, consumed);
    return consumed;
}

bool GestureManager::onKeyUp(const KeyEvent& event) {
    if (passKeyThroughCount_ >= 1) {
        if (lastPassThroughKeyDown_ == event.keycode) {
            lastPassThroughKeyDown_.reset();
        }
        --passKeyThroughCount_;
        if (passKeyThroughCount_ == 0) {
            passKeyThroughCount_ = -1;
        }
        return false;
    }

    if (lastAlusKeyCode_ == event.keycode) {
        // The ALUS key was released with no key press in between: a second
        // press within the timeout now means "give me the real key".
        lastAlusKeyRelease_ = std::chrono::steady_clock::now();
    }
    bypassAlusKey_ = false;
    heldModifiers_.erase(event.keycode);
    if (heldModifiers_.empty()) {
        lastModifierWasInSayAll_ = false;
    }

    const auto trapped = trappedKeys_.find(event.keycode);
    if (trapped != trappedKeys_.end()) {
        trappedKeys_.erase(trapped);
        return true;
    }
    return false;
}

GestureManager::Result GestureManager::executeGesture(const std::shared_ptr<Gesture>& gesture) {
    if (hooks_.isRecovering && hooks_.isRecovering()) {
        return Result::NoAction;
    }
    if (!decideExecuteGesture_.decide(*gesture)) {
        // Deliberately NOT reported as unbound: an unbound gesture may be sent
        // on to the system, and a handler that said "do not execute this" did
        // not ask for that.
        ALUS_DEBUG("gesture execution cancelled by a decide_executeGesture handler");
        return Result::Captured;
    }

    std::optional<script::Script> script = dispatcher_.findScript(*gesture);
    if (auto* keyboardGesture = dynamic_cast<KeyboardGesture*>(gesture.get())) {
        keyboardGesture->setHasScript(script.has_value());
    }

    // Say-all bookkeeping, NVDA's exactly: a modifier pressed during say-all
    // marks the gesture that follows it, so that skim reading resumes.
    bool wasInSayAll = false;
    const bool sayAllRunning = hooks_.isSayAllRunning && hooks_.isSayAllRunning();
    if (gesture->isModifier()) {
        if (!lastModifierWasInSayAll_) {
            lastModifierWasInSayAll_ = sayAllRunning;
            wasInSayAll = sayAllRunning;
        }
    } else if (lastModifierWasInSayAll_) {
        wasInSayAll = true;
        lastModifierWasInSayAll_ = false;
    } else {
        wasInSayAll = sayAllRunning;
    }
    gesture->wasInSayAll = wasInSayAll;

    applySpeechEffect(*gesture, gesture->speechEffectWhenExecuted());

    if (core::Log::instance().isEnabledFor(core::LogLevel::Io) && !gesture->isModifier()) {
        const std::vector<std::string> identifiers = gesture->identifiers();
        ALUS_IO("input: {}", identifiers.empty() ? std::string("?") : identifiers.front());
    }

    if (inputHelp_.active()) {
        const bool bypass = inputHelp_.shouldBypass(*gesture, script);
        auto report = [this, gesture, script, bypass]() {
            const std::optional<script::Script> toRun =
                inputHelp_.report(*gesture, script, bypass);
            if (toRun.has_value()) {
                // The input help toggle runs even in input help, or there is
                // no way out of it.
                dispatcher_.executeScript(*toRun, *gesture);
            }
        };
        loop_.queueFunction(std::move(report), true, "inputHelp");
        if (!bypass) {
            return Result::Captured;
        }
    }

    if (gesture->isModifier()) {
        return Result::Modifier;
    }

    if (config_.speakCommandKeys && gesture->shouldReportAsCommand()) {
        reportCommandKey(gesture);
    }
    gesture->reportExtra();

    if (script.has_value()) {
        dispatcher_.queueScript(*script, gesture);
        ++stats_.scriptsQueued;
        return Result::Executed;
    }

    ++stats_.unboundGestures;
    // NVDA clears the memorised last script here, so that an unbound gesture
    // between two identical bound ones is not counted as a repeat (T3.7).
    loop_.queueFunction([this]() { dispatcher_.clearLastScript(); }, false, "clearLastScript");
    return Result::NoAction;
}

void GestureManager::applySpeechEffect(const Gesture& /*gesture*/, SpeechEffect effect) {
    switch (effect) {
        case SpeechEffect::Cancel:
            if (hooks_.cancelSpeech) {
                loop_.queueFunction([this]() { hooks_.cancelSpeech(); }, true, "cancelSpeech");
            }
            break;
        case SpeechEffect::Pause:
        case SpeechEffect::Resume:
            if (hooks_.pauseSpeech) {
                const bool pause = effect == SpeechEffect::Pause;
                // Immediate, like the cancel above, and for the same reason:
                // the two must run in the order the keys came. Shift (pause)
                // then Tab (cancel) with the pause queued behind the cancel
                // left speech paused after it (phase-10.md N123).
                loop_.queueFunction([this, pause]() { hooks_.pauseSpeech(pause); }, true,
                                    "pauseSpeech");
            }
            break;
        case SpeechEffect::None:
            break;
    }
}

void GestureManager::reportCommandKey(const std::shared_ptr<Gesture>& gesture) {
    if (!hooks_.speakText) {
        return;
    }
    const std::string name = gesture->displayName();
    loop_.queueFunction([this, name]() { hooks_.speakText(name, false); }, true, "speakCommandKey");
}

void GestureManager::echoTypedCharacter(const KeyboardGesture& gesture, bool consumed) {
    if (consumed || gesture.isModifier()) {
        // A key that ran a command is reported as a command, not echoed as a
        // character.
        return;
    }
    if (gesture.mainKeyName() == "backspace") {
        // The word buffer has to follow the correction, whatever the echo
        // settings are.
        typedChars_.onCharacter("\b");
        return;
    }
    const std::optional<std::string> character = gesture.character();
    if (!character.has_value()) {
        return;
    }
    ++stats_.charactersEchoed;
    typedChars_.onCharacter(*character);
}

void GestureManager::passNextKeyThrough() {
    if (passKeyThroughCount_ == -1) {
        passKeyThroughCount_ = 0;
        lastPassThroughKeyDown_.reset();
    }
}

}  // namespace alus::input
