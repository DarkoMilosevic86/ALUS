// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The system clipboard, through `alus-clipboard` (src/gui/clipboard_main.cpp
// says why it is a process of its own, on X11). Both calls are asynchronous:
// the child is spawned, the answer comes back on a pipe watched by the main
// loop, and the screen reader never waits for it (phase-10.md N145).
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace alus::core {

// Where `alus-clipboard` is: beside this executable, then the uninstalled
// build's src/, then $PATH. Empty when it is not there.
std::string clipboardProgram();

// Reads the clipboard's text. `done` runs on the main loop: the text, or
// nullopt when there is none (or no clipboard could be reached). False when
// the helper could not be started at all; `done` is then not called.
bool readClipboard(std::function<void(std::optional<std::string>)> done);

// Puts `text` on the clipboard. `done` runs on the main loop with whether the
// clipboard is now ours. False when the helper could not be started.
bool writeClipboard(const std::string& text, std::function<void(bool)> done);

}  // namespace alus::core
