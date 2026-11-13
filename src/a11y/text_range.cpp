// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/a11y/text_range.hpp.

#include "alus/a11y/text_range.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace alus::a11y {
namespace {

// ---------------------------------------------------------------------------
// The AT-SPI text attribute normaliser
// ---------------------------------------------------------------------------
//
// THE TABLE BELOW IS THE INTERESTING PART OF THIS FILE. AT-SPI's attribute
// names come from ATK, which took them from Pango, which took them from CSS;
// NVDA's format field keys come from IAccessible2 and from Microsoft Word. The
// two sets overlap in meaning and agree on almost no spelling, and every row
// here is a decision about what a toolkit meant.
//
// The AT-SPI side is specified in `atk/atkobject.h`
// (`AtkTextAttribute`) and in the ATK documentation; the values are STRINGS,
// always, even the numeric ones.

std::optional<double> parseNumber(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        return std::stod(std::string(text));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool isTrue(std::string_view value) {
    return value == "true" || value == "True" || value == "1";
}

// AT-SPI colours are "r,g,b" with components 0-65535 in GTK and 0-255 in
// others. The presentation layer wants something it can say, and colour NAMING
// is phase 9's job (the colour table is localised); until then the normalised
// form is "#rrggbb", which is at least unambiguous and comparable -- the
// format cache diffs these strings, so an unstable spelling would report a
// colour change on every line.
std::string normaliseColour(std::string_view value) {
    int components[3] = {0, 0, 0};
    std::size_t index = 0;
    std::size_t start = 0;
    bool wide = false;
    while (index < 3 && start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::string_view part =
            value.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                : comma - start);
        const std::optional<double> number = parseNumber(part);
        if (!number.has_value()) {
            return std::string(value);
        }
        if (*number > 255.0) {
            wide = true;
        }
        components[index] = static_cast<int>(*number);
        ++index;
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    if (index < 3) {
        return std::string(value);
    }
    char buffer[8] = {};
    const auto scale = [wide](int component) {
        // GTK reports 0-65535 (Pango's scale), Qt and Chromium 0-255.
        return wide ? component / 257 : component;
    };
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", scale(components[0]) & 0xFF,
                  scale(components[1]) & 0xFF, scale(components[2]) & 0xFF);
    return std::string(buffer);
}

}  // namespace

text::FormatField normaliseTextAttributes(const Attributes& attributes) {
    text::FormatField field;
    const auto value = [&attributes](std::string_view key) -> std::string_view {
        const auto it = attributes.find(key);
        return it == attributes.end() ? std::string_view{} : std::string_view{it->second};
    };

    // ---- font ----
    if (const std::string_view family = value("family-name"); !family.empty()) {
        field.set("font-name", std::string(family));
        field.set("font-family", std::string(family));
    }
    if (const std::string_view size = value("size"); !size.empty()) {
        if (const std::optional<double> points = parseNumber(size)) {
            // NVDA says "12 pt"; the unit belongs in the value because the
            // format cache compares strings and a bare number from one toolkit
            // and "12pt" from another would read as a font change.
            field.set("font-size", std::to_string(static_cast<int>(std::lround(*points))) +
                                       " pt");
        } else {
            field.set("font-size", std::string(size));
        }
    }
    // Pango weights: 400 is normal, 700 bold. Toolkits also send "bold".
    if (const std::string_view weight = value("weight"); !weight.empty()) {
        if (const std::optional<double> number = parseNumber(weight)) {
            field.set("bold", *number >= 600.0);
        } else {
            field.set("bold", weight == "bold");
        }
    }
    if (const std::string_view style = value("style"); !style.empty()) {
        field.set("italic", style == "italic" || style == "oblique");
    }
    if (const std::string_view underline = value("underline"); !underline.empty()) {
        // ATK: "none", "single", "double", "low", "error". "error" is how
        // LibreOffice marks a spelling error through the underline attribute.
        if (underline == "error") {
            field.set("invalid-spelling", true);
        } else {
            field.set("underline", underline != "none");
        }
    }
    if (const std::string_view strike = value("strikethrough"); !strike.empty()) {
        field.set("strikethrough", isTrue(strike));
    }
    // ATK's `invisible`: text that is there and not drawn. NVDA reports it as
    // "hidden", and it matters -- hidden text in a document is text the user
    // would otherwise never know about. Found by measurement: GTK sets this
    // attribute on every run (tools/phase5-text-check.sh step 3).
    if (const std::string_view invisible = value("invisible"); !invisible.empty()) {
        field.set("hidden", isTrue(invisible));
    }
    // Superscript and subscript arrive two ways: as the ATK text-position
    // attribute, and as a non-zero `rise`. Both are read, because GTK sets the
    // first and Pango-based renderers the second.
    if (const std::string_view position = value("text-position"); !position.empty()) {
        if (position == "super") {
            field.set("text-position", std::string("superscript"));
        } else if (position == "sub") {
            field.set("text-position", std::string("subscript"));
        } else {
            field.set("text-position", std::string("baseline"));
        }
    } else if (const std::optional<double> rise = parseNumber(value("rise"))) {
        if (*rise > 0.0) {
            field.set("text-position", std::string("superscript"));
        } else if (*rise < 0.0) {
            field.set("text-position", std::string("subscript"));
        }
    }

    // ---- colour ----
    if (const std::string_view foreground = value("fg-color"); !foreground.empty()) {
        field.set("color", normaliseColour(foreground));
    }
    if (const std::string_view background = value("bg-color"); !background.empty()) {
        field.set("background-color", normaliseColour(background));
    }

    // ---- spelling and grammar ----
    // ATK's `invalid` attribute, which is also what `aria-invalid` becomes.
    if (const std::string_view invalid = value("invalid"); !invalid.empty()) {
        if (invalid == "spelling") {
            field.set("invalid-spelling", true);
        } else if (invalid == "grammar") {
            field.set("invalid-grammar", true);
        }
    }

    // ---- language ----
    if (const std::string_view language = value("language"); !language.empty()) {
        field.set("language", std::string(language));
    }

    // ---- paragraph ----
    if (const std::string_view justification = value("justification");
        !justification.empty()) {
        // ATK: "left", "right", "center", "fill". NVDA's own words for the
        // last one are "justify".
        field.set("text-align",
                  justification == "fill" ? std::string("justify")
                                          : std::string(justification));
    }
    if (const std::string_view vertical = value("vertical-align"); !vertical.empty()) {
        field.set("vertical-align", std::string(vertical));
    }
    if (const std::optional<double> indent = parseNumber(value("indent"))) {
        // ATK's `indent` is the FIRST LINE indent in pixels; a negative one is
        // a hanging indent, which is a different thing to say.
        if (*indent > 0.0) {
            field.set("first-line-indent", std::to_string(static_cast<int>(*indent)));
        } else if (*indent < 0.0) {
            field.set("hanging-indent", std::to_string(static_cast<int>(-*indent)));
        }
    }
    if (const std::optional<double> left = parseNumber(value("left-margin"))) {
        if (*left > 0.0) {
            field.set("left-indent", std::to_string(static_cast<int>(*left)));
        }
    }
    if (const std::optional<double> right = parseNumber(value("right-margin"))) {
        if (*right > 0.0) {
            field.set("right-indent", std::to_string(static_cast<int>(*right)));
        }
    }
    if (const std::string_view spacing = value("line-height"); !spacing.empty()) {
        field.set("line-spacing", std::string(spacing));
    }

    // ---- style ----
    // LibreOffice puts the paragraph style name here; GTK sends nothing.
    if (const std::string_view style = value("paragraph-style"); !style.empty()) {
        field.set("style", std::string(style));
    }

    return field;
}

// ---------------------------------------------------------------------------
// Object::makeTextRange -- defined here, beside the class it builds, so that
// object.hpp does not have to know what an ObjectTextRange is.
// ---------------------------------------------------------------------------

text::TextRangePtr Object::makeTextRange(text::Position position) const {
    return ObjectTextRange::make(
        std::const_pointer_cast<Object>(shared_from_this()), position);
}

text::TextRangePtr Object::makeTextRange(int start, int end) const {
    return ObjectTextRange::make(std::const_pointer_cast<Object>(shared_from_this()), start,
                                 end);
}

// ---------------------------------------------------------------------------
// ObjectTextRange
// ---------------------------------------------------------------------------

ObjectTextRange::ObjectTextRange(ObjectPtr object) : object_(std::move(object)) {}

ObjectTextRange::~ObjectTextRange() = default;

Backend& ObjectTextRange::backend() const {
    return object_->backend();
}

std::unique_ptr<ObjectTextRange> ObjectTextRange::make(ObjectPtr object,
                                                       text::Position position) {
    if (!object || !object->hasInterface(Interface::Text)) {
        return nullptr;
    }
    std::unique_ptr<ObjectTextRange> range(new ObjectTextRange(std::move(object)));
    if (!range->initialize(position)) {
        // NVDA raises here for POSITION_CARET on an object with no caret. The
        // range is still usable, collapsed at 0, and `review::objectPosition`
        // depends on that: it asks for the caret and falls back to the first
        // position rather than giving up on the object.
        if (position == text::Position::Caret || position == text::Position::Selection) {
            return range;
        }
        return nullptr;
    }
    return range;
}

std::unique_ptr<ObjectTextRange> ObjectTextRange::make(ObjectPtr object, int start, int end) {
    if (!object || !object->hasInterface(Interface::Text)) {
        return nullptr;
    }
    std::unique_ptr<ObjectTextRange> range(new ObjectTextRange(std::move(object)));
    range->setOffsets(start, end);
    return range;
}

std::unique_ptr<ObjectTextRange> ObjectTextRange::makeAtPoint(ObjectPtr object, int x, int y) {
    if (!object || !object->hasInterface(Interface::Text)) {
        return nullptr;
    }
    std::unique_ptr<ObjectTextRange> range(new ObjectTextRange(std::move(object)));
    const int offset = range->backend().offsetAtPoint(range->object_->ref(), x, y);
    if (offset < 0) {
        return nullptr;
    }
    range->setOffsets(offset, offset);
    return range;
}

std::unique_ptr<text::TextRange> ObjectTextRange::copy() const {
    std::unique_ptr<ObjectTextRange> range(new ObjectTextRange(object_));
    range->copyOffsetsFrom(*this);
    return range;
}

const void* ObjectTextRange::bookmarkKind() const {
    // The accessible's own reference: identity for the platform, and therefore
    // identity for a bookmark. Two ranges over the same object share it; two
    // ranges over different objects never do, which is what makes
    // `nav::EditableText`'s bookmark comparison mean "the caret moved" rather
    // than "the focus moved".
    return reinterpret_cast<const void*>(object_->ref());
}

int ObjectTextRange::storyLength() const {
    return object_->characterCount();
}

std::string ObjectTextRange::storyText() const {
    return backend().text(object_->ref(), 0, -1);
}

std::string ObjectTextRange::textRange(int start, int end) const {
    if (end <= start) {
        return {};
    }
    return backend().text(object_->ref(), start, end);
}

std::optional<int> ObjectTextRange::caretOffset() const {
    const int offset = object_->caretOffset();
    if (offset < 0) {
        return std::nullopt;
    }
    return offset;
}

bool ObjectTextRange::setCaretOffset(int offset) {
    if (!object_->isAlive()) {
        return false;
    }
    return backend().setCaretOffset(object_->ref(), offset);
}

std::optional<text::OffsetsTextRange::Offsets> ObjectTextRange::selectionOffsets() const {
    const std::optional<TextSegment> segment = backend().selection(object_->ref(), 0);
    if (!segment.has_value()) {
        return std::nullopt;
    }
    return Offsets{segment->start, segment->end};
}

bool ObjectTextRange::setSelectionOffsets(int start, int end) {
    if (!object_->isAlive()) {
        return false;
    }
    return backend().setSelection(object_->ref(), start, end);
}

std::optional<text::OffsetsTextRange::Offsets> ObjectTextRange::lineOffsets(int offset) const {
    // THE PLATFORM ANSWERS THIS ONE. See the header: only the toolkit knows
    // where it wrapped the text, and there is no character in the string that
    // says so.
    const std::optional<TextSegment> segment =
        backend().stringAtOffset(object_->ref(), offset, TextGranularity::Line);
    if (!segment.has_value()) {
        return std::nullopt;
    }
    return Offsets{segment->start, segment->end};
}

std::optional<text::OffsetsTextRange::Offsets> ObjectTextRange::paragraphOffsets(
    int offset) const {
    const std::optional<TextSegment> segment =
        backend().stringAtOffset(object_->ref(), offset, TextGranularity::Paragraph);
    if (!segment.has_value()) {
        return std::nullopt;
    }
    return Offsets{segment->start, segment->end};
}

std::optional<int> ObjectTextRange::lineNumberFromOffset(int offset) const {
    if (offset <= 0) {
        return 0;
    }
    // One platform call, and only when the user asked for line numbers --
    // which is why `formatFieldAndOffsets` asks for it behind that setting.
    const std::string before = textRange(0, offset);
    return static_cast<int>(std::count(before.begin(), before.end(), '\n'));
}

std::optional<int> ObjectTextRange::lineCount() const {
    const std::string whole = storyText();
    if (whole.empty()) {
        return 1;
    }
    int lines = static_cast<int>(std::count(whole.begin(), whole.end(), '\n'));
    // A trailing newline does not open a line the user can stand on.
    if (whole.back() != '\n') {
        ++lines;
    }
    return std::max(lines, 1);
}

std::optional<core::Rect> ObjectTextRange::boundingRectFromOffset(int offset) const {
    const core::Rect rect = backend().characterExtents(object_->ref(), offset);
    if (rect.empty()) {
        return std::nullopt;
    }
    return rect;
}

std::optional<int> ObjectTextRange::offsetFromPoint(int x, int y) const {
    const int offset = backend().offsetAtPoint(object_->ref(), x, y);
    if (offset < 0) {
        return std::nullopt;
    }
    return offset;
}

std::optional<core::Rect> ObjectTextRange::objectLocation() const {
    const core::Rect& location = object_->location();
    if (location.empty()) {
        return std::nullopt;
    }
    return location;
}

std::pair<text::FormatField, text::OffsetsTextRange::Offsets>
ObjectTextRange::formatFieldAndOffsets(int offset, const text::FormatReportSettings& settings,
                                       bool calculateOffsets) const {
    // `includeDefaults` is true: a font name that was never overridden is
    // still the font the text is in, and NVDA reports it.
    const AttributeRun run = backend().attributeRun(object_->ref(), offset, true);
    text::FormatField field = normaliseTextAttributes(run.attributes);

    if (settings.reportLineNumber) {
        if (const std::optional<int> number = lineNumberFromOffset(offset)) {
            field.set("line-number", *number + 1);
        }
    }

    Offsets bounds{run.start, run.end};
    if (bounds.end <= bounds.start) {
        // A toolkit that answers with an empty run would make
        // `getTextWithFields` step one character at a time over the whole range.
        // The line is the honest bound in that case.
        if (const std::optional<Offsets> line = lineOffsets(offset)) {
            bounds = *line;
        } else {
            bounds = Offsets{offset, storyLength()};
        }
    }
    if (!calculateOffsets) {
        bounds = Offsets{startOffset(), endOffset()};
    }
    return {std::move(field), bounds};
}

std::string ObjectTextRange::textLocale() const {
    // The language of the text itself, where the toolkit says so; otherwise
    // the application's own, which is all ALUS can know.
    const AttributeRun run = backend().attributeRun(object_->ref(), startOffset(), true);
    const auto it = run.attributes.find("language");
    if (it != run.attributes.end() && !it->second.empty()) {
        return it->second;
    }
    return {};
}

ObjectPtr ObjectTextRange::objectAtOffset(int offset) const {
    const int index = backend().linkIndexAtOffset(object_->ref(), offset);
    if (index < 0) {
        return object_;
    }
    const std::optional<LinkInfo> link = backend().hyperlink(object_->ref(), index);
    if (!link.has_value() || link->object == 0) {
        return object_;
    }
    // adoptObject takes the reference the backend handed over, which is the
    // rule N44 made explicit: a reference inside a returned struct is one
    // somebody has to give back.
    return object_->registry().adoptObject(link->object);
}

std::optional<text::OffsetsTextRange::Offsets> ObjectTextRange::offsetsForObject(
    const Object& other) const {
    if (other.sameAs(*object_)) {
        return Offsets{0, storyLength()};
    }
    const int count = backend().hyperlinkCount(object_->ref());
    for (int index = 0; index < count; ++index) {
        const std::optional<LinkInfo> link = backend().hyperlink(object_->ref(), index);
        if (!link.has_value()) {
            continue;
        }
        const Ref embedded = link->object;
        if (embedded != 0) {
            backend().release(embedded);
        }
        if (embedded == other.ref()) {
            return Offsets{link->startIndex, link->endIndex};
        }
    }
    return std::nullopt;
}

}  // namespace alus::a11y
