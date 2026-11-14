// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The Wayland input backend: org.freedesktop.a11y.KeyboardMonitor, directly.
//
//     interface org.freedesktop.a11y.KeyboardMonitor  (Mutter 48+, KWin 6.3.6+)
//       WatchKeyboard();  UnwatchKeyboard();
//       GrabKeyboard();   UngrabKeyboard();
//       SetKeyGrabs(in au modifier_keysyms, in a(uu) keystrokes);
//       signal KeyEvent(b released, u state, u keysym, u unichar, q keycode);
//
// WHY NOT libatspi's AtspiDevice, which architecture.md §5.2 named. Reading
// `atspi-device-a11y-manager.c` (at-spi2-core 2.56.2) answered the question
// phase 0 left open, and the answer rules that layer out for ALUS:
//
//   1. IT IS THE N19 HANG. Every compositor call in that file is
//      `g_dbus_proxy_call_sync(..., timeout = -1, ...)` with the result
//      DISCARDED, and `SetKeyGrabs` is issued from a `g_timeout_add`
//      callback — that is, from inside the client's main loop. A compositor
//      that is slow to answer therefore blocks the client's main loop with no
//      timeout, forever, while its grabs stay installed: the user loses both
//      speech and keyboard. That is exactly what phase 0 observed (549 s, only
//      SIGKILL ended it), and no amount of care on ALUS's side can fix it from
//      the outside.
//   2. IT IS ALSO N7. Those same calls pass `NULL` for the GError, so
//      `AccessDenied` from the compositor cannot be seen by the caller:
//      `add_key_grab()` returns success while nothing is grabbed.
//
// So ALUS speaks to the interface itself: every call is ASYNCHRONOUS with a
// finite timeout, every reply is read, and `emergencyRelease()` can drop the
// grabs from another thread without the main loop running at all.
//
// What this costs: the X11 and legacy AT-SPI paths that libatspi would have
// given for free. They are a second IInputBackend (that is what the interface
// is for), and H6 is met by the same build choosing a backend at run time, not
// by compiling differently.
//
// TWO THINGS THE COMPOSITOR DOES NOT DO FOR US, both confirmed in that source:
//
//   * virtual modifiers are a CLIENT-SIDE fiction. `SetKeyGrabs` takes the
//     KEYSYMS to treat as modifiers; the index (15..31) and the bit in the
//     state mask are the client's own bookkeeping, and the client has to OR
//     that bit into the state of every subsequent event itself.
//   * `state` is a plain X11 modifier mask (bit 0 shift, 2 control, 3 Mod1,
//     4 Mod2/NumLock, 6 Mod4/Super). libatspi rewrites bit 4 into AT-SPI's own
//     NumLock bit on the way to its clients; ALUS reads the raw mask instead.
//
// AND ONE THING IT DOES THAT HAD TO BE MEASURED (finding N33): the same key
// event can arrive TWICE, 0-2 ms apart. Measured on GNOME 48.7: 14 repeats in
// 56 events, every gap in 0-2 ms, and audibly — "numpad 7" was announced
// twice. `MetaA11yManager::notify_clients` notifies each client once per
// event, so the event itself traverses the compositor's filter twice. This
// backend therefore drops an event identical to the one before it inside
// `kDuplicateWindow`, which is where a platform quirk belongs.
#pragma once

#include "alus/input/backend.hpp"
#include "alus/input/key_event_filter.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

using GDBusConnection = struct _GDBusConnection;
using GVariant = struct _GVariant;

namespace alus::input {

// At namespace scope, not nested in the backend, for the same reason
// LoopConfig and ServiceOptions are: a nested class's default member
// initializers cannot be used in a default argument of the enclosing class.
struct A11yManagerOptions {
    // The D-Bus name ALUS asks for. Mutter's allowlist contains exactly one
    // assistive-technology name, `org.gnome.Orca` (risks.md R1, confirmed
    // by the control experiment N6: with `org.alus` every call is refused).
    // Until the upstream patch lands, ALUS BORROWS Orca's name, which also
    // means Orca must not be running (N8).
    std::string appId = "org.gnome.Orca";
    // Every call gets a finite timeout. See the file comment for why this is
    // the central decision in this class.
    std::chrono::milliseconds callTimeout{2000};
};

class A11yManagerBackend final : public IInputBackend {
public:
    using Options = A11yManagerOptions;

    explicit A11yManagerBackend(Options options = {});
    ~A11yManagerBackend() override;

    std::string_view name() const override { return "a11y-manager"; }

    bool start(std::string* error) override;
    void stop() override;
    bool running() const override { return running_; }

    void setKeyHandler(KeyHandler handler) override;

    std::uint32_t mapModifier(std::uint32_t keysym) override;
    void unmapModifier(std::uint32_t keysym) override;

    bool setGrabs(const std::vector<KeyDefinition>& grabs, std::string* error) override;
    bool grabKeyboard(bool enable, std::string* error) override;

    // Wayland cannot do this through AT-SPI (N18): `generate_keyboard_event`
    // is a no-op there, and the KeyboardMonitor interface has no injection
    // method at all. A privileged /dev/uinput helper is the v1.1 answer.
    bool canSendKeys() const override { return false; }
    bool sendKey(const KeyDefinition& key, std::string* error) override;

    // The compositor's own contract (N34): "If any of the keys in @modifiers
    // is pressed alone, the compositor is required to ignore the key press and
    // release event if a second key press of the same modifier is not received
    // within a reasonable time frame... If such event is received, this second
    // event is processed normally."
    bool modifierDoublePressReachesApplication() const override { return true; }

    BackendHealth health() const override { return health_; }
    void emergencyRelease() override;

    // Diagnostics, used by the service's start-up log and by the manual tests.
    const std::string& busName() const noexcept { return busName_; }
    std::string lastError() const;
    // Which process holds the KeyboardMonitor name, when it is not us (N8).
    const std::string& nameHolder() const noexcept { return nameHolder_; }
    std::uint64_t eventsReceived() const noexcept { return eventsReceived_; }
    std::uint64_t callFailures() const noexcept { return callFailures_; }
    // How many duplicated events this backend absorbed (N33). Worth watching:
    // if it ever stays at zero on a compositor, the quirk was fixed upstream.
    std::uint64_t duplicatesDropped() const noexcept { return duplicateFilter_.dropped(); }

private:
    struct VirtualModifier {
        std::uint32_t keysym;
        unsigned index;
    };

    void onKeyEventSignal(GVariant* parameters);
    // All three of these require `mutex_` to be held. The convention is
    // explicit because the first bug in this class was the obvious one: a
    // locked `setGrabs` calling a locking `callAsync` and deadlocking the
    // process at start-up (docs/history/phase-3.md N28).
    void sendGrabsLocked();
    void callAsyncLocked(const char* method, GVariant* parameters);
    void callAsync(const char* method, GVariant* parameters);
    bool callSync(const char* method, GVariant* parameters, std::string* error);
    void setError(std::string message);
    bool matchesGrab(std::uint32_t keysym, std::uint32_t state) const;

    static void onSignal(GDBusConnection* connection, const char* senderName,
                         const char* objectPath, const char* interfaceName,
                         const char* signalName, GVariant* parameters, void* userData);

    Options options_;
    GDBusConnection* connection_ = nullptr;
    unsigned signalSubscription_ = 0;
    unsigned nameOwnerId_ = 0;
    std::string busName_;
    std::string nameHolder_;

    KeyHandler handler_;
    std::vector<KeyDefinition> grabs_;
    std::vector<VirtualModifier> virtualModifiers_;
    // The virtual modifier bits currently held down. The compositor does not
    // track these for us (see the file comment), so this is ALUS's own state.
    std::uint32_t enabledVirtualModifiers_ = 0;

    // The platform quirk and the reasoning behind the window live in
    // key_event_filter.hpp, where they can be tested without a compositor.
    KeyEventFilter duplicateFilter_;

    bool running_ = false;
    // True while stop() is clearing up: the grabs still have to be dropped,
    // and that is the one time a call goes out after running_ is false.
    bool stopping_ = false;
    bool keyboardGrabbed_ = false;
    std::atomic<BackendHealth> health_{BackendHealth::Unknown};
    std::atomic<std::uint64_t> eventsReceived_{0};
    std::atomic<std::uint64_t> callFailures_{0};

    // Guards the grab set and the connection pointer, because
    // `emergencyRelease()` runs on the watchdog thread.
    mutable std::mutex mutex_;
    std::string lastError_;
};

}  // namespace alus::input
