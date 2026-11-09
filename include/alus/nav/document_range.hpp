// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// A text range over a whole DOCUMENT, not over one accessible.
//
// This is the other half of 01-ARHITEKTURA.md §9.3, and the file the phase 5
// handover pointed at: `nav::ControlFieldSynth` makes the fields, and
// `AncestorChainDiff` here turns a sequence of leaves into NVDA's nested
// `controlStart`/`controlEnd` stream. Together they are the port of
// `compoundDocuments.TreeCompoundTextInfo.getTextWithFields` (:331-368).
//
// ONE DEVIATION FROM §9.4, AND IT IS MEASURED. The plan specifies
//
//     AtspiDocumentTextRange : BrowseModeDocumentTextRange, OffsetsTextRange
//
// -- a flat offsets range over the document root, on the strength of §9.1's
// claim that "Gecko and Chromium expose the document body as AtspiText". Phase
// 5 measured that claim and it does not hold on this desktop:
//
//   * N47: the text of a focused Chrome document is "￼￼￼".
//     Three characters, three embedded objects, NO document text at all. The
//     root's AtspiText is a list of its children, not the body.
//   * N45: Chromium answers neither LINE nor WORD nor CHARACTER granularity,
//     so every line boundary over such a root would be ALUS searching the
//     whole text itself -- over D-Bus, per keystroke.
//
// So the range is COMPOUND: its position is (leaf object, byte offset in that
// leaf), and the per-leaf unit work is delegated to `a11y::ObjectTextRange`,
// which phase 5 already got right against four toolkits. That is NVDA's own
// `TreeCompoundTextInfo` design, and the payoff is that LibreOffice Writer
// needs no second mechanism: §9.4's `CompoundDocument` and its browser
// document are the same class with a different root.
//
// WHAT A "LEAF" IS, because everything rests on it:
//   * an object with text that contains no U+FFFC -- it holds real characters;
//   * an object with neither text nor children -- an image, a button: it
//     contributes one U+FFFC and a control field whose `content` is its name,
//     which is how N47's placeholder finally gets resolved into words.
// Anything else is a container and is descended into.
//
// THE N49 RULE APPLIES HERE TOO. This range holds its leaf and its root alive,
// so nothing an object owns may store one. `text::Bookmark` is what survives,
// and this range's bookmark carries the leaf's object path in `data` -- which
// is also why `Bookmark::data` exists at all (range.hpp: "for implementations
// whose position is not two numbers").
//
// THREADING: main thread only.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/nav/control_field.hpp"
#include "alus/text/fields.hpp"
#include "alus/text/range.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace alus::nav {

// Which leaves a document subtree has, and how to walk them. Free-standing
// because quick navigation and the table navigation need the same walk, and
// because a walk with a bug in it is cheaper to test on its own.
class DocumentLeaves {
public:
    // Whether this object holds the document's text rather than containing it.
    static bool isLeaf(const a11y::Object& object);
    // A leaf with no characters of its own: an image, a button, a check box.
    // It occupies one U+FFFC in its parent's text.
    static bool isEmbeddedLeaf(const a11y::Object& object);

    // Depth first, document order. Null at the end.
    static a11y::ObjectPtr first(const a11y::ObjectPtr& root);
    static a11y::ObjectPtr last(const a11y::ObjectPtr& root);
    static a11y::ObjectPtr next(const a11y::ObjectPtr& root, const a11y::ObjectPtr& from);
    static a11y::ObjectPtr previous(const a11y::ObjectPtr& root, const a11y::ObjectPtr& from);

    // The leaf that contains this object, or the first leaf inside it. This is
    // how a focus event or a quick-nav match becomes a document position.
    static a11y::ObjectPtr leafFor(const a11y::ObjectPtr& root, const a11y::ObjectPtr& object);

    // Whether `object` is inside `root`'s subtree. Checked before every
    // position is accepted, because an application can move an object out of
    // the document between two pump cycles.
    static bool contains(const a11y::ObjectPtr& root, const a11y::ObjectPtr& object);

    // The ancestor chain from just inside `root` down to `leaf` INCLUSIVE,
    // outermost first. What the diff diffs.
    static std::vector<a11y::ObjectPtr> ancestorChain(const a11y::ObjectPtr& root,
                                                      const a11y::ObjectPtr& leaf);
};

// The port of `getTextWithFields`'s own body: a running field stack that is
// told one leaf at a time and emits the commands that get from the previous
// leaf's chain to this one's.
//
// IT IS A SEPARATE CLASS, not a loop inside the range, because it is the one
// piece of phase 6 whose correctness is not observable from the outside: a
// missing `controlEnd` does not crash and does not log -- it makes the user
// hear "out of list" two paragraphs late, or never. T6.2 and T6.3 assert on
// this class directly.
class AncestorChainDiff {
public:
    AncestorChainDiff(a11y::ObjectPtr root, const text::FormatReportSettings& settings);

    // Emits the commands for moving to this leaf, then nothing about the leaf
    // itself -- the caller appends the text. The first call emits the whole
    // initial chain, which is what `getTextWithFields` must start with.
    void enter(const a11y::ObjectPtr& leaf, text::TextWithFields& into);
    // Closes everything still open. Called once at the end; without it the
    // last line of a document never leaves its container.
    void finish(text::TextWithFields& into);

    // How many fields are open. Used by the tests and by the log.
    // How many fields are OPEN -- the ones that were emitted, which is what a
    // reader of the stream can observe.
    std::size_t depth() const noexcept;

private:
    a11y::ObjectPtr root_;
    ControlFieldSynth synth_;
    // The chain as it stands, outermost first, with the field each object
    // produced. The FIELD is kept, not re-synthesised, so that closing it
    // reports the same control the user was told they entered.
    struct OpenField {
        a11y::ObjectPtr object;
        text::ControlField field;
        // Whether this chain element produced a field at all. The stack keeps
        // the ones that did NOT (a paragraph, a named link destination) so
        // that it mirrors the ancestor chain index for index; see the comment
        // in `enter`, which is where getting this wrong goes inaudible.
        bool emitted = false;
    };
    std::vector<OpenField> open_;
    bool started_ = false;
};

// A position in a document: which leaf, and the byte offset inside it.
struct DocumentPosition {
    a11y::ObjectPtr leaf;
    int offset = 0;

    bool valid() const noexcept { return leaf != nullptr; }
};

class DocumentTextRange final : public text::TextRange {
public:
    // Null when the root has no leaves at all, or when the position cannot be
    // obtained (no caret in the document).
    static std::unique_ptr<DocumentTextRange> make(a11y::ObjectPtr root,
                                                   const text::FormatReportSettings& settings,
                                                   text::Position position);
    // At an explicit position, which is how a bookmark and a quick-nav match
    // become a range.
    static std::unique_ptr<DocumentTextRange> makeAt(a11y::ObjectPtr root,
                                                     const text::FormatReportSettings& settings,
                                                     DocumentPosition start,
                                                     DocumentPosition end);

    ~DocumentTextRange() override;

    std::string_view rangeKind() const override { return "document"; }
    std::unique_ptr<text::TextRange> copy() const override;

    const a11y::ObjectPtr& root() const noexcept { return root_; }
    const DocumentPosition& start() const noexcept { return start_; }
    const DocumentPosition& end() const noexcept { return end_; }

    // The object at the start of the range, which is what browse mode
    // activates, what the navigator object follows, and what the elements list
    // moves to.
    a11y::ObjectPtr objectAtStart() const;

    // ---- text::TextRange ----
    std::string text() const override;
    text::TextWithFields getTextWithFields(
        const text::FormatReportSettings& settings) const override;
    int compareEndPoints(const text::TextRange& other, text::EndPointPair which) const override;
    bool setEndPoint(const text::TextRange& other, text::EndPointPair which) override;
    bool expand(text::TextUnit unit) override;
    void collapse(bool toEnd = false) override;
    int move(text::TextUnit unit, int direction,
             std::optional<text::EndPoint> endPoint = std::nullopt) override;
    bool find(std::string_view needle, bool caseSensitive = false,
              bool reverse = false) override;
    bool updateCaret() override;
    bool updateSelection() override;
    text::Bookmark bookmark() const override;
    bool moveToBookmark(const text::Bookmark& mark) override;
    std::vector<core::Rect> boundingRects() const override;
    std::optional<core::Point> pointAtStart() const override;
    std::optional<int> unitIndex(text::TextUnit unit) const override;
    std::optional<int> unitCount(text::TextUnit unit) const override;
    std::string locationText() const override;

private:
    DocumentTextRange(a11y::ObjectPtr root, const text::FormatReportSettings& settings);

    // The leaf ranges this range covers, in document order, each clipped to
    // this range. NVDA: `_getTextInfos`. The ONE place that knows how a
    // compound range decomposes, so the text, the fields and the rectangles
    // cannot disagree about it.
    struct LeafSpan {
        a11y::ObjectPtr leaf;
        int start = 0;
        int end = 0;
    };
    std::vector<LeafSpan> leafSpans() const;

    // A range over one leaf, for delegating the unit work. Null when the leaf
    // has no Text interface (an embedded leaf).
    text::TextRangePtr rangeFor(const a11y::ObjectPtr& leaf, int start, int end) const;
    int leafLength(const a11y::ObjectPtr& leaf) const;
    // Document order: -1, 0, 1. Compares two positions, which is what every
    // endpoint comparison and every move boundary test reduces to.
    int comparePositions(const DocumentPosition& a, const DocumentPosition& b) const;
    // Walks `count` leaves and lands at the near edge. The crossing logic, in
    // one place.
    bool stepLeaf(DocumentPosition& position, bool forwards) const;
    bool normalise(DocumentPosition& position) const;

    a11y::ObjectPtr root_;
    text::FormatReportSettings settings_;
    DocumentPosition start_;
    DocumentPosition end_;
};

}  // namespace alus::nav
