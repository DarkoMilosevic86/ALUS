// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Why a piece of output was produced.
//
// Port of NVDA's `controlTypes/outputReason.py`. This is not decoration: the
// reason decides which properties are spoken at all, whether the role is said,
// and whether "not selected" is worth saying (see §6.5 and
// src/a11y/object_speech.cpp). The same object announced for two different
// reasons is two different utterances.
#pragma once

namespace alus::a11y {

enum class OutputReason {
    // The focus moved here.
    Focus,
    // An ancestor of the new focus: spoken like Focus, with fewer properties.
    FocusEntered,
    // The object under the mouse.
    Mouse,
    // The user asked (alus+tab, alus+numpad5).
    Query,
    // A change to an object that already had been announced.
    Change,
    // A screen-reader message of our own.
    Message,
    // Part of a say-all.
    SayAll,
    // Caret movement or similar.
    Caret,
    // No output at all: only update the "what was last said" cache, so that
    // the next Change can tell what changed.
    OnlyCache,
    // Quick navigation in browse mode (phase 6).
    QuickNav,
};

const char* outputReasonName(OutputReason reason);

}  // namespace alus::a11y
