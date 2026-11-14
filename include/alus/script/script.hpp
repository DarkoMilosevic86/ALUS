// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// A script: one command, its metadata and its implementation.
//
// Port of NVDA's `@script` decorator and `scriptHandler` (architecture.md
// §5.3). The attributes carried here are not decoration — each one changes
// dispatch:
//
//   description   what input help speaks and the gesture dialog shows; a
//                 script WITHOUT one is hidden from the dialog, as in NVDA
//   category      how the gesture dialog groups commands
//   canPropagate  whether the script also applies on a focus ANCESTOR
//   bypassInputHelp   whether it runs even while input help is on
//   allowInSleepMode  whether it runs while the app is in sleep mode
//   resumeSayAllMode  which say-all is resumed after it runs
//   speakOnDemand     whether it speaks in the "on demand" speech mode
//   ignoreTreeInterceptorPassThrough
//                 whether it survives a tree interceptor in pass-through
//                 mode — the flag that makes single-letter quick nav work in
//                 browse mode and fall through in focus mode (§5.1, step 6)
//   needsKeyPassThrough
//                 whether the APPLICATION must also receive the key. NVDA has
//                 no such flag because it can always re-send a key it
//                 swallowed; on Wayland ALUS cannot (N18), so a script that
//                 only OBSERVES a key the application is acting on has to say
//                 so, or the key would be grabbed and the application would
//                 never see it. Phase 5's caret commands are exactly this
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace alus::input {
class Gesture;
}

namespace alus::script {

// What a script's say-all resumption refers to.
enum class SayAllCursor { None, Caret, Review };

SayAllCursor sayAllCursorFromName(std::string_view name);
std::string_view sayAllCursorName(SayAllCursor cursor);

// The metadata of one script. Stable in memory for the lifetime of the
// scriptable object that owns it: the ADDRESS is the script's identity for
// repeat counting, which is how NVDA's `lastScriptRepeatCount` works
// (it compares the underlying function object).
struct ScriptInfo {
    std::string name;
    std::string location;  // "globalCommands.GlobalCommands"
    std::string description;
    std::string category;
    std::vector<std::string> gestures;
    bool canPropagate = false;
    bool bypassInputHelp = false;
    bool allowInSleepMode = false;
    bool speakOnDemand = false;
    bool ignoreTreeInterceptorPassThrough = false;
    // See the file comment: the application must receive this key too, so it
    // must not be grabbed on a platform that cannot re-send it.
    bool needsKeyPassThrough = false;
    SayAllCursor resumeSayAllMode = SayAllCursor::None;

    // NVDA hides a script with no description from the gesture dialog (and so
    // must ALUS: T9.3), because a command with no description cannot be
    // explained to the user who is rebinding it.
    bool isVisibleInGestureDialog() const { return !description.empty(); }
};

using ScriptFunction = std::function<void(input::Gesture&)>;

// What ALUS_SCRIPT writes down at the binding site. Anything left empty is
// filled from the generated catalog (data/scripts.csv), so NVDA's own
// descriptions and categories are not transcribed by hand.
struct ScriptSpec {
    std::string description;
    std::string category;
    std::vector<std::string> gestures;
    bool canPropagate = false;
    bool bypassInputHelp = false;
    bool allowInSleepMode = false;
    bool speakOnDemand = false;
    bool ignoreTreeInterceptorPassThrough = false;
    bool needsKeyPassThrough = false;
    SayAllCursor resumeSayAllMode = SayAllCursor::None;
};

// A resolved script: what to run and what it is.
struct Script {
    const ScriptInfo* info = nullptr;
    ScriptFunction function;

    // The identity used for repeat counting and for "a script may not run
    // inside itself".
    const void* identity() const noexcept { return static_cast<const void*>(info); }
    bool valid() const noexcept { return info != nullptr && static_cast<bool>(function); }
    explicit operator bool() const noexcept { return valid(); }
};

}  // namespace alus::script
