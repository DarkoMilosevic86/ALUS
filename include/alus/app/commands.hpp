// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The commands the app layer owns.
//
//     alus+end            the status line (alus+shift+end on a laptop)
//     alus+control+f1     which app module the focused application got
//     alus+shift+s        sleep mode for this application, on and off
//
// The first of them is the one NVDA's `AppModule.statusBar`
// and `getStatusBarText` exist to serve: the status line. It lives here and
// not in the object model because the answer comes from the app module -- the
// fifth customisation mechanism of §12.2 is what makes `alus+end` work in
// LibreOffice, where the status bar is not where GTK puts it.
//
// They answer to `globalCommands.GlobalCommands`, like every other command
// group in ALUS, so NVDA's own key assignments from the catalogue reach them:
// `alus+end` on a desktop layout and `alus+shift+end` on a laptop one,
// neither of which is written down here.
#pragma once

#include "alus/script/scriptable.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace alus::script {
class Dispatcher;
}

namespace alus::app {

class Registry;

class AppCommands final : public script::ScriptableObject {
public:
    // `dispatcher` may be null in a test that calls the scripts directly; the
    // repeat count (press twice to spell) is then always zero.
    AppCommands(Registry& modules, std::function<void(const std::string&)> message,
                std::function<void(const std::string&)> spell,
                const script::Catalog* catalog = nullptr,
                script::Dispatcher* dispatcher = nullptr);
    ~AppCommands() override;

private:
    // NVDA: `script_reportStatusLine`. Once reads it, twice spells it, three
    // times copies it -- the third press reports that the clipboard is phase
    // 9's work, which is what every other copy command in ALUS does rather
    // than failing silently.
    void reportStatusLine(input::Gesture& gesture);
    // NVDA: `script_reportAppModuleInfo`. Which module is loaded for the
    // focused application, and what that application is.
    void reportAppModuleInfo(input::Gesture& gesture);
    // NVDA: `script_toggleCurrentAppSleepMode`. In sleep mode ALUS says
    // nothing about this application at all -- for a program that speaks for
    // itself.
    void toggleCurrentAppSleepMode(input::Gesture& gesture);
    // N156: NVDA's separate status bar commands, which have no key of their
    // own and can be given one in the Input gestures dialog.
    void readStatusLine(input::Gesture& gesture);
    void spellStatusLine(input::Gesture& gesture);
    void copyStatusLine(input::Gesture& gesture);

    // The focused application's status bar text, or nullopt after saying why
    // there is none (no application, no status bar).
    std::optional<std::string> statusText() const;
    void copy(const std::string& text) const;

    Registry& modules_;
    std::function<void(const std::string&)> message_;
    std::function<void(const std::string&)> spell_;
    script::Dispatcher* dispatcher_;
    // Lets a clipboard answer that arrives after this object is gone be
    // dropped rather than used.
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};

}  // namespace alus::app
