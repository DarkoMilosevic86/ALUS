// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/text_region.hpp.

#include "alus/braille/text_region.hpp"

#include "alus/braille/formatting.hpp"
#include "alus/braille/labels.hpp"
#include "alus/braille/properties.hpp"
#include "alus/core/log.hpp"
#include "alus/nav/browse_mode.hpp"
#include "alus/nav/cursor_manager.hpp"
#include "alus/nav/review.hpp"
#include "alus/text/offsets.hpp"

#include <algorithm>

namespace alus::braille {
namespace {

int codepoints(std::string_view text) { return static_cast<int>(text::codepointCount(text)); }

// The line terminators NVDA strips off the end of a reading unit. A braille
// display has no newline, and a line that ends in one has a blank cell at the
// end that the cursor can sit on and nothing can be routed to.
bool isLineEnding(std::uint32_t codepoint) {
    return codepoint == '\r' || codepoint == '\n' || codepoint == '\0' || codepoint == '\v' ||
           codepoint == '\f';
}

}  // namespace

// ---------------------------------------------------------------------------
// The three sources
// ---------------------------------------------------------------------------

ObjectTextSource::ObjectTextSource(a11y::ObjectPtr object) : object_(std::move(object)) {}

text::TextRangePtr ObjectTextSource::selection() const {
    if (object_ == nullptr || !object_->isAlive()) {
        return nullptr;
    }
    if (text::TextRangePtr range = object_->makeTextRange(text::Position::Selection)) {
        return range;
    }
    // NVDA's own fallback: a control with no selection and no caret still has
    // a first position, and showing its first line is better than showing
    // nothing at all.
    return object_->makeTextRange(text::Position::First);
}

bool ObjectTextSource::setCursor(const text::TextRange& at) {
    text::TextRangePtr copy = at.copy();
    return copy != nullptr && copy->updateCaret();
}

bool ObjectTextSource::isMultiline() const {
    if (object_ == nullptr) {
        return false;
    }
    // A terminal and a document are inherently multi-line and do not carry
    // the state (NVDA's own note).
    const a11y::Role role = object_->role();
    return role == a11y::Role::Terminal || role == a11y::Role::Document ||
           object_->states().has(a11y::State::MultiLine);
}

bool ObjectTextSource::activate(const text::TextRange& /*at*/) {
    if (object_ == nullptr || !object_->isAlive() || object_->actionCount() == 0) {
        return false;
    }
    return object_->doAction(0);
}

bool ObjectTextSource::isAlive() const { return object_ != nullptr && object_->isAlive(); }

CursorManagerSource::CursorManagerSource(nav::BrowseMode& browse)
    : browse_(&browse), object_(browse.rootObject()) {}

text::TextRangePtr CursorManagerSource::selection() const {
    if (browse_ == nullptr) {
        return nullptr;
    }
    if (text::TextRangePtr selected = browse_->cursor().selection()) {
        return selected;
    }
    return browse_->cursor().caretRange();
}

bool CursorManagerSource::setCursor(const text::TextRange& at) {
    return browse_ != nullptr && browse_->cursor().setCaret(at);
}

bool CursorManagerSource::activate(const text::TextRange& /*at*/) {
    return browse_ != nullptr && browse_->activatePosition();
}

bool CursorManagerSource::isAlive() const { return browse_ != nullptr && browse_->isAlive(); }

ReviewSource::ReviewSource(nav::Review& review, a11y::ObjectPtr object)
    : review_(&review), object_(std::move(object)) {}

text::TextRangePtr ReviewSource::selection() const {
    if (review_ == nullptr) {
        return nullptr;
    }
    const text::TextRange* position = review_->position();
    return position != nullptr ? position->copy() : nullptr;
}

bool ReviewSource::setCursor(const text::TextRange& at) {
    if (review_ == nullptr) {
        return false;
    }
    return review_->setPosition(at.copy(), /*clearNavigator=*/false);
}

bool ReviewSource::activate(const text::TextRange& /*at*/) {
    if (review_ == nullptr) {
        return false;
    }
    const a11y::ObjectPtr object = review_->objectAtPosition();
    if (object == nullptr || !object->isAlive() || object->actionCount() == 0) {
        return false;
    }
    return object->doAction(0);
}

bool ReviewSource::isAlive() const { return review_ != nullptr; }

// ---------------------------------------------------------------------------
// TextInfoRegion
// ---------------------------------------------------------------------------

TextInfoRegion::TextInfoRegion(RegionHost& host, std::unique_ptr<TextSource> source,
                               bool review)
    : Region(host), source_(std::move(source)), review_(review) {}

TextInfoRegion::~TextInfoRegion() = default;

a11y::ObjectPtr TextInfoRegion::object() const {
    return source_ != nullptr ? source_->object() : nullptr;
}

bool TextInfoRegion::isAlive() const { return source_ != nullptr && source_->isAlive(); }

text::TextUnit TextInfoRegion::readingUnit() const {
    return host().brailleSettings().readByParagraph ? text::TextUnit::Paragraph
                                                    : text::TextUnit::Line;
}

std::string TextInfoRegion::describe() const {
    return std::string{review_ ? "review text " : "text "} + "\"" + rawText + "\"";
}

Typeform TextInfoRegion::typeformFor(const text::FormatField& field) const {
    const Settings& settings = host().brailleSettings();
    if (!settings.reportFontAttributes ||
        settings.fontFormattingDisplay != FontFormattingDisplay::Liblouis) {
        return Typeform::PlainText;
    }
    Typeform typeform = Typeform::PlainText;
    if (field.getBool("bold").value_or(false)) {
        typeform |= Typeform::Bold;
    }
    if (field.getBool("italic").value_or(false)) {
        typeform |= Typeform::Italic;
    }
    if (field.getBool("underline").value_or(false)) {
        typeform |= Typeform::Underline;
    }
    return typeform;
}

void TextInfoRegion::appendContent(const std::string& text, Typeform typeform) {
    const int length = codepoints(text);
    rawText += text;
    rawTextTypeforms.insert(rawTextTypeforms.end(), static_cast<std::size_t>(length),
                            typeform);
    for (int step = 0; step < length; ++step) {
        rawToContentPos_.push_back(currentContentPos_ + step);
    }
    currentContentPos_ += length;
}

void TextInfoRegion::addFieldText(const std::string& text, std::optional<int> contentPos,
                                  bool separate) {
    std::string piece = text;
    if (separate && !rawText.empty()) {
        piece = std::string{kTextSeparator} + piece;
    }
    const int length = codepoints(piece);
    rawText += piece;
    rawTextTypeforms.insert(rawTextTypeforms.end(), static_cast<std::size_t>(length),
                            Typeform::PlainText);
    // Field text maps to the START of the field's content, so a routing key on
    // "lst" moves the caret to the first character of the list item rather
    // than nowhere.
    const int mapped = contentPos.value_or(currentContentPos_);
    for (int step = 0; step < length; ++step) {
        rawToContentPos_.push_back(mapped);
    }
}

void TextInfoRegion::addTextWithFields(const text::TextRange& range, bool isSelection) {
    const text::FormatReportSettings& format = host().formatSettings();
    const Settings& braille = host().brailleSettings();
    const a11y::Settings& objectSettings = host().objectSettings();

    bool moveCursorToFirstContent = !isSelection && cursorPos.has_value();
    std::vector<text::ControlField> stack;
    Typeform typeform = Typeform::PlainText;
    // NVDA: `inClickable`. Inside a clickable field, further nested clickables
    // are not reported -- otherwise a clickable div full of clickable spans
    // fills the display with "clk".
    bool inClickable = false;

    text::TextWithFields items;
    if (!range.isCollapsed()) {
        items = range.getTextWithFields(format);
        // Phase 6's own rule, and for the same reason speech applies it
        // (a11y/text_speech.cpp): the TRAILING `controlEnd` commands close
        // the containers the range merely ENDS INSIDE, not ones that really
        // ended. Reporting them would say "lst end" on every line of a list.
        // This is ALUS's equivalent of NVDA's `_endOfNode` flag.
        while (!items.empty() && items.back().isCommand() &&
               items.back().command->kind() == text::FieldCommand::Kind::ControlEnd) {
            items.pop_back();
        }
    }

    for (const text::TextOrField& item : items) {
        if (item.isText()) {
            // Text breaks a run of clickables.
            inClickable = false;
            formatFieldAtStart_ = false;
            const std::string& content = *item.text;
            if (content.empty()) {
                continue;
            }
            if (endsWithField_) {
                // The last thing added was field text, so separate it from
                // the content it describes.
                appendContent(std::string{kTextSeparator}, Typeform::PlainText);
                // The separator is not content: undo the content advance so
                // the map stays honest.
                --currentContentPos_;
                rawToContentPos_.back() = currentContentPos_;
            }
            if (isSelection && !selectionStart.has_value()) {
                selectionStart = codepoints(rawText);
            } else if (moveCursorToFirstContent) {
                // The cursor was sitting on field text; put it on the first
                // real character instead.
                cursorPos = codepoints(rawText);
                moveCursorToFirstContent = false;
            }
            appendContent(content, typeform);
            if (isSelection) {
                selectionEnd = codepoints(rawText);
            }
            endsWithField_ = false;
            continue;
        }
        if (!item.isCommand()) {
            continue;
        }
        const text::FieldCommand& command = *item.command;
        std::string fieldText;
        text::PresentationCategory category = text::PresentationCategory::Layout;

        if (command.kind() == text::FieldCommand::Kind::FormatChange) {
            const text::FormatField* field = command.format();
            if (field == nullptr) {
                continue;
            }
            typeform = typeformFor(*field);
            fieldText =
                getFormatFieldBraille(*field, formatCache_, formatFieldAtStart_, format,
                                      braille);
            if (fieldText.empty()) {
                continue;
            }
            addFieldText(fieldText, currentContentPos_);
            endsWithField_ = true;
            continue;
        }

        if (command.kind() == text::FieldCommand::Kind::ControlStart) {
            const text::ControlField* field = command.control();
            if (field == nullptr) {
                continue;
            }
            const bool alreadyRendered =
                !field->uniqueId.empty() && renderedFields_.count(field->uniqueId) != 0;
            if (skipFieldsNotAtStartOfNode_ && alreadyRendered) {
                // The second and third chunks of the reading unit re-open the
                // containers the first chunk already rendered. NVDA tests
                // `_startOfNode`; ALUS can do better, because §9.1 gave every
                // field a stable identity -- so the test is "have I shown
                // this exact control in this update" (claude/FAZA7.md O5).
                stack.push_back(*field);
                continue;
            }
            std::vector<std::string> pieces;
            if (!inClickable && format.reportClickable &&
                field->states.has(a11y::State::Clickable)) {
                const text::PresentationCategory clickableCategory =
                    field->getPresentationCategory(stack, format);
                if (clickableCategory == text::PresentationCategory::Layout) {
                    // Nothing else about this field is interesting, so the
                    // fact that it can be clicked is the news.
                    pieces.emplace_back(statePositiveAbbreviation(a11y::State::Clickable));
                }
                inClickable = true;
            }
            if (const std::optional<std::string> text =
                    getControlFieldBraille(*field, stack, /*reportStart=*/true, format,
                                           objectSettings, braille, &category);
                text.has_value() && !text->empty()) {
                pieces.push_back(*text);
            }
            for (const std::string& piece : pieces) {
                if (!fieldText.empty()) {
                    fieldText += kTextSeparator;
                }
                fieldText += piece;
            }
            stack.push_back(*field);
            if (!field->uniqueId.empty()) {
                renderedFields_.insert(field->uniqueId);
            }
            if (fieldText.empty()) {
                continue;
            }
            if (category == text::PresentationCategory::Marker) {
                // A marker's text IS what the user cares about -- a separator,
                // a footnote -- so the cursor belongs on it rather than on the
                // content behind it.
                int fieldStart = codepoints(rawText);
                if (fieldStart > 0) {
                    ++fieldStart;  // the separator addFieldText will prepend
                }
                if (isSelection && !selectionStart.has_value()) {
                    selectionStart = fieldStart;
                } else if (moveCursorToFirstContent) {
                    cursorPos = fieldStart;
                    moveCursorToFirstContent = false;
                }
            }
            addFieldText(fieldText, currentContentPos_);
            endsWithField_ = true;
            continue;
        }

        // ControlEnd.
        inClickable = false;
        if (stack.empty()) {
            continue;
        }
        const text::ControlField closed = stack.back();
        stack.pop_back();
        if (const std::optional<std::string> text =
                getControlFieldBraille(closed, stack, /*reportStart=*/false, format,
                                       objectSettings, braille, &category);
            text.has_value() && !text->empty()) {
            // The end text maps to the LAST character of the field's content.
            addFieldText(*text, std::max(currentContentPos_ - 1, 0));
            endsWithField_ = true;
        }
    }

    if (isSelection && !selectionStart.has_value()) {
        // The "selection" was collapsed: it is a cursor.
        cursorPos = codepoints(rawText);
    }
    // Only the first chunk renders fields that are not at the start of their
    // node; see the member's comment.
    skipFieldsNotAtStartOfNode_ = true;
}

void TextInfoRegion::update() {
    const Settings& braille = host().brailleSettings();
    const text::TextUnit unit = readingUnit();

    rawText.clear();
    rawTextTypeforms.clear();
    rawToContentPos_.clear();
    currentContentPos_ = 0;
    cursorPos.reset();
    selectionStart.reset();
    selectionEnd.reset();
    formatFieldAtStart_ = true;
    skipFieldsNotAtStartOfNode_ = false;
    endsWithField_ = false;
    renderedFields_.clear();
    inputIndicatorStart_.reset();
    readingInfo_.reset();

    text::TextRangePtr reading = source_ != nullptr ? source_->selection() : nullptr;
    if (reading == nullptr) {
        // Nothing to show. A single space so the display is blank rather than
        // stale, and so the cursor has a cell to sit on.
        rawText = std::string{kTextSeparator};
        rawTextTypeforms.assign(1, Typeform::PlainText);
        rawToContentPos_.assign(1, 0);
        cursorPos = 0;
        Region::update();
        return;
    }

    text::TextRangePtr selection = reading->copy();
    if (!selection->isCollapsed()) {
        const bool anchoredAtStart = source_->selectionAnchoredAtStart();
        if (anchoredAtStart) {
            // The end of a range is exclusive; make it inclusive before
            // collapsing to it, or the reading unit is the one AFTER the
            // selection whenever the selection ends at a line boundary.
            reading->move(text::TextUnit::Character, -1, text::EndPoint::End);
        }
        reading->collapse(anchoredAtStart);
        reading->expand(unit);
        // Restrict the selection to the reading unit.
        if (selection->compareEndPoints(*reading, text::EndPointPair::StartToStart) < 0) {
            selection->setEndPoint(*reading, text::EndPointPair::StartToStart);
        }
        if (selection->compareEndPoints(*reading, text::EndPointPair::EndToEnd) > 0) {
            selection->setEndPoint(*reading, text::EndPointPair::EndToEnd);
        }
    } else {
        reading->expand(unit);
    }

    // The reading unit in three pieces; see the file comment.
    text::TextRangePtr chunk = reading->copy();
    chunk->collapse();
    chunk->setEndPoint(*selection, text::EndPointPair::EndToStart);
    addTextWithFields(*chunk, /*isSelection=*/false);

    // Untranslated braille input goes in front of the selection, so the user
    // sees the cells they have typed where the word is being built.
    const std::string untranslated = host().untranslatedBraille();
    std::optional<int> indicatorStartRaw;
    int indicatorEndRaw = 0;
    if (!untranslated.empty()) {
        indicatorStartRaw = codepoints(rawText);
        addFieldText(std::string{kInputStartIndicator} + untranslated +
                         std::string{kInputEndIndicator},
                     std::nullopt, /*separate=*/false);
        indicatorEndRaw = codepoints(rawText);
    }

    addTextWithFields(*selection, /*isSelection=*/true);

    chunk->setEndPoint(*reading, text::EndPointPair::EndToEnd);
    chunk->setEndPoint(*selection, text::EndPointPair::StartToEnd);
    addTextWithFields(*chunk, /*isSelection=*/false);

    // Strip the line terminators off the end.
    {
        const text::OffsetConverter converter{rawText};
        std::size_t length = converter.codepointLength();
        while (length > 0) {
            const std::uint32_t last =
                text::codepointAt(rawText, converter.toByte(length - 1));
            if (!isLineEnding(last)) {
                break;
            }
            --length;
        }
        if (length < converter.codepointLength()) {
            rawText.resize(converter.toByte(length));
            if (length < rawToContentPos_.size()) {
                currentContentPos_ = rawToContentPos_[length];
            }
            rawTextTypeforms.resize(length);
            // NVDA leaves `_rawToContentPos` untrimmed here ("we'll only ever
            // ask for indexes valid in rawText"), but then APPENDS the
            // trailing space's entry to it -- which lands past the text and
            // leaves the last cell mapped to the stripped newline instead of
            // to the end of the line. Trimming costs nothing and makes the
            // map exact, so a routing key on the last cell of a line lands at
            // the end of that line (claude/FAZA7.md O6).
            rawToContentPos_.resize(length);
        }
    }

    int rawLength = codepoints(rawText);
    if (rawLength == 0 || !endsWithField_) {
        // There is nothing left, or the last thing added can hold a cursor:
        // add a cell for the cursor to sit on at the end of the reading unit.
        rawText += kTextSeparator;
        ++rawLength;
        rawTextTypeforms.push_back(Typeform::PlainText);
        rawToContentPos_.push_back(currentContentPos_);
    }
    if (cursorPos.has_value() && *cursorPos >= rawLength) {
        cursorPos = rawLength - 1;
    }

    // If this is not the start of the object, the context in front of the
    // display no longer applies: hide it.
    if (const a11y::ObjectPtr object = source_->object();
        object != nullptr && object->isAlive()) {
        if (const text::TextRangePtr first = object->makeTextRange(text::Position::First)) {
            hidePreviousRegions =
                first->compareEndPoints(*reading, text::EndPointPair::StartToStart) < 0;
        }
    }
    // Do not clear focusToHardLeft: the focus-context builder may already have
    // set it for this region.
    if (!focusToHardLeft) {
        focusToHardLeft = source_->isMultiline();
    }

    readingInfo_ = std::move(reading);
    Region::update();

    if (indicatorStartRaw.has_value() && !rawToBraillePos.empty()) {
        const auto cellAt = [this](int rawPosition) {
            const int clamped =
                std::clamp(rawPosition, 0, static_cast<int>(rawToBraillePos.size()) - 1);
            return rawToBraillePos[static_cast<std::size_t>(clamped)];
        };
        inputIndicatorStart_ = cellAt(*indicatorStartRaw);
        inputIndicatorEnd_ = cellAt(indicatorEndRaw);
        inputStart_ = *inputIndicatorStart_ + codepoints(std::string{kInputStartIndicator});
        inputEnd_ = inputIndicatorEnd_ - codepoints(std::string{kInputEndIndicator});
        brailleCursorPos = inputStart_ + host().untranslatedCursorPos();
    }
    (void)braille;
}

text::TextRangePtr TextInfoRegion::rangeForBraillePos(int braillePos) const {
    if (readingInfo_ == nullptr || brailleToRawPos.empty()) {
        return nullptr;
    }
    const int cell =
        std::clamp(braillePos, 0, static_cast<int>(brailleToRawPos.size()) - 1);
    const int rawPosition = brailleToRawPos[static_cast<std::size_t>(cell)];
    int contentPos = 0;
    if (!rawToContentPos_.empty()) {
        const int clamped =
            std::clamp(rawPosition, 0, static_cast<int>(rawToContentPos_.size()) - 1);
        contentPos = rawToContentPos_[static_cast<std::size_t>(clamped)];
    }
    text::TextRangePtr destination = readingInfo_->copy();
    if (destination == nullptr) {
        return nullptr;
    }
    destination->collapse();
    if (contentPos > 0) {
        destination->move(text::TextUnit::Character, contentPos);
    }
    return destination;
}

void TextInfoRegion::routeTo(int braillePos) {
    if (inputIndicatorStart_.has_value() && braillePos >= *inputIndicatorStart_ &&
        braillePos < inputIndicatorEnd_) {
        // The user is moving within braille they have typed but not yet
        // translated. Routing to either indicator means "the nearest end of
        // the input", which is what makes the two-cell brackets usable.
        int target = braillePos;
        target = std::max(target, inputStart_);
        target = std::min(target, inputEnd_);
        host().setUntranslatedCursorPos(target - inputStart_);
        brailleCursorPos = inputStart_ + host().untranslatedCursorPos();
        host().updateForUntranslatedInput();
        return;
    }
    text::TextRangePtr destination = rangeForBraillePos(braillePos);
    if (destination == nullptr) {
        return;
    }
    routeToRange(*destination);
}

void TextInfoRegion::routeToRange(const text::TextRange& at) {
    // With a selection showing, `brailleCursorPos` is absent. Then a routing
    // key MOVES the cursor (dropping the selection) rather than activating --
    // NVDA weighed both and chose moving as the more intuitive one.
    if (brailleCursorPos.has_value()) {
        if (const text::TextRangePtr cursor = rangeForBraillePos(*brailleCursorPos)) {
            if (at.compareEndPoints(*cursor, text::EndPointPair::StartToStart) == 0) {
                // The cursor is already here, so the second press activates.
                source_->activate(at);
                return;
            }
        }
    }
    if (!source_->setCursor(at)) {
        return;
    }
    if (review_ && host().routingShouldMoveSystemCaret()) {
        // Tethered to review with the caret following: push the position into
        // the application as well, so typing continues where the user read.
        text::TextRangePtr copy = at.copy();
        if (copy != nullptr) {
            copy->updateCaret();
        }
    }
    if (host().brailleSettings().speakOnRouting) {
        host().speakCharacterAt(at);
    }
}

void TextInfoRegion::speakAfterUnitMove(const text::TextRange& at) {
    if (host().brailleSettings().speakOnNavigatingByUnit) {
        host().speakUnitAt(at, readingUnit());
    }
}

void TextInfoRegion::nextLine() {
    if (readingInfo_ == nullptr) {
        return;
    }
    text::TextRangePtr destination = readingInfo_->copy();
    if (destination == nullptr) {
        return;
    }
    bool collapseToEnd = false;
    if (destination->move(readingUnit(), 1) == 0) {
        if (!review_ && source_->supportsPageTurns() && source_->turnPage(false)) {
            if (const a11y::ObjectPtr object = source_->object()) {
                if (text::TextRangePtr first =
                        object->makeTextRange(text::Position::First)) {
                    destination = std::move(first);
                }
            }
        } else {
            // The end of the text: stop scrolling by itself rather than
            // sitting on the last line forever.
            host().stopAutoScroll();
            collapseToEnd = true;
        }
    }
    destination->collapse(collapseToEnd);
    source_->setCursor(*destination);
    speakAfterUnitMove(*destination);
}

void TextInfoRegion::previousLine(bool toStart) {
    if (readingInfo_ == nullptr) {
        return;
    }
    text::TextRangePtr destination = readingInfo_->copy();
    if (destination == nullptr) {
        return;
    }
    destination->collapse();
    // NVDA: moving to the END of the previous unit is one CHARACTER back, not
    // one unit -- a unit step would land on the start of the previous unit and
    // the display would show its beginning instead of its end.
    const text::TextUnit unit = toStart ? readingUnit() : text::TextUnit::Character;
    if (destination->move(unit, -1) == 0) {
        if (!review_ && source_->supportsPageTurns() && source_->turnPage(true)) {
            if (const a11y::ObjectPtr object = source_->object()) {
                if (text::TextRangePtr last = object->makeTextRange(text::Position::Last)) {
                    destination = std::move(last);
                    destination->expand(unit);
                }
            }
        } else {
            return;
        }
    }
    destination->collapse();
    source_->setCursor(*destination);
    speakAfterUnitMove(*destination);
}

// ---------------------------------------------------------------------------
// The two named subclasses
// ---------------------------------------------------------------------------

CursorRegion::CursorRegion(RegionHost& host, nav::BrowseMode& browse, bool review)
    : TextInfoRegion(host, std::make_unique<CursorManagerSource>(browse), review) {}

ReviewTextRegion::ReviewTextRegion(RegionHost& host, nav::Review& review,
                                   a11y::ObjectPtr object)
    : TextInfoRegion(host, std::make_unique<ReviewSource>(review, std::move(object)),
                     /*review=*/true) {}

}  // namespace alus::braille
