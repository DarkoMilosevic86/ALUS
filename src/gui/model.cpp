// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/gui/model.hpp. Every title and subtitle here is N_(): the
// table is built once at first use and the language can change afterwards
// without a restart, so translating now would freeze the first language.

#include "alus/gui/model.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/speech/ssip_synth.hpp"

#include <algorithm>

namespace alus::gui {
namespace {

// The choice lists that more than one row uses.

std::vector<Choice> typingEchoChoices() {
    return {
        {"off", N_("Off")},
        {"editControls", N_("Only in edit controls")},
        {"always", N_("Always")},
    };
}

std::vector<Page> build() {
    std::vector<Page> out;

    // -----------------------------------------------------------------------
    // General
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "general", N_("General"), "preferences-system-symbolic",
        {
            Group{N_("Language"),
                  N_("The language ALUS speaks and shows. Independent of the "
                     "desktop's language on purpose: a user may want an English "
                     "desktop and a Serbian screen reader."),
                  {
                      Row{RowKind::Choice, "general", "language", N_("Language"),
                          N_("Takes effect immediately; ALUS does not need to be "
                             "restarted."),
                          {{"", N_("Same as the session")},
                           {"en", N_("English")},
                           {"sr", N_("Serbian (Cyrillic)")},
                           {"sr@latin", N_("Serbian (Latin)")}}},
                  }},
            Group{N_("Logging"), nullptr,
                  {
                      Row{RowKind::Choice, "general", "logLevel", N_("Log level"),
                          N_("\"debug\" and \"io\" write a great deal and slow ALUS "
                             "down; use them when reporting a problem."),
                          {{"off", N_("Off")},
                           {"error", N_("Errors only")},
                           {"warning", N_("Warnings")},
                           {"info", N_("Information")},
                           {"debug", N_("Debug")},
                           {"io", N_("Debug with input and output")}}},
                  }},
            Group{N_("Configuration"), nullptr,
                  {
                      Row{RowKind::Action, {}, {}, N_("Open the configuration folder"),
                          N_("Where alus.ini, gestures.ini and the profiles are."),
                          {}, 0, 100, 1, "config-dir"},
                      Row{RowKind::Action, {}, {}, N_("Reset every setting"),
                          N_("Returns every setting to its factory value. The "
                             "gesture map and the profiles are not touched."),
                          {}, 0, 100, 1, "reset"},
                  }},
        }});

    // -----------------------------------------------------------------------
    // Speech
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "speech", N_("Speech"), "audio-speakers-symbolic",
        {
            Group{N_("Synthesizer"),
                  N_("ALUS speaks through speech-dispatcher, so every SSIP module "
                     "already installed is available here."),
                  {
                      Row{RowKind::Choice, "speech", "synth", N_("Synthesizer"),
                          nullptr,
                          {{"ssip", N_("speech-dispatcher")}, {"none", N_("None")}}},
                      Row{RowKind::Choice, "speech", "outputModule", N_("Output module"),
                          N_("The speech-dispatcher module. Takes effect when ALUS "
                             "restarts its speech, which happens on the next "
                             "configuration reset."),
                          {{"", N_("Default")}}, 0, 100, 1, {}, "speech-modules"},
                      Row{RowKind::Choice, "speech", "voice", N_("Voice"),
                          N_("The voices of the output module above. After changing "
                             "the module, close and reopen this window to see its "
                             "voices."),
                          {{"", N_("Default")}}, 0, 100, 1, {}, "speech-voices"},
                  }},
            Group{N_("Voice settings"), nullptr,
                  {
                      Row{RowKind::Integer, "speech", "rate", N_("Rate")},
                      Row{RowKind::Integer, "speech", "pitch", N_("Pitch")},
                      Row{RowKind::Integer, "speech", "volume", N_("Volume")},
                      Row{RowKind::Integer, "speech", "inflection", N_("Inflection")},
                  }},
            Group{N_("What is spoken"), nullptr,
                  {
                      Row{RowKind::Choice, "speech", "symbolLevel",
                          N_("Punctuation and symbol level"),
                          N_("How much punctuation is spoken. `alus+p` cycles this "
                             "without opening the settings."),
                          {{"none", N_("None")},
                           {"some", N_("Some")},
                           {"most", N_("Most")},
                           {"all", N_("All")},
                           {"char", N_("Character")}}},
                      Row{RowKind::Toggle, "speech", "autoLanguageSwitching",
                          N_("Follow the language of the text"),
                          N_("Switches the voice when a document says its text is "
                             "in another language.")},
                      Row{RowKind::Toggle, "speech", "autoDialectSwitching",
                          N_("Follow the dialect too"),
                          N_("Switches between, for example, British and American "
                             "English. Off by default.")},
                      Row{RowKind::Toggle, "speech", "unicodeNormalization",
                          N_("Normalize Unicode before speaking"),
                          N_("Makes mathematical and styled letters readable: "
                             "ℂ is spoken as C.")},
                  }},
        }});

    // -----------------------------------------------------------------------
    // Braille
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "braille", N_("Braille"), "input-touchpad-symbolic",
        {
            Group{N_("Display"),
                  N_("The display is driven through brltty's BrlAPI, so every "
                     "display brltty supports works here."),
                  {
                      Row{RowKind::Choice, "braille", "display", N_("Display"),
                          nullptr,
                          {{"brlapi", N_("brltty (BrlAPI)")}, {"none", N_("No display")}}},
                      Row{RowKind::Text, "braille", "brlapiHost", N_("BrlAPI host"),
                          N_("Empty is the normal case: whatever $BRLAPI_HOST or "
                             "brltty's own default says.")},
                  }},
            Group{N_("Translation tables"), nullptr,
                  {
                      Row{RowKind::Text, "braille", "translationTable",
                          N_("Output table"),
                          N_("A liblouis table file name, or \"auto\" for the table "
                             "that matches the language.")},
                      Row{RowKind::Text, "braille", "inputTable", N_("Input table"),
                          nullptr},
                      Row{RowKind::Toggle, "braille", "expandAtCursor",
                          N_("Show the word at the cursor uncontracted"),
                          N_("So a contracted table does not hide the characters "
                             "being edited.")},
                      Row{RowKind::Toggle, "braille", "unicodeNormalization",
                          N_("Normalize Unicode before translating")},
                  }},
            Group{N_("Cursor"), nullptr,
                  {
                      Row{RowKind::Toggle, "braille", "showCursor", N_("Show the cursor")},
                      Row{RowKind::Toggle, "braille", "cursorBlink", N_("Blink the cursor")},
                      Row{RowKind::Integer, "braille", "cursorBlinkRate",
                          N_("Blink rate, in milliseconds"), nullptr, {}, 100, 2000, 50},
                      Row{RowKind::Choice, "braille", "cursorShapeFocus",
                          N_("Cursor shape when following the focus"), nullptr,
                          {{"dots78", N_("dots 7 and 8")},
                           {"dot8", N_("dot 8")},
                           {"allDots", N_("all dots")}}},
                      Row{RowKind::Choice, "braille", "cursorShapeReview",
                          N_("Cursor shape when following review"), nullptr,
                          {{"dots78", N_("dots 7 and 8")},
                           {"dot8", N_("dot 8")},
                           {"allDots", N_("all dots")}}},
                      Row{RowKind::Toggle, "braille", "showSelection",
                          N_("Show the selection")},
                  }},
            Group{N_("What the display follows"), nullptr,
                  {
                      Row{RowKind::Choice, "braille", "tetherTo", N_("Tethered to"),
                          N_("`alus+control+t` changes this without opening the "
                             "settings."),
                          {{"auto", N_("automatically")},
                           {"focus", N_("to focus")},
                           {"review", N_("to review")}}},
                      Row{RowKind::Choice, "braille", "mode", N_("Mode"), nullptr,
                          {{"followCursors", N_("follow cursors")},
                           {"speechOutput", N_("speech output")}}},
                      Row{RowKind::Choice, "braille", "reviewRoutingMovesSystemCaret",
                          N_("A routing key also moves the caret"), nullptr,
                          {{"never", N_("never")},
                           {"always", N_("always")},
                           {"onlyWhenAutoTethered",
                            N_("only when automatically tethered")}}},
                      Row{RowKind::Choice, "braille", "focusContextPresentation",
                          N_("How much context is shown"), nullptr,
                          {{"changedContext", N_("changedContext")},
                           {"fill", N_("fill")},
                           {"scroll", N_("scroll")}}},
                  }},
            Group{N_("Reading"), nullptr,
                  {
                      Row{RowKind::Choice, "braille", "textWrap", N_("Word wrap"),
                          nullptr,
                          {{"none", N_("Break words")},
                           {"markWordCuts", N_("Break words and mark the break")},
                           {"atWordBoundaries", N_("At word boundaries")},
                           {"atWordOrSyllableBoundaries",
                            N_("At word or syllable boundaries")}}},
                      Row{RowKind::Toggle, "braille", "readByParagraph",
                          N_("Read by paragraph"),
                          N_("Off by default: a paragraph on a 40-cell display is a "
                             "lot of scrolling.")},
                      Row{RowKind::Integer, "braille", "autoScrollRate",
                          N_("Automatic scroll rate, in cells per second"), nullptr, {},
                          1, 60, 1},
                      Row{RowKind::Choice, "braille", "showMessages", N_("Show messages"),
                          nullptr,
                          {{"disabled", N_("Off")},
                           {"useTimeout", N_("For a while")},
                           {"showIndefinitely", N_("Until dismissed")}}},
                      Row{RowKind::Integer, "braille", "messageTimeout",
                          N_("How long a message stays, in milliseconds"), nullptr, {},
                          500, 20000, 500},
                  }},
            Group{N_("Speech alongside braille"), nullptr,
                  {
                      Row{RowKind::Toggle, "braille", "speakOnRouting",
                          N_("Speak the character a routing key lands on")},
                      Row{RowKind::Toggle, "braille", "speakOnNavigatingByUnit",
                          N_("Speak the line when the display's own keys move")},
                  }},
            Group{N_("Braille keyboard"),
                  N_("Off by default, and that is a platform decision rather than a "
                     "preference: on Wayland ALUS cannot inject a keystroke, so "
                     "brltty types and ALUS does not. Turn this on and ALUS "
                     "translates the cells itself, which is the only way a braille "
                     "keyboard can issue ALUS commands."),
                  {
                      Row{RowKind::Toggle, "braille", "handleBrailleInput",
                          N_("ALUS handles braille typing")},
                  }},
        }});

    // -----------------------------------------------------------------------
    // Keyboard
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "keyboard", N_("Keyboard"), "input-keyboard-symbolic",
        {
            Group{N_("Layout"), nullptr,
                  {
                      Row{RowKind::Choice, "keyboard", "keyboardLayout", N_("Layout"),
                          N_("Two layouts with the same commands: a desktop has a "
                             "numeric keypad to review with, a laptop does not."),
                          {{"desktop", N_("Desktop")}, {"laptop", N_("Laptop")}}},
                  }},
            Group{N_("The ALUS key"),
                  N_("Which keys act as the ALUS modifier. More than one may be "
                     "chosen; pressing one twice in quick succession sends the real "
                     "key to the application."),
                  {
                      Row{RowKind::Toggle, "keyboard", "alusKeyInsert", N_("Insert")},
                      Row{RowKind::Toggle, "keyboard", "alusKeyNumpadInsert",
                          N_("Numeric keypad Insert")},
                      Row{RowKind::Toggle, "keyboard", "alusKeyCapsLock",
                          N_("Caps Lock")},
                      Row{RowKind::Integer, "keyboard", "multiPressTimeout",
                          N_("How long a double press may take, in milliseconds"),
                          nullptr, {}, 100, 2000, 50},
                  }},
            Group{N_("Typing echo"), nullptr,
                  {
                      Row{RowKind::Choice, "keyboard", "speakTypedCharacters",
                          N_("Speak typed characters"), nullptr, typingEchoChoices()},
                      Row{RowKind::Choice, "keyboard", "speakTypedWords",
                          N_("Speak typed words"), nullptr, typingEchoChoices()},
                      Row{RowKind::Toggle, "keyboard", "speakCommandKeys",
                          N_("Speak command keys"),
                          N_("Says the name of every key that is not a character, "
                             "which is what `alus+4` toggles.")},
                  }},
            Group{N_("Interrupting speech"), nullptr,
                  {
                      Row{RowKind::Toggle, "keyboard", "speechInterruptForCharacters",
                          N_("Typing a character stops speech")},
                      Row{RowKind::Toggle, "keyboard", "speechInterruptForEnter",
                          N_("Enter stops speech")},
                      Row{RowKind::Toggle, "keyboard", "allowSkimReadingInSayAll",
                          N_("Movement keys skim during say all"),
                          N_("Lets the arrow keys move within a say all instead of "
                             "stopping it.")},
                  }},
        }});

    // -----------------------------------------------------------------------
    // Object presentation
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "presentation", N_("Object presentation"), "view-list-symbolic",
        {
            Group{N_("What is announced about an object"),
                  N_("The defaults are deliberately sparse: a screen reader that "
                     "announces everything it can is unusable."),
                  {
                      Row{RowKind::Toggle, "presentation", "reportObjectDescriptions",
                          N_("Descriptions")},
                      Row{RowKind::Toggle, "presentation", "reportKeyboardShortcuts",
                          N_("Keyboard shortcuts")},
                      Row{RowKind::Toggle, "presentation",
                          "reportObjectPositionInformation",
                          N_("Position information"),
                          N_("\"3 of 7\", and an item's level in a tree.")},
                      Row{RowKind::Toggle, "presentation", "reportTooltips",
                          N_("Tooltips"),
                          N_("Off by default: a tooltip under the pointer is not "
                             "what a keyboard user is doing.")},
                      Row{RowKind::Toggle, "presentation", "reportHelpBalloons",
                          N_("Help balloons and notifications")},
                      Row{RowKind::Toggle, "presentation", "reportMultiSelect",
                          N_("Whether a list allows several selections")},
                      Row{RowKind::Toggle, "presentation",
                          "reportDynamicContentChanges",
                          N_("Text that changes by itself"),
                          N_("A terminal, a chat window, a log. On by default: a "
                             "terminal that says nothing when a command finishes is "
                             "unusable.")},
                  }},
            Group{N_("Progress bars"), nullptr,
                  {
                      Row{RowKind::Choice, "presentation.progressBarUpdates",
                          "progressBarOutputMode", N_("Report progress with"), nullptr,
                          {{"off", N_("Nothing")},
                           {"beep", N_("A beep")},
                           {"speak", N_("Speech only")},
                           {"both", N_("A beep and speech")}}},
                      Row{RowKind::Toggle, "presentation.progressBarUpdates",
                          "reportBackgroundProgressBars",
                          N_("Report progress bars in other windows")},
                      Row{RowKind::Integer, "presentation.progressBarUpdates",
                          "beepPercentageInterval",
                          N_("Beep every N percent"), nullptr, {}, 1, 50, 1},
                      Row{RowKind::Integer, "presentation.progressBarUpdates",
                          "speechPercentageInterval",
                          N_("Speak every N percent"), nullptr, {}, 1, 50, 1},
                  }},
            Group{N_("Annotations"), nullptr,
                  {
                      Row{RowKind::Toggle, "annotations", "reportDetails",
                          N_("That further details are available")},
                      Row{RowKind::Toggle, "annotations", "reportAriaDescription",
                          N_("An element's accessible description")},
                  }},
            Group{N_("Object navigation"), nullptr,
                  {
                      Row{RowKind::Toggle, "objectNavigation", "reportEdge",
                          N_("Play a sound at the edge of the tree"),
                          N_("An expected failure must be noticeable without being "
                             "an error.")},
                  }},
        }});

    // -----------------------------------------------------------------------
    // Review cursor
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "review", N_("Review cursor"), "edit-find-symbolic",
        {
            Group{N_("What the review cursor follows"), nullptr,
                  {
                      Row{RowKind::Toggle, "reviewCursor", "followFocus",
                          N_("The keyboard focus"),
                          N_("On by default, and the reason `alus+numpad5` says what "
                             "was just focused.")},
                      Row{RowKind::Toggle, "reviewCursor", "followCaret",
                          N_("The caret")},
                      Row{RowKind::Toggle, "reviewCursor", "followMouse",
                          N_("The mouse pointer")},
                      Row{RowKind::Toggle, "reviewCursor", "simpleReviewMode",
                          N_("Simple review"),
                          N_("Skips the containers a toolkit uses for layout and "
                             "that carry no information of their own.")},
                  }},
            Group{N_("Caret"), nullptr,
                  {
                      Row{RowKind::Integer, "editableText", "caretMoveTimeoutMs",
                          N_("How long to wait for an application to move its "
                             "caret, in milliseconds"),
                          N_("Every toolkit reports caret movement differently; this "
                             "is the budget before ALUS reports what is there "
                             "anyway."),
                          {}, 10, 1000, 10},
                  }},
            Group{N_("Document navigation"), nullptr,
                  {
                      Row{RowKind::Choice, "documentNavigation", "paragraphStyle",
                          N_("What ends a paragraph"),
                          N_("No toolkit on Linux answers the accessibility API's "
                             "own paragraph granularity, so these two styles are "
                             "the only way paragraph movement exists at all."),
                          {{"application", N_("The application decides")},
                           {"single", N_("A single line break")},
                           {"multi", N_("A blank line")}}},
                      Row{RowKind::Choice, "documentNavigation", "sayAllReadingUnit",
                          N_("Say all reads by"), nullptr,
                          {{"paragraph", N_("Paragraph")}, {"line", N_("Line")}}},
                  }},
        }});

    // -----------------------------------------------------------------------
    // Document formatting
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "formatting", N_("Document formatting"), "format-text-rich-symbolic",
        {
            Group{N_("Font"), nullptr,
                  {
                      Row{RowKind::Toggle, "documentFormatting", "reportFontName",
                          N_("Font name")},
                      Row{RowKind::Toggle, "documentFormatting", "reportFontSize",
                          N_("Font size")},
                      Row{RowKind::Toggle, "documentFormatting",
                          "reportFontAttributes", N_("Bold, italic and underline")},
                      Row{RowKind::Toggle, "documentFormatting",
                          "reportSuperscriptsAndSubscripts",
                          N_("Superscripts and subscripts")},
                      Row{RowKind::Toggle, "documentFormatting", "reportEmphasis",
                          N_("Emphasis")},
                      Row{RowKind::Toggle, "documentFormatting", "reportHighlight",
                          N_("Highlighted text")},
                      Row{RowKind::Toggle, "documentFormatting", "reportStyle",
                          N_("Style name")},
                      Row{RowKind::Toggle, "documentFormatting", "reportColor",
                          N_("Colours")},
                  }},
            Group{N_("Document information"), nullptr,
                  {
                      Row{RowKind::Toggle, "documentFormatting", "reportComments",
                          N_("Comments")},
                      Row{RowKind::Toggle, "documentFormatting", "reportBookmarks",
                          N_("Bookmarks")},
                      Row{RowKind::Toggle, "documentFormatting", "reportRevisions",
                          N_("Editor revisions")},
                      Row{RowKind::Toggle, "documentFormatting",
                          "reportSpellingErrors", N_("Spelling errors")},
                      Row{RowKind::Toggle, "documentFormatting",
                          "reportGrammarErrors", N_("Grammar errors")},
                  }},
            Group{N_("Pages and spacing"), nullptr,
                  {
                      Row{RowKind::Toggle, "documentFormatting", "reportPage",
                          N_("Page numbers")},
                      Row{RowKind::Toggle, "documentFormatting", "reportLineNumber",
                          N_("Line numbers")},
                      Row{RowKind::Choice, "documentFormatting",
                          "reportLineIndentation", N_("Line indentation"), nullptr,
                          {{"off", N_("Off")},
                           {"speech", N_("Speech only")},
                           {"tones", N_("Tones")},
                           {"both", N_("Speech and tones")}}},
                      Row{RowKind::Integer, "documentFormatting", "indentToneDuration",
                          N_("How long an indentation tone lasts, in milliseconds"),
                          nullptr, {}, 10, 200, 10},
                      Row{RowKind::Toggle, "documentFormatting",
                          "ignoreBlankLinesForRLI",
                          N_("Ignore blank lines when reporting indentation")},
                      Row{RowKind::Toggle, "documentFormatting", "reportLineSpacing",
                          N_("Line spacing")},
                      Row{RowKind::Toggle, "documentFormatting", "reportAlignment",
                          N_("Alignment")},
                  }},
            Group{N_("Tables"), nullptr,
                  {
                      Row{RowKind::Toggle, "documentFormatting", "reportTables",
                          N_("Tables")},
                      Row{RowKind::Toggle, "documentFormatting",
                          "reportTableCellCoords", N_("Cell coordinates")},
                      Row{RowKind::Choice, "documentFormatting", "reportTableHeaders",
                          N_("Table headers"), nullptr,
                          {{"off", N_("Off")},
                           {"rowsAndColumns", N_("Rows and columns")},
                           {"rows", N_("Rows only")},
                           {"columns", N_("Columns only")}}},
                      Row{RowKind::Toggle, "documentFormatting", "reportCellBorders",
                          N_("Cell borders")},
                      Row{RowKind::Toggle, "documentFormatting", "includeLayoutTables",
                          N_("Tables used only for layout"),
                          N_("`alus+shift+f` reports the formatting at the caret; "
                             "this decides whether a table that only arranges a page "
                             "counts as a table at all.")},
                  }},
            Group{N_("Elements"), nullptr,
                  {
                      Row{RowKind::Toggle, "documentFormatting", "reportHeadings",
                          N_("Headings")},
                      Row{RowKind::Toggle, "documentFormatting", "reportLinks",
                          N_("Links")},
                      Row{RowKind::Toggle, "documentFormatting", "reportLinkType",
                          N_("Whether a link has been visited")},
                      Row{RowKind::Toggle, "documentFormatting", "reportGraphics",
                          N_("Graphics")},
                      Row{RowKind::Toggle, "documentFormatting", "reportLists",
                          N_("Lists")},
                      Row{RowKind::Toggle, "documentFormatting", "reportBlockQuotes",
                          N_("Block quotes")},
                      Row{RowKind::Toggle, "documentFormatting", "reportGroupings",
                          N_("Groupings")},
                      Row{RowKind::Toggle, "documentFormatting", "reportLandmarks",
                          N_("Landmarks and regions")},
                      Row{RowKind::Toggle, "documentFormatting", "reportArticles",
                          N_("Articles")},
                      Row{RowKind::Toggle, "documentFormatting", "reportFrames",
                          N_("Frames")},
                      Row{RowKind::Toggle, "documentFormatting", "reportFigures",
                          N_("Figures and captions")},
                      Row{RowKind::Toggle, "documentFormatting", "reportClickable",
                          N_("Clickable elements")},
                  }},
            Group{N_("When formatting is reported"), nullptr,
                  {
                      Row{RowKind::Toggle, "documentFormatting",
                          "detectFormatAfterCursor",
                          N_("Report formatting changes after the cursor"),
                          N_("Costs a call per line; off by default for that "
                             "reason.")},
                  }},
            Group{N_("Braille"), nullptr,
                  {
                      Row{RowKind::Toggle, "documentFormatting",
                          "reportFontAttributesInBraille",
                          N_("Bold, italic and underline on the display")},
                      Row{RowKind::Toggle, "documentFormatting",
                          "reportSpellingErrorsInBraille",
                          N_("Spelling errors on the display")},
                  }},
        }});

    // -----------------------------------------------------------------------
    // Gestures -- a page with one button, because the gesture editor is a
    // dialog of its own (a tree of ~390 commands does not fit a preferences
    // row).
    // -----------------------------------------------------------------------
    out.push_back(Page{
        "gestures", N_("Gestures"), "input-dialpad-symbolic",
        {
            Group{N_("Commands and keys"),
                  N_("Every command ALUS knows, with the keys bound to it. A command "
                     "with no description is not shown, because a command that "
                     "cannot be explained cannot be rebound sensibly."),
                  {
                      Row{RowKind::Action, {}, {}, N_("Edit gestures"), nullptr, {}, 0,
                          100, 1, "gestures"},
                  }},
        }});

    return out;
}

}  // namespace

SpeechChoices querySpeechChoices(const std::string& module) {
    SpeechChoices out;
    speech::SsipSynth synth;
    if (!module.empty()) {
        synth.setPreferredOutputModule(module);
    }
    std::string error;
    if (!synth.initialize(&error)) {
        ALUS_WARNING("settings: speech-dispatcher could not be asked for its voices: {}", error);
        return out;
    }
    out.reachable = true;
    out.modules = synth.availableModules();
    for (const speech::VoiceInfo& voice : synth.availableVoices()) {
        out.voices.push_back({voice.id, voice.name.empty() ? voice.id : voice.name,
                              voice.language});
    }
    synth.terminate();
    return out;
}

std::vector<Choice> resolveChoices(const Row& row, const SpeechChoices& speech,
                                   const std::string& current) {
    std::vector<Choice> out = row.choices;
    if (row.source == "speech-modules") {
        for (const std::string& module : speech.modules) {
            out.push_back(Choice{module, nullptr, module});
        }
    } else if (row.source == "speech-voices") {
        for (const SpeechChoices::Voice& voice : speech.voices) {
            // The language in the label, because two voices can share a name
            // across languages, and because "Marija (hr)" tells a Serbian
            // user something "Marija" does not.
            out.push_back(Choice{voice.id, nullptr,
                                 voice.language.empty()
                                     ? voice.name
                                     : voice.name + " (" + voice.language + ")"});
        }
    } else {
        return out;
    }
    const bool offered = std::any_of(out.begin(), out.end(), [&current](const Choice& choice) {
        return choice.value == current;
    });
    if (!offered) {
        // Translators: a voice or speech module that the configuration names
        // but the synthesizer does not offer -- uninstalled since, or never
        // valid. %s is the configured name, in quotation marks.
        out.push_back(Choice{current, nullptr,
                             i18n::substitute(_("%s (not offered by the synthesizer)"),
                                              "\u201c" + current + "\u201d")});
    }
    return out;
}

const std::vector<Page>& pages() {
    static const std::vector<Page> value = build();
    return value;
}

const Page* findPage(std::string_view id) {
    const std::vector<Page>& all = pages();
    const auto it = std::find_if(all.begin(), all.end(),
                                 [id](const Page& page) { return page.id == id; });
    return it == all.end() ? nullptr : &*it;
}

std::vector<std::pair<std::string, std::string>> allSettingKeys() {
    std::vector<std::pair<std::string, std::string>> keys;
    for (const Page& page : pages()) {
        for (const Group& group : page.groups) {
            for (const Row& row : group.rows) {
                if (row.kind != RowKind::Action) {
                    keys.emplace_back(row.section, row.key);
                }
            }
        }
    }
    return keys;
}

std::vector<std::string> pageIds() {
    std::vector<std::string> ids;
    ids.reserve(pages().size());
    for (const Page& page : pages()) {
        ids.push_back(page.id);
    }
    return ids;
}

}  // namespace alus::gui
