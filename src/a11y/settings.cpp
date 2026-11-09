// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/settings.hpp"

#include "alus/config/config.hpp"
#include "alus/text/unit.hpp"

#include <algorithm>

namespace alus::a11y {
namespace {

// The section names are NVDA's, so that a user's two configurations read the
// same way side by side.
constexpr const char* kPresentation = "presentation";
constexpr const char* kProgress = "presentation.progressBarUpdates";
constexpr const char* kFormatting = "documentFormatting";
constexpr const char* kAnnotations = "annotations";
constexpr const char* kReview = "reviewCursor";
constexpr const char* kNavigation = "objectNavigation";
constexpr const char* kDocumentNavigation = "documentNavigation";
constexpr const char* kEditableText = "editableText";

std::string boolText(bool value) { return value ? "true" : "false"; }

const char* lineIndentationName(text::FormatReportSettings::LineIndentation value) {
    using LineIndentation = text::FormatReportSettings::LineIndentation;
    switch (value) {
        case LineIndentation::Off:
            return "off";
        case LineIndentation::Speech:
            return "speech";
        case LineIndentation::Tones:
            return "tones";
        case LineIndentation::SpeechAndTones:
            return "speechAndTones";
    }
    return "off";
}

text::FormatReportSettings::LineIndentation parseLineIndentation(std::string_view name) {
    using LineIndentation = text::FormatReportSettings::LineIndentation;
    if (name == "speech") {
        return LineIndentation::Speech;
    }
    if (name == "tones") {
        return LineIndentation::Tones;
    }
    if (name == "speechAndTones") {
        return LineIndentation::SpeechAndTones;
    }
    return LineIndentation::Off;
}

}  // namespace

const char* progressOutputName(ProgressOutput output) {
    switch (output) {
        case ProgressOutput::Off:
            return "off";
        case ProgressOutput::Beep:
            return "beep";
        case ProgressOutput::Speak:
            return "speak";
        case ProgressOutput::Both:
            return "both";
    }
    return "beep";
}

ProgressOutput parseProgressOutput(std::string_view name) {
    if (name == "off") {
        return ProgressOutput::Off;
    }
    if (name == "speak") {
        return ProgressOutput::Speak;
    }
    if (name == "both") {
        return ProgressOutput::Both;
    }
    return ProgressOutput::Beep;
}

void registerSettingDefaults(config::Manager& manager) {
    const Settings defaults;
    manager.setDefault(kPresentation, "reportObjectDescriptions",
                       boolText(defaults.reportObjectDescriptions));
    manager.setDefault(kPresentation, "reportKeyboardShortcuts",
                       boolText(defaults.reportKeyboardShortcuts));
    manager.setDefault(kPresentation, "reportObjectPositionInformation",
                       boolText(defaults.reportObjectPositionInformation));
    manager.setDefault(kPresentation, "reportTooltips", boolText(defaults.reportTooltips));
    manager.setDefault(kPresentation, "reportHelpBalloons", boolText(defaults.reportHelpBalloons));
    manager.setDefault(kPresentation, "reportMultiSelect", boolText(defaults.reportMultiSelect));
    manager.setDefault(kPresentation, "reportDynamicContentChanges",
                       boolText(defaults.reportDynamicContentChanges));

    manager.setDefault(kProgress, "progressBarOutputMode",
                       progressOutputName(defaults.progressBarOutput));
    manager.setDefault(kProgress, "reportBackgroundProgressBars",
                       boolText(defaults.reportBackgroundProgressBars));
    manager.setDefault(kProgress, "beepPercentageInterval",
                       std::to_string(defaults.beepPercentageInterval));
    manager.setDefault(kProgress, "speechPercentageInterval",
                       std::to_string(defaults.speechPercentageInterval));
    manager.setDefault(kProgress, "beepMinHZ",
                       std::to_string(static_cast<int>(defaults.beepMinHz)));

    manager.setDefault(kFormatting, "reportTables", boolText(defaults.reportTables));
    manager.setDefault(kFormatting, "reportTableCellCoords",
                       boolText(defaults.reportTableCellCoords));
    manager.setDefault(kFormatting, "reportTableHeaders",
                       std::to_string(static_cast<int>(defaults.reportTableHeaders)));
    manager.setDefault(kFormatting, "reportLandmarks", boolText(defaults.reportLandmarks));
    manager.setDefault(kFormatting, "reportClickable", boolText(defaults.reportClickable));
    manager.setDefault(kFormatting, "reportLinkType", boolText(defaults.reportLinkType));

    manager.setDefault(kAnnotations, "reportDetails", boolText(defaults.reportDetails));
    manager.setDefault(kAnnotations, "reportAriaDescription",
                       boolText(defaults.reportAriaDescription));

    // The text layer's half of [documentFormatting] (phase 5). NVDA's keys and
    // NVDA's defaults: reporting every font change on every line is what makes
    // a screen reader unusable, which is why most of these are off.
    const text::FormatReportSettings& format = defaults.format;
    manager.setDefault(kFormatting, "reportFontName", boolText(format.reportFontName));
    manager.setDefault(kFormatting, "reportFontSize", boolText(format.reportFontSize));
    manager.setDefault(kFormatting, "reportFontAttributes",
                       boolText(format.reportFontAttributes));
    manager.setDefault(kFormatting, "reportSuperscriptsAndSubscripts",
                       boolText(format.reportSuperscriptsAndSubscripts));
    manager.setDefault(kFormatting, "reportColor", boolText(format.reportColor));
    manager.setDefault(kFormatting, "reportEmphasis", boolText(format.reportEmphasis));
    manager.setDefault(kFormatting, "reportStyle", boolText(format.reportStyle));
    manager.setDefault(kFormatting, "reportAlignment", boolText(format.reportAlignment));
    manager.setDefault(kFormatting, "reportLineSpacing", boolText(format.reportLineSpacing));
    manager.setDefault(kFormatting, "reportParagraphIndentation",
                       boolText(format.reportParagraphIndentation));
    manager.setDefault(kFormatting, "reportCellBorders", boolText(format.reportBorderStyle));
    manager.setDefault(kFormatting, "reportLineNumber", boolText(format.reportLineNumber));
    manager.setDefault(kFormatting, "reportPage", boolText(format.reportPage));
    manager.setDefault(kFormatting, "reportSpellingErrors",
                       boolText(format.reportSpellingErrors));
    manager.setDefault(kFormatting, "reportGrammarErrors",
                       boolText(format.reportGrammarErrors));
    manager.setDefault(kFormatting, "reportComments", boolText(format.reportComments));
    manager.setDefault(kFormatting, "reportBookmarks", boolText(format.reportBookmarks));
    manager.setDefault(kFormatting, "reportRevisions", boolText(format.reportRevisions));
    manager.setDefault(kFormatting, "reportHighlight", boolText(format.reportHighlight));
    manager.setDefault(kFormatting, "reportHeadings", boolText(format.reportHeadings));
    manager.setDefault(kFormatting, "reportLinks", boolText(format.reportLinks));
    manager.setDefault(kFormatting, "reportLists", boolText(format.reportLists));
    manager.setDefault(kFormatting, "reportBlockQuotes", boolText(format.reportBlockQuotes));
    manager.setDefault(kFormatting, "reportGroupings", boolText(format.reportGroupings));
    manager.setDefault(kFormatting, "reportFigures", boolText(format.reportFigures));
    manager.setDefault(kFormatting, "reportFrames", boolText(format.reportFrames));
    manager.setDefault(kFormatting, "reportArticles", boolText(format.reportArticles));
    manager.setDefault(kFormatting, "reportGraphics", boolText(format.reportGraphics));
    manager.setDefault(kFormatting, "includeLayoutTables",
                       boolText(format.includeLayoutTables));
    manager.setDefault(kFormatting, "detectFormatAfterCursor",
                       boolText(format.detectFormatAfterCursor));
    manager.setDefault(kFormatting, "reportLineIndentation",
                       lineIndentationName(format.reportLineIndentation));
    manager.setDefault(kFormatting, "ignoreBlankLinesForRLI",
                       boolText(format.ignoreBlankLinesForLineIndentation));
    manager.setDefault(kFormatting, "indentToneDuration",
                       std::to_string(format.indentToneDuration));

    manager.setDefault(kReview, "followFocus", boolText(defaults.reviewCursorFollowFocus));
    manager.setDefault(kReview, "followCaret", boolText(defaults.reviewCursorFollowCaret));
    manager.setDefault(kReview, "followMouse", boolText(defaults.reviewCursorFollowMouse));
    manager.setDefault(kReview, "simpleReviewMode", boolText(defaults.reviewCursorSimple));
    manager.setDefault(kDocumentNavigation, "paragraphStyle",
                       std::string(text::paragraphStyleName(defaults.paragraphStyle)));
    manager.setDefault(kDocumentNavigation, "sayAllReadingUnit",
                       std::string(text::readingUnitName(defaults.sayAllReadingUnit)));
    manager.setDefault(kEditableText, "caretMoveTimeoutMs",
                       std::to_string(defaults.caretMoveTimeoutMilliseconds));
    manager.setDefault(kNavigation, "reportEdge", boolText(defaults.reportNavigationEdge));
}

Settings settingsFrom(const config::Manager& manager) {
    Settings settings;
    settings.reportObjectDescriptions =
        manager.getBool(kPresentation, "reportObjectDescriptions", true);
    settings.reportKeyboardShortcuts =
        manager.getBool(kPresentation, "reportKeyboardShortcuts", true);
    settings.reportObjectPositionInformation =
        manager.getBool(kPresentation, "reportObjectPositionInformation", true);
    settings.reportTooltips = manager.getBool(kPresentation, "reportTooltips", false);
    settings.reportHelpBalloons = manager.getBool(kPresentation, "reportHelpBalloons", true);
    settings.reportMultiSelect = manager.getBool(kPresentation, "reportMultiSelect", false);
    settings.reportDynamicContentChanges =
        manager.getBool(kPresentation, "reportDynamicContentChanges", true);

    settings.progressBarOutput =
        parseProgressOutput(manager.getString(kProgress, "progressBarOutputMode", "beep"));
    settings.reportBackgroundProgressBars =
        manager.getBool(kProgress, "reportBackgroundProgressBars", false);
    settings.beepPercentageInterval =
        static_cast<int>(manager.getInt(kProgress, "beepPercentageInterval", 1));
    settings.speechPercentageInterval =
        static_cast<int>(manager.getInt(kProgress, "speechPercentageInterval", 10));
    settings.beepMinHz = static_cast<double>(manager.getInt(kProgress, "beepMinHZ", 110));

    settings.reportTables = manager.getBool(kFormatting, "reportTables", true);
    settings.reportTableCellCoords = manager.getBool(kFormatting, "reportTableCellCoords", true);
    const auto headers = manager.getInt(kFormatting, "reportTableHeaders",
                                        static_cast<int>(TableHeaders::RowsAndColumns));
    settings.reportTableHeaders =
        static_cast<TableHeaders>(std::clamp<std::int64_t>(headers, 0, 3));
    settings.reportLandmarks = manager.getBool(kFormatting, "reportLandmarks", true);
    settings.reportClickable = manager.getBool(kFormatting, "reportClickable", true);
    settings.reportLinkType = manager.getBool(kFormatting, "reportLinkType", true);

    settings.reportDetails = manager.getBool(kAnnotations, "reportDetails", true);
    settings.reportAriaDescription = manager.getBool(kAnnotations, "reportAriaDescription", true);

    text::FormatReportSettings& format = settings.format;
    // The keys that exist in both shapes come from ONE configuration entry.
    format.reportTables = settings.reportTables;
    format.reportTableCellCoords = settings.reportTableCellCoords;
    format.reportTableHeaders = settings.reportTableHeaders != TableHeaders::Off;
    format.reportLandmarks = settings.reportLandmarks;
    format.reportClickable = settings.reportClickable;

    const text::FormatReportSettings formatDefaults;
    format.reportFontName = manager.getBool(kFormatting, "reportFontName",
                                            formatDefaults.reportFontName);
    format.reportFontSize = manager.getBool(kFormatting, "reportFontSize",
                                            formatDefaults.reportFontSize);
    format.reportFontAttributes = manager.getBool(kFormatting, "reportFontAttributes",
                                                  formatDefaults.reportFontAttributes);
    format.reportSuperscriptsAndSubscripts =
        manager.getBool(kFormatting, "reportSuperscriptsAndSubscripts",
                        formatDefaults.reportSuperscriptsAndSubscripts);
    format.reportColor = manager.getBool(kFormatting, "reportColor", formatDefaults.reportColor);
    format.reportEmphasis =
        manager.getBool(kFormatting, "reportEmphasis", formatDefaults.reportEmphasis);
    format.reportStyle = manager.getBool(kFormatting, "reportStyle", formatDefaults.reportStyle);
    format.reportAlignment =
        manager.getBool(kFormatting, "reportAlignment", formatDefaults.reportAlignment);
    format.reportLineSpacing =
        manager.getBool(kFormatting, "reportLineSpacing", formatDefaults.reportLineSpacing);
    format.reportParagraphIndentation = manager.getBool(
        kFormatting, "reportParagraphIndentation", formatDefaults.reportParagraphIndentation);
    format.reportBorderStyle =
        manager.getBool(kFormatting, "reportCellBorders", formatDefaults.reportBorderStyle);
    format.reportLineNumber =
        manager.getBool(kFormatting, "reportLineNumber", formatDefaults.reportLineNumber);
    format.reportPage = manager.getBool(kFormatting, "reportPage", formatDefaults.reportPage);
    format.reportSpellingErrors = manager.getBool(kFormatting, "reportSpellingErrors",
                                                  formatDefaults.reportSpellingErrors);
    format.reportGrammarErrors = manager.getBool(kFormatting, "reportGrammarErrors",
                                                 formatDefaults.reportGrammarErrors);
    format.reportComments =
        manager.getBool(kFormatting, "reportComments", formatDefaults.reportComments);
    format.reportBookmarks =
        manager.getBool(kFormatting, "reportBookmarks", formatDefaults.reportBookmarks);
    format.reportRevisions =
        manager.getBool(kFormatting, "reportRevisions", formatDefaults.reportRevisions);
    format.reportHighlight =
        manager.getBool(kFormatting, "reportHighlight", formatDefaults.reportHighlight);
    format.reportHeadings =
        manager.getBool(kFormatting, "reportHeadings", formatDefaults.reportHeadings);
    format.reportLinks = manager.getBool(kFormatting, "reportLinks", formatDefaults.reportLinks);
    format.reportLists = manager.getBool(kFormatting, "reportLists", formatDefaults.reportLists);
    format.reportBlockQuotes =
        manager.getBool(kFormatting, "reportBlockQuotes", formatDefaults.reportBlockQuotes);
    format.reportGroupings =
        manager.getBool(kFormatting, "reportGroupings", formatDefaults.reportGroupings);
    format.reportFigures =
        manager.getBool(kFormatting, "reportFigures", formatDefaults.reportFigures);
    format.reportFrames =
        manager.getBool(kFormatting, "reportFrames", formatDefaults.reportFrames);
    format.reportArticles =
        manager.getBool(kFormatting, "reportArticles", formatDefaults.reportArticles);
    format.reportGraphics =
        manager.getBool(kFormatting, "reportGraphics", formatDefaults.reportGraphics);
    format.includeLayoutTables =
        manager.getBool(kFormatting, "includeLayoutTables", formatDefaults.includeLayoutTables);
    format.detectFormatAfterCursor = manager.getBool(kFormatting, "detectFormatAfterCursor",
                                                     formatDefaults.detectFormatAfterCursor);
    format.reportLineIndentation = parseLineIndentation(
        manager.getString(kFormatting, "reportLineIndentation",
                          lineIndentationName(formatDefaults.reportLineIndentation)));
    format.ignoreBlankLinesForLineIndentation =
        manager.getBool(kFormatting, "ignoreBlankLinesForRLI",
                        formatDefaults.ignoreBlankLinesForLineIndentation);
    format.indentToneDuration = static_cast<int>(
        manager.getInt(kFormatting, "indentToneDuration", formatDefaults.indentToneDuration));

    settings.reviewCursorFollowFocus = manager.getBool(kReview, "followFocus", true);
    settings.reviewCursorFollowCaret = manager.getBool(kReview, "followCaret", true);
    settings.reviewCursorFollowMouse = manager.getBool(kReview, "followMouse", false);
    settings.reviewCursorSimple = manager.getBool(kReview, "simpleReviewMode", true);
    settings.paragraphStyle =
        text::parseParagraphStyle(manager.getString(kDocumentNavigation, "paragraphStyle",
                                                    "application"))
            .value_or(text::ParagraphStyle::Application);
    settings.sayAllReadingUnit =
        text::parseReadingUnit(
            manager.getString(kDocumentNavigation, "sayAllReadingUnit", "paragraph"))
            .value_or(text::ReadingUnit::Paragraph);
    settings.caretMoveTimeoutMilliseconds =
        static_cast<int>(manager.getInt(kEditableText, "caretMoveTimeoutMs", 100));
    settings.reportNavigationEdge = manager.getBool(kNavigation, "reportEdge", true);
    return settings;
}

}  // namespace alus::a11y
