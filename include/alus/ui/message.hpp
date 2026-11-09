// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Messages to the user.
//
// Port of the part of NVDA's `ui.py` that phase 2 can honestly provide:
// `ui.message()`. In NVDA this is the single entry point every script uses to
// tell the user something ("Browse mode", "No next heading"), and it goes to
// BOTH speech and braille. Braille arrives in phase 7 and subscribes to
// `messageShown()`; nothing else about the interface changes then.
#pragma once

#include "alus/core/extension_points.hpp"
#include "alus/speech/priority.hpp"

#include <string>

namespace alus::speech {
class Presenter;
}

namespace alus::ui {

// The presenter may be null, which makes message() a no-op; that is what
// `alus-daemon --no-speech` and the unit tests of other subsystems want.
void initialize(speech::Presenter* presenter);
void terminate();
bool isInitialized();

// NVDA: ui.message(). Blank text is announced as "blank" rather than silently
// dropped (Presenter::speakMessage).
void message(const std::string& text, speech::Priority priority = speech::Priority::Normal);

// Notified for every message, so other outputs (braille in phase 7, the speech
// viewer in phase 9) can show the same text.
core::Action<const std::string&, speech::Priority>& messageShown();

}  // namespace alus::ui
