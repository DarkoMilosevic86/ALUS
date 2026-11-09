// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// How a range of text is said out loud.
//
// Port of the text half of NVDA's `speech/speech.py`: `getTextInfoSpeech`
// (391 lines), `getFormatFieldSpeech` (501 lines), `getIndentationSpeech`,
// `speakSelectionChange` and the selection message helpers.
//
// It lives in the object model for the same reason `object_speech.hpp` does
// (claude/FAZA4.md O1): the dependency runs a11y -> speech and never back, and
// a `text::ControlField` carries an `a11y::Role`. Every function here is PURE
// except the three that speak, so a test asserts on what WOULD be said with no
// synthesizer (which is how T5.6 and T5.7 are measured at all).
//
// THE ONE CONTRACT THAT IS NOT OBVIOUS: `getTextRangeSpeech` RETURNS SEVERAL
// SEQUENCES, not one. NVDA's is a generator, and the reason is spelling: a
// character movement yields the properties, then the spelled character, then
// (a second later) its description -- three utterances, because the second
// must be able to interrupt the third. A caller that joins them has broken
// delayed character descriptions.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/a11y/output_reason.hpp"
#include "alus/a11y/settings.hpp"
#include "alus/speech/commands.hpp"
#include "alus/speech/priority.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/symbols.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <optional>
#include <string>
#include <vector>

namespace alus::speech {
class Presenter;
}

namespace alus::a11y {

// NVDA: `SpeakTextInfoState`. What was last said about this object's text, so
// that reading the next line reports only what CHANGED -- the format, the
// indentation, the field stack. Owned by the caller (the object, say-all, the
// review cursor), because it must outlive the one-cycle property cache.
struct TextRangeSpeechState {
    // Phase 6 fills this; phase 5's ranges emit no control fields.
    std::vector<text::ControlField> controlFieldStack;
    // NVDA: `formatFieldAttributesCache`. The attributes as last spoken.
    text::Field formatFieldAttributes;
    // NVDA: `indentationCache`.
    std::string indentation;
};

// NVDA: `speech.getIndentationSpeech`. Tones, words, or both.
speech::SpeechSequence getIndentationSpeech(std::string_view indentation,
                                            const text::FormatReportSettings& settings,
                                            const std::string& locale,
                                            text::SymbolRegistry* symbols);

// NVDA: `speech.getFormatFieldSpeech`. Reports the attributes that DIFFER from
// `attributesCache`, and updates it. A null cache means "report everything",
// which is what the formatting command (alus+f) asks for.
speech::SpeechSequence getFormatFieldSpeech(const text::Field& attributes,
                                            text::Field* attributesCache,
                                            const text::FormatReportSettings& settings,
                                            OutputReason reason,
                                            std::optional<text::TextUnit> unit,
                                            bool extraDetail, bool initialFormat);

// NVDA's `fieldType` argument to `getControlFieldSpeech`, which is a string
// there. Six values, and the distinctions between them are the whole reason
// the function is 300 lines: entering a control, already being in it, and
// leaving it are three different announcements, and each has a "relative"
// variant for a control that opens and closes INSIDE the range being read.
enum class ControlFieldType {
    // NVDA: "start_addedToControlFieldStack". The user has just entered this
    // control -- it is in the new field stack and was not in the old one.
    StartAddedToStack,
    // NVDA: "start_inControlFieldStack". The user was already inside it; said
    // only when moving by line, and only for a single-line category.
    StartInStack,
    // NVDA: "start_relative". The control opens inside the text being read.
    StartRelative,
    // NVDA: "end_removedFromControlFieldStack". The user has left it.
    EndRemovedFromStack,
    // NVDA: "end_inControlFieldStack". Closing a field the user is still in,
    // which is what makes a table cell's reading end cleanly.
    EndInStack,
    // NVDA: "end_relative". The control closes inside the text being read.
    EndRelative,
};

std::string_view controlFieldTypeName(ControlFieldType type);

// NVDA: `speech.getControlFieldSpeech` (:2288-2614). WHAT THE USER HEARS WHEN
// THEY ENTER OR LEAVE A CONTROL INSIDE A DOCUMENT -- "list with 5 items",
// "table with 3 rows and 4 columns", "row 2 column 1", "link", "out of list".
//
// Pure, like everything else in this header, which is how T6.1 and T6.2
// assert on announcements with no synthesizer and no desktop.
//
// `ancestors` is the field stack ABOVE this field, outermost first.
// `objectSettings` carries the presentation settings that are not document
// formatting (report descriptions, report shortcuts) -- NVDA reads them from
// `config.conf` inside the function, and passing them in is what keeps it a
// pure function of its inputs.
speech::SpeechSequence getControlFieldSpeech(const text::ControlField& field,
                                             const std::vector<text::ControlField>& ancestors,
                                             ControlFieldType fieldType,
                                             const text::FormatReportSettings& settings,
                                             OutputReason reason, bool extraDetail,
                                             const Settings& objectSettings);

struct TextRangeSpeechOptions {
    // The unit the range was expanded to, where there was one. It changes what
    // is reported: a character or a word turns on `extraDetail`, and only a
    // line reports indentation.
    std::optional<text::TextUnit> unit;
    OutputReason reason = OutputReason::Query;
    // NVDA's `_prefixSpeechCommand`: say-all's "I have started this line"
    // callback, which must come before everything else in the sequence.
    speech::CommandPtr prefixCommand;
    bool onlyInitialFields = false;
    bool suppressBlanks = false;
    // The presentation settings that are not document formatting (report
    // descriptions, report shortcuts), for the control fields. Null means the
    // defaults, which is what every phase 5 caller wants -- a range with no
    // control fields in it never reads this.
    const Settings* objectSettings = nullptr;
};

struct TextRangeSpeechResult {
    std::vector<speech::SpeechSequence> sequences;
    // NVDA's return value: whether the TEXT itself was spoken, as opposed to
    // only fields. Say-all needs it to know whether to wait.
    bool spokeText = false;
};

// NVDA: `speech.getTextInfoSpeech`. `state` may be null (no caching, report
// everything). `presenter` may be null, in which case a single character is
// spoken rather than spelled -- a test that only wants the words passes null.
TextRangeSpeechResult getTextRangeSpeech(const text::TextRange& range,
                                         TextRangeSpeechState* state,
                                         const text::FormatReportSettings& settings,
                                         const TextRangeSpeechOptions& options,
                                         const speech::Presenter* presenter);

// NVDA: `speech.speakTextInfo`. Returns what the result's `spokeText` says.
bool speakTextRange(speech::Presenter& presenter, const text::TextRange& range,
                    TextRangeSpeechState* state,
                    const text::FormatReportSettings& settings,
                    const TextRangeSpeechOptions& options,
                    speech::Priority priority = speech::Priority::Normal);

// NVDA: `speech.spellTextInfo`. Spells the range, honouring the language
// changes inside it.
void spellTextRange(speech::Presenter& presenter, const text::TextRange& range,
                    const text::FormatReportSettings& settings,
                    bool useCharacterDescriptions = false);

// ---- the selection ----
// NVDA: `getPreselectedTextSpeech` / `speakTextSelected` /
// `speakSelectionMessage`. The ORDER of the word "selected" differs between
// the two: a control that ALREADY had text selected when it got the focus says
// "selected hello world", and text the user has just selected says "hello
// world selected". The difference tells the user whether they are about to
// overwrite something.
speech::SpeechSequence getPreselectedTextSpeech(const std::string& text);
speech::SpeechSequence getSelectionMessageSpeech(const std::string& messageWithPlaceholder,
                                                 const std::string& text);
speech::SpeechSequence getSelectionMessageSpeech(const std::string& messageWithPlaceholder,
                                                 speech::SpeechSequence text);

// NVDA: `speech.speakSelectionChange`. `generalize` is for the case where the
// text itself may have changed between the two ranges, so the specific
// substrings cannot be trusted and only the new selection is reported.
void speakSelectionChange(speech::Presenter& presenter, const text::TextRange& oldRange,
                          const text::TextRange& newRange, bool speakSelected = true,
                          bool speakUnselected = true, bool generalize = false,
                          speech::Priority priority = speech::Priority::Normal);

// NVDA's cap on reporting a selection by its content: beyond it, the number of
// characters is reported instead, because nobody wants to hear a whole file.
inline constexpr std::size_t kMaxLengthForSelectionReporting = 512;

}  // namespace alus::a11y
