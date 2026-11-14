// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Dropping key events the platform delivered twice.
//
// MEASURED, not defensive (finding N33). In the interactive run on GNOME 48.7,
// 14 of 56 key events arrived a second time, every duplicate 0-2 ms after the
// first, and the effect was audible: "numpad 7" was announced twice and a
// typed character would have been echoed twice. Mutter notifies each
// accessibility client once per event
// (`MetaA11yManager::notify_clients`), so the event itself goes through the
// compositor's event filter twice.
//
// The window separates the quirk from two real things that look similar:
//
//   key auto-repeat    >= 30 ms even at the fastest rate
//   a human double tap >= 50 ms
//
// 5 ms sits well below both and 2.5x above the largest gap measured.
//
// AND THE DUPLICATE IS NOT ALWAYS THE NEXT EVENT (N130). Typing fast, keys
// overlap, and the doubled stream interleaves:
//
//     up p, down e, up p (again), down e (again)
//
// A filter that compared each event with the previous one only let both
// copies of `e` through, and the user heard "peeku" while typing "peku". So
// the last few events are remembered, and a copy of any of them inside the
// window is dropped. Two identical events can never be real within 5 ms: a
// key cannot go down twice without coming up in between.
#pragma once

#include "alus/input/gesture.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace alus::input {

class KeyEventFilter {
public:
    static constexpr std::chrono::milliseconds kDefaultWindow{5};

    explicit KeyEventFilter(std::chrono::milliseconds window = kDefaultWindow)
        : window_(window) {}

    // True when this event repeats one of the last few and should be
    // dropped. Call it exactly once per event: it records what it saw.
    bool isDuplicate(const KeyEvent& event,
                     std::chrono::steady_clock::time_point now =
                         std::chrono::steady_clock::now()) {
        bool same = false;
        for (std::size_t i = 0; i < count_; ++i) {
            const Seen& seen = recent_[i];
            if (event.pressed == seen.event.pressed && event.keycode == seen.event.keycode &&
                event.keysym == seen.event.keysym && event.state == seen.event.state &&
                now - seen.time < window_) {
                same = true;
                break;
            }
        }
        if (same) {
            ++dropped_;
            return true;
        }
        // Newest first; the oldest falls off.
        for (std::size_t i = kRemembered - 1; i > 0; --i) {
            recent_[i] = recent_[i - 1];
        }
        recent_[0] = Seen{event, now};
        if (count_ < kRemembered) {
            ++count_;
        }
        return false;
    }

    std::uint64_t dropped() const noexcept { return dropped_; }
    std::chrono::milliseconds window() const noexcept { return window_; }

private:
    // Enough for two overlapping keys, each with its down and up.
    static constexpr std::size_t kRemembered = 4;
    struct Seen {
        KeyEvent event{};
        std::chrono::steady_clock::time_point time{};
    };
    std::chrono::milliseconds window_;
    std::array<Seen, kRemembered> recent_{};
    std::size_t count_ = 0;
    std::uint64_t dropped_ = 0;
};

}  // namespace alus::input
