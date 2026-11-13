// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/a11y/text_speech.hpp.

#include "alus/a11y/text_speech.hpp"

#include "alus/a11y/object_speech.hpp"

#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace alus::a11y {
namespace {

using speech::SpeechItem;
using speech::SpeechSequence;

// NVDA: IDT_BASE_FREQUENCY, one octave below middle A, and the limit beyond
// which the indentation is spoken rather than played -- 72 spaces of tone
// would hurt.
constexpr double kIndentBaseFrequency = 220.0;
constexpr int kMaxIndentQuarterTones = 72;

// For checking that a line is completely blank: NVDA's LINE_END_CHARS.
bool isOnlyLineEnds(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char character) {
               return character == '\r' || character == '\n';
           });
}

void append(SpeechSequence& sequence, std::string text) {
    if (!text.empty()) {
        sequence.emplace_back(std::move(text));
    }
}

// The diff test NVDA repeats for every attribute: report when the value
// changed, and -- for a value that can be switched off -- also when it went
// away, but only if the cache knew about it.
bool changed(const text::Field& attributes, const text::Field* cache, std::string_view key) {
    const std::string_view value = attributes.get(key);
    if (cache == nullptr) {
        return !value.empty();
    }
    const bool hadIt = cache->contains(key);
    const std::string_view old = cache->get(key);
    if (!value.empty()) {
        return value != old;
    }
    return hadIt && !old.empty();
}

bool isSet(const text::Field& attributes, std::string_view key) {
    const std::optional<bool> value = attributes.getBool(key);
    if (value.has_value()) {
        return *value;
    }
    return !attributes.get(key).empty();
}

// NVDA reports a boolean attribute both ways round: "bold" and "no bold",
// because the absence of bold on this line is news when the last one had it.
void reportToggle(SpeechSequence& out, const text::Field& attributes,
                  const text::Field* cache, std::string_view key, std::string_view onText,
                  std::string_view offText) {
    if (!changed(attributes, cache, key)) {
        return;
    }
    out.emplace_back(std::string(isSet(attributes, key) ? onText : offText));
}

}  // namespace

// ---------------------------------------------------------------------------
// Indentation
// ---------------------------------------------------------------------------

SpeechSequence getIndentationSpeech(std::string_view indentation,
                                    const text::FormatReportSettings& settings,
                                    const std::string& locale,
                                    text::SymbolRegistry* symbols) {
    using LineIndentation = text::FormatReportSettings::LineIndentation;
    const bool speakIt = settings.reportLineIndentation == LineIndentation::Speech ||
                         settings.reportLineIndentation == LineIndentation::SpeechAndTones;
    const bool toneIt = settings.reportLineIndentation == LineIndentation::Tones ||
                        settings.reportLineIndentation == LineIndentation::SpeechAndTones;

    SpeechSequence sequence;
    if (indentation.empty()) {
        if (toneIt) {
            sequence.emplace_back(speech::beep(kIndentBaseFrequency, settings.indentToneDuration));
        }
        if (speakIt) {
            // Translators: spoken when the line has no indentation.
            sequence.emplace_back(std::string(_("no indent")));
        }
        return sequence;
    }

    // NVDA replaces the non-breaking space here, because it is semantically a
    // space and a tone for it would otherwise be a different tone.
    std::string normalised;
    normalised.reserve(indentation.size());
    {
        const text::OffsetConverter converter(indentation);
        for (std::size_t index = 0; index < converter.codepointLength(); ++index) {
            const auto [start, end] = converter.codepointRangeAt(converter.toByte(index));
            const std::string_view piece = indentation.substr(start, end - start);
            normalised += piece == "\xC2\xA0" ? " " : std::string(piece);
        }
    }

    // NVDA: RE_INDENTATION_CONVERT, r"(?P<char>\s)(?P=char)*" -- runs of the
    // SAME whitespace character, so "two tab four space" and not eight words.
    std::vector<std::string> words;
    int quarterTones = 0;
    std::size_t index = 0;
    while (index < normalised.size()) {
        const char character = normalised[index];
        std::size_t run = 1;
        while (index + run < normalised.size() && normalised[index + run] == character) {
            ++run;
        }
        const std::string raw(1, character);
        const std::string symbol =
            symbols != nullptr ? symbols->processSymbol(locale, raw) : raw;
        if (symbol == raw) {
            // No replacement for this character: NVDA passes it through
            // unchanged rather than inventing a word for it.
            words.push_back(normalised.substr(index, run));
        } else if (run == 1) {
            words.push_back(symbol);
        } else {
            words.push_back(std::to_string(run) + " " + symbol);
        }
        // A tab is worth four spaces of pitch, which is what makes code
        // indentation audible at all.
        quarterTones += character == '\t' ? static_cast<int>(run) * 4 : static_cast<int>(run);
        index += run;
    }

    bool speakAnyway = speakIt;
    if (toneIt) {
        if (quarterTones <= kMaxIndentQuarterTones) {
            // 24 quarter tones to the octave.
            const double pitch =
                kIndentBaseFrequency * std::pow(2.0, static_cast<double>(quarterTones) / 24.0);
            sequence.emplace_back(speech::beep(pitch, settings.indentToneDuration));
        } else {
            // More than 72 spaces: speak it instead of hurting the user's ears.
            speakAnyway = true;
        }
    }
    if (speakAnyway) {
        for (std::string& word : words) {
            sequence.emplace_back(std::move(word));
        }
    }
    return sequence;
}

// ---------------------------------------------------------------------------
// Format fields
// ---------------------------------------------------------------------------

SpeechSequence getFormatFieldSpeech(const text::Field& attributes,
                                    text::Field* attributesCache,
                                    const text::FormatReportSettings& settings,
                                    OutputReason reason, std::optional<text::TextUnit> unit,
                                    bool extraDetail, bool initialFormat) {
    SpeechSequence out;
    const text::Field* cache = attributesCache;

    // NVDA reports a heading level and a collapsed state NOT ONLY WHEN THEY
    // CHANGE but also at the start of a line or a paragraph, so that a heading
    // in a document sounds like a heading would if it were a control field.
    const bool startOfReadableUnit =
        initialFormat && (reason == OutputReason::Focus || reason == OutputReason::QuickNav ||
                          unit == text::TextUnit::Line || unit == text::TextUnit::Paragraph);

    // ---- page and section ----
    if (settings.reportPage) {
        if (changed(attributes, cache, "page-number")) {
            // Translators: the page number in a document; %s is the number.
            append(out, i18n::substitute(_("page %s"), attributes.get("page-number")));
        }
        if (changed(attributes, cache, "section-number")) {
            // Translators: the section number in a document; %s is the number.
            append(out,
                   i18n::substitute(_("section %s"), attributes.get("section-number")));
        }
        const std::optional<int> columnCount = attributes.getInt("text-column-count");
        const std::optional<int> columnNumber = attributes.getInt("text-column-number");
        const bool columnChanged = changed(attributes, cache, "text-column-number") ||
                                   changed(attributes, cache, "text-column-count");
        // NVDA's exception: a document that simply has one column is not news.
        const bool onlyOneColumn =
            columnCount.has_value() && *columnCount <= 1 &&
            (cache == nullptr || !cache->contains("text-column-count"));
        if (columnChanged && !onlyOneColumn) {
            if (columnNumber.has_value() && columnCount.has_value()) {
                append(out, "column " + std::to_string(*columnNumber) + " of " +
                                std::to_string(*columnCount));
            } else if (columnCount.has_value()) {
                append(out, std::to_string(*columnCount) + " columns");
            } else if (columnNumber.has_value()) {
                append(out, "column " + std::to_string(*columnNumber));
            }
        }
    }
    if (!attributes.get("section-break").empty()) {
        const std::string_view kind = attributes.get("section-break");
        if (kind == "0") {
            append(out, "continuous section break");
        } else if (kind == "1") {
            append(out, "new column section break");
        } else if (kind == "2") {
            append(out, "new page section break");
        } else if (kind == "3") {
            append(out, "even pages section break");
        } else if (kind == "4") {
            append(out, "odd pages section break");
        }
    }
    if (!attributes.get("column-break").empty()) {
        append(out, "column break");
    }

    // ---- heading and collapsed: reported at the start of a unit too ----
    if (settings.reportHeadings) {
        const std::optional<int> level = attributes.getInt("heading-level");
        if (level.has_value() &&
            (startOfReadableUnit || changed(attributes, cache, "heading-level"))) {
            append(out, "heading level " + std::to_string(*level));
        }
    }
    if (isSet(attributes, "collapsed") &&
        (startOfReadableUnit || changed(attributes, cache, "collapsed"))) {
        append(out, std::string(stateLabel(State::Collapsed)));
    }

    // ---- style and borders ----
    if (settings.reportStyle && changed(attributes, cache, "style")) {
        const std::string_view style = attributes.get("style");
        append(out, style.empty() ? std::string("default style")
                                  : "style " + std::string(style));
    }
    if (settings.reportBorderStyle && changed(attributes, cache, "border-style")) {
        const std::string_view border = attributes.get("border-style");
        append(out, border.empty() ? std::string("no border lines") : std::string(border));
    }

    // ---- font ----
    if (settings.reportFontName) {
        if (changed(attributes, cache, "font-family")) {
            append(out, std::string(attributes.get("font-family")));
        }
        // NVDA reports both keys, because a toolkit may set only one of them.
        if (changed(attributes, cache, "font-name") &&
            attributes.get("font-name") != attributes.get("font-family")) {
            append(out, std::string(attributes.get("font-name")));
        }
    }
    if (settings.reportFontSize && changed(attributes, cache, "font-size")) {
        append(out, std::string(attributes.get("font-size")));
    }

    // ---- colour ----
    if (settings.reportColor) {
        const std::string_view color = attributes.get("color");
        const std::string_view background = attributes.get("background-color");
        const bool colorChanged = changed(attributes, cache, "color");
        const bool backgroundChanged = changed(attributes, cache, "background-color");
        if (!color.empty() && !background.empty() && colorChanged && backgroundChanged) {
            // Translators: both the text and background colours changed; the
            // first %s is the text colour and the second the background,
            // e.g. "white on blue".
            append(out, i18n::substitute(_("%s on %s"), color, background));
        } else if (!color.empty() && colorChanged) {
            append(out, std::string(color));
        } else if (!background.empty() && backgroundChanged) {
            // Translators: only the background colour changed; %s is the
            // colour, e.g. "blue background".
            append(out, i18n::substitute(_("%s background"), background));
        }
    }

    // ---- the line number ----
    if (settings.reportLineNumber && changed(attributes, cache, "line-number")) {
        append(out, "line " + std::string(attributes.get("line-number")));
    }

    // ---- revisions, highlight, emphasis ----
    if (settings.reportRevisions) {
        reportToggle(out, attributes, cache, "revision-insertion", "inserted", "not inserted");
        reportToggle(out, attributes, cache, "revision-deletion", "deleted", "not deleted");
        if (changed(attributes, cache, "revision")) {
            const std::string_view revision = attributes.get("revision");
            append(out, revision.empty() ? std::string("no revised")
                                         : "revised " + std::string(revision));
        }
    }
    if (settings.reportHighlight) {
        reportToggle(out, attributes, cache, "marked", "marked", "not marked");
        if (changed(attributes, cache, "highlight-color")) {
            const std::string_view color = attributes.get("highlight-color");
            append(out, color.empty() ? std::string("not highlighted")
                                      : "highlighted in " + std::string(color));
        }
    }
    if (settings.reportEmphasis) {
        reportToggle(out, attributes, cache, "strong", "strong", "not strong");
        reportToggle(out, attributes, cache, "emphasised", "emphasised", "not emphasised");
    }

    // ---- the font attributes ----
    if (settings.reportFontAttributes) {
        reportToggle(out, attributes, cache, "bold", "bold", "no bold");
        reportToggle(out, attributes, cache, "italic", "italic", "no italic");
        if (changed(attributes, cache, "strikethrough")) {
            if (isSet(attributes, "strikethrough")) {
                append(out, attributes.get("strikethrough") == "double"
                                ? std::string("double strikethrough")
                                : std::string("strikethrough"));
            } else {
                append(out, std::string("no strikethrough"));
            }
        }
        reportToggle(out, attributes, cache, "underline", "underlined", "not underlined");
        reportToggle(out, attributes, cache, "hidden", "hidden", "not hidden");
    }

    // ---- superscript and subscript ----
    if (settings.reportSuperscriptsAndSubscripts &&
        changed(attributes, cache, "text-position")) {
        const std::string_view position = attributes.get("text-position");
        const bool wasKnown = cache != nullptr && cache->contains("text-position") &&
                              !cache->get("text-position").empty();
        if (position == "superscript" || position == "subscript") {
            append(out, std::string(position));
        } else if (position == "baseline" && wasKnown) {
            // NVDA says "baseline" only on the way BACK from a superscript:
            // every ordinary line is baseline and saying so would be noise.
            append(out, std::string(position));
        }
    }

    // ---- alignment and indentation ----
    if (settings.reportAlignment) {
        if (changed(attributes, cache, "text-align")) {
            append(out, std::string(attributes.get("text-align")));
        }
        if (changed(attributes, cache, "vertical-align")) {
            append(out, std::string(attributes.get("vertical-align")));
        }
    }
    if (settings.reportParagraphIndentation) {
        struct IndentLabel {
            std::string_view key;
            std::string_view label;
            std::string_view none;
        };
        static constexpr std::array<IndentLabel, 4> kIndents{{
            {"left-indent", "left indent", "no left indent"},
            {"right-indent", "right indent", "no right indent"},
            {"hanging-indent", "hanging indent", "no hanging indent"},
            {"first-line-indent", "first line indent", "no first line indent"},
        }};
        for (const IndentLabel& indent : kIndents) {
            if (!changed(attributes, cache, indent.key)) {
                continue;
            }
            const std::string_view value = attributes.get(indent.key);
            append(out, value.empty() ? std::string(indent.none)
                                      : std::string(indent.label) + " " + std::string(value));
        }
    }
    if (settings.reportLineSpacing && changed(attributes, cache, "line-spacing")) {
        append(out, "line spacing " + std::string(attributes.get("line-spacing")));
    }

    // ---- links, comments, bookmarks ----
    if (settings.reportLinks && changed(attributes, cache, "link")) {
        append(out, isSet(attributes, "link") ? std::string("link")
                                              : std::string("out of link"));
    }
    if (settings.reportComments && changed(attributes, cache, "comment")) {
        const std::string_view comment = attributes.get("comment");
        if (!comment.empty()) {
            if (comment == "draft") {
                append(out, std::string("has draft comment"));
            } else if (comment == "resolved") {
                append(out, std::string("has resolved comment"));
            } else {
                append(out, std::string("has comment"));
            }
        } else if (extraDetail) {
            append(out, std::string("out of comment"));
        }
    }
    if (settings.reportBookmarks && changed(attributes, cache, "bookmark")) {
        if (isSet(attributes, "bookmark")) {
            append(out, std::string("bookmark"));
        } else if (extraDetail) {
            append(out, std::string("out of bookmark"));
        }
    }

    // ---- spelling and grammar ----
    if (settings.reportSpellingErrors && changed(attributes, cache, "invalid-spelling")) {
        if (isSet(attributes, "invalid-spelling")) {
            append(out, std::string("spelling error"));
        } else if (extraDetail) {
            append(out, std::string("out of spelling error"));
        }
    }
    if (settings.reportGrammarErrors && changed(attributes, cache, "invalid-grammar")) {
        if (isSet(attributes, "invalid-grammar")) {
            append(out, std::string("grammar error"));
        } else if (extraDetail) {
            append(out, std::string("out of grammar error"));
        }
    }

    // ---- the list bullet ----
    // NVDA's note, kept: `line-prefix` is the bullet or number of a list item
    // when it is not in the text itself, and it may repeat across the format
    // fields of one item -- so it is only safe for a whole-line unit, unless
    // the implementation promises otherwise with `line-prefix_speakAlways`.
    const bool prefixAlways = isSet(attributes, "line-prefix_speakAlways");
    if (prefixAlways || unit == text::TextUnit::Line || unit == text::TextUnit::Sentence ||
        unit == text::TextUnit::Paragraph || unit == text::TextUnit::ReadingChunk) {
        append(out, std::string(attributes.get("line-prefix")));
    }

    if (attributesCache != nullptr) {
        *attributesCache = attributes;
    }
    return out;
}

// ---------------------------------------------------------------------------
// getControlFieldSpeech
//
// Port of NVDA's `speech.getControlFieldSpeech` (:2288-2614). The hole phase 5
// left behind on purpose (FAZA5.md O14): the loop in `getTextRangeSpeech`
// already maintained the field stack, and this is what it was waiting for.
// ---------------------------------------------------------------------------

namespace {

bool isTableCellField(const text::ControlField& field) {
    return field.role == Role::TableCell || field.role == Role::TableColumnHeader ||
           field.role == Role::TableRowHeader;
}

// NVDA: `_shouldSpeakContentFirst` (:2253-2283). Whether the control's CONTENT
// comes before what the control is. On a focus change it does, because the
// user wants the link's text before the word "link" -- but never for a
// container, never for an editable, and never inside a table.
bool shouldSpeakContentFirst(OutputReason reason, Role role,
                             text::PresentationCategory category, bool hasTableId,
                             const StateSet& states) {
    switch (role) {
        case Role::EditableText:
        case Role::ComboBox:
        case Role::TreeView:
        case Role::List:
        case Role::Landmark:
        case Role::Region:
            return false;
        default:
            break;
    }
    const bool focusLike = reason == OutputReason::Focus || reason == OutputReason::QuickNav;
    // #11103: an article is the one container whose content still comes first.
    const bool categoryAllows =
        category != text::PresentationCategory::Container || role == Role::Article;
    return focusLike && categoryAllows && !hasTableId && !states.has(State::Editable);
}

// The field's own values, read once. NVDA reads `attrs.get(...)` inline
// thirty times; collecting them is what makes the rules below readable
// against the original.
struct FieldValues {
    Role role = Role::Unknown;
    StateSet states;
    std::string name;
    std::string value;
    std::string description;
    std::string keyboardShortcut;
    std::string placeholder;
    std::string errorMessage;
    std::string content;
    std::string roleText;
    std::string landmark;
    IsCurrent current = IsCurrent::No;
    bool hasDetails = false;
    std::optional<int> level;
    std::optional<int> childControlCount;
    std::optional<int> tableId;
    std::optional<int> rowNumber;
    std::optional<int> columnNumber;
    std::optional<int> rowSpan;
    std::optional<int> columnSpan;
    std::optional<int> rowCount;
    std::optional<int> columnCount;
    std::string rowHeaderText;
    std::string columnHeaderText;
    bool alwaysReportName = false;
    bool labelledByContent = false;
};

FieldValues readField(const text::ControlField& field) {
    FieldValues out;
    out.role = field.role;
    out.states = field.states;
    out.name = field.name;
    out.roleText = field.roleText;
    out.landmark = field.landmark;
    out.value = std::string(field.get("value"));
    out.description = std::string(field.get("description"));
    out.keyboardShortcut = std::string(field.get("keyboardShortcut"));
    out.placeholder = std::string(field.get("placeholder"));
    out.content = std::string(field.get("content"));
    out.current = isCurrentFromString(field.get("current"));
    out.hasDetails = field.getBool("hasDetails").value_or(false);
    out.alwaysReportName = field.getBool("alwaysReportName").value_or(false);
    out.labelledByContent = field.getBool("labelledByContent").value_or(false);
    out.level = field.getInt("level");
    out.childControlCount = field.getInt("_childcontrolcount");
    out.tableId = field.getInt("table-id");
    out.rowNumber = field.getInt("table-rownumber");
    out.columnNumber = field.getInt("table-columnnumber");
    out.rowSpan = field.getInt("table-rowsspanned");
    out.columnSpan = field.getInt("table-columnsspanned");
    out.rowCount = field.getInt("table-rowcount");
    out.columnCount = field.getInt("table-columncount");
    out.rowHeaderText = std::string(field.get("table-rowheadertext"));
    out.columnHeaderText = std::string(field.get("table-columnheadertext"));
    // An error message is only news when the entry is actually invalid.
    if (out.states.has(State::InvalidEntry)) {
        out.errorMessage = std::string(field.get("errorMessage"));
    }
    return out;
}

void extend(SpeechSequence& into, SpeechSequence from) {
    for (SpeechItem& item : from) {
        into.push_back(std::move(item));
    }
}

// P_ and not `count == 1 ? ... : ...`: Serbian has three plural forms and
// English has two, so the count has to reach the catalogue or every Slavic
// translation is wrong at 2, 3 and 4.
std::string itemCountText(int count) {
    // Translators: Number of items in a list (example output: list with 5
    // items); %s is the count.
    return i18n::substitute(P_("with %s item", "with %s items", count),
                            std::to_string(count));
}

}  // namespace

std::string_view controlFieldTypeName(ControlFieldType type) {
    switch (type) {
        case ControlFieldType::StartAddedToStack:
            return "start_addedToControlFieldStack";
        case ControlFieldType::StartInStack:
            return "start_inControlFieldStack";
        case ControlFieldType::StartRelative:
            return "start_relative";
        case ControlFieldType::EndRemovedFromStack:
            return "end_removedFromControlFieldStack";
        case ControlFieldType::EndInStack:
            return "end_inControlFieldStack";
        case ControlFieldType::EndRelative:
            return "end_relative";
    }
    return "?";
}

SpeechSequence getControlFieldSpeech(const text::ControlField& field,
                                     const std::vector<text::ControlField>& ancestors,
                                     ControlFieldType fieldType,
                                     const text::FormatReportSettings& settings,
                                     OutputReason reason, bool extraDetail,
                                     const Settings& objectSettings) {
    // A hidden field says nothing at all -- it is text that exists and is not
    // drawn (N46), and announcing its container would be a lie about the page.
    if (field.isHidden) {
        return {};
    }

    const FieldValues values = readField(field);
    const text::PresentationCategory category =
        field.getPresentationCategory(ancestors, settings, extraDetail);

    const bool focusLike = reason == OutputReason::Focus || reason == OutputReason::QuickNav;

    // THE NAME IS NOT ALWAYS SAID. Reading a document line by line must not
    // repeat the container's name on every line; a focus change and a
    // quick-nav jump must say it, and a landmark always says it.
    const std::string name = (focusLike || values.alwaysReportName) ? values.name : std::string{};

    // A layout field carries no table identity: that is what stops a layout
    // table from announcing coordinates.
    const std::optional<int> tableId =
        category != text::PresentationCategory::Layout ? values.tableId : std::nullopt;

    // ---- the property sequences, each built once ----
    PropertyValues base;
    base.settings = objectSettings;
    base.realRole = values.role;
    base.realStates = values.states;

    SpeechSequence roleTextSequence;
    if (!values.roleText.empty()) {
        roleTextSequence.emplace_back(values.roleText);
    } else if (values.role == Role::Landmark && !values.landmark.empty()) {
        // "navigation landmark": the ARIA token and the word, in that order.
        PropertyValues landmarkValues = base;
        landmarkValues.role = Role::Landmark;
        SpeechSequence roleOnly = getPropertiesSpeech(landmarkValues, reason);
        std::string label = values.landmark;
        if (!roleOnly.empty() && roleOnly.front().isText()) {
            label += " " + roleOnly.front().text();
        }
        roleTextSequence.emplace_back(std::move(label));
    } else {
        PropertyValues roleValues = base;
        roleValues.role = values.role;
        roleTextSequence = getPropertiesSpeech(roleValues, reason);
    }

    SpeechSequence stateTextSequence;
    {
        PropertyValues stateValues = base;
        stateValues.states = values.states;
        stateTextSequence = getPropertiesSpeech(stateValues, reason);
    }

    SpeechSequence nameSequence;
    if (!name.empty()) {
        PropertyValues nameValues = base;
        nameValues.name = name;
        nameSequence = getPropertiesSpeech(nameValues, reason);
    }

    SpeechSequence valueSequence;
    if (!values.value.empty()) {
        PropertyValues valueValues = base;
        valueValues.value = values.value;
        valueSequence = getPropertiesSpeech(valueValues, reason);
    }

    SpeechSequence placeholderSequence;
    if (!values.placeholder.empty()) {
        PropertyValues placeholderValues = base;
        placeholderValues.placeholder = values.placeholder;
        placeholderSequence = getPropertiesSpeech(placeholderValues, reason);
    }

    // The description is reported far more sparingly than the name: only on a
    // focus-like reason, and only when the user asked for descriptions at all.
    SpeechSequence descriptionSequence;
    const bool reportDescription =
        !values.description.empty() && objectSettings.reportObjectDescriptions && focusLike;
    if (reportDescription) {
        PropertyValues descriptionValues = base;
        descriptionValues.description = values.description;
        descriptionSequence = getPropertiesSpeech(descriptionValues, reason);
    }

    SpeechSequence keyboardShortcutSequence;
    if (objectSettings.reportKeyboardShortcuts && !values.keyboardShortcut.empty()) {
        PropertyValues shortcutValues = base;
        shortcutValues.keyboardShortcut = values.keyboardShortcut;
        keyboardShortcutSequence = getPropertiesSpeech(shortcutValues, reason);
    }

    SpeechSequence isCurrentSequence;
    if (values.current != IsCurrent::No) {
        PropertyValues currentValues = base;
        currentValues.current = values.current;
        isCurrentSequence = getPropertiesSpeech(currentValues, reason);
    }

    SpeechSequence hasDetailsSequence;
    if (values.hasDetails) {
        PropertyValues detailValues = base;
        detailValues.hasDetails = true;
        hasDetailsSequence = getPropertiesSpeech(detailValues, reason);
    }

    SpeechSequence errorMessageSequence;
    if (!values.errorMessage.empty()) {
        PropertyValues errorValues = base;
        errorValues.errorMessage = values.errorMessage;
        errorMessageSequence = getPropertiesSpeech(errorValues, reason);
    }

    SpeechSequence levelSequence;
    if (values.level.has_value()) {
        PropertyValues levelValues = base;
        levelValues.level = values.level;
        levelSequence = getPropertiesSpeech(levelValues, reason);
    }

    // ---- under what circumstances is this control spoken at all ----
    // NVDA's four flags, and the table that sets them is the presentation
    // category. This is the rule that keeps a document readable: a CONTAINER
    // is announced on entry and on exit, a SINGLELINE also while you are
    // inside it, and a LAYOUT field never.
    bool speakEntry = false;
    bool speakWithinForLine = false;
    bool speakExitForLine = false;
    bool speakExitForOther = false;
    switch (category) {
        case text::PresentationCategory::SingleLine:
            speakEntry = true;
            speakWithinForLine = true;
            speakExitForOther = true;
            break;
        case text::PresentationCategory::Marker:
        case text::PresentationCategory::Cell:
            speakEntry = true;
            break;
        case text::PresentationCategory::Container:
            speakEntry = true;
            // A landmark with no roleText does not announce its exit: "out of
            // navigation landmark" on every line of a nav block is noise.
            speakExitForLine = !values.roleText.empty() || values.role != Role::Landmark;
            speakExitForOther = true;
            break;
        case text::PresentationCategory::Layout:
            break;
    }

    const bool speakContentFirst = shouldSpeakContentFirst(
        reason, values.role, category, tableId.has_value(), values.states);
    // A link says its states before its role: "visited link", not "link
    // visited".
    const bool speakStatesFirst = values.role == Role::Link;

    std::string containerContainsText;

    // ---- the special cases, in NVDA's own order ----

    // A list announces how many items it has. #7652: the text is computed here
    // and emitted by the general case below, so that the name, the states and
    // the level are still reported in the right order.
    if (values.childControlCount.value_or(0) > 0 &&
        fieldType == ControlFieldType::StartAddedToStack && values.role == Role::List &&
        values.states.has(State::ReadOnly)) {
        containerContainsText = itemCountText(*values.childControlCount);
    } else if (fieldType == ControlFieldType::StartAddedToStack &&
               values.role == Role::Table && tableId.has_value()) {
        // A table announces its size, and nothing else: entering a table is
        // the one moment the user needs the shape before the content.
        SpeechSequence out = nameSequence;
        extend(out, roleTextSequence);
        extend(out, stateTextSequence);
        PropertyValues tableValues = base;
        tableValues.tableId = static_cast<Ref>(*tableId);
        tableValues.rowCount = values.rowCount;
        tableValues.columnCount = values.columnCount;
        extend(out, getPropertiesSpeech(tableValues, reason));
        extend(out, levelSequence);
        return out;
    } else if (!nameSequence.empty() && focusLike &&
               fieldType == ControlFieldType::StartAddedToStack &&
               (values.role == Role::Grouping || values.role == Role::PropertyPage ||
                values.role == Role::Landmark || values.role == Role::Region)) {
        // #10095, #3321, #709, #13307: a grouping, a tab page, a landmark and
        // a region report their LABEL and their role, and nothing else.
        SpeechSequence out = nameSequence;
        if ((values.role != Role::Landmark && values.role != Role::Region) ||
            settings.reportLandmarks) {
            extend(out, roleTextSequence);
        }
        return out;
    } else if ((fieldType == ControlFieldType::StartAddedToStack ||
                fieldType == ControlFieldType::StartRelative) &&
               isTableCellField(field) && tableId.has_value()) {
        // A table cell: its coordinates, its headers, its states. NOT its
        // role -- "row 2 column 1" already says it is a cell.
        PropertyValues cellValues = base;
        cellValues.tableId = static_cast<Ref>(*tableId);
        cellValues.rowNumber = values.rowNumber;
        cellValues.columnNumber = values.columnNumber;
        cellValues.rowSpan = values.rowSpan;
        cellValues.columnSpan = values.columnSpan;
        cellValues.includeTableCellCoords = settings.reportTableCellCoords;
        if (settings.reportTableHeaders) {
            if (!values.rowHeaderText.empty()) {
                cellValues.rowHeaderText = values.rowHeaderText;
            }
            if (!values.columnHeaderText.empty()) {
                cellValues.columnHeaderText = values.columnHeaderText;
            }
        }
        SpeechSequence out = getPropertiesSpeech(cellValues, reason);
        extend(out, stateTextSequence);
        extend(out, isCurrentSequence);
        extend(out, hasDetailsSequence);
        return out;
    }

    // ---- the general cases ----
    const bool entryMoment =
        speakEntry &&
        ((speakContentFirst && (fieldType == ControlFieldType::EndRelative ||
                                fieldType == ControlFieldType::EndInStack)) ||
         (!speakContentFirst && (fieldType == ControlFieldType::StartAddedToStack ||
                                 fieldType == ControlFieldType::StartRelative)));
    const bool withinMoment = speakWithinForLine && !speakContentFirst && !extraDetail &&
                              fieldType == ControlFieldType::StartInStack;

    if (entryMoment || withinMoment) {
        SpeechSequence out;
        if (!values.content.empty() && speakContentFirst) {
            out.emplace_back(values.content);
        }
        SpeechSequence effectiveValue = valueSequence;
        if (!values.placeholder.empty()) {
            if (!valueSequence.empty()) {
                ALUS_WARNING("control field has both a value and a placeholder");
            }
            effectiveValue = placeholderSequence;
        }
        // Avoid saying the name twice: a check box labelled by a child element
        // has that child's text coming right behind it.
        if (!values.labelledByContent) {
            extend(out, nameSequence);
        } else {
            ALUS_DEBUG("control field is labelled by content; its name is left to the content");
        }
        extend(out, speakStatesFirst ? stateTextSequence : roleTextSequence);
        extend(out, speakStatesFirst ? roleTextSequence : stateTextSequence);
        if (!containerContainsText.empty()) {
            out.emplace_back(containerContainsText);
        }
        extend(out, isCurrentSequence);
        extend(out, hasDetailsSequence);
        extend(out, effectiveValue);
        extend(out, descriptionSequence);
        extend(out, levelSequence);
        extend(out, keyboardShortcutSequence);
        if (!values.content.empty() && !speakContentFirst) {
            out.emplace_back(values.content);
        }
        extend(out, errorMessageSequence);
        return out;
    }

    const bool exitMoment = (fieldType == ControlFieldType::EndRemovedFromStack ||
                             fieldType == ControlFieldType::EndRelative) &&
                            !roleTextSequence.empty() &&
                            ((!extraDetail && speakExitForLine) ||
                             (extraDetail && speakExitForOther));
    if (exitMoment) {
        const bool allText = std::all_of(roleTextSequence.begin(), roleTextSequence.end(),
                                         [](const SpeechItem& item) { return item.isText(); });
        if (!allText) {
            return roleTextSequence;
        }
        std::string joined;
        for (const SpeechItem& item : roleTextSequence) {
            if (!joined.empty()) {
                joined += " ";
            }
            joined += item.text();
        }
        SpeechSequence out;
        // Translators: Indicates the end of something; %s is what is being
        // left, so the result reads "out of list".
        out.emplace_back(i18n::substitute(_("out of %s"), joined));
        return out;
    }

    // A control that is NOT announced on entry may still carry news: an
    // aria-current, details, or a tree item's expanded state and level.
    if (!speakEntry && (fieldType == ControlFieldType::StartAddedToStack ||
                        fieldType == ControlFieldType::StartRelative)) {
        SpeechSequence out;
        extend(out, isCurrentSequence);
        extend(out, hasDetailsSequence);
        if (values.role == Role::TreeViewItem) {
            if (values.states.has(State::Expanded) || values.states.has(State::Collapsed)) {
                PropertyValues stateValues = base;
                stateValues.states = StateSet{values.states.has(State::Expanded)
                                                  ? State::Expanded
                                                  : State::Collapsed};
                extend(out, getPropertiesSpeech(stateValues, reason));
            }
            extend(out, levelSequence);
        }
        if (values.role == Role::Graphic && !values.content.empty()) {
            out.emplace_back(values.content);
        }
        return out;
    }

    return {};
}

// ---------------------------------------------------------------------------
// getTextRangeSpeech
// ---------------------------------------------------------------------------

TextRangeSpeechResult getTextRangeSpeech(const text::TextRange& range,
                                         TextRangeSpeechState* state,
                                         const text::FormatReportSettings& settings,
                                         const TextRangeSpeechOptions& options,
                                         const speech::Presenter* presenter) {
    TextRangeSpeechResult result;

    // `extraDetail` is NVDA's word for "the user is inspecting one character
    // or one word", and it turns on the reporting that would be unbearable on
    // a whole line ("out of comment", "out of spelling error").
    const bool extraDetail =
        options.unit == text::TextUnit::Character || options.unit == text::TextUnit::Word;
    text::FormatReportSettings formatSettings = settings;
    if (extraDetail) {
        formatSettings.detectFormatAfterCursor = true;
    }
    const bool reportIndentation =
        options.unit == text::TextUnit::Line &&
        settings.reportLineIndentation != text::FormatReportSettings::LineIndentation::Off;
    text::FormatReportSettings effective = formatSettings;
    if ((options.unit == text::TextUnit::Paragraph || options.unit == text::TextUnit::Cell) &&
        options.reason == OutputReason::Caret) {
        // NVDA: for performance, spelling errors are not announced when
        // navigating by paragraph or by table cell.
        effective.reportSpellingErrors = false;
        effective.reportGrammarErrors = false;
    }

    text::TextWithFields fields = range.getTextWithFields(effective);

    const Settings defaultObjectSettings;
    const Settings& objectSettings = options.objectSettings != nullptr
                                         ? *options.objectSettings
                                         : defaultObjectSettings;

    // The initial fields: everything before the first piece of text. These
    // become the new field STACK, and what is said about them is a DIFF
    // against the stack that was last spoken -- which is the whole reason
    // reading a hundred lines of a list does not say "list with 5 items" a
    // hundred times (T6.3).
    text::FormatField initialFormat;
    std::vector<text::ControlField> newStack;
    std::size_t initialCount = 0;
    for (const text::TextOrField& item : fields) {
        if (!item.isCommand()) {
            break;
        }
        const text::FieldCommand& command = *item.command;
        if (command.kind() == text::FieldCommand::Kind::ControlStart) {
            if (command.control() != nullptr) {
                newStack.push_back(*command.control());
            }
        } else if (command.kind() == text::FieldCommand::Kind::FormatChange) {
            if (command.format() != nullptr) {
                initialFormat.update(*command.format());
            }
        } else {
            break;
        }
        ++initialCount;
    }
    fields.erase(fields.begin(), fields.begin() + static_cast<std::ptrdiff_t>(initialCount));

    // NVDA strips the TRAILING controlEnd commands too: the fields the range
    // ends inside are closed by the end-of-stack pass below, not by the body
    // loop, or every line of a list would say "out of list".
    std::size_t endFieldCount = 0;
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
        if (it->isCommand() && it->command->kind() == text::FieldCommand::Kind::ControlEnd) {
            ++endFieldCount;
        } else {
            break;
        }
    }
    if (endFieldCount > 0) {
        fields.erase(fields.end() - static_cast<std::ptrdiff_t>(endFieldCount), fields.end());
    }

    // #2199: how many fields the old and new stacks share. The uniqueID is
    // compared INSTEAD of the whole field when either side has one, because a
    // field carries its container's item count and comparing those maps on
    // every line of a long document is the cost this test exists to avoid.
    const std::vector<text::ControlField>& oldStack =
        state != nullptr ? state->controlFieldStack : newStack;
    std::size_t commonFieldCount = 0;
    if (state != nullptr) {
        const std::size_t limit = std::min(newStack.size(), oldStack.size());
        for (std::size_t count = 0; count < limit; ++count) {
            const std::string_view oldId = oldStack[count].uniqueId;
            const std::string_view newId = newStack[count].uniqueId;
            const bool byId = (!oldId.empty() || !newId.empty()) && oldId == newId;
            if (byId || newStack[count] == oldStack[count]) {
                ++commonFieldCount;
            } else {
                break;
            }
        }
    } else {
        // No cache: report everything, which is what alus+f and the tests ask.
        commonFieldCount = 0;
    }

    SpeechSequence sequence;
    // NVDA: the TextInfo is "blank" unless something below says otherwise.
    bool shouldConsiderBlank = true;

    // #2591: the fields the user has LEFT, outermost last. Not on a focus
    // change and not on a quick-nav jump -- hearing "out of list" while
    // tabbing makes navigation slower, not clearer.
    if (options.reason != OutputReason::Focus && options.reason != OutputReason::QuickNav &&
        state != nullptr) {
        bool endingBlock = false;
        for (std::size_t count = oldStack.size(); count > commonFieldCount; --count) {
            const std::size_t index = count - 1;
            const std::vector<text::ControlField> ancestorsOf(oldStack.begin(),
                                                              oldStack.begin() + static_cast<std::ptrdiff_t>(index));
            SpeechSequence fieldSpeech = getControlFieldSpeech(
                oldStack[index], ancestorsOf, ControlFieldType::EndRemovedFromStack, effective,
                options.reason, extraDetail, objectSettings);
            for (SpeechItem& item : fieldSpeech) {
                sequence.push_back(std::move(item));
            }
            if (!endingBlock && options.reason == OutputReason::SayAll) {
                endingBlock = oldStack[index].isBlock;
            }
        }
        if (endingBlock) {
            // Say-all must BREAK the utterance at the end of a block, or the
            // synthesizer runs the last line of a list into the next
            // paragraph as if it were one sentence.
            sequence.emplace_back(speech::endUtterance());
        }
    }

    if (options.prefixCommand != nullptr) {
        sequence.emplace_back(options.prefixCommand);
    }

    // The fields the user is ALREADY in. Only a single-line category says
    // anything here, and only when the user is not inspecting one character.
    if (!extraDetail) {
        for (std::size_t count = 0; count < commonFieldCount; ++count) {
            const std::vector<text::ControlField> ancestorsOf(newStack.begin(),
                                                              newStack.begin() + static_cast<std::ptrdiff_t>(count));
            SpeechSequence fieldSpeech =
                getControlFieldSpeech(newStack[count], ancestorsOf,
                                      ControlFieldType::StartInStack, effective, options.reason,
                                      extraDetail, objectSettings);
            if (!fieldSpeech.empty()) {
                for (SpeechItem& item : fieldSpeech) {
                    sequence.push_back(std::move(item));
                }
                shouldConsiderBlank = false;
            }
        }
    }

    // The fields the user has just ENTERED. `inClickable` is NVDA's guard
    // against announcing "clickable" once per nesting level: only the
    // OUTERMOST clickable is news.
    bool inClickable = false;
    for (std::size_t count = commonFieldCount; count < newStack.size(); ++count) {
        const text::ControlField& field = newStack[count];
        const std::vector<text::ControlField> ancestorsOf(newStack.begin(),
                                                          newStack.begin() + static_cast<std::ptrdiff_t>(count));
        if (!inClickable && effective.reportClickable && field.states.has(State::Clickable)) {
            const text::PresentationCategory category =
                field.getPresentationCategory(ancestorsOf, effective, extraDetail);
            if (category == text::PresentationCategory::Layout) {
                // Nothing else about this field will be said, so the one thing
                // worth knowing is that it can be clicked.
                sequence.emplace_back(std::string("clickable"));
                shouldConsiderBlank = false;
            }
            inClickable = true;
        }
        SpeechSequence fieldSpeech =
            getControlFieldSpeech(field, ancestorsOf, ControlFieldType::StartAddedToStack,
                                  effective, options.reason, extraDetail, objectSettings);
        if (!fieldSpeech.empty()) {
            for (SpeechItem& item : fieldSpeech) {
                sequence.push_back(std::move(item));
            }
            shouldConsiderBlank = false;
        }
        ++commonFieldCount;
    }

    text::Field* formatCache = state != nullptr ? &state->formatFieldAttributes : nullptr;
    SpeechSequence initialFieldSpeech =
        getFormatFieldSpeech(initialFormat, formatCache, effective, options.reason,
                             options.unit, extraDetail, /*initialFormat=*/true);
    for (SpeechItem& item : initialFieldSpeech) {
        sequence.push_back(std::move(item));
    }

    std::string language;
    if (presenter != nullptr && presenter->settings().autoLanguageSwitching) {
        language = std::string(initialFormat.get("language"));
        sequence.emplace_back(speech::langChange(language));
    }

    // NVDA's short circuit: ONE CHARACTER (or one word of one character) is
    // SPELLED, not spoken, and the properties go out first so the spelling can
    // be interrupted on its own.
    const bool wordOrCharacter =
        options.unit == text::TextUnit::Character || options.unit == text::TextUnit::Word;
    std::string firstText;
    if (!fields.empty() && fields.front().isText()) {
        // NVDA strips the text before the test, UNLESS it is all whitespace --
        // then the whitespace is the thing being inspected.
        const std::string& raw = *fields.front().text;
        const bool allSpace = !raw.empty() && std::all_of(raw.begin(), raw.end(), [](char c) {
                                  return std::isspace(static_cast<unsigned char>(c)) != 0;
                              });
        if (allSpace) {
            firstText = raw;
        } else {
            const std::size_t first = raw.find_first_not_of(" \t\r\n");
            const std::size_t last = raw.find_last_not_of(" \t\r\n");
            firstText =
                first == std::string::npos ? std::string() : raw.substr(first, last - first + 1);
        }
    }
    const bool onlyFieldEndsFollow =
        std::all_of(fields.begin() + (fields.empty() ? 0 : 1), fields.end(),
                    [](const text::TextOrField& item) {
                        return item.isCommand() && item.command->kind() ==
                                                       text::FieldCommand::Kind::ControlEnd;
                    });
    if (options.onlyInitialFields ||
        (wordOrCharacter && text::isSingleCharacter(firstText) && onlyFieldEndsFollow)) {
        if (options.reason != OutputReason::OnlyCache) {
            if (options.onlyInitialFields || !sequence.empty()) {
                result.sequences.push_back(std::move(sequence));
            }
            if (!options.onlyInitialFields) {
                if (presenter != nullptr) {
                    result.sequences.push_back(presenter->getSpellingSpeech(firstText, language));
                    if (options.reason == OutputReason::Caret &&
                        options.unit == text::TextUnit::Character &&
                        presenter->settings().delayedCharacterDescriptions) {
                        SpeechSequence description =
                            presenter->getSingleCharacterDescription(firstText, language);
                        if (!description.empty()) {
                            result.sequences.push_back(std::move(description));
                        }
                    }
                } else {
                    SpeechSequence plain;
                    plain.emplace_back(firstText);
                    result.sequences.push_back(std::move(plain));
                }
            }
        }
        if (state != nullptr) {
            state->controlFieldStack = newStack;
        }
        result.spokeText = false;
        return result;
    }

    // The body: the text, with the format changes between its pieces.
    SpeechSequence relative;
    bool inTextChunk = false;
    std::string allIndentation;
    bool indentationDone = false;
    std::string lastLanguage = language;

    for (const text::TextOrField& item : fields) {
        if (item.isText()) {
            std::string piece = *item.text;
            // U+FFFC is never spoken. It stands for an embedded object -- a
            // link inside a paragraph, an image -- and the object's own field
            // has already said what it is and what it says (`content`, set by
            // nav::DocumentTextRange). Spoken, it was "<U+FFFC>" after every
            // link in a navigation bar (FAZA10.md N114); it stays in the
            // STREAM only so braille's cell-to-offset map stays one to one.
            for (std::size_t at = piece.find("\xEF\xBF\xBC"); at != std::string::npos;
                 at = piece.find("\xEF\xBF\xBC", at)) {
                piece.erase(at, 3);
            }
            if (reportIndentation && !indentationDone) {
                const auto [indent, rest] = text::splitIndentation(piece);
                allIndentation += std::string(indent);
                piece = std::string(rest);
                if (!piece.empty()) {
                    indentationDone = true;
                }
            }
            if (piece.empty()) {
                continue;
            }
            if (inTextChunk && !relative.empty() && relative.back().isText()) {
                // NVDA appends to the previous text item rather than making a
                // new one, so the chunk separator is not inserted inside what
                // is really one run of text.
                relative.back() = SpeechItem(relative.back().text() + piece);
            } else {
                relative.emplace_back(std::move(piece));
                inTextChunk = true;
            }
            continue;
        }

        const text::FieldCommand& command = *item.command;
        SpeechSequence fieldSpeech;
        std::string newLanguage = lastLanguage;
        if (command.kind() == text::FieldCommand::Kind::FormatChange &&
            command.format() != nullptr) {
            fieldSpeech = getFormatFieldSpeech(*command.format(), formatCache, effective,
                                               options.reason, options.unit, extraDetail,
                                               /*initialFormat=*/false);
            if (!fieldSpeech.empty()) {
                inTextChunk = false;
            }
            if (presenter != nullptr && presenter->settings().autoLanguageSwitching) {
                newLanguage = std::string(command.format()->get("language"));
                if (newLanguage != lastLanguage) {
                    inTextChunk = false;
                }
            }
        } else {
            // A control field ALWAYS starts a new chunk, even when it has
            // nothing to say: the text before it and the text after it are
            // not one run.
            inTextChunk = false;
            if (command.kind() == text::FieldCommand::Kind::ControlStart &&
                command.control() != nullptr) {
                const text::ControlField& field = *command.control();
                if (!inClickable && effective.reportClickable &&
                    field.states.has(State::Clickable)) {
                    const text::PresentationCategory category =
                        field.getPresentationCategory(newStack, effective, extraDetail);
                    if (category == text::PresentationCategory::Layout) {
                        fieldSpeech.emplace_back(std::string("clickable"));
                    }
                    inClickable = true;
                }
                SpeechSequence relativeField =
                    getControlFieldSpeech(field, newStack, ControlFieldType::StartRelative,
                                          effective, options.reason, extraDetail,
                                          objectSettings);
                for (SpeechItem& relativeItem : relativeField) {
                    fieldSpeech.push_back(std::move(relativeItem));
                }
                newStack.push_back(field);
            } else if (command.kind() == text::FieldCommand::Kind::ControlEnd &&
                       !newStack.empty()) {
                // Leaving a control breaks a run of clickables.
                inClickable = false;
                const std::vector<text::ControlField> ancestorsOf(newStack.begin(),
                                                                  newStack.end() - 1);
                fieldSpeech = getControlFieldSpeech(
                    newStack.back(), ancestorsOf, ControlFieldType::EndRelative, effective,
                    options.reason, extraDetail, objectSettings);
                newStack.pop_back();
                commonFieldCount = std::min(commonFieldCount, newStack.size());
            }
        }
        if (!inTextChunk) {
            if (!fieldSpeech.empty()) {
                if (presenter != nullptr && presenter->settings().autoLanguageSwitching &&
                    !lastLanguage.empty()) {
                    // Fields are spoken in the DEFAULT language, whatever the
                    // text around them is in.
                    relative.emplace_back(speech::langChange(std::string{}));
                    lastLanguage.clear();
                }
                for (SpeechItem& field : fieldSpeech) {
                    relative.push_back(std::move(field));
                }
            }
            if (presenter != nullptr && presenter->settings().autoLanguageSwitching &&
                newLanguage != lastLanguage) {
                relative.emplace_back(speech::langChange(newLanguage));
                lastLanguage = newLanguage;
            }
        }
    }

    // The indentation goes in front of the text, and only when it CHANGED.
    if (reportIndentation && state != nullptr) {
        const bool blankLine =
            std::all_of(fields.begin(), fields.end(), [](const text::TextOrField& item) {
                return !item.isText() || isOnlyLineEnds(*item.text) || item.text->empty();
            });
        if ((!settings.ignoreBlankLinesForLineIndentation || !blankLine) &&
            allIndentation != state->indentation) {
            const std::string indentLocale =
                presenter != nullptr ? presenter->settings().language : std::string("en");
            SpeechSequence indentSpeech =
                getIndentationSpeech(allIndentation, settings, indentLocale, nullptr);
            if (!sequence.empty() && sequence.back().isCommand() &&
                sequence.back().is(speech::CommandType::LangChange)) {
                // The indentation must be spoken in the default language, but
                // the initial format field already selected another one: the
                // indentation goes BEFORE that command.
                SpeechItem langCommand = sequence.back();
                sequence.pop_back();
                for (SpeechItem& indentItem : indentSpeech) {
                    sequence.push_back(std::move(indentItem));
                }
                sequence.push_back(std::move(langCommand));
            } else {
                for (SpeechItem& indentItem : indentSpeech) {
                    sequence.push_back(std::move(indentItem));
                }
            }
            state->indentation = allIndentation;
        }
    }

    const bool relativeBlank =
        std::none_of(relative.begin(), relative.end(), [](const SpeechItem& item) {
            return item.isText() && !speech::isBlank(item.text());
        });
    if (!relativeBlank) {
        for (SpeechItem& item : relative) {
            sequence.push_back(std::move(item));
        }
        shouldConsiderBlank = false;
    }

    if (presenter != nullptr && presenter->settings().autoLanguageSwitching &&
        !lastLanguage.empty()) {
        sequence.emplace_back(speech::langChange(std::string{}));
    }

    // Finally the fields the range ends INSIDE and that the user was already
    // in: closing them is what makes a table cell's reading end cleanly. The
    // fields it merely passed through were closed by the body loop.
    if (!extraDetail) {
        const std::size_t closeFrom = std::min(newStack.size(), commonFieldCount);
        for (std::size_t count = closeFrom; count > 0; --count) {
            const std::size_t index = count - 1;
            const std::vector<text::ControlField> ancestorsOf(newStack.begin(),
                                                              newStack.begin() + static_cast<std::ptrdiff_t>(index));
            SpeechSequence fieldSpeech =
                getControlFieldSpeech(newStack[index], ancestorsOf,
                                      ControlFieldType::EndInStack, effective, options.reason,
                                      extraDetail, objectSettings);
            if (!fieldSpeech.empty()) {
                for (SpeechItem& item : fieldSpeech) {
                    sequence.push_back(std::move(item));
                }
                shouldConsiderBlank = false;
            }
        }
    }

    if (!options.suppressBlanks && options.reason != OutputReason::SayAll &&
        shouldConsiderBlank) {
        // Translators: spoken when the line is blank.
        sequence.emplace_back(std::string(_("blank")));
    }

    if (state != nullptr) {
        state->controlFieldStack = newStack;
    }

    if (options.reason == OutputReason::OnlyCache || sequence.empty()) {
        result.spokeText = false;
        return result;
    }
    result.sequences.push_back(std::move(sequence));
    result.spokeText = true;
    return result;
}

bool speakTextRange(speech::Presenter& presenter, const text::TextRange& range,
                    TextRangeSpeechState* state,
                    const text::FormatReportSettings& settings,
                    const TextRangeSpeechOptions& options, speech::Priority priority) {
    TextRangeSpeechResult result =
        getTextRangeSpeech(range, state, settings, options, &presenter);
    for (SpeechSequence& sequence : result.sequences) {
        if (sequence.empty()) {
            continue;
        }
        presenter.speak(std::move(sequence), std::nullopt, priority);
    }
    return result.spokeText;
}

void spellTextRange(speech::Presenter& presenter, const text::TextRange& range,
                    const text::FormatReportSettings& settings,
                    bool useCharacterDescriptions) {
    if (!presenter.settings().autoLanguageSwitching) {
        presenter.speakSpelling(range.text(), {}, useCharacterDescriptions);
        return;
    }
    // With automatic language switching on, the range is spelled piece by
    // piece in the language each piece is in -- which is the whole reason
    // `spellTextInfo` exists separately from `speakSpelling`.
    std::string currentLanguage;
    for (const text::TextOrField& item : range.getTextWithFields(settings)) {
        if (item.isText()) {
            presenter.speakSpelling(*item.text, currentLanguage, useCharacterDescriptions);
        } else if (item.command->kind() == text::FieldCommand::Kind::FormatChange &&
                   item.command->format() != nullptr) {
            currentLanguage = std::string(item.command->format()->get("language"));
        }
    }
}

// ---------------------------------------------------------------------------
// The selection
// ---------------------------------------------------------------------------

SpeechSequence getSelectionMessageSpeech(const std::string& messageWithPlaceholder,
                                         const std::string& text) {
    SpeechSequence sequence;
    const std::size_t placeholder = messageWithPlaceholder.find("%s");
    if (placeholder == std::string::npos) {
        ALUS_WARNING("selection message '{}' has no placeholder", messageWithPlaceholder);
        sequence.emplace_back(messageWithPlaceholder);
        sequence.emplace_back(text);
        return sequence;
    }
    const std::string prefix = messageWithPlaceholder.substr(0, placeholder);
    const std::string suffix = messageWithPlaceholder.substr(placeholder + 2);
    std::string middle = text;
    if (text.size() >= kMaxLengthForSelectionReporting) {
        // Translators: a large selection is reported by its size.
        const std::size_t characters = text::codepointCount(text);
        // Translators: a large selection is reported by its size; %s is the
        // number of characters.
        middle = i18n::substitute(P_("%s character", "%s characters", characters),
                                  std::to_string(characters));
    }
    sequence.emplace_back(prefix + middle + suffix);
    return sequence;
}

SpeechSequence getSelectionMessageSpeech(const std::string& messageWithPlaceholder,
                                         SpeechSequence text) {
    // The sequence form: the words go AROUND the sequence as separate items,
    // so they stay outside the commands inside it (a PitchCommand for a
    // capital must not cover the word "selected").
    const std::size_t placeholder = messageWithPlaceholder.find("%s");
    if (placeholder == std::string::npos) {
        ALUS_WARNING("selection message '{}' has no placeholder", messageWithPlaceholder);
        SpeechSequence sequence;
        sequence.emplace_back(messageWithPlaceholder);
        for (SpeechItem& item : text) {
            sequence.push_back(std::move(item));
        }
        return sequence;
    }
    const std::string prefix = messageWithPlaceholder.substr(0, placeholder);
    const std::string suffix = messageWithPlaceholder.substr(placeholder + 2);
    SpeechSequence sequence;
    if (!prefix.empty()) {
        sequence.emplace_back(prefix);
    }
    for (SpeechItem& item : text) {
        sequence.push_back(std::move(item));
    }
    if (!suffix.empty()) {
        sequence.emplace_back(suffix);
    }
    return sequence;
}

SpeechSequence getPreselectedTextSpeech(const std::string& text) {
    // Translators: text that was ALREADY selected when the control got the
    // focus. "selected" comes FIRST on purpose: the user may be about to
    // overwrite it.
    return getSelectionMessageSpeech(_("selected %s"), text);
}

void speakSelectionChange(speech::Presenter& presenter, const text::TextRange& oldRange,
                          const text::TextRange& newRange, bool speakSelected,
                          bool speakUnselected, bool generalize,
                          speech::Priority priority) {
    // Ported from `speakSelectionChange`. The four endpoint comparisons and
    // the branches on them are the anchor logic §8.2 warns about: get them
    // wrong and the selection "jumps" in the announcement.
    std::vector<std::string> selectedText;
    std::vector<std::string> unselectedText;
    if (newRange.isCollapsed() && oldRange.isCollapsed()) {
        return;
    }
    const int startToStart =
        newRange.compareEndPoints(oldRange, text::EndPointPair::StartToStart);
    const int startToEnd = newRange.compareEndPoints(oldRange, text::EndPointPair::StartToEnd);
    const int endToStart = newRange.compareEndPoints(oldRange, text::EndPointPair::EndToStart);
    const int endToEnd = newRange.compareEndPoints(oldRange, text::EndPointPair::EndToEnd);

    if (speakSelected && oldRange.isCollapsed()) {
        selectedText.push_back(newRange.text());
    } else if (speakUnselected && newRange.isCollapsed()) {
        unselectedText.push_back(oldRange.text());
    } else if (startToEnd > 0 || endToStart < 0) {
        // The two selections do not overlap at all: the whole of one was
        // unselected and the whole of the other selected.
        if (speakSelected && !newRange.isCollapsed()) {
            selectedText.push_back(newRange.text());
        }
        if (speakUnselected && !oldRange.isCollapsed()) {
            unselectedText.push_back(oldRange.text());
        }
    } else {
        if (speakSelected && startToStart < 0 && !newRange.isCollapsed()) {
            text::TextRangePtr piece = newRange.copy();
            piece->setEndPoint(oldRange, text::EndPointPair::EndToStart);
            selectedText.push_back(piece->text());
        }
        if (speakSelected && endToEnd > 0 && !newRange.isCollapsed()) {
            text::TextRangePtr piece = newRange.copy();
            piece->setEndPoint(oldRange, text::EndPointPair::StartToEnd);
            selectedText.push_back(piece->text());
        }
        if (startToStart > 0 && !oldRange.isCollapsed()) {
            text::TextRangePtr piece = oldRange.copy();
            piece->setEndPoint(newRange, text::EndPointPair::EndToStart);
            unselectedText.push_back(piece->text());
        }
        if (endToEnd < 0 && !oldRange.isCollapsed()) {
            text::TextRangePtr piece = oldRange.copy();
            piece->setEndPoint(newRange, text::EndPointPair::StartToEnd);
            unselectedText.push_back(piece->text());
        }
    }

    const auto speakPiece = [&](const std::string& message, const std::string& piece) {
        if (text::isSingleCharacter(piece)) {
            // A single character is SPELLED, so that "a" is not read as the
            // word. `endsUtterance` false, or the message would be cut off
            // from the character it belongs to.
            SpeechSequence spelled =
                presenter.getSpellingSpeech(piece, {}, false, /*endsUtterance=*/false,
                                            /*useCharacterMode=*/false);
            presenter.speak(getSelectionMessageSpeech(message, std::move(spelled)),
                            std::nullopt, priority);
            return;
        }
        presenter.speak(getSelectionMessageSpeech(message, piece), std::nullopt, priority);
    };

    if (speakSelected) {
        if (!generalize) {
            for (const std::string& piece : selectedText) {
                // Translators: what has just been selected: "hello world selected".
                speakPiece(_("%s selected"), piece);
            }
        } else if (!selectedText.empty()) {
            speakPiece("%s selected", newRange.text());
        }
    }
    if (speakUnselected) {
        if (!generalize) {
            for (const std::string& piece : unselectedText) {
                // Translators: what has just been unselected.
                speakPiece(_("%s unselected"), piece);
            }
        } else if (!unselectedText.empty()) {
            if (!newRange.isCollapsed()) {
                // Translators: the previous selection was replaced by a new one.
                speakPiece(_("%s selected instead"), newRange.text());
            } else {
                // Translators: the selection was removed.
                presenter.speakMessage(_("selection removed"), priority);
            }
        }
    }
}

}  // namespace alus::a11y
