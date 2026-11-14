// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// What a piece of text is: its formatting, and the controls that contain it.
//
// Port of NVDA's `textInfos.Field`, `FormatField`, `ControlField` and
// `FieldCommand` (architecture.md §7.1), including
// `ControlField.getPresentationCategory`.
//
// WHY A STRING MAP AND NOT A STRUCT. NVDA's fields are dicts, and two things
// depend on that being kept:
//
//  1. THE FORMAT CACHE IS A DICTIONARY DIFF. `getFormatFieldSpeech` is given
//     the new field and the one that was last spoken, and reports only the
//     keys that differ (`speech.py:2620`). With a struct, every new attribute
//     a toolkit invents needs a member, a comparison and a line in the diff;
//     with a map, an unknown key compares and caches correctly and is simply
//     not spoken.
//  2. THE KEYS ARE AN INTERFACE. They are NVDA's own names
//     ("font-size", "invalid-spelling", "text-position"), because the format
//     reporting settings, the braille formatting code (phase 7) and the
//     document formatting dialog (phase 9) all address them by name, and an
//     app module (phase 8) adds its own.
//
// Values are strings for the same reason -- "bold" is `True` in one toolkit,
// `"700"` in another and `1` in a third, and the normaliser (§7.2's
// "+ normalizator") is the one place that decides. The typed getters below
// are how a reader asks without repeating that parsing.
#pragma once

#include "alus/a11y/role.hpp"
#include "alus/a11y/state.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::text {

// NVDA: `textInfos.Field`, a dict.
class Field {
public:
    using Values = std::map<std::string, std::string, std::less<>>;

    Field() = default;

    bool contains(std::string_view key) const { return values_.find(key) != values_.end(); }
    // The raw value, or an empty view when the key is absent. An ABSENT key
    // and an EMPTY value are different things to the diff, so `contains` is
    // what a caller asks when that difference matters.
    std::string_view get(std::string_view key) const;
    std::optional<bool> getBool(std::string_view key) const;
    std::optional<int> getInt(std::string_view key) const;
    std::optional<double> getDouble(std::string_view key) const;

    void set(std::string key, std::string value);
    void set(std::string key, bool value);
    void set(std::string key, int value);
    // A STRING LITERAL IS A STRING, not a bool. Without this overload
    // `set("content", "Downloads")` picks the bool one -- a pointer-to-bool
    // standard conversion beats the user-defined conversion to std::string --
    // and silently stores "1". The compiler says nothing, and the key reads
    // back as a true boolean.
    void set(std::string key, const char* value) {
        set(std::move(key), std::string{value});
    }
    void erase(std::string_view key);

    // NVDA: `field.update(other)`.
    void update(const Field& other);

    const Values& values() const noexcept { return values_; }
    bool empty() const noexcept { return values_.empty(); }
    std::size_t size() const noexcept { return values_.size(); }

    bool operator==(const Field& other) const noexcept { return values_ == other.values_; }

private:
    Values values_;
};

// NVDA: `textInfos.FormatField`. Font, colour, revision, spelling errors,
// line number, language.
class FormatField : public Field {
public:
    using Field::Field;
};

// NVDA: `ControlField.PRESCAT_*`. What a control inside text is worth saying
// as, and the reason a GTK document does not announce six nested boxes.
enum class PresentationCategory {
    // NVDA's PRESCAT_LAYOUT, which is `None`: say nothing at all.
    Layout,
    // A single-line item: a link, a heading, a button.
    SingleLine,
    // A marker: a separator, a footnote.
    Marker,
    // A container, usually multi-line.
    Container,
    // A section of a larger container, next to similar ones: a table cell.
    Cell,
};

const char* presentationCategoryName(PresentationCategory category);

// What the user asked to have reported, as `getPresentationCategory` needs it.
// NVDA reads `config.conf["documentFormatting"]` from inside that method;
// passing it in is what makes the method a pure function of its inputs, and
// therefore testable one rule at a time.
struct FormatReportSettings {
    bool includeLayoutTables = false;
    bool reportTables = true;
    bool reportTableHeaders = true;
    bool reportTableCellCoords = true;
    bool reportLinks = true;
    bool reportGraphics = true;
    bool reportHeadings = true;
    bool reportBlockQuotes = true;
    bool reportGroupings = true;
    bool reportLists = true;
    bool reportArticles = false;
    bool reportHighlight = true;
    bool reportFrames = true;
    bool reportRevisions = true;
    bool reportLandmarks = true;
    bool reportFigures = true;
    bool reportClickable = true;
    bool reportComments = true;
    bool reportBookmarks = true;

    // The character/word-level detail that `extraDetail` turns on.
    bool detectFormatAfterCursor = false;

    // Font and colour.
    bool reportFontName = false;
    bool reportFontSize = false;
    bool reportFontAttributes = false;
    bool reportSuperscriptsAndSubscripts = false;
    bool reportColor = false;
    bool reportEmphasis = false;
    bool reportStyle = false;
    bool reportAlignment = false;
    bool reportLineSpacing = false;
    bool reportParagraphIndentation = false;
    bool reportBorderStyle = false;
    bool reportBorderColor = false;
    bool reportLineNumber = false;
    bool reportPage = true;
    bool reportSpellingErrors = true;
    bool reportGrammarErrors = true;
    // NVDA: reportLineIndentation. Off, speech, tones, or both.
    enum class LineIndentation { Off, Speech, Tones, SpeechAndTones };
    LineIndentation reportLineIndentation = LineIndentation::Off;
    bool ignoreBlankLinesForLineIndentation = false;
    int indentToneDuration = 40;
};

// NVDA: `textInfos.ControlField`. A control that encompasses text.
//
// The typed members are the ones `getPresentationCategory` decides on; every
// other attribute a document layer wants to carry goes in the inherited map,
// which is also what the field comparison uses.
class ControlField : public Field {
public:
    using Field::Field;

    a11y::Role role = a11y::Role::Unknown;
    a11y::StateSet states;
    std::string name;
    std::string landmark;
    // aria-roledescription. When set it REPLACES the role in speech, and it
    // also changes the presentation category (NVDA's last rule).
    std::string roleText;
    // Whether the control occupies its own block, which decides whether a
    // roleText field reads as a container or as a single line.
    bool isBlock = false;
    // A layout table, as the document layer judged it: a table used for
    // positioning, with no headers and no meaning.
    bool tableLayout = false;
    bool isHidden = false;
    // NVDA's `uniqueID`: compared INSTEAD of the whole field when present,
    // which is what keeps a long document's field stack cheap to diff (#2199).
    std::string uniqueId;

    // NVDA: `ControlField.getPresentationCategory`. `ancestors` is the field
    // stack above this one, outermost first.
    PresentationCategory getPresentationCategory(const std::vector<ControlField>& ancestors,
                                                 const FormatReportSettings& settings,
                                                 bool extraDetail = false) const;

    bool operator==(const ControlField& other) const noexcept;
};

// NVDA: `textInfos.FieldCommand`. A marker in a sequence of text and fields.
class FieldCommand {
public:
    enum class Kind {
        // The start of a ControlField.
        ControlStart,
        // The end of a ControlField; the field may be absent.
        ControlEnd,
        // A FormatField change.
        FormatChange,
    };

    static FieldCommand controlStart(ControlField field);
    static FieldCommand controlEnd();
    static FieldCommand formatChange(FormatField field);

    Kind kind() const noexcept { return kind_; }
    // Null for a ControlEnd, and for a malformed command.
    const ControlField* control() const noexcept { return control_.get(); }
    const FormatField* format() const noexcept { return format_.get(); }

    std::string repr() const;

private:
    FieldCommand() = default;

    Kind kind_ = Kind::ControlEnd;
    std::shared_ptr<ControlField> control_;
    std::shared_ptr<FormatField> format_;
};

// NVDA: the list `getTextWithFields` returns -- text strings interspersed with
// field commands. A variant would be tidier; a struct with one of the two set
// is what keeps the port readable against NVDA's `isinstance(x, str)` tests.
struct TextOrField {
    // Set when this item is text.
    std::optional<std::string> text;
    // Set when this item is a command.
    std::optional<FieldCommand> command;

    bool isText() const noexcept { return text.has_value(); }
    bool isCommand() const noexcept { return command.has_value(); }

    static TextOrField ofText(std::string value) { return TextOrField{std::move(value), {}}; }
    static TextOrField ofCommand(FieldCommand value) {
        return TextOrField{{}, std::move(value)};
    }
};

using TextWithFields = std::vector<TextOrField>;

}  // namespace alus::text
