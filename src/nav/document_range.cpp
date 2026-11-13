// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/document_range.hpp.
#include "alus/nav/document_range.hpp"

#include "alus/a11y/api.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_range.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace alus::nav {
namespace {

using a11y::Object;
using a11y::ObjectPtr;

// U+FFFC OBJECT REPLACEMENT CHARACTER, in UTF-8. `textUtils.OBJ_REPLACEMENT_CHAR`.
constexpr std::string_view kObjectReplacement = "\xEF\xBF\xBC";

// How deep a document may nest before the walk gives up. A malformed tree with
// a parent cycle is not a theoretical worry: it is what a crashing web content
// process looks like from the outside, and a screen reader that spins on it
// takes the user's session with it.
constexpr int kMaxDepth = 256;

// How far a walk of the document will go before giving up. A document with
// more leaves than this exists (a very long web page), and the walks that use
// it are the ones a user triggers once -- a bookmark restore, a line count --
// never the ones on the keystroke path.
constexpr int kMaxWalk = 4096;

// Separates the two leaf paths inside a bookmark. ASCII US, which cannot occur
// in an AT-SPI object path.
constexpr char kLeafSeparator = '\x1f';

bool hasText(const Object& object) {
    return object.hasInterface(a11y::Interface::Text);
}

bool textIsAllPlaceholders(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (std::size_t at = 0; at < text.size();) {
        if (text.compare(at, kObjectReplacement.size(), kObjectReplacement) != 0) {
            return false;
        }
        at += kObjectReplacement.size();
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// DocumentLeaves
// ---------------------------------------------------------------------------

bool DocumentLeaves::isEmbeddedLeaf(const Object& object) {
    if (object.childCount() > 0) {
        return false;
    }
    if (!hasText(object)) {
        return true;
    }
    return object.characterCount() == 0;
}

bool DocumentLeaves::isLeaf(const Object& object) {
    if (!object.isAlive()) {
        return false;
    }
    if (object.childCount() == 0) {
        // Nothing below it: either it holds characters or it is an embedded
        // object that occupies one U+FFFC in its parent.
        return true;
    }
    if (!hasText(object) || object.characterCount() == 0) {
        return false;
    }
    // It has both children and text. Whether it is a leaf depends on whether
    // the text is REAL or is only a list of its children -- which is exactly
    // the N47 case: a Chrome document root with three children answers
    // "￼￼￼" and is a container, while a paragraph containing a
    // link answers "see the docs" and is a leaf whose link is resolved inside
    // it by the hypertext path.
    const std::string own = object.textRange(0, -1);
    return !textIsAllPlaceholders(own);
}

ObjectPtr DocumentLeaves::first(const ObjectPtr& root) {
    if (root == nullptr || !root->isAlive()) {
        return nullptr;
    }
    ObjectPtr current = root;
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        if (isLeaf(*current)) {
            return current;
        }
        const ObjectPtr child = current->firstChild();
        if (child == nullptr) {
            return current;
        }
        current = child;
    }
    ALUS_WARNING("document: first leaf gave up at depth {}", kMaxDepth);
    return current;
}

ObjectPtr DocumentLeaves::last(const ObjectPtr& root) {
    if (root == nullptr || !root->isAlive()) {
        return nullptr;
    }
    ObjectPtr current = root;
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        if (isLeaf(*current)) {
            return current;
        }
        const ObjectPtr child = current->lastChild();
        if (child == nullptr) {
            return current;
        }
        current = child;
    }
    ALUS_WARNING("document: last leaf gave up at depth {}", kMaxDepth);
    return current;
}

bool DocumentLeaves::contains(const ObjectPtr& root, const ObjectPtr& object) {
    if (root == nullptr || object == nullptr) {
        return false;
    }
    ObjectPtr current = object;
    for (int depth = 0; depth < kMaxDepth && current != nullptr; ++depth) {
        if (current->sameAs(*root)) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

ObjectPtr DocumentLeaves::next(const ObjectPtr& root, const ObjectPtr& from) {
    if (from == nullptr || root == nullptr) {
        return nullptr;
    }
    // Down first: a container's own first leaf comes before its siblings.
    if (!isLeaf(*from)) {
        if (const ObjectPtr child = from->firstChild()) {
            return first(child);
        }
    }
    // Then across, then up. The root itself is never stepped past.
    ObjectPtr current = from;
    for (int depth = 0; depth < kMaxDepth && current != nullptr; ++depth) {
        if (current->sameAs(*root)) {
            return nullptr;
        }
        if (const ObjectPtr sibling = current->next()) {
            return first(sibling);
        }
        current = current->parent();
        if (current != nullptr && !contains(root, current)) {
            return nullptr;
        }
    }
    return nullptr;
}

ObjectPtr DocumentLeaves::previous(const ObjectPtr& root, const ObjectPtr& from) {
    if (from == nullptr || root == nullptr) {
        return nullptr;
    }
    ObjectPtr current = from;
    for (int depth = 0; depth < kMaxDepth && current != nullptr; ++depth) {
        if (current->sameAs(*root)) {
            return nullptr;
        }
        if (const ObjectPtr sibling = current->previous()) {
            return last(sibling);
        }
        current = current->parent();
        if (current == nullptr || !contains(root, current)) {
            return nullptr;
        }
        if (current->sameAs(*root)) {
            return nullptr;
        }
        // A container that is itself a leaf is a position in its own right.
        if (isLeaf(*current)) {
            return current;
        }
    }
    return nullptr;
}

ObjectPtr DocumentLeaves::leafFor(const ObjectPtr& root, const ObjectPtr& object) {
    if (object == nullptr || !contains(root, object)) {
        return nullptr;
    }
    if (isLeaf(*object)) {
        return object;
    }
    return first(object);
}

std::vector<ObjectPtr> DocumentLeaves::ancestorChain(const ObjectPtr& root,
                                                     const ObjectPtr& leaf) {
    std::vector<ObjectPtr> chain;
    if (leaf == nullptr || root == nullptr) {
        return chain;
    }
    ObjectPtr current = leaf;
    for (int depth = 0; depth < kMaxDepth && current != nullptr; ++depth) {
        if (current->sameAs(*root)) {
            break;
        }
        chain.push_back(current);
        current = current->parent();
    }
    // The walk went upwards; the stream needs outermost first.
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// ---------------------------------------------------------------------------
// AncestorChainDiff -- the port of getTextWithFields' own body
// ---------------------------------------------------------------------------

AncestorChainDiff::AncestorChainDiff(ObjectPtr root,
                                     const text::FormatReportSettings& settings)
    : root_(std::move(root)), synth_(settings) {}

void AncestorChainDiff::enter(const ObjectPtr& leaf, text::TextWithFields& into,
                              std::string_view content) {
    const std::vector<ObjectPtr> chain = DocumentLeaves::ancestorChain(root_, leaf);

    // How much of the open stack this leaf still shares. Compared by OBJECT
    // IDENTITY, which on AT-SPI is reference identity (§6.2) -- not by field
    // equality, because two sibling list items produce equal fields and
    // treating them as the same one would swallow the boundary between them.
    //
    // THE STACK MIRRORS THE CHAIN ONE FOR ONE, including the objects that
    // produce NO field (a paragraph, a named link destination). If it held
    // only the emitted ones, this index comparison would silently drift the
    // moment a document contained a paragraph -- and the symptom would be a
    // `controlEnd` for the wrong control, which is inaudible until the user is
    // told they left a table they are still in.
    std::size_t common = 0;
    while (common < open_.size() && common < chain.size() &&
           open_[common].object->sameAs(*chain[common])) {
        ++common;
    }

    // Close what was left, innermost first. This is the announcement the user
    // hears as "out of list".
    for (std::size_t count = open_.size(); count > common; --count) {
        if (open_[count - 1].emitted) {
            into.push_back(text::TextOrField::ofCommand(text::FieldCommand::controlEnd()));
        }
    }
    open_.resize(common);

    // Open what is new, outermost first.
    for (std::size_t index = common; index < chain.size(); ++index) {
        const ObjectPtr& object = chain[index];
        // The leaf itself is a text node unless it is an embedded object: a
        // paragraph is not announced, an image is.
        const bool isTheLeaf = index + 1 == chain.size();
        const bool embedded = isTheLeaf && DocumentLeaves::isEmbeddedLeaf(*object);
        std::optional<text::ControlField> field =
            synth_.forObject(*object, /*ignoreEditableText=*/!embedded);
        if (!field.has_value()) {
            open_.push_back(OpenField{object, text::ControlField{}, false});
            continue;
        }
        // NVDA's `_startOfNode`, which virtualBuffers sets and which both
        // braille and the format reporting read: does this container BEGIN
        // here, or was the range opened inside one that started earlier? A
        // list's third item must not re-announce the list.
        //
        // `leaf` is the first leaf of `object` exactly when every step from
        // `object` down to the leaf is a first child -- which is the same walk
        // `DocumentLeaves::first` makes, read off the chain already in hand
        // rather than descending the tree again (one cached `indexInParent`
        // per step, no extra platform call).
        bool startOfNode = true;
        for (std::size_t below = index + 1; below < chain.size(); ++below) {
            if (chain[below]->indexInParent() != 0) {
                startOfNode = false;
                break;
            }
        }
        field->set("_startOfNode", startOfNode);
        if (isTheLeaf && !content.empty()) {
            // A link (or any object with text of its own) inside a paragraph:
            // it occupies ONE U+FFFC in its parent's text, and this is what
            // that character stands for. FAZA10.md N114.
            field->set("content", std::string(content));
        } else if (embedded && !object->name().empty()) {
            // N47 resolved: the U+FFFC placeholder becomes the object's name,
            // which is what the user came to hear. NVDA does the same thing at
            // the same place (`controlField["content"] = childObject.name`).
            field->set("content", object->name());
        }
        into.push_back(
            text::TextOrField::ofCommand(text::FieldCommand::controlStart(*field)));
        open_.push_back(OpenField{object, *field, true});
    }
    started_ = true;
}

std::size_t AncestorChainDiff::depth() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(open_.begin(), open_.end(),
                      [](const OpenField& one) { return one.emitted; }));
}

void AncestorChainDiff::finish(text::TextWithFields& into) {
    if (!started_) {
        return;
    }
    for (std::size_t count = open_.size(); count > 0; --count) {
        if (open_[count - 1].emitted) {
            into.push_back(text::TextOrField::ofCommand(text::FieldCommand::controlEnd()));
        }
    }
    open_.clear();
}


// ---------------------------------------------------------------------------
// DocumentTextRange
// ---------------------------------------------------------------------------

DocumentTextRange::DocumentTextRange(ObjectPtr root,
                                     const text::FormatReportSettings& settings)
    : root_(std::move(root)), settings_(settings) {}

DocumentTextRange::~DocumentTextRange() = default;

std::unique_ptr<DocumentTextRange> DocumentTextRange::make(
    ObjectPtr root, const text::FormatReportSettings& settings, text::Position position) {
    if (root == nullptr || !root->isAlive()) {
        return nullptr;
    }
    auto range = std::unique_ptr<DocumentTextRange>(
        new DocumentTextRange(std::move(root), settings));

    switch (position) {
        case text::Position::First:
        case text::Position::All: {
            const ObjectPtr firstLeaf = DocumentLeaves::first(range->root_);
            if (firstLeaf == nullptr) {
                return nullptr;
            }
            range->start_ = DocumentPosition{firstLeaf, 0};
            if (position == text::Position::All) {
                const ObjectPtr lastLeaf = DocumentLeaves::last(range->root_);
                range->end_ = DocumentPosition{lastLeaf, range->leafLength(lastLeaf)};
            } else {
                range->end_ = range->start_;
            }
            return range;
        }
        case text::Position::Last: {
            const ObjectPtr lastLeaf = DocumentLeaves::last(range->root_);
            if (lastLeaf == nullptr) {
                return nullptr;
            }
            // NVDA's POSITION_LAST is the last CHARACTER, not the end of the
            // text: a range collapsed past the end has nothing to read.
            const int length = range->leafLength(lastLeaf);
            range->start_ = DocumentPosition{lastLeaf, length > 0 ? length - 1 : 0};
            range->end_ = range->start_;
            return range;
        }
        case text::Position::Caret:
        case text::Position::Selection: {
            // The caret is wherever the focused leaf says it is. The document
            // root does not have one -- which is the whole N47 problem --
            // so the leaf is asked, and a document with no focused leaf has
            // no caret position rather than a wrong one.
            const ObjectPtr focused = DocumentLeaves::leafFor(
                range->root_, range->root_->registry().api().focus());
            const ObjectPtr leaf =
                focused != nullptr ? focused : DocumentLeaves::first(range->root_);
            if (leaf == nullptr) {
                return nullptr;
            }
            text::TextRangePtr inLeaf = leaf->makeTextRange(position);
            if (inLeaf == nullptr) {
                range->start_ = DocumentPosition{leaf, 0};
                range->end_ = range->start_;
                return range;
            }
            const text::Bookmark mark = inLeaf->bookmark();
            range->start_ = DocumentPosition{leaf, static_cast<int>(mark.start)};
            range->end_ = DocumentPosition{leaf, static_cast<int>(mark.end)};
            return range;
        }
    }
    return nullptr;
}

std::unique_ptr<DocumentTextRange> DocumentTextRange::makeAt(
    ObjectPtr root, const text::FormatReportSettings& settings, DocumentPosition start,
    DocumentPosition end) {
    if (root == nullptr || !start.valid() || !end.valid()) {
        return nullptr;
    }
    if (!DocumentLeaves::contains(root, start.leaf) ||
        !DocumentLeaves::contains(root, end.leaf)) {
        return nullptr;
    }
    auto range = std::unique_ptr<DocumentTextRange>(
        new DocumentTextRange(std::move(root), settings));
    range->start_ = std::move(start);
    range->end_ = std::move(end);
    range->normalise(range->start_);
    range->normalise(range->end_);
    return range;
}

std::unique_ptr<text::TextRange> DocumentTextRange::copy() const {
    auto range = std::unique_ptr<DocumentTextRange>(new DocumentTextRange(root_, settings_));
    range->start_ = start_;
    range->end_ = end_;
    range->setReadingUnit(readingUnit());
    return range;
}

ObjectPtr DocumentTextRange::objectAtStart() const {
    if (!start_.valid()) {
        return nullptr;
    }
    // Inside a leaf, the hypertext path resolves the embedded object at the
    // offset -- a link inside a paragraph is what the user activates, not the
    // paragraph.
    if (text::TextRangePtr inLeaf = rangeFor(start_.leaf, start_.offset, start_.offset)) {
        if (auto* objectRange = dynamic_cast<a11y::ObjectTextRange*>(inLeaf.get())) {
            if (ObjectPtr embedded = objectRange->objectAtOffset(start_.offset)) {
                return embedded;
            }
        }
    }
    return start_.leaf;
}

int DocumentTextRange::leafLength(const ObjectPtr& leaf) const {
    if (leaf == nullptr) {
        return 0;
    }
    if (DocumentLeaves::isEmbeddedLeaf(*leaf)) {
        // One U+FFFC: three bytes, and the offsets in this layer are bytes
        // (§7.3). An embedded leaf is one character wide so that moving by
        // character over an image takes one press, not three.
        return static_cast<int>(kObjectReplacement.size());
    }
    return leaf->characterCount();
}

text::TextRangePtr DocumentTextRange::rangeFor(const ObjectPtr& leaf, int start,
                                               int end) const {
    if (leaf == nullptr || DocumentLeaves::isEmbeddedLeaf(*leaf)) {
        return nullptr;
    }
    return leaf->makeTextRange(start, end);
}

bool DocumentTextRange::normalise(DocumentPosition& position) const {
    if (!position.valid()) {
        return false;
    }
    const int length = leafLength(position.leaf);
    position.offset = std::clamp(position.offset, 0, length);
    return true;
}

int DocumentTextRange::comparePositions(const DocumentPosition& a,
                                        const DocumentPosition& b) const {
    if (!a.valid() || !b.valid()) {
        return 0;
    }
    // A position inside a link compares as its placeholder in the parent, and
    // then by its offset inside the link: (parent, ph) < (link, any) <
    // (parent, ph + 1). N117.
    if (!a.leaf->sameAs(*b.leaf) && (inlineParent(a.leaf) != nullptr ||
                                     inlineParent(b.leaf) != nullptr)) {
        const DocumentPosition outerA = toParent(a);
        const DocumentPosition outerB = toParent(b);
        const int outer = comparePositions(outerA, outerB);
        if (outer != 0) {
            return outer;
        }
        const int innerA = inlineParent(a.leaf) != nullptr ? a.offset : -1;
        const int innerB = inlineParent(b.leaf) != nullptr ? b.offset : -1;
        return innerA < innerB ? -1 : innerA > innerB ? 1 : 0;
    }
    if (a.leaf->sameAs(*b.leaf)) {
        if (a.offset < b.offset) {
            return -1;
        }
        return a.offset > b.offset ? 1 : 0;
    }
    // Different leaves: document order decides. Walking forwards from `a` is
    // O(leaves), and that is acceptable because the two leaves of a comparison
    // are almost always neighbours -- a selection spanning a whole document is
    // the rare case, and the alternative (a position index per leaf) would
    // have to be invalidated on every DOM change.
    ObjectPtr walk = a.leaf;
    for (int step = 0; step < kMaxWalk && walk != nullptr; ++step) {
        walk = DocumentLeaves::next(root_, walk);
        if (walk != nullptr && walk->sameAs(*b.leaf)) {
            return -1;
        }
    }
    return 1;
}

std::vector<DocumentTextRange::LeafSpan> DocumentTextRange::leafSpans() const {
    std::vector<LeafSpan> spans;
    if (!start_.valid() || !end_.valid()) {
        return spans;
    }
    if (start_.leaf->sameAs(*end_.leaf)) {
        spans.push_back(LeafSpan{start_.leaf, start_.offset, end_.offset});
        return spans;
    }
    // A range that starts or ends INSIDE a link and reaches past it is walked
    // in the parent's coordinates, the whole link included (its placeholder
    // carries its text into the stream, N114). Only a range wholly inside one
    // link -- a character, a word -- is read from the link itself, above.
    const DocumentPosition first = toParent(start_);
    const DocumentPosition last = toParent(end_, /*after=*/true);
    if (first.leaf->sameAs(*last.leaf)) {
        spans.push_back(LeafSpan{first.leaf, first.offset, last.offset});
        return spans;
    }
    const DocumentPosition& endAt = last;
    ObjectPtr current = first.leaf;
    spans.push_back(LeafSpan{current, first.offset, leafLength(current)});
    for (int step = 0; step < kMaxWalk; ++step) {
        current = DocumentLeaves::next(root_, current);
        if (current == nullptr) {
            ALUS_DEBUG("document range: end leaf not reached walking forwards");
            break;
        }
        if (current->sameAs(*endAt.leaf)) {
            spans.push_back(LeafSpan{current, 0, endAt.offset});
            break;
        }
        spans.push_back(LeafSpan{current, 0, leafLength(current)});
    }
    return spans;
}

std::string DocumentTextRange::text() const {
    std::string out;
    for (const LeafSpan& span : leafSpans()) {
        if (span.end <= span.start) {
            continue;
        }
        if (DocumentLeaves::isEmbeddedLeaf(*span.leaf)) {
            out += kObjectReplacement;
            continue;
        }
        out += span.leaf->textRange(span.start, span.end);
    }
    return out;
}

void DocumentTextRange::appendLeafSpan(const ObjectPtr& leaf, int start, int end,
                                       AncestorChainDiff& diff, text::TextWithFields& out,
                                       const text::FormatReportSettings& settings) const {
    // The leaf's own format fields come from the range phase 5 built, so the
    // font, the colour, the spelling errors and the language inside a document
    // line are exactly what they are inside a text box.
    const auto appendPlain = [&](int from, int to) {
        if (to <= from) {
            return;
        }
        diff.enter(leaf, out);
        if (text::TextRangePtr inLeaf = rangeFor(leaf, from, to)) {
            for (text::TextOrField& item : inLeaf->getTextWithFields(settings)) {
                out.push_back(std::move(item));
            }
        } else {
            out.push_back(text::TextOrField::ofText(leaf->textRange(from, to)));
        }
    };

    // N114. A leaf whose text MIXES characters and U+FFFC -- a paragraph with
    // a link in it, a navigation bar of links separated by spaces -- used to
    // go into the stream as it was, placeholders and all, and the user heard
    // "banner, Glavni meni, navigation, <U+FFFC> <U+FFFC> <U+FFFC>" where the
    // page says "Početna Vesti Kontakt". DocumentLeaves deliberately makes such
    // an object a leaf (its children are NOT visited as leaves of their own,
    // or the text between them would belong to nobody), so this is the one
    // place its links can be read: each placeholder is resolved through the
    // hypertext to the object it stands for, and that object's field carries
    // its text as `content`.
    //
    // THE PLACEHOLDER STAYS IN THE STREAM, one character for one offset, and
    // that is not an oversight: braille maps display cells to range offsets
    // from this stream, and replacing one character by seven would move the
    // routing of everything after the link by six cells. Speech drops the
    // character (a11y/text_speech.cpp) because the field already said it.
    const std::string text = leaf->textRange(start, end);
    if (text.find(kObjectReplacement) == std::string::npos) {
        appendPlain(start, end);
        return;
    }
    text::TextRangePtr hypertext = rangeFor(leaf, start, end);
    auto* objectRange = dynamic_cast<a11y::ObjectTextRange*>(hypertext.get());

    int runStart = start;
    int offset = start;  // in the leaf's own unit: codepoints (N113)
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<unsigned char>(text[at]);
        const std::size_t width = lead < 0x80U ? 1 : (lead >> 5U) == 0x6U ? 2
                                                     : (lead >> 4U) == 0xEU ? 3 : 4;
        if (text.compare(at, kObjectReplacement.size(), kObjectReplacement) == 0) {
            appendPlain(runStart, offset);
            ObjectPtr child = objectRange != nullptr ? objectRange->objectAtOffset(offset)
                                                     : nullptr;
            if (child != nullptr && !child->sameAs(*leaf)) {
                std::string content;
                if (hasText(*child) && child->characterCount() > 0) {
                    content = child->textRange(0, -1);
                    // One level of nesting is resolved by NAME rather than
                    // recursively: a link whose own text holds another
                    // placeholder is an image link, and its name is what it
                    // is called.
                    if (content.find(kObjectReplacement) != std::string::npos) {
                        content = child->name();
                    }
                }
                if (content.empty()) {
                    content = child->name();
                }
                diff.enter(child, out, content);
            } else {
                diff.enter(leaf, out);
            }
            out.push_back(text::TextOrField::ofText(std::string(kObjectReplacement)));
            runStart = offset + 1;
        }
        at += width;
        ++offset;
    }
    appendPlain(runStart, offset);
}

text::TextWithFields DocumentTextRange::getTextWithFields(
    const text::FormatReportSettings& settings) const {
    // §9.3. The stream the whole of document reading rests on.
    text::TextWithFields out;
    AncestorChainDiff diff{root_, settings};
    for (const LeafSpan& span : leafSpans()) {
        diff.enter(span.leaf, out);
        if (DocumentLeaves::isEmbeddedLeaf(*span.leaf)) {
            // The placeholder itself still goes into the stream: the
            // presentation layer decides whether to say the content or the
            // character, and a stream that silently dropped it would make the
            // offsets of everything after it wrong.
            if (span.end > span.start) {
                out.push_back(text::TextOrField::ofText(std::string(kObjectReplacement)));
            }
            continue;
        }
        if (span.end <= span.start) {
            continue;
        }
        appendLeafSpan(span.leaf, span.start, span.end, diff, out, settings);
    }
    diff.finish(out);
    return out;
}

int DocumentTextRange::compareEndPoints(const text::TextRange& other,
                                        text::EndPointPair which) const {
    const auto* document = dynamic_cast<const DocumentTextRange*>(&other);
    if (document == nullptr) {
        ALUS_WARNING("document range compared with a {} range", other.rangeKind());
        return 0;
    }
    switch (which) {
        case text::EndPointPair::StartToStart:
            return comparePositions(start_, document->start_);
        case text::EndPointPair::StartToEnd:
            return comparePositions(start_, document->end_);
        case text::EndPointPair::EndToStart:
            return comparePositions(end_, document->start_);
        case text::EndPointPair::EndToEnd:
            return comparePositions(end_, document->end_);
    }
    return 0;
}

bool DocumentTextRange::setEndPoint(const text::TextRange& other, text::EndPointPair which) {
    const auto* document = dynamic_cast<const DocumentTextRange*>(&other);
    if (document == nullptr) {
        ALUS_WARNING("document range endpoint set from a {} range", other.rangeKind());
        return false;
    }
    switch (which) {
        case text::EndPointPair::StartToStart:
            start_ = document->start_;
            break;
        case text::EndPointPair::StartToEnd:
            start_ = document->end_;
            break;
        case text::EndPointPair::EndToStart:
            end_ = document->start_;
            break;
        case text::EndPointPair::EndToEnd:
            end_ = document->end_;
            break;
    }
    return true;
}

void DocumentTextRange::collapse(bool toEnd) {
    if (toEnd) {
        start_ = end_;
    } else {
        end_ = start_;
    }
}

bool DocumentTextRange::expand(text::TextUnit unit) {
    if (!start_.valid()) {
        return false;
    }
    const text::TextUnit resolved = resolveReadingChunk(unit);
    if (resolved == text::TextUnit::Story) {
        const ObjectPtr firstLeaf = DocumentLeaves::first(root_);
        const ObjectPtr lastLeaf = DocumentLeaves::last(root_);
        if (firstLeaf == nullptr || lastLeaf == nullptr) {
            return false;
        }
        start_ = DocumentPosition{firstLeaf, 0};
        end_ = DocumentPosition{lastLeaf, leafLength(lastLeaf)};
        return true;
    }
    // Inside a link only a character and a word are the LINK's units; a line,
    // a paragraph, a sentence are its parent's (N117).
    if (resolved != text::TextUnit::Character && resolved != text::TextUnit::Word) {
        start_ = toParent(start_);
    }
    // A leaf is never split across a unit smaller than itself, so the unit
    // work is the leaf's -- which is the point of the compound design: phase
    // 5 already measured lines, words and sentences against four toolkits.
    if (DocumentLeaves::isEmbeddedLeaf(*start_.leaf)) {
        start_.offset = 0;
        end_ = DocumentPosition{start_.leaf, leafLength(start_.leaf)};
        return true;
    }
    text::TextRangePtr inLeaf = rangeFor(start_.leaf, start_.offset, start_.offset);
    if (inLeaf == nullptr || !inLeaf->expand(resolved)) {
        return false;
    }
    const text::Bookmark mark = inLeaf->bookmark();
    start_ = DocumentPosition{start_.leaf, static_cast<int>(mark.start)};
    end_ = DocumentPosition{start_.leaf, static_cast<int>(mark.end)};
    return true;
}

// ---------------------------------------------------------------------------
// Positions inside a link (N117)
// ---------------------------------------------------------------------------

ObjectPtr DocumentTextRange::inlineParent(const ObjectPtr& object) const {
    if (object == nullptr) {
        return nullptr;
    }
    const ObjectPtr parent = object->parent();
    if (parent == nullptr || parent->sameAs(*root_) || !hasText(*parent) ||
        !DocumentLeaves::isLeaf(*parent)) {
        return nullptr;
    }
    // A child of a leaf is never visited by the leaf walk, so it can only be
    // reached as one of the leaf's placeholders: that is what makes it inline.
    return parent;
}

std::optional<int> DocumentTextRange::placeholderOffset(const ObjectPtr& parent,
                                                        const ObjectPtr& child) const {
    text::TextRangePtr range = rangeFor(parent, 0, 0);
    const auto* objectRange = dynamic_cast<a11y::ObjectTextRange*>(range.get());
    if (objectRange == nullptr || child == nullptr) {
        return std::nullopt;
    }
    if (const auto offsets = objectRange->offsetsForObject(*child)) {
        return offsets->start;
    }
    return std::nullopt;
}

ObjectPtr DocumentTextRange::inlineChildAt(const ObjectPtr& leaf, int offset) const {
    if (leaf == nullptr || offset < 0 || offset >= leafLength(leaf) ||
        DocumentLeaves::isEmbeddedLeaf(*leaf)) {
        return nullptr;
    }
    if (leaf->textRange(offset, offset + 1) != kObjectReplacement) {
        return nullptr;
    }
    text::TextRangePtr range = rangeFor(leaf, offset, offset);
    const auto* objectRange = dynamic_cast<a11y::ObjectTextRange*>(range.get());
    ObjectPtr child = objectRange != nullptr ? objectRange->objectAtOffset(offset) : nullptr;
    if (child == nullptr || child->sameAs(*leaf) || !hasText(*child) ||
        child->characterCount() <= 0) {
        return nullptr;
    }
    // An image link -- its own text only placeholders -- stays one stop: there
    // is nothing to walk through, and its name is what the field says.
    if (textIsAllPlaceholders(child->textRange(0, -1))) {
        return nullptr;
    }
    return child;
}

DocumentPosition DocumentTextRange::toParent(const DocumentPosition& position, bool after) const {
    if (!position.valid()) {
        return position;
    }
    const ObjectPtr parent = inlineParent(position.leaf);
    if (parent == nullptr) {
        return position;
    }
    const std::optional<int> offset = placeholderOffset(parent, position.leaf);
    return DocumentPosition{parent, offset.value_or(0) + (after ? 1 : 0)};
}

void DocumentTextRange::descendIfInline(DocumentPosition& position, text::TextUnit unit,
                                        bool forwards) const {
    const ObjectPtr child = inlineChildAt(position.leaf, position.offset);
    if (child == nullptr) {
        return;
    }
    if (forwards) {
        position = DocumentPosition{child, 0};
        return;
    }
    const int length = leafLength(child);
    position = DocumentPosition{child, 0};
    if (text::TextRangePtr range = rangeFor(child, length, length)) {
        if (range->move(unit, -1) != 0) {
            position.offset = static_cast<int>(range->bookmark().start);
        }
    }
}

bool DocumentTextRange::stepUnit(DocumentPosition& position, text::TextUnit unit,
                                 bool forwards) const {
    // Inside a link: move within it, and at its edge step out into the parent.
    if (const ObjectPtr parent = inlineParent(position.leaf)) {
        if (text::TextRangePtr range = rangeFor(position.leaf, position.offset, position.offset)) {
            if (range->move(unit, forwards ? 1 : -1) != 0) {
                const int landed = static_cast<int>(range->bookmark().start);
                // The #12808 guard again: moving past the last unit "succeeds"
                // onto the exclusive end, where there is nothing.
                if (!forwards || landed < leafLength(position.leaf)) {
                    position.offset = landed;
                    return true;
                }
            }
        }
        const std::optional<int> placeholder = placeholderOffset(parent, position.leaf);
        if (!placeholder.has_value()) {
            position = DocumentPosition{parent, 0};
            return false;
        }
        if (forwards) {
            // Just after the link. If the parent's text ends there, the next
            // stop is in the next leaf, which the caller's crossing finds.
            position = DocumentPosition{parent, *placeholder + 1};
            if (position.offset >= leafLength(parent)) {
                return false;
            }
            descendIfInline(position, unit, true);
            return true;
        }
        // Back out to the placeholder, then one unit back in the parent.
        position = DocumentPosition{parent, *placeholder};
    }
    // In a leaf: the leaf's own unit, then into a link if it landed on one.
    if (text::TextRangePtr range = rangeFor(position.leaf, position.offset, position.offset)) {
        if (range->move(unit, forwards ? 1 : -1) != 0) {
            const int landed = static_cast<int>(range->bookmark().start);
            if (!(forwards && landed >= leafLength(position.leaf))) {
                position.offset = landed;
                descendIfInline(position, unit, forwards);
                return true;
            }
        }
    }
    return false;
}

bool DocumentTextRange::stepLeaf(DocumentPosition& position, bool forwards) const {
    const ObjectPtr next = forwards ? DocumentLeaves::next(root_, position.leaf)
                                    : DocumentLeaves::previous(root_, position.leaf);
    if (next == nullptr) {
        return false;
    }
    position.leaf = next;
    position.offset = forwards ? 0 : leafLength(next);
    return true;
}

int DocumentTextRange::move(text::TextUnit unit, int direction,
                            std::optional<text::EndPoint> endPoint) {
    if (direction == 0 || !start_.valid()) {
        return 0;
    }
    const text::TextUnit resolved = resolveReadingChunk(unit);
    const bool forwards = direction > 0;
    const int wanted = forwards ? direction : -direction;

    DocumentPosition position =
        endPoint == text::EndPoint::End ? end_ : start_;
    // N117: characters and words go INTO a link's text; every other unit is
    // the parent's, so a position inside a link is lifted out first.
    const bool inlineUnit =
        resolved == text::TextUnit::Character || resolved == text::TextUnit::Word;
    if (!inlineUnit) {
        position = toParent(position);
    }
    int moved = 0;
    for (int step = 0; step < wanted; ++step) {
        bool stepped = false;
        if (inlineUnit && !DocumentLeaves::isEmbeddedLeaf(*position.leaf)) {
            stepped = stepUnit(position, resolved, forwards);
        } else if (!DocumentLeaves::isEmbeddedLeaf(*position.leaf)) {
            if (text::TextRangePtr inLeaf =
                    rangeFor(position.leaf, position.offset, position.offset)) {
                if (inLeaf->move(resolved, forwards ? 1 : -1) != 0) {
                    const text::Bookmark mark = inLeaf->bookmark();
                    const int landed = static_cast<int>(mark.start);
                    // THE #12808 GUARD, which phase 5 already needed for a
                    // single object and which a compound range needs twice as
                    // much. An offsets range asked to move past its last unit
                    // reports SUCCESS and lands on the EXCLUSIVE END of the
                    // text -- there is no unit there. Accepting that is how a
                    // document range gets stuck at the end of every paragraph
                    // and answers "edge of document" in the middle of a page,
                    // which is exactly what T6.2 and the leaf-crossing test
                    // caught.
                    if (forwards && landed >= leafLength(position.leaf)) {
                        stepped = false;
                    } else {
                        position.offset = landed;
                        stepped = true;
                    }
                }
            }
        }
        if (!stepped) {
            // The leaf is exhausted: cross into the next one. THE CROSSING IS
            // THE WHOLE POINT of a document range -- a user pressing down
            // arrow at the end of a paragraph expects the next paragraph, not
            // "no more text".
            if (!stepLeaf(position, forwards)) {
                break;
            }
            // Landing on a leaf edge counts as the move; landing past it would
            // skip the first unit of the new leaf.
            if (!forwards && !DocumentLeaves::isEmbeddedLeaf(*position.leaf)) {
                if (text::TextRangePtr inLeaf =
                        rangeFor(position.leaf, position.offset, position.offset)) {
                    if (inLeaf->expand(resolved)) {
                        const text::Bookmark mark = inLeaf->bookmark();
                        position.offset = static_cast<int>(mark.start);
                    }
                }
            }
            // A leaf that begins (or, going back, ends) with a link: the
            // first stop is inside the link, not its placeholder.
            if (inlineUnit) {
                descendIfInline(position, resolved, forwards);
            }
        }
        ++moved;
    }
    if (moved == 0) {
        return 0;
    }
    if (endPoint == text::EndPoint::End) {
        end_ = position;
    } else if (endPoint == text::EndPoint::Start) {
        start_ = position;
    } else {
        start_ = position;
        end_ = position;
    }
    return forwards ? moved : -moved;
}

bool DocumentTextRange::find(std::string_view needle, bool caseSensitive, bool reverse) {
    if (needle.empty() || !start_.valid()) {
        return false;
    }
    const auto fold = [caseSensitive](std::string value) {
        if (!caseSensitive) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        }
        return value;
    };
    const std::string wanted = fold(std::string(needle));

    ObjectPtr leaf = start_.leaf;
    int from = start_.offset;
    for (int step = 0; step < kMaxWalk && leaf != nullptr; ++step) {
        if (!DocumentLeaves::isEmbeddedLeaf(*leaf)) {
            const std::string haystack = fold(leaf->textRange(0, -1));
            std::size_t at = std::string::npos;
            if (reverse) {
                const std::size_t limit = from > 0 ? static_cast<std::size_t>(from) - 1
                                                   : std::string::npos;
                if (from > 0) {
                    at = haystack.rfind(wanted, limit);
                }
            } else {
                at = haystack.find(wanted, static_cast<std::size_t>(from) + 1 <= haystack.size()
                                               ? static_cast<std::size_t>(from) + 1
                                               : haystack.size());
            }
            if (at != std::string::npos) {
                start_ = DocumentPosition{leaf, static_cast<int>(at)};
                end_ = DocumentPosition{leaf, static_cast<int>(at + wanted.size())};
                return true;
            }
        }
        leaf = reverse ? DocumentLeaves::previous(root_, leaf)
                       : DocumentLeaves::next(root_, leaf);
        if (leaf == nullptr) {
            break;
        }
        from = reverse ? leafLength(leaf) : -1;
    }
    return false;
}

bool DocumentTextRange::updateCaret() {
    if (!start_.valid() || DocumentLeaves::isEmbeddedLeaf(*start_.leaf)) {
        return false;
    }
    // Two steps, and both are needed: the leaf must have the focus before its
    // caret means anything, and a browser that moved the focus for us is the
    // normal case rather than an error.
    if (!start_.leaf->hasFocus()) {
        start_.leaf->setFocus();
    }
    if (text::TextRangePtr inLeaf = rangeFor(start_.leaf, start_.offset, start_.offset)) {
        return inLeaf->updateCaret();
    }
    return false;
}

bool DocumentTextRange::updateSelection() {
    // A selection spanning leaves cannot be expressed through AT-SPI at all:
    // `Text::set_selection` is per object. The part inside the start leaf is
    // set, which is what a browser can honour, and the rest is ALUS's own
    // virtual selection (`nav::CursorManager`).
    if (!start_.valid() || !end_.valid()) {
        return false;
    }
    if (!start_.leaf->sameAs(*end_.leaf)) {
        ALUS_DEBUG("document range: selection spans leaves; only the first is set natively");
    }
    const int to = start_.leaf->sameAs(*end_.leaf) ? end_.offset : leafLength(start_.leaf);
    if (text::TextRangePtr inLeaf = rangeFor(start_.leaf, start_.offset, to)) {
        return inLeaf->updateSelection();
    }
    return false;
}

text::Bookmark DocumentTextRange::bookmark() const {
    text::Bookmark mark;
    static const char kKind = 0;
    mark.kind = &kKind;
    if (!start_.valid()) {
        return mark;
    }
    // INSIDE A LINK (N117): the link is not a leaf, so the walk that restores
    // a bookmark would never find it. The PARENT's path goes with it after
    // ASCII RS, and the restore finds the parent first and the link among its
    // children. A range that only starts or ends inside a link is kept in the
    // parent's coordinates instead.
    if (const ObjectPtr parent = inlineParent(start_.leaf);
        parent != nullptr && end_.valid() && end_.leaf->sameAs(*start_.leaf)) {
        mark.start = start_.offset;
        mark.end = end_.offset;
        mark.data = start_.leaf->path() + "\x1e" + parent->path();
        return mark;
    }
    const DocumentPosition first = toParent(start_);
    const DocumentPosition last = end_.valid() ? toParent(end_, /*after=*/true) : first;
    mark.start = first.offset;
    mark.end = last.offset;
    // The leaf identity, so a bookmark survives the object cache being thrown
    // away between pump cycles -- which is what the N49 rule requires of
    // anything an object owns.
    mark.data = first.leaf->path();
    if (!last.leaf->sameAs(*first.leaf)) {
        mark.data += "\x1f" + last.leaf->path();
    }
    return mark;
}

bool DocumentTextRange::moveToBookmark(const text::Bookmark& mark) {
    if (!mark.valid() || mark.data.empty()) {
        return false;
    }
    if (const std::size_t rs = mark.data.find('\x1e'); rs != std::string::npos) {
        const std::string childPath = mark.data.substr(0, rs);
        const std::string parentPath = mark.data.substr(rs + 1);
        for (ObjectPtr leaf = DocumentLeaves::first(root_); leaf != nullptr;
             leaf = DocumentLeaves::next(root_, leaf)) {
            if (leaf->path() != parentPath) {
                continue;
            }
            for (const ObjectPtr& child : leaf->children()) {
                if (child != nullptr && child->path() == childPath) {
                    start_ = DocumentPosition{child, static_cast<int>(mark.start)};
                    end_ = DocumentPosition{child, static_cast<int>(mark.end)};
                    normalise(start_);
                    normalise(end_);
                    return true;
                }
            }
            // The link is gone (the page changed) but its line is not: stay
            // there rather than falling back to the top of the page.
            start_ = end_ = DocumentPosition{leaf, 0};
            return true;
        }
        return false;
    }
    const std::size_t separator = mark.data.find('\x1f');
    const std::string startPath = mark.data.substr(0, separator);
    const std::string endPath =
        separator == std::string::npos ? startPath : mark.data.substr(separator + 1);

    ObjectPtr startLeaf;
    ObjectPtr endLeaf;
    ObjectPtr leaf = DocumentLeaves::first(root_);
    for (int step = 0; step < kMaxWalk && leaf != nullptr; ++step) {
        if (leaf->path() == startPath) {
            startLeaf = leaf;
        }
        if (leaf->path() == endPath) {
            endLeaf = leaf;
        }
        if (startLeaf != nullptr && endLeaf != nullptr) {
            break;
        }
        leaf = DocumentLeaves::next(root_, leaf);
    }
    if (startLeaf == nullptr) {
        return false;
    }
    start_ = DocumentPosition{startLeaf, static_cast<int>(mark.start)};
    end_ = DocumentPosition{endLeaf != nullptr ? endLeaf : startLeaf,
                            static_cast<int>(mark.end)};
    normalise(start_);
    normalise(end_);
    return true;
}

std::vector<core::Rect> DocumentTextRange::boundingRects() const {
    std::vector<core::Rect> rects;
    for (const LeafSpan& span : leafSpans()) {
        if (DocumentLeaves::isEmbeddedLeaf(*span.leaf)) {
            const core::Rect& location = span.leaf->location();
            if (location.width > 0 && location.height > 0) {
                rects.push_back(location);
            }
            continue;
        }
        if (text::TextRangePtr inLeaf = rangeFor(span.leaf, span.start, span.end)) {
            for (const core::Rect& rect : inLeaf->boundingRects()) {
                rects.push_back(rect);
            }
        }
    }
    return rects;
}

std::optional<core::Point> DocumentTextRange::pointAtStart() const {
    if (!start_.valid()) {
        return std::nullopt;
    }
    if (DocumentLeaves::isEmbeddedLeaf(*start_.leaf)) {
        const core::Rect& location = start_.leaf->location();
        if (location.width <= 0 && location.height <= 0) {
            return std::nullopt;
        }
        return core::Point{location.x, location.y};
    }
    if (text::TextRangePtr inLeaf = rangeFor(start_.leaf, start_.offset, start_.offset)) {
        return inLeaf->pointAtStart();
    }
    return std::nullopt;
}

std::optional<int> DocumentTextRange::unitIndex(text::TextUnit unit) const {
    if (unit != text::TextUnit::Line || !start_.valid()) {
        return std::nullopt;
    }
    // The line number WITHIN THE DOCUMENT: the lines of every leaf before this
    // one, plus the line inside it. One walk of the document, so the caller
    // must want it (alus+numpad5 does; reading a line does not).
    int total = 0;
    ObjectPtr leaf = DocumentLeaves::first(root_);
    for (int step = 0; step < kMaxWalk && leaf != nullptr; ++step) {
        if (leaf->sameAs(*start_.leaf)) {
            if (text::TextRangePtr inLeaf = rangeFor(leaf, start_.offset, start_.offset)) {
                total += inLeaf->unitIndex(text::TextUnit::Line).value_or(1);
            } else {
                total += 1;
            }
            return total;
        }
        if (text::TextRangePtr inLeaf = rangeFor(leaf, 0, 0)) {
            total += inLeaf->unitCount(text::TextUnit::Line).value_or(1);
        } else {
            total += 1;
        }
        leaf = DocumentLeaves::next(root_, leaf);
    }
    return std::nullopt;
}

std::optional<int> DocumentTextRange::unitCount(text::TextUnit unit) const {
    if (unit != text::TextUnit::Line) {
        return std::nullopt;
    }
    int total = 0;
    ObjectPtr leaf = DocumentLeaves::first(root_);
    for (int step = 0; step < kMaxWalk && leaf != nullptr; ++step) {
        if (text::TextRangePtr inLeaf = rangeFor(leaf, 0, 0)) {
            total += inLeaf->unitCount(text::TextUnit::Line).value_or(1);
        } else {
            total += 1;
        }
        leaf = DocumentLeaves::next(root_, leaf);
    }
    return total > 0 ? std::optional<int>{total} : std::nullopt;
}

std::string DocumentTextRange::locationText() const {
    const std::optional<int> index = unitIndex(text::TextUnit::Line);
    const std::optional<int> count = unitCount(text::TextUnit::Line);
    if (!index.has_value() || !count.has_value() || *count <= 0) {
        return {};
    }
    // Translators: the caret's position in a document; the first %s is the
    // line number and the second the number of lines. ONE pattern rather than
    // three concatenated pieces, because the word order is not English's in
    // every language.
    return i18n::substitute(_("line %s of %s"), std::to_string(*index),
                            std::to_string(*count));
}

}  // namespace alus::nav
