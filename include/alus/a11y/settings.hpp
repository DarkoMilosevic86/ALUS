// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What the user wants announced.
//
// These are NVDA's `config.conf["presentation"]`, `["documentFormatting"]`,
// `["annotations"]` and `["reviewCursor"]` keys, with NVDA's own defaults --
// which matters more than it looks: `reportTooltips` defaults to FALSE and
// `reportHelpBalloons` to TRUE, `progressBarOutputMode` to "beep" and not
// "both". A screen reader that announces everything it can is unusable, and
// NVDA's defaults are two decades of that lesson.
//
// They live in one struct rather than being read from the configuration at
// each use, because the presentation path runs on every focus change and must
// not do file-backed lookups per property.
#pragma once

#include "alus/text/fields.hpp"
#include "alus/text/unit.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace alus::config {
class Manager;
}

namespace alus::a11y {

// NVDA: config.configFlags.ReportTableHeaders. The numeric values are NVDA's,
// because they appear in the configuration file.
enum class TableHeaders : int { Off = 0, RowsAndColumns = 1, Rows = 2, Columns = 3 };

// NVDA: config.conf["presentation"]["progressBarUpdates"]["progressBarOutputMode"].
enum class ProgressOutput { Off, Beep, Speak, Both };

struct Settings {
    // ---- [presentation] ----
    bool reportObjectDescriptions = true;
    bool reportKeyboardShortcuts = true;
    bool reportObjectPositionInformation = true;
    // False in NVDA too: a tooltip under the pointer is not what the keyboard
    // user is doing.
    bool reportTooltips = false;
    bool reportHelpBalloons = true;
    bool reportMultiSelect = false;
    // NVDA: reportDynamicContentChanges. Whether text that changes by itself
    // (a terminal, a chat window, a log) is announced. True in NVDA too: a
    // terminal that says nothing when a command finishes is unusable.
    bool reportDynamicContentChanges = true;

    // ---- [presentation.progressBarUpdates] ----
    ProgressOutput progressBarOutput = ProgressOutput::Beep;
    bool reportBackgroundProgressBars = false;
    int beepPercentageInterval = 1;
    int speechPercentageInterval = 10;
    double beepMinHz = 110.0;

    // ---- [documentFormatting] (the part the object model uses) ----
    bool reportTables = true;
    bool reportTableCellCoords = true;
    TableHeaders reportTableHeaders = TableHeaders::RowsAndColumns;
    bool reportLandmarks = true;
    bool reportClickable = true;
    bool reportLinkType = true;

    // The rest of [documentFormatting], which the TEXT layer needs (phase 5):
    // what to report about the formatting of text, as opposed to about an
    // object. It is a separate struct because `text::` must not depend on
    // `a11y::` -- the text layer is given these settings, it does not read the
    // configuration. The handful of keys that appear in both places are
    // filled from the same configuration entry by settingsFrom(), so there is
    // one source of truth and two shapes of it (FAZA5.md O4).
    text::FormatReportSettings format;

    // ---- [reviewCursor] ----
    // NVDA: followFocus / followCaret / followMouse / simpleReviewMode.
    bool reviewCursorFollowCaret = true;
    bool reviewCursorFollowMouse = false;
    bool reviewCursorSimple = true;

    // ---- [documentNavigation] ----
    text::ParagraphStyle paragraphStyle = text::ParagraphStyle::Application;
    // What say-all reads at a time (NVDA: sayAllReadingUnit).
    text::ReadingUnit sayAllReadingUnit = text::ReadingUnit::Paragraph;

    // ---- [editableText] ----
    // NVDA: caretMoveTimeoutMs. How long ALUS waits for an application to move
    // its caret before giving up and reporting what is there (§8.3).
    int caretMoveTimeoutMilliseconds = 100;

    // ---- [annotations] ----
    bool reportDetails = true;
    bool reportAriaDescription = true;

    // ---- [reviewCursor] ----
    // The navigator object follows the focus. NVDA's default, and the reason
    // alus+numpad5 says what was just focused.
    bool reviewCursorFollowFocus = true;

    // ---- [objectNavigation] (ALUS's own) ----
    // Announce an object navigation move that hits the edge of the tree with a
    // sound rather than silence. §3.8: an expected failure must be noticeable
    // without being an error.
    bool reportNavigationEdge = true;
};

// The factory defaults, into the configuration manager (NVDA: confspec).
void registerSettingDefaults(config::Manager& manager);
// Reads every key above. Called at start-up and from resetConfiguration().
Settings settingsFrom(const config::Manager& manager);

const char* progressOutputName(ProgressOutput output);
ProgressOutput parseProgressOutput(std::string_view name);

}  // namespace alus::a11y
