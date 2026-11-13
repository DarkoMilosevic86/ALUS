// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What the settings window CONTAINS, as data, with no GTK in sight.
//
// WHY THE MODEL IS SEPARATE FROM THE WIDGETS. 02-FAZE.md's rule for every
// phase is that the exit criterion is a runnable test. A GTK window needs a
// display, and a test that needs a display does not run in CI -- so if the
// pages were built inline in `settings_window.cpp`, T9.1 ("the shortcuts open
// the right settings") and T9.3 ("a script with no description does not
// appear") would be checked by a person clicking, which is exactly what this
// project does not accept as a criterion.
//
// So the window is two layers:
//
//   * this one: every page, every group, every row, and for each row the
//     configuration section and key it reads and writes. Pure C++, no display,
//     fully asserted in tests/gui.
//   * src/gui/settings_window.cpp: a loop that turns a Row into an
//     AdwSwitchRow / AdwComboRow / AdwSpinRow. It holds no knowledge of what
//     any setting MEANS, so there is nothing in it a test would want to check
//     beyond "it ran", which the smoke test does when a display is there.
//
// The second consequence matters as much: a row cannot name a key that does
// not exist, because tests/gui asserts every row's (section, key) against the
// defaults every subsystem registers. A settings window with a typo in a key
// silently writes a setting nothing reads, and the user sees a switch that
// does nothing -- the sort of defect that survives a long time.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace alus::config {
class Manager;
}

namespace alus::gui {

enum class RowKind {
    // A boolean: AdwSwitchRow.
    Toggle,
    // One of a fixed set of values: AdwComboRow. The stored value is
    // `Choice::value`, which is the configuration spelling and never
    // translated; `Choice::label` is what the user sees and is.
    Choice,
    // A number in a range: AdwSpinRow.
    Integer,
    // Free text: AdwEntryRow. Used for the BrlAPI host and little else; a
    // screen reader's settings are almost all switches and choices.
    Text,
    // Not a setting: a button that opens something (the gesture dialog, the
    // configuration directory). `Row::action` says which.
    Action,
};

struct Choice {
    // The configuration spelling. NEVER translated: a configuration file
    // written in Serbian has to load in English.
    std::string value;
    // What the user sees. A msgid (the tables are built with N_()), translated
    // when the row is rendered.
    const char* label = nullptr;
    // A label that is NOT a msgid and is shown as it is: a voice's name, a
    // speech-dispatcher module's name. Used instead of `label` when set.
    // Those names come from the synthesizer at run time, and passing them
    // through gettext would at best do nothing and at worst translate a voice
    // that happens to share its name with a word in the catalogue.
    std::string text;
};

struct Row {
    RowKind kind = RowKind::Toggle;
    // The configuration section and key. Empty for RowKind::Action.
    std::string section;
    std::string key;
    // msgids, translated at render time.
    const char* title = nullptr;
    // May be null. The sentence under the title: what the setting is FOR,
    // which is the only documentation most users will read.
    const char* subtitle = nullptr;
    // RowKind::Choice.
    std::vector<Choice> choices;
    // RowKind::Integer.
    int minimum = 0;
    int maximum = 100;
    int step = 1;
    // RowKind::Action. One of the ids in `actions.hpp`-free form:
    // "gestures", "config-dir", "profiles", "reset".
    std::string action;
    // RowKind::Choice whose values come from the running system rather than
    // from this table: "speech-modules" or "speech-voices". `choices` then
    // holds only the fixed entries that come first ("Default"); the rest is
    // resolveChoices()'s job.
    std::string source;
};

struct Group {
    const char* title = nullptr;  // msgid, may be null for an untitled group
    const char* description = nullptr;
    std::vector<Row> rows;
};

struct Page {
    // The stable id. This is what a shortcut opens (`openPage("braille")`) and
    // what the tests name, so it is NOT translated and does not change.
    std::string id;
    const char* title = nullptr;     // msgid
    const char* iconName = nullptr;  // a stock icon name, not translated
    std::vector<Group> groups;
};

// What speech-dispatcher offers, asked once when the window opens.
//
// WHY THESE TWO SETTINGS ARE CHOICES AND NOT TEXT ENTRIES. They used to be
// free text, which meant typing the exact name of a voice -- the one thing a
// user who cannot make out the synthesizer cannot find out -- and an empty
// entry was written as `voice=\s` (FAZA10.md N107). A choice can only hold a
// value the synthesizer actually has.
struct SpeechChoices {
    // False when speech-dispatcher could not be reached. The rows then offer
    // the fixed entries and whatever the configuration already says, and
    // nothing else: inventing names would be worse than offering none.
    bool reachable = false;
    std::vector<std::string> modules;
    struct Voice {
        std::string id;        // what the configuration stores
        std::string name;      // what the user hears
        std::string language;  // BCP 47, may be empty
    };
    std::vector<Voice> voices;
};

// Asks speech-dispatcher, through the same driver the screen reader uses.
// `module` is the configured output module, because the voice list is the
// MODULE's: rhvoice and espeak-ng offer different voices. Never throws; an
// unreachable server is `reachable == false`.
SpeechChoices querySpeechChoices(const std::string& module);

// The choices a row shows: its fixed ones, then what `speech` offers for its
// `source`, then -- if the configuration names something the system does not
// offer -- that value too, labelled as not offered. The last one matters: a
// stale `voice=` must be VISIBLE in the window, or the user cannot see what to
// replace. Rows without a `source` get `row.choices` unchanged.
std::vector<Choice> resolveChoices(const Row& row, const SpeechChoices& speech,
                                   const std::string& current);

// Every page, in the order the window shows them. Built once, const
// thereafter.
const std::vector<Page>& pages();
// Null when there is no such page; callers report that rather than crashing,
// because a shortcut's page id comes from the script table.
const Page* findPage(std::string_view id);

// Every (section, key) any row names. For the test that checks them all
// against the registered defaults.
std::vector<std::pair<std::string, std::string>> allSettingKeys();

// The page ids a keyboard shortcut can open, in the order of `pages()`.
std::vector<std::string> pageIds();

}  // namespace alus::gui
