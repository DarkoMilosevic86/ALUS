// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What identifies an application, for the purpose of choosing its app module.
//
// Port of the concept behind NVDA's `appModuleHandler.getAppNameFromProcessID`
// and `EXECUTABLE_NAMES_TO_APP_MODS`, with 01-ARHITEKTURA.md §12.1's key:
//
//     primary:       basename(readlink("/proc/<pid>/exe"))
//     secondary:     the Application accessible's name
//     discriminator: Application::get_toolkit_name() + version
//
// WHY THE EXECUTABLE AND NOT THE ACCESSIBLE NAME, when the accessible name is
// free and the executable costs a readlink. Because the accessible name is
// what the application chose to call itself, in the user's language and
// subject to change: LibreOffice Writer calls itself "soffice" on one build
// and "LibreOffice Writer" on another, GNOME Terminal is "gnome-terminal-server"
// as a process and "Terminal" as an accessible, and a Qt application whose
// window title leaks into the application name changes its own identity when
// the user opens a file. Measured on the target system, where the four
// applications phase 8 ships modules for report:
//
//     soffice.bin             toolkit "gail"      name "soffice"
//     gnome-terminal-server   toolkit "gail"      name "gnome-terminal-server"
//     code                    toolkit "chromium"  name "Code"
//     nautilus                toolkit "gtk"       name "nautilus"
//
// The toolkit is the DISCRIMINATOR and it is what replaces NVDA's
// `windowClassName`: it is the thing that says how an application behaves even
// when nobody has written a module for it, which is why a rule may key on it
// alone (every Electron application at once, T8.1).
//
// THREADING: main thread only, except `executableForPid`, which only reads
// /proc and is safe anywhere.
#pragma once

#include "alus/a11y/backend.hpp"

#include <string>
#include <string_view>

namespace alus::app {

struct AppKey {
    // Lower-cased basename of the executable; empty when /proc could not be
    // read, which is the normal answer for a sandboxed (Flatpak, Snap)
    // application in its own PID namespace.
    std::string executable;
    // The Application accessible's own name, as the platform reports it. Kept
    // in its original case: it is shown to the user in `alus-cli app-info`.
    std::string name;
    std::string toolkit;  // lower-cased
    std::string toolkitVersion;
    int processId = 0;

    bool empty() const noexcept {
        return executable.empty() && name.empty() && toolkit.empty();
    }
    // One line for the log: "soffice.bin (gail 2.56.2, pid 4711)".
    std::string describe() const;
};

// Reads /proc/<pid>/exe. Empty when the process is gone, when it lives in
// another PID namespace, or when the link is not readable -- all of which are
// ordinary, so a caller must not treat an empty answer as an error.
std::string executableForPid(int processId);

// The key for an application the object model has already described. The only
// part that is not already in `AppInfo` is the executable, so this is one
// readlink.
AppKey keyFor(const a11y::AppInfo& info);

// NVDA's `EXECUTABLE_NAMES_TO_APP_MODS`, as a function: the executable name a
// module is registered under. `code-insiders` and `code-oss` are the same
// editor as `code`; `soffice.bin` is the same suite as `soffice`. Returns the
// argument unchanged when there is no alias, so it is safe to call on
// everything.
//
// The built-in table is small on purpose. An alias is a statement that two
// executables behave identically, and that is cheap to add and expensive to be
// wrong about; the registry takes more of them at run time
// (`Registry::addAlias`).
std::string_view canonicalExecutable(std::string_view executable);

}  // namespace alus::app
