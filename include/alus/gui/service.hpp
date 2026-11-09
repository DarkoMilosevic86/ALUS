// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The GUI as a subsystem, and the commands that open it.
//
// IT DOES NOT CONTAIN THE WINDOW. The window is `alus-settings`, a separate
// process, and this service starts it -- because a window in the screen
// reader's own process cannot be read at all: ALUS's synchronous AT-SPI calls
// about it are answered from the very main loop they are blocking
// (claude/FAZA9.md N79, and the file comment in src/gui/main.cpp). So this
// layer is the same whether or not GTK is installed; what changes is whether
// `alus-settings` exists to start.
//
// What it answers, all without a display:
//
//   * which page a shortcut would open (T9.1 is asserted here, because the
//     mapping from script name to page id is data),
//   * that the settings are not available, and why -- SPOKEN, not silent
//     (§3.8: a shortcut that does nothing is indistinguishable from a key the
//     compositor swallowed).
#pragma once

#include "alus/config/config.hpp"
#include "alus/script/scriptable.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace alus::input {
class GestureManager;
class KeyNames;
}
namespace alus::script {
class Catalog;
}

namespace alus::gui {

// Which settings page each NVDA settings script opens. The inherited command
// table has one script per NVDA settings panel (`showSpeechSettings`,
// `showBrailleSettings`, ...), and ALUS has seven pages rather than NVDA's
// fifteen panels, so several scripts land on one page. That mapping is DATA
// and is asserted by tests/gui -- which is what makes T9.1 a runnable test.
struct ScriptPage {
    // The script name as the generated command table spells it.
    std::string script;
    // The page id in gui::pages(), or empty when ALUS has no such page and the
    // command must say so instead of opening the wrong one.
    std::string page;
};

const std::vector<ScriptPage>& scriptPages();
// The page a script opens; empty when there is none.
std::string pageForScript(std::string_view script);

struct ServiceOptions {
    // --no-gui: register the commands, start nothing. What the daemon smoke
    // test and a headless session use.
    bool noGui = false;
};

class Service final : public config::Subsystem {
public:
    struct Hooks {
        std::function<void(const std::string&)> message;
        // Re-read one configuration section. Narrower than
        // resetConfiguration(): changing the speech rate must not restart
        // braille.
        std::function<void(const std::string& section)> reloadSection;
        std::function<void()> resetAll;
    };

    Service(config::Manager& manager, input::GestureManager* gestures,
            const input::KeyNames* keyNames, const script::Catalog* catalog, Hooks hooks,
            ServiceOptions options = {});
    ~Service() override;

    std::string_view name() const override { return "gui"; }
    void initialize() override;
    void terminate() override;

    // The scriptable that owns the settings scripts (`alus+control+g` and the
    // rest). Handed to input::Service::addContextContributor by the daemon, the
    // same way every other layer contributes its scripts -- there is no
    // registry, the dispatch context is assembled per key press (§5.1 step 10).
    script::ScriptableObject* scriptable();

    // True when a window could actually be shown here.
    bool available() const;
    // Why not. Empty when available().
    std::string unavailableReason() const;

    // Opens a page, or says why it cannot. Returns what happened, so the
    // script can report it and the test can assert it.
    enum class OpenResult { Opened, NoSuchPage, NotAvailable, Disabled };
    // Starts `alus-settings` on that page, or raises the one already running
    // (the child does the raising). Returns as soon as the child is forked:
    // the screen reader's main loop never waits for a window.
    OpenResult openPage(std::string_view pageId);
    OpenResult openGestures();

    // Where `alus-settings` is, or empty when it cannot be found. Beside this
    // executable first, then $PATH.
    std::string settingsProgram() const;

    // The page that is showing, or empty. For T9.1.
    std::string currentPage() const;

    // What a settings script does: looks its page up in scriptPages() and
    // either opens it or SAYS why it cannot. Public because the script class
    // is a private nested class that calls it, and because tests/gui drives
    // T9.1 through it without a display.
    void openByScript(std::string_view script);

private:
    class Commands;

    config::Manager& manager_;
    input::GestureManager* gestures_;
    const input::KeyNames* keyNames_;
    const script::Catalog* catalog_;
    Hooks hooks_;
    ServiceOptions options_;
    std::unique_ptr<Commands> commands_;
    std::string page_;
    // Cached; "-" means "looked and did not find it", so the lookup happens
    // once rather than on every key press of a settings shortcut.
    mutable std::string program_;

    static bool hasDisplay();
};

}  // namespace alus::gui
