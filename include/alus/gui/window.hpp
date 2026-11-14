// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The settings window and the gesture dialog, as GTK4 + libadwaita.
//
// IN THE SCREEN READER'S OWN PROCESS, on the main thread, sharing the main
// loop. NVDA does the same with wx, and the reason is the same: the gesture
// dialog has to show the LIVE gesture maps and a setting has to take effect as
// the user changes it. A separate process would need every map, every
// catalogue row and every setting to cross a D-Bus boundary, and the first bug
// would be a dialog showing a map the daemon no longer has.
//
// What that costs, and how each cost is paid:
//
//   * GTK needs its own initialisation, which can FAIL (no display, no
//     Wayland socket, a GTK that will not start). It must not take the screen
//     reader down: `Window::isAvailable()` answers whether the GUI can run at
//     all, the daemon starts without it, and the shortcut SAYS so rather than
//     doing nothing (§3.8).
//   * GTK runs on the default GMainContext, which is the context core::Loop
//     already iterates (src/core/loop.cpp), so no second loop exists and the
//     pump keeps its fixed order. This is why no GtkApplication is used: it
//     would want to own the loop.
//   * The window is an AT-SPI client of the screen reader reading it. That is
//     not a problem -- it is how NVDA's and Orca's own dialogs work -- but it
//     does mean the window must never be shown from inside an AT-SPI callback,
//     so opening it goes through the queue like every other script.
#pragma once

#include "alus/gui/model.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace alus::config {
class Manager;
}
namespace alus::input {
class GestureManager;
class KeyNames;
}
namespace alus::script {
class Catalog;
}

namespace alus::gui {

// Everything the window needs from the rest of ALUS, as references and
// callables, so that nothing in the GUI reaches for a global and the whole
// thing can be built against test doubles.
struct Context {
    config::Manager* config = nullptr;
    input::GestureManager* gestures = nullptr;
    const input::KeyNames* keyNames = nullptr;
    const script::Catalog* catalog = nullptr;
    // Speak a message. The GUI uses it for what GTK cannot say by itself: that
    // a write failed, or that a setting needs something that is not installed.
    std::function<void(const std::string&)> message;
    // Called after a setting is changed, so the owning subsystem re-reads it.
    // This is resetConfiguration()'s narrow cousin: changing the speech rate
    // must not restart braille.
    std::function<void(const std::string& section)> settingChanged;
    // Called when the user asks for a factory reset.
    std::function<void()> resetAll;
    // Called when the user presses OK: the changes are to be KEPT. Closing
    // the window any other way -- Cancel, Escape, the header's close button
    // -- discards them, as a Windows dialog does (FAZA10.md N125).
    std::function<void()> accept;
    // What speech-dispatcher offers, for the voice and module rows. Filled
    // by alus-settings when it starts; empty means "not asked", and the rows
    // then offer only their fixed entries.
    SpeechChoices speech;
    // Asks again for one module, when the user chooses another output module,
    // so the voice row shows THAT module's voices without the window being
    // closed. Null in the tests, and then the voice row keeps what it has.
    std::function<SpeechChoices(const std::string& module)> requerySpeech;
};

// Whether a GUI can run in this session at all. Checks for a display and that
// GTK initialises; cheap after the first call, and does NOT create a window.
//
// Called before the window is built AND reported by `alus-cli gui-info`, so
// "the settings window does not open" has an answer that does not require
// opening it.
bool isAvailable();
// Why not, when isAvailable() is false. Empty when it is true.
std::string unavailableReason();

class Window {
public:
    explicit Window(Context context);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Shows the window with `pageId` selected, creating it on first use.
    // Returns false when the GUI is not available; the caller SAYS why.
    bool open(std::string_view pageId = "general");
    // Shows the gesture dialog (`alus+f6` and the Gestures page's button).
    bool openGestures();

    void close();
    bool isOpen() const;
    // The gesture dialog is a window of its own and the user may well leave it
    // open with the settings closed, so `alus-settings` has to ask about both
    // before it decides it is finished.
    bool isGesturesOpen() const;

    // The page the window is showing, or empty when it is not open. For the
    // test that T9.1's shortcuts select the right page.
    std::string currentPage() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace alus::gui
