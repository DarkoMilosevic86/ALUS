// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Speech priorities.
//
// Port of NVDA's `speech/priorities.py`. See 01-ARHITEKTURA.md §4.1.
//
// The numeric values are NVDA's, because they are part of a user-visible
// contract (they appear in NVDA's controller client interface, and add-ons
// pass them around as integers).
#pragma once

#include <array>

namespace alus::speech {

enum class Priority : int {
    // Normal priority: queued behind everything already queued.
    Normal = 0,
    // Spoken after the next utterance of a lower priority completes.
    Next = 1,
    // Important enough to interrupt lower priority speech right now. The
    // interrupted queue RESUMES afterwards. Note that this does NOT interrupt
    // previously queued speech at the same priority — see SpeechManager.
    Now = 2,
};

// Highest first. The manager walks the queues in this order.
inline constexpr std::array<Priority, 3> kPriorities{Priority::Now, Priority::Next,
                                                     Priority::Normal};

const char* priorityName(Priority priority);

}  // namespace alus::speech
