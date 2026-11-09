// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/audio/tones.hpp"

#include "alus/core/log.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace alus::audio {

core::Decider<double, int, int, int, bool>& decideBeep() {
    static core::Decider<double, int, int, int, bool> decider;
    return decider;
}

std::vector<std::uint8_t> generateBeep(const Format& format, double hz, int milliseconds,
                                       int left, int right) {
    std::vector<std::uint8_t> out;
    if (milliseconds <= 0 || hz <= 0.0 || format.sampleRate == 0 || format.channels == 0 ||
        format.bitsPerSample != 16) {
        return out;
    }
    const auto frames = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(format.sampleRate) * static_cast<std::uint64_t>(milliseconds)) /
        1000ULL);
    if (frames == 0) {
        return out;
    }

    // 5 ms of ramp at each end, or a quarter of the tone if it is shorter than
    // that — otherwise the ramps would overlap.
    const std::size_t ramp =
        std::min(frames / 4, static_cast<std::size_t>(format.sampleRate / 200U));
    const double amplitudeLeft = static_cast<double>(std::clamp(left, 0, 100)) / 100.0;
    const double amplitudeRight = static_cast<double>(std::clamp(right, 0, 100)) / 100.0;
    const double step = 2.0 * std::numbers::pi * hz / static_cast<double>(format.sampleRate);
    constexpr double kPeak = 32767.0;

    out.resize(frames * format.bytesPerFrame());
    auto* samples = reinterpret_cast<std::int16_t*>(out.data());
    for (std::size_t frame = 0; frame < frames; ++frame) {
        double envelope = 1.0;
        if (ramp > 0) {
            if (frame < ramp) {
                envelope = static_cast<double>(frame) / static_cast<double>(ramp);
            } else if (frame >= frames - ramp) {
                envelope = static_cast<double>(frames - frame) / static_cast<double>(ramp);
            }
        }
        const double value = std::sin(step * static_cast<double>(frame)) * envelope * kPeak;
        for (unsigned channel = 0; channel < format.channels; ++channel) {
            const double gain = (format.channels == 2 && channel == 1) ? amplitudeRight
                                                                       : amplitudeLeft;
            samples[frame * format.channels + channel] =
                static_cast<std::int16_t>(value * gain);
        }
    }
    return out;
}

void beep(double hz, int milliseconds, int left, int right, bool isSpeechBeepCommand) {
    ALUS_IO("beep at {} Hz for {} ms, left {}, right {}", hz, milliseconds, left, right);
    if (!decideBeep().decide(hz, milliseconds, left, right, isSpeechBeepCommand)) {
        ALUS_DEBUG("beep cancelled by a decide_beep handler");
        return;
    }
    WavePlayer* player = soundsPlayer();
    if (player == nullptr) {
        return;
    }
    const std::vector<std::uint8_t> data = generateBeep(player->format(), hz, milliseconds, left,
                                                        right);
    if (data.empty()) {
        return;
    }
    // NVDA stops before feeding: a new beep replaces the previous one instead
    // of queueing behind it.
    player->stop();
    player->feed(data);
}

}  // namespace alus::audio
