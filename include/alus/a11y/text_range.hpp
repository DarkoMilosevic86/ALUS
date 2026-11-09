// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `ObjectTextRange`: a text range over one accessible's `AtspiText`.
//
// This is the subclass 01-ARHITEKTURA.md §7.2 specifies, hook for hook:
//
//     _getStoryText      -> Text::get_text(0, get_character_count())
//     _getStoryLength    -> Text::get_character_count()
//     _getTextRange      -> Text::get_text(s, e)
//     _getCaretOffset    -> Text::get_caret_offset()
//     _setCaretOffset    -> Text::set_caret_offset()
//     _getSelectionOffsets -> Text::get_selection(0)
//     _setSelectionOffsets -> Text::set_selection(0, s, e)
//     _getLineOffsets    -> Text::get_string_at_offset(o, GRANULARITY_LINE)
//     _getWordOffsets    -> GRANULARITY_WORD, or ICU segmentation
//     _getSentenceOffsets-> GRANULARITY_SENTENCE, or ICU
//     _getParagraphOffsets -> GRANULARITY_PARAGRAPH
//     _getFormatFieldAndOffsets -> Text::get_attribute_run(offset) + normaliser
//     _getOffsetFromPoint -> Text::get_offset_at_point
//     _getBoundingRectFromOffset -> Text::get_character_extents / range_extents
//     _getNVDAObjectFromOffset -> Hypertext::get_link_index -> get_link ->
//                                 Hyperlink::get_object
//     _getOffsetsFromNVDAObject -> Hyperlink::get_start_index / get_end_index
//
// WHERE IT DEVIATES, and why (both measured, both in FAZA5.md):
//
//   * WORDS DO NOT COME FROM THE PLATFORM. `GRANULARITY_WORD` gives UAX#29
//     word boundaries, which stop before the space after a word; NVDA's word
//     unit INCLUDES that space, or moving by word takes two presses per word.
//     No toolkit offers NVDA's unit, so the segmenter computes it (§7.2's "or
//     ICU segmentation", chosen rather than offered).
//   * SENTENCES DO NOT COME FROM THE PLATFORM EITHER, for the same kind of
//     reason: GTK answers `GRANULARITY_SENTENCE` with the whole line, and
//     LibreOffice with its own idea of a sentence. ICU over the containing
//     paragraph is one answer everywhere.
//
// LINES AND PARAGRAPHS DO come from the platform, because there the toolkit
// knows something ALUS cannot: where the text was WRAPPED. A line of a wrapped
// paragraph has no character in it that says so.
//
// ONE RULE ABOUT LIFETIME, learned under valgrind (FAZA5.md N49): THIS RANGE
// HOLDS ITS OBJECT ALIVE. So nothing that is OWNED BY an object may store one
// -- a behaviour that kept its last selection as a range made a reference ring
// through itself, and every editable field the user focused leaked itself and
// its whole cached subtree. Store a `text::Bookmark` instead and rebuild the
// range from it; that is what `nav::EditableTextBehavior` does.
//
// THREADING: main thread only, like everything that touches an a11y::Object.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/offsets_range.hpp"

#include <memory>
#include <optional>
#include <string>

namespace alus::a11y {

// AT-SPI text attributes -> NVDA format field keys. The plan's
// "+ normalizator" (§7.2), and the one place that knows how each toolkit
// spells a thing.
//
// `defaults` are the object's default attributes, merged under the run's own.
text::FormatField normaliseTextAttributes(const Attributes& attributes);

class ObjectTextRange final : public text::OffsetsTextRange {
public:
    // Null when the object has no Text interface at all -- which is not an
    // error and happens constantly (a button has no text range).
    static std::unique_ptr<ObjectTextRange> make(ObjectPtr object, text::Position position);
    // At an explicit byte range, for a bookmark or a known offset.
    static std::unique_ptr<ObjectTextRange> make(ObjectPtr object, int start, int end);
    // NVDA: `makeTextInfo(Point)`. The offset under a screen point, which is
    // what mouse tracking and `nav::ScreenExplorer` need.
    static std::unique_ptr<ObjectTextRange> makeAtPoint(ObjectPtr object, int x, int y);

    ~ObjectTextRange() override;

    std::string_view rangeKind() const override { return "atspi"; }
    std::unique_ptr<text::TextRange> copy() const override;

    const ObjectPtr& object() const noexcept { return object_; }

    // NVDA: `_getNVDAObjectFromOffset`. The embedded object at an offset (a
    // link, an image in a document), or the range's own object.
    ObjectPtr objectAtOffset(int offset) const;
    // NVDA: `_getOffsetsFromNVDAObject`. Where an embedded object sits in this
    // text, or unset when it is not in it.
    std::optional<Offsets> offsetsForObject(const Object& other) const;

protected:
    const void* bookmarkKind() const override;

    int storyLength() const override;
    std::string storyText() const override;
    std::string textRange(int start, int end) const override;

    std::optional<int> caretOffset() const override;
    bool setCaretOffset(int offset) override;
    std::optional<Offsets> selectionOffsets() const override;
    bool setSelectionOffsets(int start, int end) override;

    std::optional<Offsets> lineOffsets(int offset) const override;
    std::optional<Offsets> paragraphOffsets(int offset) const override;

    std::optional<int> lineNumberFromOffset(int offset) const override;
    std::optional<int> lineCount() const override;

    std::optional<core::Rect> boundingRectFromOffset(int offset) const override;
    std::optional<int> offsetFromPoint(int x, int y) const override;
    std::optional<core::Rect> objectLocation() const override;

    std::pair<text::FormatField, Offsets> formatFieldAndOffsets(
        int offset, const text::FormatReportSettings& settings,
        bool calculateOffsets) const override;

    std::string textLocale() const override;

private:
    explicit ObjectTextRange(ObjectPtr object);

    Backend& backend() const;

    ObjectPtr object_;
};

}  // namespace alus::a11y
