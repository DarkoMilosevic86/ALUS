// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The input backend: where key events come from.
//
// `IInputBackend` is the abstraction required by 00-VIZIJA.md Z6 (one build
// for GNOME Wayland, KDE Wayland and X11, no conditional compilation in the
// core). `AtspiDeviceBackend` is its first implementation.
//
// ONE DIFFERENCE FROM NVDA RUNS THROUGH THIS WHOLE FILE. NVDA hooks the
// keyboard and decides, per event, whether to swallow it: the hook's return
// value IS the decision. The Wayland KeyboardMonitor works the other way
// round — ALUS declares IN ADVANCE which keys the compositor should consume
// (`setGrabs`), and a grabbed key has already been consumed by the time ALUS
// is told about it. So:
//
//   * the grab set has to be recomputed whenever the gesture maps change;
//   * `KeyHandler`'s return value is honoured by backends that can act on it
//     (X11, legacy) and is bookkeeping only for the KeyboardMonitor backend;
//   * a key ALUS has not grabbed always reaches the application, which is why
//     watching (for typed-character echo) is separate from grabbing.
//
// TWO FINDINGS FROM PHASE 0 ARE BUILT INTO THIS INTERFACE:
//
//   N7: libatspi SWALLOWS AccessDenied — `add_key_grab` returned success while
//       the compositor was refusing every call. So health is not inferred from
//       return values: `health()` must be backed by a DIRECT call to the
//       compositor whose answer was actually read.
//   N19: a client holding a grab wedged itself and kept the user's keyboard
//       for nine minutes. So `emergencyRelease()` exists, is callable from any
//       thread (the watchdog's), and must not need the main loop to run.
#pragma once

#include "alus/input/gesture.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace alus::input {

// One key combination the compositor is asked to consume on ALUS's behalf.
struct KeyDefinition {
    std::uint32_t keysym = 0;
    // The X11 modifier state mask, including the virtual modifier that stands
    // for the ALUS key. A MASK, never an index (N9).
    std::uint32_t modifiers = 0;
    // Informational: what this grab was built from, for the log.
    std::string origin;

    bool operator==(const KeyDefinition& other) const noexcept {
        return keysym == other.keysym && modifiers == other.modifiers;
    }
};

enum class BackendHealth {
    Unknown,       // not started yet
    Working,       // a direct call to the compositor succeeded
    AccessDenied,  // the compositor refuses us: wrong app id (R1/N6)
    NameTaken,     // another screen reader holds the bus name (N8)
    NoDevice,      // no KeyboardMonitor and no X11 fallback (R2)
    Failed,
};

std::string_view backendHealthName(BackendHealth health);

class IInputBackend {
public:
    // True means ALUS consumed the event. See the file comment for what that
    // does and does not mean per backend.
    using KeyHandler = std::function<bool(const KeyEvent&)>;

    virtual ~IInputBackend() = default;

    virtual std::string_view name() const = 0;

    virtual bool start(std::string* error) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;

    virtual void setKeyHandler(KeyHandler handler) = 0;

    // Makes a key act as a modifier the compositor enforces (§5.2). Returns
    // the modifier MASK, or 0 on failure.
    virtual std::uint32_t mapModifier(std::uint32_t keysym) = 0;
    virtual void unmapModifier(std::uint32_t keysym) = 0;

    // Replaces the whole grab set. Called whenever the maps change.
    virtual bool setGrabs(const std::vector<KeyDefinition>& grabs, std::string* error) = 0;

    // Everything, for input help and learn mode. The riskiest state ALUS can
    // be in (N19), which is why `emergencyRelease()` must undo it.
    virtual bool grabKeyboard(bool enable, std::string* error) = 0;

    // Whether this platform can inject a key press. False on Wayland (N18).
    virtual bool canSendKeys() const = 0;
    virtual bool sendKey(const KeyDefinition& key, std::string* error) = 0;

    // Whether the PLATFORM already gives the application a real key press when
    // a grabbed modifier is pressed twice in quick succession — the ALUS key's
    // "let me type a real Insert" rule (Z2).
    //
    // On the KeyboardMonitor backend this is true and it is a published part
    // of the interface contract (finding N34), which is why ALUS does not try
    // to inject anything there. A backend that answers false has to be asked
    // to send the key, and if it cannot either, the rule cannot be honoured on
    // that platform and ALUS says so in the log.
    virtual bool modifierDoublePressReachesApplication() const { return false; }

    virtual BackendHealth health() const = 0;

    // Drops every grab. Callable from any thread and from a wedged process.
    virtual void emergencyRelease() = 0;
};

// Input without a platform: `--no-input`, and the test double's base. Nothing
// here is a stub for its own sake — the daemon needs a mode where the whole
// pipeline runs with no keyboard attached, for the same reason speech has
// NullSynth.
class NullInputBackend final : public IInputBackend {
public:
    std::string_view name() const override { return "null"; }
    bool start(std::string* /*error*/) override {
        running_ = true;
        return true;
    }
    void stop() override { running_ = false; }
    bool running() const override { return running_; }
    void setKeyHandler(KeyHandler handler) override { handler_ = std::move(handler); }
    // One virtual modifier per keysym, starting at index 15, exactly as the
    // real backend allocates them. Returning the SAME mask for every key
    // would hide the bug that cost this phase a debugging session: a gesture
    // with the ALUS key has to be grabbed once per ALUS key, because insert
    // and numpad insert are different virtual modifiers.
    std::uint32_t mapModifier(std::uint32_t keysym) override {
        const auto existing = modifierMasks_.find(keysym);
        if (existing != modifierMasks_.end()) {
            return existing->second;
        }
        const std::uint32_t mask = 1u << nextModifierIndex_;
        ++nextModifierIndex_;
        modifierMasks_[keysym] = mask;
        return mask;
    }
    void unmapModifier(std::uint32_t keysym) override { modifierMasks_.erase(keysym); }
    bool setGrabs(const std::vector<KeyDefinition>& grabs, std::string* /*error*/) override {
        grabs_ = grabs;
        return true;
    }
    bool grabKeyboard(bool enable, std::string* /*error*/) override {
        keyboardGrabbed_ = enable;
        return true;
    }
    bool canSendKeys() const override { return false; }
    bool sendKey(const KeyDefinition& /*key*/, std::string* error) override {
        if (error != nullptr) {
            *error = "this backend cannot inject key presses";
        }
        return false;
    }
    BackendHealth health() const override {
        return running_ ? BackendHealth::Working : BackendHealth::Unknown;
    }
    void emergencyRelease() override {
        grabs_.clear();
        keyboardGrabbed_ = false;
    }

    bool modifierDoublePressReachesApplication() const override {
        return doublePressReachesApplication_;
    }

    // Test and diagnostic surface.
    void setModifierDoublePressReachesApplication(bool value) noexcept {
        doublePressReachesApplication_ = value;
    }
    bool deliver(const KeyEvent& event) { return handler_ ? handler_(event) : false; }
    const std::vector<KeyDefinition>& grabs() const noexcept { return grabs_; }
    bool keyboardGrabbed() const noexcept { return keyboardGrabbed_; }

private:
    KeyHandler handler_;
    std::vector<KeyDefinition> grabs_;
    bool running_ = false;
    bool keyboardGrabbed_ = false;
    std::map<std::uint32_t, std::uint32_t> modifierMasks_;
    unsigned nextModifierIndex_ = 15;
    bool doublePressReachesApplication_ = false;
};

}  // namespace alus::input
