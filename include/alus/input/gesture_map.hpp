// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// A gesture map: `gestures.ini`, loaded, queried and written back.
//
// Port of NVDA's `inputCore.GlobalGestureMap` (architecture.md §5.1). The
// file format is NVDA's, down to the section names, because a hard requirement
// (vision.md H3) is that a user's own NVDA `gestures.ini` loads into ALUS
// WITHOUT EDITS:
//
//     [globalCommands.GlobalCommands]
//         zoomOut = kb:nvda+shift+'
//         leftMouseClick = kb(laptop):nvda+š
//         None = kb:h, kb:control+h
//
//   * the section is the scriptable class the binding applies to;
//   * the key is the script name, the value a comma separated gesture list;
//   * the script name `None` UNBINDS those gestures for that class;
//   * a script name of the form `kb:<keyname>` means "emulate that key"
//     rather than "run a script" (NVDA: `_makeKbEmulateScript`).
//
// Written by hand rather than with GKeyFile: GKeyFile rejects a repeated key,
// has no notion of the `None` entry, and would quote the values on write.
// The format is three rules; the parser is cheaper than the mismatch.
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace alus::input {

class GestureMap {
public:
    struct Entry {
        // "globalCommands.GlobalCommands" — the scriptable class.
        std::string location;
        // The script name, or empty for an unbind (the `None` entry).
        std::string script;
    };

    GestureMap() = default;
    explicit GestureMap(std::string name) : name_(std::move(name)) {}

    const std::string& name() const noexcept { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    void clear();
    bool empty() const noexcept { return map_.empty(); }
    std::size_t size() const noexcept { return map_.size(); }

    // A malformed line never aborts the load: the rest of the file is still
    // the user's configuration. The flag is what the GUI reports afterwards.
    bool lastUpdateContainedError() const noexcept { return lastUpdateContainedError_; }

    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    bool loadFromString(std::string_view text, std::string* error = nullptr);
    bool save(const std::filesystem::path& path, std::string* error = nullptr) const;
    // The file contents this map would write.
    std::string serialize() const;

    // `gesture` is normalized on the way in, so callers may pass either form.
    void add(std::string_view gesture, std::string_view location, std::string_view script,
             bool replace = false);
    bool remove(std::string_view gesture, std::string_view location, std::string_view script);

    // Bindings for an identifier that is ALREADY normalized, in insertion
    // order. The name says "normalized" because the identifiers are stored
    // sorted (`kb:nvda+numpad6` is stored as `kb:numpad6+nvda`), and a lookup
    // with the unsorted spelling silently finds nothing; the hot path has the
    // normalized form in hand already (Gesture::normalizedIdentifiers).
    const std::vector<Entry>* entriesForNormalized(std::string_view normalizedGesture) const;

    // Every (normalized gesture, entry) pair; for the gesture dialog and for
    // computing the set of keys the compositor has to grab.
    std::vector<std::pair<std::string, Entry>> allEntries() const;

    const std::filesystem::path& fileName() const noexcept { return fileName_; }

private:
    std::string name_;
    std::filesystem::path fileName_;
    // Ordered so that serialization is reproducible.
    std::map<std::string, std::vector<Entry>, std::less<>> map_;
    bool lastUpdateContainedError_ = false;
};

}  // namespace alus::input
