// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Which commands ALUS has not written yet, and what the key does meanwhile.
//
// data/scripts.csv and data/gestures-builtin.ini are generated from upstream,
// so they say what a command IS and which key runs it -- never whether ALUS
// implements it. data/commands-status.ini is the hand-maintained third source
// that says so, and it is what makes an unfinished command AUDIBLE:
//
//   [announce]   the key is bound and says the command is not in this
//                version. §3.8's rule and phase-6.md's O10: an expected gap is
//                reported, never swallowed. A blind user can tell an
//                unfinished feature from a broken one only if it speaks.
//   [platform]   the key must NOT be bound, because it belongs to the
//                application and ALUS cannot give it back (N18: no key
//                injection on Wayland). Grabbing it to announce would cost
//                the user the key and give them a sentence.
//
// A command that is in neither list is expected to be implemented, and
// tests/coverage/test_command_coverage.cpp is what checks that it is.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace alus::script {

class Catalog;
class ScriptableObject;

class CommandStatus {
public:
    enum class State {
        // Not in either list: ALUS is expected to implement it.
        Implemented,
        // [announce]: bound to a spoken "not in this version".
        Announce,
        // [platform]: deliberately unbound; the application needs the key.
        Platform,
        // [missing]: not implemented, no key; hidden from every list (N154).
        Missing,
    };

    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    bool loadFromString(std::string_view text, std::string* error = nullptr);

    State stateOf(std::string_view script) const;
    // The note written beside the command, or empty.
    std::string_view reasonFor(std::string_view script) const;

    const std::map<std::string, std::string, std::less<>>& announced() const noexcept {
        return announce_;
    }
    const std::map<std::string, std::string, std::less<>>& platformLimited() const noexcept {
        return platform_;
    }
    const std::map<std::string, std::string, std::less<>>& missing() const noexcept {
        return missing_;
    }
    // Whether the command belongs in the lists the user sees (the Input
    // gestures dialog, the commands quick reference): only what ALUS really
    // does. A key that explains why a command is absent still works.
    bool isAvailable(std::string_view script) const { return stateOf(script) == State::Implemented; }
    std::size_t size() const noexcept {
        return announce_.size() + platform_.size() + missing_.size();
    }
    bool empty() const noexcept { return size() == 0; }

private:
    std::map<std::string, std::string, std::less<>> announce_;
    std::map<std::string, std::string, std::less<>> platform_;
    std::map<std::string, std::string, std::less<>> missing_;
};

// Binds every [announce] command that `object` may legitimately answer for --
// the catalogue's location for the command is one of the NVDA class names the
// object answers to -- and that the object does not already implement, to a
// spoken "not in this version". Returns how many were bound.
//
// WHERE IT IS CALLED MATTERS, and the reason is the grab set: ALUS asks the
// lookup whether a gesture would find a script in order to decide whether the
// compositor should hand ALUS the key at all. So an announcement bound in the
// wrong place takes a key away from the application. It is called on
//
//   * a global-commands object consulted LAST in step 10, which is why it
//     cannot shadow the real implementations in the other subsystems, and
//   * browse mode itself, reached at step 6 only while the focus is in a
//     document -- which is the only place `kb:p` may be taken from the
//     application at all.
//
// The [platform] list is never bound anywhere: see the header comment.
std::size_t bindAnnouncements(ScriptableObject& object, const Catalog& catalog,
                              std::function<void(const std::string&)> say);

}  // namespace alus::script
