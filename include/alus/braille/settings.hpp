// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The `[braille]` configuration section, as one struct.
//
// NVDA's `config.conf["braille"]`, with NVDA's own defaults. The same argument
// as `a11y::Settings`: the braille path runs on every caret movement and must
// not do a file-backed lookup per key, so the section is read once (at
// start-up and on SIGHUP) into a value every region, the buffer and the
// handler are given.
//
// THE DEFAULTS ARE NVDA'S AND THAT MATTERS. `expandAtCursor` on, `showCursor`
// on, `cursorBlink` on at 500 ms, `textWrap` at word boundaries, `tetherTo`
// automatic, `readByParagraph` OFF: a user coming from NVDA must find the
// display behaving the way they left it (architecture.md H1).
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace alus::config {
class Manager;
}

namespace alus::braille {

// NVDA: `config.configFlags.TetherTo`. The string values are NVDA's, because
// they are written into the configuration file.
enum class TetherTo { Focus, Review, Auto };

std::string_view tetherName(TetherTo tether);
std::string_view tetherLabel(TetherTo tether);
TetherTo parseTether(std::string_view name);

// NVDA: `config.featureFlagEnums.BrailleTextWrapFlag`. The four word-wrap
// modes phase-plan.md T7.5 measures.
enum class TextWrap {
    // Cut at the edge of the display, mid-word, with no mark. The display
    // shows as much as fits and the user scrolls.
    None,
    // Cut one cell early and put a continuation mark (dots 7-8) there, so the
    // reader knows a word was split.
    MarkWordCuts,
    // Do not split a word at all: end the row at the last space.
    AtWordBoundaries,
    // As above, but if the word would be pushed off entirely, split it at a
    // hyphenation point instead and mark the cut.
    AtWordOrSyllableBoundaries,
};

std::string_view textWrapName(TextWrap wrap);
TextWrap parseTextWrap(std::string_view name);

// NVDA: `config.configFlags.ShowMessages`.
enum class ShowMessages { Disabled, UseTimeout, ShowIndefinitely };

std::string_view showMessagesName(ShowMessages value);
ShowMessages parseShowMessages(std::string_view name);

// NVDA: `braille.constants.CONTEXTPRES_*`.
enum class FocusContext { ChangedContext, Fill, Scroll };

std::string_view focusContextName(FocusContext value);
FocusContext parseFocusContext(std::string_view name);

// NVDA: `config.configFlags.BrailleMode`. In `SpeechOutput` the display shows
// what is being SPOKEN instead of following the focus, which is how a sighted
// teacher reads along with a blind student.
enum class Mode { FollowCursors, SpeechOutput };

std::string_view modeName(Mode mode);
std::string_view modeLabel(Mode mode);
Mode parseMode(std::string_view name);

// NVDA: `config.featureFlagEnums.ReviewRoutingMovesSystemCaretFlag`.
enum class ReviewRoutingMovesCaret { Never, Always, OnlyWhenAutoTethered };

std::string_view reviewRoutingName(ReviewRoutingMovesCaret value);
std::string_view reviewRoutingLabel(ReviewRoutingMovesCaret value);
ReviewRoutingMovesCaret parseReviewRouting(std::string_view name);

// NVDA: `config.featureFlagEnums.FontFormattingBrailleModeFlag`. Either the
// translation table is told about bold and italic (`typeform`), or ALUS puts
// explicit tags in the text. They are mutually exclusive: a table that renders
// an emphasis sign and a tag both would say it twice.
enum class FontFormattingDisplay { Liblouis, Tags };

std::string_view fontFormattingName(FontFormattingDisplay value);
FontFormattingDisplay parseFontFormatting(std::string_view name);

// NVDA: the cursor shapes of `braille.constants.CURSOR_SHAPES`.
constexpr std::uint8_t kCursorDots78 = 0xC0;
constexpr std::uint8_t kCursorDot8 = 0x80;
constexpr std::uint8_t kCursorAllDots = 0xFF;
// NVDA: `SELECTION_SHAPE` and `CONTINUATION_SHAPE`, both dots 7 and 8.
constexpr std::uint8_t kSelectionShape = 0xC0;
constexpr std::uint8_t kContinuationShape = 0xC0;
// NVDA: `END_OF_BRAILLE_OUTPUT_SHAPE`. Shown on the cell just past the last
// one ALUS is using, so a 40-cell display driven as 20 cells says where it
// stops rather than looking broken.
constexpr std::uint8_t kEndOfOutputShape = 0xFF;

struct Settings {
    // ---- the tables ----
    // The file name, or "auto" for "the table for the current language".
    std::string translationTable = "auto";
    std::string inputTable = "auto";
    // The language `auto` resolves against. Filled from [general] language.
    std::string language = "en";

    // ---- translation ----
    // NVDA: expandAtCursor. The word under the cursor in computer braille, so
    // a contracted table does not hide the characters being edited.
    bool expandAtCursor = true;
    // NVDA: unicodeNormalization. Off by default, as in NVDA.
    bool unicodeNormalization = false;

    // ---- the cursor and the selection ----
    bool showCursor = true;
    bool cursorBlink = true;
    std::chrono::milliseconds cursorBlinkRate{500};
    std::uint8_t cursorShapeFocus = kCursorDots78;
    std::uint8_t cursorShapeReview = kCursorDot8;
    bool showSelection = true;

    // ---- messages ----
    ShowMessages showMessages = ShowMessages::UseTimeout;
    std::chrono::milliseconds messageTimeout{4000};

    // ---- what the display follows ----
    TetherTo tetherTo = TetherTo::Auto;
    ReviewRoutingMovesCaret reviewRoutingMovesCaret = ReviewRoutingMovesCaret::Never;
    FocusContext focusContextPresentation = FocusContext::ChangedContext;
    Mode mode = Mode::FollowCursors;

    // ---- the reading unit and the window ----
    // NVDA: readByParagraph. Off by default: a paragraph on a 40-cell display
    // is a lot of scrolling.
    bool readByParagraph = false;
    std::string paragraphStartMarker = "¶";
    TextWrap textWrap = TextWrap::AtWordBoundaries;

    // ---- automatic scroll ----
    // NVDA: autoScrollRate, in cells per second.
    int autoScrollRate = 8;

    // ---- speech alongside braille ----
    // NVDA: speakOnRouting. Say the character the routing key landed on.
    bool speakOnRouting = false;
    // NVDA: speakOnNavigatingByUnit. Read the line when the display's own
    // line keys move the reading unit.
    bool speakOnNavigatingByUnit = false;

    // ---- formatting ----
    FontFormattingDisplay fontFormattingDisplay = FontFormattingDisplay::Liblouis;
    // NVDA: documentFormatting.fontAttributeReporting & OutputMode.BRAILLE.
    // Whether bold/italic/underline reach braille at all.
    bool reportFontAttributes = false;
    // NVDA: reportSpellingErrors2 & ReportSpellingErrors.BRAILLE.
    bool reportSpellingErrorsInBraille = true;

    // ---- braille input ----
    // Whether ALUS translates the braille keyboard itself, or leaves it to
    // brltty.
    //
    // FALSE BY DEFAULT, and that is a platform decision, not a preference.
    // NVDA's own brltty driver says it: "BRLTTY simulates key presses for
    // braille typing keys, so let BRLTTY handle them." On Wayland the reason
    // is stronger -- finding N18: ALUS cannot inject a keystroke at all, so
    // translating the cells itself and then having nowhere to send them would
    // mean a braille keyboard that types nothing. brltty can type, through
    // its own uinput device, so by default it does.
    //
    // Set it true and ALUS takes over: the cells are translated here, the
    // untranslated ones are shown on the display, and `bk:` gestures reach
    // the script layer. That is the only mode in which a braille keyboard can
    // issue ALUS COMMANDS, which is why it exists.
    bool handleBrailleInput = false;

    // ---- the display ----
    // The driver name. "brlapi" is the only real one in v1.0 (§14); "none"
    // disables the display without disabling the rest of the braille layer,
    // which is what the tests and `--no-braille` use.
    std::string display = "brlapi";
    // The BrlAPI host, as brltty spells it (":0", "host:0"). Empty means
    // "whatever $BRLAPI_HOST or the default says", which is the normal case.
    std::string brlapiHost;
};

// The factory defaults, into the configuration manager (NVDA: confspec).
void registerSettingDefaults(config::Manager& manager);
// Reads every key above.
Settings settingsFrom(const config::Manager& manager);

}  // namespace alus::braille
