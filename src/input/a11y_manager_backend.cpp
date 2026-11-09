// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/input/a11y_manager_backend.hpp"

#include "alus/core/log.hpp"
#include "alus/input/keysyms.hpp"

#include <gio/gio.h>

#include <algorithm>
#include <utility>

namespace alus::input {
namespace {

constexpr const char* kBusName = "org.freedesktop.a11y.Manager";
constexpr const char* kObjectPath = "/org/freedesktop/a11y/Manager";
constexpr const char* kInterface = "org.freedesktop.a11y.KeyboardMonitor";

// RequestName flags. DO_NOT_QUEUE matters: without it a second screen reader
// silently waits in a queue and looks healthy while receiving nothing (N8).
constexpr std::uint32_t kNameFlagDoNotQueue = 1u << 2;
constexpr std::uint32_t kRequestNamePrimaryOwner = 1;
constexpr std::uint32_t kRequestNameExists = 3;
constexpr std::uint32_t kRequestNameAlreadyOwner = 4;

std::string takeError(GError* error) {
    if (error == nullptr) {
        return {};
    }
    std::string message = error->message != nullptr ? error->message : "unknown D-Bus error";
    g_error_free(error);
    return message;
}

}  // namespace

std::string_view backendHealthName(BackendHealth health) {
    switch (health) {
        case BackendHealth::Unknown:
            return "unknown";
        case BackendHealth::Working:
            return "working";
        case BackendHealth::AccessDenied:
            return "access denied";
        case BackendHealth::NameTaken:
            return "bus name taken";
        case BackendHealth::NoDevice:
            return "no keyboard monitor";
        case BackendHealth::Failed:
            return "failed";
    }
    return "?";
}

A11yManagerBackend::A11yManagerBackend(Options options) : options_(std::move(options)) {}

A11yManagerBackend::~A11yManagerBackend() { stop(); }

void A11yManagerBackend::setError(std::string message) {
    const std::lock_guard<std::mutex> lock(mutex_);
    lastError_ = std::move(message);
}

std::string A11yManagerBackend::lastError() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void A11yManagerBackend::setKeyHandler(KeyHandler handler) {
    handler_ = std::move(handler);
}

bool A11yManagerBackend::start(std::string* error) {
    if (running_) {
        return true;
    }
    GError* gerror = nullptr;
    connection_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    if (connection_ == nullptr) {
        const std::string message = "no session bus: " + takeError(gerror);
        setError(message);
        health_ = BackendHealth::Failed;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }

    // Is there a KeyboardMonitor at all? On a compositor without one (sway,
    // wlroots — 03-RIZICI.md R2) this is where ALUS finds out, instead of
    // appearing to work and reporting no keys (N7's failure mode).
    GVariant* owner = g_dbus_connection_call_sync(
        connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetNameOwner", g_variant_new("(s)", kBusName), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE, static_cast<int>(options_.callTimeout.count()), nullptr, &gerror);
    if (owner == nullptr) {
        const std::string message =
            std::string("this compositor exposes no ") + kBusName + " (" + takeError(gerror) +
            "); keyboard input is not available — see 03-RIZICI.md R2";
        setError(message);
        health_ = BackendHealth::NoDevice;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }
    g_variant_unref(owner);

    // Claim `<appId>.KeyboardMonitor`. The compositor's access control is by
    // this name (R1/N6), so failing to get it is a hard failure, not a detail.
    busName_ = options_.appId + ".KeyboardMonitor";
    GVariant* reply = g_dbus_connection_call_sync(
        connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", g_variant_new("(su)", busName_.c_str(), kNameFlagDoNotQueue),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE,
        static_cast<int>(options_.callTimeout.count()), nullptr, &gerror);
    if (reply == nullptr) {
        const std::string message = "cannot request " + busName_ + ": " + takeError(gerror);
        setError(message);
        health_ = BackendHealth::Failed;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }
    std::uint32_t result = 0;
    g_variant_get(reply, "(u)", &result);
    g_variant_unref(reply);
    if (result != kRequestNamePrimaryOwner && result != kRequestNameAlreadyOwner) {
        // Another screen reader holds it. NVDA has no equivalent of this
        // failure; on Linux it is the common one, and it must be SAID, because
        // the alternative is a screen reader that starts, says nothing about
        // it and never reacts to a key (N8).
        nameHolder_.clear();
        GVariant* holder = g_dbus_connection_call_sync(
            connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "GetNameOwner", g_variant_new("(s)", busName_.c_str()), G_VARIANT_TYPE("(s)"),
            G_DBUS_CALL_FLAGS_NONE, static_cast<int>(options_.callTimeout.count()), nullptr,
            nullptr);
        if (holder != nullptr) {
            const char* unique = nullptr;
            g_variant_get(holder, "(&s)", &unique);
            if (unique != nullptr) {
                nameHolder_ = unique;
            }
            g_variant_unref(holder);
        }
        const std::string message =
            busName_ + " is already held by " +
            (nameHolder_.empty() ? std::string("another process") : nameHolder_) +
            " (another screen reader is running); stop it and start ALUS again" +
            (result == kRequestNameExists ? "" : " [unexpected RequestName result]");
        setError(message);
        health_ = BackendHealth::NameTaken;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }

    signalSubscription_ = g_dbus_connection_signal_subscribe(
        connection_, kBusName, kInterface, "KeyEvent", kObjectPath, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, &A11yManagerBackend::onSignal, this, nullptr);

    // THE DIRECT CALL WHOSE ANSWER IS READ (N7). Until this succeeds, ALUS
    // does not claim that input works. Synchronous on purpose: this is
    // start-up, not a main-loop callback, and it has a finite timeout.
    std::string watchError;
    if (!callSync("WatchKeyboard", nullptr, &watchError)) {
        const bool denied = watchError.find("AccessDenied") != std::string::npos ||
                            watchError.find("not allowed") != std::string::npos;
        const std::string message =
            denied ? "the compositor refuses keyboard monitoring for the name '" + busName_ +
                         "' — only its allow-listed assistive technology may monitor keys "
                         "(03-RIZICI.md R1); start ALUS with an allow-listed app id"
                   : "WatchKeyboard failed: " + watchError;
        setError(message);
        health_ = denied ? BackendHealth::AccessDenied : BackendHealth::Failed;
        g_dbus_connection_signal_unsubscribe(connection_, signalSubscription_);
        signalSubscription_ = 0;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }

    running_ = true;
    health_ = BackendHealth::Working;
    ALUS_INFO("input backend a11y-manager: watching the keyboard as {}", busName_);
    return true;
}

void A11yManagerBackend::stop() {
    if (connection_ == nullptr) {
        return;
    }
    stopping_ = true;
    if (running_) {
        // Order matters: drop the grabs before giving up the name, so there is
        // no window in which the compositor holds grabs for a client that no
        // longer exists.
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            grabs_.clear();
            virtualModifiers_.clear();
            enabledVirtualModifiers_ = 0;
            sendGrabsLocked();
        }
        if (keyboardGrabbed_) {
            callAsync("UngrabKeyboard", nullptr);
            keyboardGrabbed_ = false;
        }
        callAsync("UnwatchKeyboard", nullptr);
    }
    if (signalSubscription_ != 0) {
        g_dbus_connection_signal_unsubscribe(connection_, signalSubscription_);
        signalSubscription_ = 0;
    }
    if (!busName_.empty()) {
        g_dbus_connection_call(connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "ReleaseName",
                               g_variant_new("(s)", busName_.c_str()), nullptr,
                               G_DBUS_CALL_FLAGS_NONE,
                               static_cast<int>(options_.callTimeout.count()), nullptr, nullptr,
                               nullptr);
        busName_.clear();
    }
    // Flush what we just queued, so the grabs are really gone by the time the
    // process exits.
    g_dbus_connection_flush_sync(connection_, nullptr, nullptr);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        g_object_unref(connection_);
        connection_ = nullptr;
    }
    running_ = false;
    stopping_ = false;
    health_ = BackendHealth::Unknown;
}

void A11yManagerBackend::callAsync(const char* method, GVariant* parameters) {
    const std::lock_guard<std::mutex> lock(mutex_);
    callAsyncLocked(method, parameters);
}

void A11yManagerBackend::callAsyncLocked(const char* method, GVariant* parameters) {
    GDBusConnection* connection = connection_;
    // Nothing is sent to a compositor that has not accepted us: after a failed
    // start the connection still exists, and pushing grabs down it would be
    // both useless and misleading in the log.
    if (connection == nullptr || (!running_ && !stopping_)) {
        if (parameters != nullptr) {
            // A floating variant that is never passed to GDBus has to be sunk
            // and dropped, or it leaks.
            g_variant_unref(g_variant_ref_sink(parameters));
        }
        return;
    }
    const std::string methodName = method;
    // Fire and check: the reply is read in the callback purely so that a
    // refusal is logged rather than swallowed (N7). Nothing waits for it
    // (N19).
    g_dbus_connection_call(
        connection, kBusName, kObjectPath, kInterface, method, parameters, nullptr,
        G_DBUS_CALL_FLAGS_NONE, static_cast<int>(options_.callTimeout.count()), nullptr,
        [](GObject* source, GAsyncResult* result, void* userData) {
            auto* self = static_cast<A11yManagerBackend*>(userData);
            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result,
                                                            &error);
            if (reply != nullptr) {
                g_variant_unref(reply);
                return;
            }
            const std::string message = takeError(error);
            self->callFailures_.fetch_add(1);
            self->setError(message);
            // An expected platform error must not play the error sound (§3.8).
            ALUS_DEBUG_WARNING("keyboard monitor call failed: {}", message);
        },
        this);
    (void)methodName;
}

bool A11yManagerBackend::callSync(const char* method, GVariant* parameters, std::string* error) {
    GError* gerror = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        connection_, kBusName, kObjectPath, kInterface, method, parameters, nullptr,
        G_DBUS_CALL_FLAGS_NONE, static_cast<int>(options_.callTimeout.count()), nullptr, &gerror);
    if (reply == nullptr) {
        const std::string message = takeError(gerror);
        if (error != nullptr) {
            *error = message;
        }
        callFailures_.fetch_add(1);
        return false;
    }
    g_variant_unref(reply);
    return true;
}

std::uint32_t A11yManagerBackend::mapModifier(std::uint32_t keysymValue) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const VirtualModifier& modifier : virtualModifiers_) {
        if (modifier.keysym == keysymValue) {
            return 1u << modifier.index;
        }
    }
    // Indexes 15..31, as libatspi allocates them, so that a mask built here
    // means the same thing to anything else on this bus.
    unsigned index = keysym::kFirstVirtualModifierIndex;
    while (index <= keysym::kLastVirtualModifierIndex) {
        const bool used = std::any_of(
            virtualModifiers_.begin(), virtualModifiers_.end(),
            [index](const VirtualModifier& modifier) { return modifier.index == index; });
        if (!used) {
            break;
        }
        ++index;
    }
    if (index > keysym::kLastVirtualModifierIndex) {
        ALUS_WARNING("no virtual modifier left for keysym 0x{:04x}", keysymValue);
        return 0;
    }
    virtualModifiers_.push_back(VirtualModifier{keysymValue, index});
    sendGrabsLocked();
    return 1u << index;
}

void A11yManagerBackend::unmapModifier(std::uint32_t keysymValue) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find_if(
        virtualModifiers_.begin(), virtualModifiers_.end(),
        [keysymValue](const VirtualModifier& modifier) { return modifier.keysym == keysymValue; });
    if (it == virtualModifiers_.end()) {
        return;
    }
    enabledVirtualModifiers_ &= ~(1u << it->index);
    virtualModifiers_.erase(it);
    sendGrabsLocked();
}

bool A11yManagerBackend::setGrabs(const std::vector<KeyDefinition>& grabs, std::string* /*error*/) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        grabs_ = grabs;
        sendGrabsLocked();
    }
    return true;
}

void A11yManagerBackend::sendGrabsLocked() {
    if (connection_ == nullptr) {
        return;
    }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("(aua(uu))"));
    // The first array is the list of KEYSYMS the compositor should treat as
    // modifiers, not a list of modifier indexes. The index never leaves this
    // process (see the file comment); getting this backwards is why phase 0's
    // step 5b could not have worked.
    g_variant_builder_open(&builder, G_VARIANT_TYPE("au"));
    for (const VirtualModifier& modifier : virtualModifiers_) {
        g_variant_builder_add(&builder, "u", modifier.keysym);
    }
    g_variant_builder_close(&builder);
    g_variant_builder_open(&builder, G_VARIANT_TYPE("a(uu)"));
    for (const KeyDefinition& grab : grabs_) {
        g_variant_builder_open(&builder, G_VARIANT_TYPE("(uu)"));
        g_variant_builder_add(&builder, "u", grab.keysym);
        g_variant_builder_add(&builder, "u", grab.modifiers);
        g_variant_builder_close(&builder);
    }
    g_variant_builder_close(&builder);
    callAsyncLocked("SetKeyGrabs", g_variant_builder_end(&builder));
}

bool A11yManagerBackend::grabKeyboard(bool enable, std::string* /*error*/) {
    if (enable == keyboardGrabbed_) {
        return true;
    }
    keyboardGrabbed_ = enable;
    // The most dangerous state ALUS can be in: every key goes to ALUS. It is
    // what input help needs, and it is why emergencyRelease() exists.
    callAsync(enable ? "GrabKeyboard" : "UngrabKeyboard", nullptr);
    return true;
}

bool A11yManagerBackend::sendKey(const KeyDefinition& /*key*/, std::string* error) {
    if (error != nullptr) {
        *error =
            "Wayland cannot inject key presses through AT-SPI (finding N18); a privileged "
            "uinput helper is planned for v1.1";
    }
    return false;
}

void A11yManagerBackend::emergencyRelease() {
    // Callable from the watchdog thread while the main loop is wedged. Nothing
    // here waits: GDBus sends on its own worker thread, so this returns even
    // if ALUS itself is not running its loop any more.
    GDBusConnection* connection = nullptr;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        connection = connection_;
        grabs_.clear();
    }
    if (connection == nullptr) {
        return;
    }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("(aua(uu))"));
    g_variant_builder_open(&builder, G_VARIANT_TYPE("au"));
    g_variant_builder_close(&builder);
    g_variant_builder_open(&builder, G_VARIANT_TYPE("a(uu)"));
    g_variant_builder_close(&builder);
    g_dbus_connection_call(connection, kBusName, kObjectPath, kInterface, "SetKeyGrabs",
                           g_variant_builder_end(&builder), nullptr, G_DBUS_CALL_FLAGS_NONE,
                           static_cast<int>(options_.callTimeout.count()), nullptr, nullptr,
                           nullptr);
    g_dbus_connection_call(connection, kBusName, kObjectPath, kInterface, "UngrabKeyboard",
                           nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                           static_cast<int>(options_.callTimeout.count()), nullptr, nullptr,
                           nullptr);
    g_dbus_connection_flush(connection, nullptr, nullptr, nullptr);
    ALUS_WARNING("input: every key grab was released by the emergency path");
}

bool A11yManagerBackend::matchesGrab(std::uint32_t keysymValue, std::uint32_t state) const {
    for (const KeyDefinition& grab : grabs_) {
        if (grab.keysym != keysymValue) {
            continue;
        }
        // The compositor matches a grab against the modifiers it was given;
        // this only answers "did we ask for this key", for the event's
        // `fromGrab` flag.
        if ((state & grab.modifiers) == grab.modifiers) {
            return true;
        }
    }
    return false;
}

void A11yManagerBackend::onSignal(GDBusConnection* /*connection*/, const char* /*senderName*/,
                                  const char* /*objectPath*/, const char* /*interfaceName*/,
                                  const char* signalName, GVariant* parameters, void* userData) {
    if (g_strcmp0(signalName, "KeyEvent") != 0) {
        return;
    }
    static_cast<A11yManagerBackend*>(userData)->onKeyEventSignal(parameters);
}

void A11yManagerBackend::onKeyEventSignal(GVariant* parameters) {
    gboolean released = FALSE;
    std::uint32_t state = 0;
    std::uint32_t keysymValue = 0;
    std::uint32_t unichar = 0;
    std::uint16_t keycode = 0;
    g_variant_get(parameters, "(buuuq)", &released, &state, &keysymValue, &unichar, &keycode);
    eventsReceived_.fetch_add(1);

    KeyEvent event;
    event.pressed = released == FALSE;
    event.keycode = keycode;
    event.keysym = keysymValue;
    event.unichar = unichar;

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        // A key that stands for a virtual modifier (the ALUS key) sets its bit
        // for as long as it is held. The compositor knows nothing about this,
        // so if ALUS does not track it, `nvda+t` can never be recognised.
        for (const VirtualModifier& modifier : virtualModifiers_) {
            if (modifier.keysym != keysymValue) {
                continue;
            }
            const std::uint32_t bit = 1u << modifier.index;
            if (event.pressed) {
                enabledVirtualModifiers_ |= bit;
            } else {
                enabledVirtualModifiers_ &= ~bit;
            }
        }
        state |= enabledVirtualModifiers_;
        event.state = state;
        event.fromGrab = matchesGrab(keysymValue, state);
    }

    if (duplicateFilter_.isDuplicate(event)) {
        // N33: the compositor delivered the same event twice. Passing it on
        // would announce the key twice and echo a typed character twice.
        ALUS_DEBUG("key {} keycode={} repeated after <{} ms; dropped as a duplicate",
                   event.pressed ? "down" : "up", event.keycode,
                   duplicateFilter_.window().count());
        return;
    }

    if (core::Log::instance().isEnabledFor(core::LogLevel::Io)) {
        ALUS_IO("key {} keycode={} keysym=0x{:04x} state=0x{:x} unichar=0x{:x}{}",
                event.pressed ? "down" : "up  ", event.keycode, event.keysym, event.state,
                event.unichar, event.fromGrab ? " (grabbed)" : "");
    }
    if (handler_) {
        handler_(event);
    }
}

}  // namespace alus::input
