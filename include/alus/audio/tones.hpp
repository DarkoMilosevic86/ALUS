// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Tone generation.
//
// Port of NVDA's `tones.py` plus `generateBeep` from NVDAHelper. Beeps are a
// real part of the interface, not decoration: progress bars, the speech-mode
// indicator and BeepCommand inside speech sequences all use them.
//
// One addition to NVDA's generator: a short amplitude ramp at both ends of the
// tone. NVDA's square-wave generator clicks; a screen reader that clicks on
// every progress-bar tick is tiring, and the ramp costs nothing.
#pragma once

#include "alus/audio/wave_player.hpp"
#include "alus/core/extension_points.hpp"

#include <cstdint>
#include <vector>

namespace alus::audio {

// NVDA: tones.decide_beep. Lets a component (a remote-control add-on, a test)
// decide that a beep must not be played.
core::Decider<double, int, int, int, bool>& decideBeep();

// 16-bit stereo samples in `format`, `left`/`right` as percentages (0–100).
std::vector<std::uint8_t> generateBeep(const Format& format, double hz, int milliseconds,
                                       int left = 50, int right = 50);

// Plays a tone through the process-wide sounds player. Silently does nothing
// before audio::initialize().
void beep(double hz, int milliseconds, int left = 50, int right = 50,
          bool isSpeechBeepCommand = false);

}  // namespace alus::audio
