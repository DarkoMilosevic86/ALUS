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
#pragma once

#include "alus/input/gesture.hpp"

#include <chrono>
#include <cstdint>

namespace alus::input {

class KeyEventFilter {
public:
    static constexpr std::chrono::milliseconds kDefaultWindow{5};

    explicit KeyEventFilter(std::chrono::milliseconds window = kDefaultWindow)
        : window_(window) {}

    // True when this event is a repeat of the previous one and should be
    // dropped. Call it exactly once per event: it records what it saw.
    bool isDuplicate(const KeyEvent& event,
                     std::chrono::steady_clock::time_point now =
                         std::chrono::steady_clock::now()) {
        const bool same = seen_ && event.pressed == last_.pressed &&
                          event.keycode == last_.keycode && event.keysym == last_.keysym &&
                          event.state == last_.state && now - lastTime_ < window_;
        last_ = event;
        lastTime_ = now;
        seen_ = true;
        if (same) {
            ++dropped_;
        }
        return same;
    }

    std::uint64_t dropped() const noexcept { return dropped_; }
    std::chrono::milliseconds window() const noexcept { return window_; }

private:
    std::chrono::milliseconds window_;
    KeyEvent last_{};
    std::chrono::steady_clock::time_point lastTime_{};
    bool seen_ = false;
    std::uint64_t dropped_ = 0;
};

}  // namespace alus::input
