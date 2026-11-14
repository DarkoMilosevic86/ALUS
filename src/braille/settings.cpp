// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/settings.hpp.

#include "alus/braille/settings.hpp"

#include "alus/config/config.hpp"
#include "alus/i18n/i18n.hpp"

#include <algorithm>

namespace alus::braille {
namespace {

// NVDA's own section name, so that a user's two configuration files read the
// same way side by side.
constexpr const char* kBraille = "braille";
constexpr const char* kGeneral = "general";
constexpr const char* kFormatting = "documentFormatting";

std::string boolText(bool value) { return value ? "true" : "false"; }

std::uint8_t parseShape(std::string_view name, std::uint8_t fallback) {
    if (name == "dots78") {
        return kCursorDots78;
    }
    if (name == "dot8") {
        return kCursorDot8;
    }
    if (name == "allDots") {
        return kCursorAllDots;
    }
    return fallback;
}

std::string_view shapeName(std::uint8_t shape) {
    switch (shape) {
        case kCursorDot8:
            return "dot8";
        case kCursorAllDots:
            return "allDots";
        default:
            return "dots78";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// The enumerations
// ---------------------------------------------------------------------------

std::string_view tetherName(TetherTo tether) {
    switch (tether) {
        case TetherTo::Focus:
            return "focus";
        case TetherTo::Review:
            return "review";
        case TetherTo::Auto:
            return "auto";
    }
    return "auto";
}

std::string_view tetherLabel(TetherTo tether) {
    switch (tether) {
        case TetherTo::Focus:
            // Translators: what `alus+control+t` reports.
            return _("to focus");
        // Translators: what `alus+control+t` reports.
        case TetherTo::Review:
            return _("to review");
        // Translators: what `alus+control+t` reports.
        case TetherTo::Auto:
            return _("automatically");
    }
    return _("automatically");
}

TetherTo parseTether(std::string_view name) {
    if (name == "focus") {
        return TetherTo::Focus;
    }
    if (name == "review") {
        return TetherTo::Review;
    }
    return TetherTo::Auto;
}

std::string_view textWrapName(TextWrap wrap) {
    switch (wrap) {
        case TextWrap::None:
            return "none";
        case TextWrap::MarkWordCuts:
            return "markWordCuts";
        case TextWrap::AtWordBoundaries:
            return "atWordBoundaries";
        case TextWrap::AtWordOrSyllableBoundaries:
            return "atWordOrSyllableBoundaries";
    }
    return "atWordBoundaries";
}

TextWrap parseTextWrap(std::string_view name) {
    if (name == "none") {
        return TextWrap::None;
    }
    if (name == "markWordCuts") {
        return TextWrap::MarkWordCuts;
    }
    if (name == "atWordOrSyllableBoundaries") {
        return TextWrap::AtWordOrSyllableBoundaries;
    }
    return TextWrap::AtWordBoundaries;
}

std::string_view showMessagesName(ShowMessages value) {
    switch (value) {
        case ShowMessages::Disabled:
            return "disabled";
        case ShowMessages::UseTimeout:
            return "useTimeout";
        case ShowMessages::ShowIndefinitely:
            return "showIndefinitely";
    }
    return "useTimeout";
}

ShowMessages parseShowMessages(std::string_view name) {
    if (name == "disabled") {
        return ShowMessages::Disabled;
    }
    if (name == "showIndefinitely") {
        return ShowMessages::ShowIndefinitely;
    }
    return ShowMessages::UseTimeout;
}

std::string_view focusContextName(FocusContext value) {
    switch (value) {
        case FocusContext::ChangedContext:
            return "changedContext";
        case FocusContext::Fill:
            return "fill";
        case FocusContext::Scroll:
            return "scroll";
    }
    return "changedContext";
}

FocusContext parseFocusContext(std::string_view name) {
    if (name == "fill") {
        return FocusContext::Fill;
    }
    if (name == "scroll") {
        return FocusContext::Scroll;
    }
    return FocusContext::ChangedContext;
}

std::string_view modeName(Mode mode) {
    return mode == Mode::SpeechOutput ? "speechOutput" : "followCursors";
}

std::string_view modeLabel(Mode mode) {
    // Translators: what `alus+alt+t` reports.
    return mode == Mode::SpeechOutput ? _("speech output") : _("follow cursors");
}

Mode parseMode(std::string_view name) {
    return name == "speechOutput" ? Mode::SpeechOutput : Mode::FollowCursors;
}

std::string_view reviewRoutingName(ReviewRoutingMovesCaret value) {
    switch (value) {
        case ReviewRoutingMovesCaret::Never:
            return "never";
        case ReviewRoutingMovesCaret::Always:
            return "always";
        case ReviewRoutingMovesCaret::OnlyWhenAutoTethered:
            return "onlyWhenAutoTethered";
    }
    return "never";
}

// The SPOKEN setting. reviewRoutingName above is the configuration value and
// must stay English, or a configuration file written in one language stops
// loading in another.
std::string_view reviewRoutingLabel(ReviewRoutingMovesCaret value) {
    switch (value) {
        // Translators: one of the settings for whether a routing key on the
        // braille display also moves the caret.
        case ReviewRoutingMovesCaret::Never:
            return _("never");
        // Translators: one of the settings for whether a routing key on the
        // braille display also moves the caret.
        case ReviewRoutingMovesCaret::Always:
            return _("always");
        // Translators: one of the settings for whether a routing key on the
        // braille display also moves the caret.
        case ReviewRoutingMovesCaret::OnlyWhenAutoTethered:
            return _("only when automatically tethered");
    }
    return _("never");
}

ReviewRoutingMovesCaret parseReviewRouting(std::string_view name) {
    if (name == "always") {
        return ReviewRoutingMovesCaret::Always;
    }
    if (name == "onlyWhenAutoTethered") {
        return ReviewRoutingMovesCaret::OnlyWhenAutoTethered;
    }
    return ReviewRoutingMovesCaret::Never;
}

std::string_view fontFormattingName(FontFormattingDisplay value) {
    return value == FontFormattingDisplay::Tags ? "tags" : "liblouis";
}

FontFormattingDisplay parseFontFormatting(std::string_view name) {
    return name == "tags" ? FontFormattingDisplay::Tags : FontFormattingDisplay::Liblouis;
}

// ---------------------------------------------------------------------------
// The configuration
// ---------------------------------------------------------------------------

void registerSettingDefaults(config::Manager& manager) {
    const Settings defaults;
    manager.setDefault(kBraille, "translationTable", defaults.translationTable);
    manager.setDefault(kBraille, "inputTable", defaults.inputTable);
    manager.setDefault(kBraille, "expandAtCursor", boolText(defaults.expandAtCursor));
    manager.setDefault(kBraille, "unicodeNormalization",
                       boolText(defaults.unicodeNormalization));
    manager.setDefault(kBraille, "showCursor", boolText(defaults.showCursor));
    manager.setDefault(kBraille, "cursorBlink", boolText(defaults.cursorBlink));
    manager.setDefault(kBraille, "cursorBlinkRate",
                       std::to_string(defaults.cursorBlinkRate.count()));
    manager.setDefault(kBraille, "cursorShapeFocus",
                       std::string{shapeName(defaults.cursorShapeFocus)});
    manager.setDefault(kBraille, "cursorShapeReview",
                       std::string{shapeName(defaults.cursorShapeReview)});
    manager.setDefault(kBraille, "showSelection", boolText(defaults.showSelection));
    manager.setDefault(kBraille, "showMessages",
                       std::string{showMessagesName(defaults.showMessages)});
    manager.setDefault(kBraille, "messageTimeout",
                       std::to_string(defaults.messageTimeout.count()));
    manager.setDefault(kBraille, "tetherTo", std::string{tetherName(defaults.tetherTo)});
    manager.setDefault(kBraille, "reviewRoutingMovesSystemCaret",
                       std::string{reviewRoutingName(defaults.reviewRoutingMovesCaret)});
    manager.setDefault(kBraille, "focusContextPresentation",
                       std::string{focusContextName(defaults.focusContextPresentation)});
    manager.setDefault(kBraille, "mode", std::string{modeName(defaults.mode)});
    manager.setDefault(kBraille, "readByParagraph", boolText(defaults.readByParagraph));
    manager.setDefault(kBraille, "paragraphStartMarker", defaults.paragraphStartMarker);
    manager.setDefault(kBraille, "textWrap", std::string{textWrapName(defaults.textWrap)});
    manager.setDefault(kBraille, "autoScrollRate", std::to_string(defaults.autoScrollRate));
    manager.setDefault(kBraille, "speakOnRouting", boolText(defaults.speakOnRouting));
    manager.setDefault(kBraille, "speakOnNavigatingByUnit",
                       boolText(defaults.speakOnNavigatingByUnit));
    manager.setDefault(kBraille, "fontFormattingDisplay",
                       std::string{fontFormattingName(defaults.fontFormattingDisplay)});
    manager.setDefault(kBraille, "handleBrailleInput", boolText(defaults.handleBrailleInput));
    manager.setDefault(kBraille, "display", defaults.display);
    manager.setDefault(kBraille, "brlapiHost", defaults.brlapiHost);
    // These two live in [documentFormatting] in NVDA as well: they are the
    // braille half of a setting speech also reads, and one key must not have
    // two homes (phase-5.md O4 again).
    manager.setDefault(kFormatting, "reportFontAttributesInBraille", boolText(false));
    manager.setDefault(kFormatting, "reportSpellingErrorsInBraille", boolText(true));
}

Settings settingsFrom(const config::Manager& manager) {
    Settings settings;
    const Settings defaults;

    settings.translationTable =
        manager.getString(kBraille, "translationTable", defaults.translationTable);
    settings.inputTable = manager.getString(kBraille, "inputTable", defaults.inputTable);
    settings.language = manager.getString(kGeneral, "language", defaults.language);

    settings.expandAtCursor =
        manager.getBool(kBraille, "expandAtCursor", defaults.expandAtCursor);
    settings.unicodeNormalization =
        manager.getBool(kBraille, "unicodeNormalization", defaults.unicodeNormalization);

    settings.showCursor = manager.getBool(kBraille, "showCursor", defaults.showCursor);
    settings.cursorBlink = manager.getBool(kBraille, "cursorBlink", defaults.cursorBlink);
    settings.cursorBlinkRate = std::chrono::milliseconds{
        std::max<std::int64_t>(0, manager.getInt(kBraille, "cursorBlinkRate",
                                                 defaults.cursorBlinkRate.count()))};
    settings.cursorShapeFocus =
        parseShape(manager.getString(kBraille, "cursorShapeFocus"), defaults.cursorShapeFocus);
    settings.cursorShapeReview = parseShape(manager.getString(kBraille, "cursorShapeReview"),
                                            defaults.cursorShapeReview);
    settings.showSelection = manager.getBool(kBraille, "showSelection", defaults.showSelection);

    settings.showMessages = parseShowMessages(manager.getString(kBraille, "showMessages"));
    settings.messageTimeout = std::chrono::milliseconds{std::max<std::int64_t>(
        0, manager.getInt(kBraille, "messageTimeout", defaults.messageTimeout.count()))};

    settings.tetherTo = parseTether(manager.getString(kBraille, "tetherTo"));
    settings.reviewRoutingMovesCaret =
        parseReviewRouting(manager.getString(kBraille, "reviewRoutingMovesSystemCaret"));
    settings.focusContextPresentation =
        parseFocusContext(manager.getString(kBraille, "focusContextPresentation"));
    settings.mode = parseMode(manager.getString(kBraille, "mode"));

    settings.readByParagraph =
        manager.getBool(kBraille, "readByParagraph", defaults.readByParagraph);
    settings.paragraphStartMarker =
        manager.getString(kBraille, "paragraphStartMarker", defaults.paragraphStartMarker);
    settings.textWrap = parseTextWrap(manager.getString(kBraille, "textWrap"));

    settings.autoScrollRate = static_cast<int>(std::max<std::int64_t>(
        1, manager.getInt(kBraille, "autoScrollRate", defaults.autoScrollRate)));

    settings.speakOnRouting =
        manager.getBool(kBraille, "speakOnRouting", defaults.speakOnRouting);
    settings.speakOnNavigatingByUnit =
        manager.getBool(kBraille, "speakOnNavigatingByUnit", defaults.speakOnNavigatingByUnit);

    settings.fontFormattingDisplay =
        parseFontFormatting(manager.getString(kBraille, "fontFormattingDisplay"));
    settings.reportFontAttributes =
        manager.getBool(kFormatting, "reportFontAttributesInBraille", false);
    settings.reportSpellingErrorsInBraille =
        manager.getBool(kFormatting, "reportSpellingErrorsInBraille", true);

    settings.handleBrailleInput =
        manager.getBool(kBraille, "handleBrailleInput", defaults.handleBrailleInput);
    settings.display = manager.getString(kBraille, "display", defaults.display);
    settings.brlapiHost = manager.getString(kBraille, "brlapiHost", defaults.brlapiHost);
    return settings;
}

}  // namespace alus::braille
