// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/nav/text_commands.hpp.

#include "alus/nav/text_commands.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_speech.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/gesture.hpp"
#include "alus/nav/paragraph_nav.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/say_all.hpp"
#include "alus/nav/service.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/text/offsets.hpp"

#include <cstdio>

namespace alus::nav {
namespace {

std::string hexOf(std::uint32_t value) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%x", value);
    return buffer;
}

}  // namespace

TextCommands::TextCommands(Service& service, const script::Catalog* catalog,
                           script::Dispatcher* dispatcher)
    // It answers to NVDA's GlobalCommands, which is what makes the generated
    // command table's keys and descriptions apply (see the header).
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      service_(service),
      dispatcher_(dispatcher) {
    ALUS_SCRIPT(review_top);
    ALUS_SCRIPT(review_previousLine);
    ALUS_SCRIPT(review_currentLine);
    ALUS_SCRIPT(review_nextLine);
    ALUS_SCRIPT(review_bottom);
    ALUS_SCRIPT(review_previousWord);
    ALUS_SCRIPT(review_currentWord);
    ALUS_SCRIPT(review_nextWord);
    ALUS_SCRIPT(review_startOfLine);
    ALUS_SCRIPT(review_previousCharacter);
    ALUS_SCRIPT(review_currentCharacter);
    ALUS_SCRIPT(review_nextCharacter);
    ALUS_SCRIPT(review_endOfLine);
    ALUS_SCRIPT(review_previousPage);
    ALUS_SCRIPT(review_nextPage);
    ALUS_SCRIPT(review_sayAll);
    ALUS_SCRIPT(reviewMode_next);
    ALUS_SCRIPT(reviewMode_previous);
    ALUS_SCRIPT(review_markStartForCopy);
    ALUS_SCRIPT(review_moveToStartMarkedForCopy);
    ALUS_SCRIPT(review_copy);
    ALUS_SCRIPT(review_startOfSelection);
    ALUS_SCRIPT(review_endOfSelection);
    ALUS_SCRIPT(sayAll);
    ALUS_SCRIPT(reportCurrentLine);
    ALUS_SCRIPT(reportCurrentSelection);
    ALUS_SCRIPT(reportCaretLocation);
    ALUS_SCRIPT(reportReviewCursorLocation);
    ALUS_SCRIPT(reportCaretLanguage);
    ALUS_SCRIPT(reportFormatting);
    ALUS_SCRIPT(reportFormattingAtCaret);
    ALUS_SCRIPT(reportFormattingAtReview);
    ALUS_SCRIPT(reportOrShowFormattingAtCaret);
    ALUS_SCRIPT(speakForeground);
    ALUS_SCRIPT(moveToParentTreeInterceptor);
    ALUS_SCRIPT(caretPos_currentDimensions);
    ALUS_SCRIPT(cycleParagraphStyle);
    ALUS_SCRIPT(toggleReportLineIndentation);
}

TextCommands::~TextCommands() = default;

// ---------------------------------------------------------------------------
// The shared helpers
// ---------------------------------------------------------------------------

int TextCommands::repeatCount() const {
    return dispatcher_ != nullptr ? dispatcher_->lastScriptRepeatCount() : 0;
}

void TextCommands::say(const std::string& text) const {
    service_.message(text);
}

text::TextRange* TextCommands::reviewOrComplain() const {
    Review* review = service_.review();
    if (review == nullptr) {
        return nullptr;
    }
    text::TextRange* position = review->position();
    if (position == nullptr) {
        // Translators: there is nothing to review.
        say(_("No review position"));
        return nullptr;
    }
    return position;
}

void TextCommands::moveReview(text::TextUnit unit, int direction, const char* edgeMessage) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    Review* review = service_.review();
    text::TextRangePtr original = position->copy();
    original->collapse();
    text::TextRangePtr moved = original->copy();
    const int result = moved->move(unit, direction);
    text::TextRangePtr newUnit = moved->copy();
    newUnit->expand(unit);

    // NVDA #12808: some implementations move one unit FURTHER than they
    // should and land on the exclusive end of the text. The move is accepted
    // only when the new unit really starts past (or before) the old position.
    const int compared =
        newUnit->compareEndPoints(*original, text::EndPointPair::StartToStart);
    const bool genuine = direction > 0 ? compared > 0 : compared < 0;
    if (result == 0 || !genuine) {
        say(edgeMessage);
    } else {
        review->setPosition(moved->copy(), /*clearNavigator=*/false);
    }
    service_.speakRange(*newUnit, unit, a11y::OutputReason::Caret,
                        review->objectAtPosition());
}

void TextCommands::reportReviewUnit(text::TextUnit unit) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    text::TextRangePtr range = position->copy();
    range->expand(unit);
    const int count = repeatCount();
    if (count == 0) {
        service_.speakRange(*range, unit, a11y::OutputReason::Caret,
                            service_.review()->objectAtPosition());
        return;
    }
    speech::Presenter* presenter = service_.presenter();
    if (presenter == nullptr) {
        say(range->text());
        return;
    }
    // Twice spells, three times describes each character.
    a11y::spellTextRange(*presenter, *range, service_.formatSettings(),
                         /*useCharacterDescriptions=*/count > 1);
}

void TextCommands::moveReviewToEdge(text::TextUnit unit, bool toEnd, bool wholeText) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    Review* review = service_.review();
    text::TextRangePtr range = position->copy();
    if (wholeText) {
        // review_top / review_bottom: the first or last unit of the whole text.
        range->expand(text::TextUnit::Story);
        range->collapse(toEnd);
        if (toEnd) {
            // The end of the story is past the last character; the last LINE
            // is what the user asked for.
            range->expand(unit);
            range->collapse();
        }
    } else {
        // review_startOfLine / review_endOfLine: within the current unit.
        range->expand(unit);
        if (toEnd) {
            // NVDA moves to the LAST CHARACTER of the line, not past it.
            text::TextRangePtr end = range->copy();
            end->collapse(/*toEnd=*/true);
            if (end->move(text::TextUnit::Character, -1) == 0) {
                end = range->copy();
            }
            range = std::move(end);
        } else {
            range->collapse();
        }
    }
    review->setPosition(range->copy(), /*clearNavigator=*/false);
    text::TextRangePtr spoken = range->copy();
    spoken->expand(wholeText ? unit : text::TextUnit::Character);
    service_.speakRange(*spoken, wholeText ? unit : text::TextUnit::Character,
                        a11y::OutputReason::Caret, review->objectAtPosition());
}

// ---------------------------------------------------------------------------
// Review movement
// ---------------------------------------------------------------------------

void TextCommands::review_top(input::Gesture& /*gesture*/) {
    moveReviewToEdge(text::TextUnit::Line, /*toEnd=*/false, /*wholeText=*/true);
}

void TextCommands::review_bottom(input::Gesture& /*gesture*/) {
    moveReviewToEdge(text::TextUnit::Line, /*toEnd=*/true, /*wholeText=*/true);
}

void TextCommands::review_previousLine(input::Gesture& /*gesture*/) {
    // Translators: the review cursor is at the top line already.
    moveReview(text::TextUnit::Line, -1, _("Top"));
}

void TextCommands::review_nextLine(input::Gesture& /*gesture*/) {
    // Translators: the review cursor is at the bottom line already.
    moveReview(text::TextUnit::Line, 1, _("Bottom"));
}

void TextCommands::review_currentLine(input::Gesture& /*gesture*/) {
    reportReviewUnit(text::TextUnit::Line);
}

void TextCommands::review_previousWord(input::Gesture& /*gesture*/) {
    moveReview(text::TextUnit::Word, -1, "Top");
}

void TextCommands::review_nextWord(input::Gesture& /*gesture*/) {
    moveReview(text::TextUnit::Word, 1, "Bottom");
}

void TextCommands::review_currentWord(input::Gesture& /*gesture*/) {
    reportReviewUnit(text::TextUnit::Word);
}

void TextCommands::review_startOfLine(input::Gesture& /*gesture*/) {
    moveReviewToEdge(text::TextUnit::Line, /*toEnd=*/false, /*wholeText=*/false);
}

void TextCommands::review_endOfLine(input::Gesture& /*gesture*/) {
    moveReviewToEdge(text::TextUnit::Line, /*toEnd=*/true, /*wholeText=*/false);
}

void TextCommands::review_previousCharacter(input::Gesture& /*gesture*/) {
    moveReview(text::TextUnit::Character, -1, "Left");
}

void TextCommands::review_nextCharacter(input::Gesture& /*gesture*/) {
    moveReview(text::TextUnit::Character, 1, "Right");
}

void TextCommands::review_currentCharacter(input::Gesture& /*gesture*/) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    text::TextRangePtr range = position->copy();
    range->expand(text::TextUnit::Character);
    const int count = repeatCount();
    speech::Presenter* presenter = service_.presenter();
    if (count == 0) {
        service_.speakRange(*range, text::TextUnit::Character, a11y::OutputReason::Caret,
                            service_.review()->objectAtPosition());
        return;
    }
    if (presenter == nullptr) {
        say(range->text());
        return;
    }
    if (count == 1) {
        // Twice: the description ("a, Avala").
        a11y::spellTextRange(*presenter, *range, service_.formatSettings(),
                             /*useCharacterDescriptions=*/true);
        return;
    }
    // Three times: the numeric value, decimal and hexadecimal. This is what a
    // user chasing a strange character in a file actually needs.
    const std::string text = range->text();
    if (text.empty()) {
        service_.speakRange(*range, text::TextUnit::Character, a11y::OutputReason::Caret,
                            service_.review()->objectAtPosition());
        return;
    }
    const text::OffsetConverter converter(text);
    const std::size_t characters = converter.codepointLength();
    for (std::size_t index = 0; index < characters; ++index) {
        const std::uint32_t value = text::codepointAt(text, converter.toByte(index));
        presenter->speakMessage(std::to_string(value) + ",");
        if (characters == 1) {
            // NVDA reports hexadecimal only for a single character; with
            // several it is confusing rather than informative.
            presenter->speakSpelling(hexOf(value));
        }
    }
}

void TextCommands::review_previousPage(input::Gesture& /*gesture*/) {
    moveReview(text::TextUnit::Page, -1, "Top");
}

void TextCommands::review_nextPage(input::Gesture& /*gesture*/) {
    moveReview(text::TextUnit::Page, 1, "Bottom");
}

void TextCommands::review_sayAll(input::Gesture& /*gesture*/) {
    SayAll* sayAllHandler = service_.sayAll();
    if (sayAllHandler == nullptr || !sayAllHandler->readText(SayAllCursor::Review)) {
        // Translators: there is nothing to read from here.
        say(_("No text to read"));
    }
}

// ---------------------------------------------------------------------------
// The review mode
// ---------------------------------------------------------------------------

void TextCommands::reviewMode_next(input::Gesture& /*gesture*/) {
    Review* review = service_.review();
    if (review == nullptr) {
        return;
    }
    if (const std::optional<std::string_view> label = review->nextMode(/*previous=*/false)) {
        say(std::string(*label));
        return;
    }
    // Translators: the last review mode is already in use.
    say(_("No next review mode"));
}

void TextCommands::reviewMode_previous(input::Gesture& /*gesture*/) {
    Review* review = service_.review();
    if (review == nullptr) {
        return;
    }
    if (const std::optional<std::string_view> label = review->nextMode(/*previous=*/true)) {
        say(std::string(*label));
        return;
    }
    // Translators: the first review mode is already in use.
    say(_("No previous review mode"));
}

// ---------------------------------------------------------------------------
// Mark, select and copy
// ---------------------------------------------------------------------------

void TextCommands::review_markStartForCopy(input::Gesture& /*gesture*/) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    copyStartMarker_ = position->copy();
    copyStartMarker_->collapse();
    // Translators: the start of the text to copy has been marked.
    say(_("Start marked"));
}

void TextCommands::review_moveToStartMarkedForCopy(input::Gesture& /*gesture*/) {
    if (copyStartMarker_ == nullptr) {
        // Translators: no start marker has been set.
        say(_("No start marker set"));
        return;
    }
    Review* review = service_.review();
    if (review == nullptr) {
        return;
    }
    review->setPosition(copyStartMarker_->copy(), /*clearNavigator=*/false);
    text::TextRangePtr spoken = copyStartMarker_->copy();
    spoken->expand(text::TextUnit::Character);
    service_.speakRange(*spoken, text::TextUnit::Character, a11y::OutputReason::Caret,
                        review->objectAtPosition());
}

void TextCommands::review_copy(input::Gesture& /*gesture*/) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    if (copyStartMarker_ == nullptr) {
        say(_("No start marker set"));
        return;
    }
    if (copyStartMarker_->rangeKind() != position->rangeKind()) {
        // Translators: the marked position is in another object.
        say(_("The start marker must reside within the same object"));
        return;
    }
    text::TextRangePtr selection = copyStartMarker_->copy();
    selection->setEndPoint(*position, text::EndPointPair::EndToEnd);
    // NVDA includes the character the review cursor is on.
    text::TextRangePtr lastCharacter = position->copy();
    lastCharacter->expand(text::TextUnit::Character);
    selection->setEndPoint(*lastCharacter, text::EndPointPair::EndToEnd);
    if (selection->compareEndPoints(*selection, text::EndPointPair::StartToEnd) == 0) {
        // Translators: nothing has been selected.
        say(_("No text to copy"));
        return;
    }
    const int count = repeatCount();
    if (count == 0) {
        // Once: select it, so the user can hear what would be copied.
        if (!selection->updateSelection()) {
            // Translators: the object refused to change its selection.
            say(_("Cannot select text here"));
            return;
        }
        // Translators: the text has been selected.
        say(_("Selected"));
        return;
    }
    // Twice: copy to the clipboard. The clipboard is phase 9's work (it needs
    // a GUI toolkit connection), so this reports what it would copy and says
    // so rather than pretending -- FAZA5.md O9.
    const std::string text = selection->text();
    ALUS_INFO("review copy requested for {} bytes; the clipboard arrives in phase 9",
              text.size());
    // Translators: copying to the clipboard is not available yet.
    say(_("Clipboard support is not available yet"));
}

void TextCommands::review_startOfSelection(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    Review* review = service_.review();
    if (registry == nullptr || review == nullptr) {
        return;
    }
    const a11y::ObjectPtr focus = registry->api().focus();
    if (focus == nullptr) {
        return;
    }
    text::TextRangePtr selection = focus->makeTextRange(text::Position::Selection);
    if (selection == nullptr || selection->isCollapsed()) {
        // Translators: there is no selection.
        say(_("No selection"));
        return;
    }
    selection->collapse();
    review->setPosition(selection->copy(), /*clearNavigator=*/false);
    text::TextRangePtr spoken = selection->copy();
    spoken->expand(text::TextUnit::Character);
    service_.speakRange(*spoken, text::TextUnit::Character, a11y::OutputReason::Caret, focus);
}

void TextCommands::review_endOfSelection(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    Review* review = service_.review();
    if (registry == nullptr || review == nullptr) {
        return;
    }
    const a11y::ObjectPtr focus = registry->api().focus();
    if (focus == nullptr) {
        return;
    }
    text::TextRangePtr selection = focus->makeTextRange(text::Position::Selection);
    if (selection == nullptr || selection->isCollapsed()) {
        say(_("No selection"));
        return;
    }
    selection->collapse(/*toEnd=*/true);
    // The last character OF the selection, not the one after it.
    selection->move(text::TextUnit::Character, -1);
    review->setPosition(selection->copy(), /*clearNavigator=*/false);
    text::TextRangePtr spoken = selection->copy();
    spoken->expand(text::TextUnit::Character);
    service_.speakRange(*spoken, text::TextUnit::Character, a11y::OutputReason::Caret, focus);
}

// ---------------------------------------------------------------------------
// The caret
// ---------------------------------------------------------------------------

void TextCommands::sayAll(input::Gesture& /*gesture*/) {
    SayAll* sayAllHandler = service_.sayAll();
    if (sayAllHandler == nullptr) {
        return;
    }
    if (sayAllHandler->readText(SayAllCursor::Caret)) {
        return;
    }
    // NVDA falls back to reading OBJECTS when there is no text to read: on a
    // dialog or a list, say-all announces each control in turn.
    a11y::Registry* registry = service_.registry();
    if (registry != nullptr && sayAllHandler->readObjects(registry->api().focus())) {
        return;
    }
    // Translators: there is nothing to read.
    say(_("No text to read"));
}

void TextCommands::speakForeground(input::Gesture& /*gesture*/) {
    SayAll* sayAllHandler = service_.sayAll();
    a11y::Registry* registry = service_.registry();
    if (sayAllHandler == nullptr || registry == nullptr) {
        // Translators: there is nothing to read.
        say(_("No text to read"));
        return;
    }
    // The FOREGROUND object and not the focus: upstream reads the whole active
    // window, which is the point of the command -- a dialog read end to end
    // tells the user what the dialog says, and the focus alone tells them
    // about one control of it.
    const a11y::ObjectPtr foreground = registry->api().foreground();
    if (foreground == nullptr || !sayAllHandler->readObjects(foreground)) {
        say(_("No text to read"));
    }
}

void TextCommands::moveToParentTreeInterceptor(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    TreeInterceptorRegistry* interceptors = service_.interceptors();
    if (registry == nullptr || interceptors == nullptr) {
        // Translators: reported when the command needs a document and the
        // text layer is not running (`alus-daemon --no-nav`).
        say(_("Not in a document"));
        return;
    }
    const a11y::ObjectPtr focus = registry->api().focus();
    if (focus == nullptr) {
        say(_("Not in a document"));
        return;
    }

    // Upstream's two walks, and both are needed. The first leaves THIS
    // document: an object and its container may share one interceptor, and
    // stopping at the first parent would land back inside the document the
    // user asked to leave. The second skips the objects between two documents
    // -- an iframe's own wrapper has no interceptor of its own.
    const script::ScriptableObject* inner = focus->treeInterceptor();
    a11y::ObjectPtr parent = focus->parent();
    while (parent != nullptr && parent->treeInterceptor() == inner) {
        parent = parent->parent();
    }
    while (parent != nullptr && parent->treeInterceptor() == nullptr) {
        parent = parent->parent();
    }
    if (parent == nullptr) {
        // Translators: reported when there is no document around this one to
        // move out to (alus+control+space).
        say(_("No containing document"));
        return;
    }

    TreeInterceptor* outer = interceptors->forObject(*parent);
    const a11y::ObjectPtr target = outer != nullptr ? outer->rootObject() : parent;
    if (target == nullptr || !target->setFocus()) {
        // Never silent: the focus not moving is exactly the case a user
        // cannot see and would otherwise read as a dead key.
        ALUS_DEBUG_WARNING("moveToParentTreeInterceptor: {} refused the focus",
                           target != nullptr ? target->path() : "the containing document");
        // Translators: reported when the containing document refused the
        // focus, which only the application can decide.
        say(_("Could not move to the containing document"));
    }
}

void TextCommands::reportCurrentLine(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return;
    }
    a11y::ObjectPtr object = registry->api().focus();
    if (object == nullptr) {
        // Translators: there is no focused object.
        say(_("No focus"));
        return;
    }
    text::TextRangePtr range = object->makeTextRange(text::Position::Caret);
    if (range == nullptr) {
        // NVDA falls back to the navigator object's text, then to the object
        // itself: `alus+upArrow` on a button says the button.
        if (const auto& speak = registry->presentation().speakObject) {
            speak(*object, a11y::OutputReason::Query);
        }
        return;
    }
    range->expand(text::TextUnit::Line);
    const int count = repeatCount();
    if (count == 0) {
        service_.speakRange(*range, text::TextUnit::Line, a11y::OutputReason::Caret, object);
        return;
    }
    if (speech::Presenter* presenter = service_.presenter()) {
        a11y::spellTextRange(*presenter, *range, service_.formatSettings(),
                             /*useCharacterDescriptions=*/count > 1);
    }
}

void TextCommands::reportCurrentSelection(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return;
    }
    const a11y::ObjectPtr object = registry->api().focus();
    if (object == nullptr) {
        say(_("No focus"));
        return;
    }
    text::TextRangePtr selection = object->makeTextRange(text::Position::Selection);
    if (selection == nullptr || selection->isCollapsed()) {
        // Translators: no text is selected.
        say(_("No selection"));
        return;
    }
    speech::Presenter* presenter = service_.presenter();
    if (presenter == nullptr) {
        say(selection->text());
        return;
    }
    // Translators: what is selected, e.g. "hello world selected".
    presenter->speak(a11y::getSelectionMessageSpeech(_("%s selected"), selection->text()));
}

void TextCommands::reportCaretLocation(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return;
    }
    const a11y::ObjectPtr object = registry->api().focus();
    if (object == nullptr) {
        say(_("No focus"));
        return;
    }
    text::TextRangePtr range = object->makeTextRange(text::Position::Caret);
    if (range == nullptr) {
        // Translators: the focused object has no caret.
        say(_("No caret"));
        return;
    }
    const std::string location = range->locationText();
    // Translators: the caret's position could not be worked out.
    say(location.empty() ? _("Unable to determine the caret position") : location);
}

void TextCommands::caretPos_currentDimensions(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        say(_("No caret"));
        return;
    }
    const a11y::ObjectPtr object = registry->api().focus();
    if (object == nullptr) {
        say(_("No focus"));
        return;
    }
    text::TextRangePtr range = object->makeTextRange(text::Position::Caret);
    if (range == nullptr) {
        say(_("No caret"));
        return;
    }
    // ONE CHARACTER, expanded from the caret: a collapsed range has no
    // rectangle to report, and the platform answers about a character and not
    // about a cursor (AT-SPI's GetCharacterExtents).
    text::TextRangePtr character = range->copy();
    if (!character->expand(text::TextUnit::Character)) {
        say(_("No location information"));
        return;
    }
    const std::vector<core::Rect> rects = character->boundingRects();
    if (rects.empty() || rects.front().empty()) {
        // Translators: the caret has no position on the screen -- a terminal
        // that reports no geometry, or a control scrolled out of view.
        say(_("No location information"));
        return;
    }
    const core::Rect& rect = rects.front();
    // The same wording as navigatorObject_currentDimensions, deliberately: two
    // commands that answer the same question about different things should not
    // answer it in two shapes. Pixels and not a percentage of the screen for
    // the reason given there -- there is no screen size to divide by until
    // phase 10.
    say(std::to_string(rect.width) + " by " + std::to_string(rect.height) + " pixels at " +
        std::to_string(rect.x) + ", " + std::to_string(rect.y));
}

void TextCommands::reportReviewCursorLocation(input::Gesture& /*gesture*/) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    const std::string location = position->locationText();
    // Translators: the review cursor's position could not be worked out. The
    // same message as for the caret above -- and it was an untranslated
    // string literal here while its twin was translated.
    say(location.empty() ? _("Unable to determine the review cursor position") : location);
}

void TextCommands::reportCaretLanguage(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return;
    }
    const a11y::ObjectPtr object = registry->api().focus();
    if (object == nullptr) {
        say(_("No focus"));
        return;
    }
    text::TextRangePtr range = object->makeTextRange(text::Position::Caret);
    if (range == nullptr) {
        say(_("No caret"));
        return;
    }
    // The language attribute at the caret, which is what automatic language
    // switching is acting on -- so when the wrong voice is used, this is how
    // the user finds out why.
    text::FormatReportSettings settings = service_.formatSettings();
    settings.detectFormatAfterCursor = false;
    for (const text::TextOrField& item : range->getTextWithFields(settings)) {
        if (!item.isCommand() ||
            item.command->kind() != text::FieldCommand::Kind::FormatChange ||
            item.command->format() == nullptr) {
            continue;
        }
        const std::string_view language = item.command->format()->get("language");
        if (!language.empty()) {
            say(std::string(language));
            return;
        }
    }
    // Translators: the application does not report the language of its text.
    say(_("Language not supported"));
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

void TextCommands::reportFormattingFor(const text::TextRange& range, bool browseMode) {
    (void)browseMode;
    speech::Presenter* presenter = service_.presenter();
    if (presenter == nullptr) {
        return;
    }
    // The formatting command reports EVERYTHING, whatever the user's
    // verbosity settings say: they asked. NVDA does the same by passing a
    // format configuration with every report flag on.
    text::FormatReportSettings settings = service_.formatSettings();
    settings.reportFontName = true;
    settings.reportFontSize = true;
    settings.reportFontAttributes = true;
    settings.reportSuperscriptsAndSubscripts = true;
    settings.reportColor = true;
    settings.reportEmphasis = true;
    settings.reportStyle = true;
    settings.reportAlignment = true;
    settings.reportLineSpacing = true;
    settings.reportParagraphIndentation = true;
    settings.reportBorderStyle = true;
    settings.reportLineNumber = true;
    settings.reportRevisions = true;
    settings.reportHighlight = true;
    settings.reportComments = true;
    settings.reportBookmarks = true;
    settings.reportSpellingErrors = true;
    settings.reportGrammarErrors = true;
    settings.detectFormatAfterCursor = false;

    text::TextRangePtr expanded = range.copy();
    expanded->expand(text::TextUnit::Character);
    speech::SpeechSequence sequence;
    for (const text::TextOrField& item : expanded->getTextWithFields(settings)) {
        if (!item.isCommand() ||
            item.command->kind() != text::FieldCommand::Kind::FormatChange ||
            item.command->format() == nullptr) {
            continue;
        }
        // No cache: every attribute is reported, not only what changed.
        speech::SpeechSequence part = a11y::getFormatFieldSpeech(
            *item.command->format(), nullptr, settings, a11y::OutputReason::Query,
            text::TextUnit::Character, /*extraDetail=*/true, /*initialFormat=*/true);
        for (speech::SpeechItem& field : part) {
            sequence.push_back(std::move(field));
        }
    }
    if (sequence.empty()) {
        // Translators: the application reports no formatting information.
        say(_("No formatting information"));
        return;
    }
    presenter->speak(std::move(sequence));
}

void TextCommands::reportFormattingAtCaret(input::Gesture& /*gesture*/) {
    a11y::Registry* registry = service_.registry();
    if (registry == nullptr) {
        return;
    }
    const a11y::ObjectPtr object = registry->api().focus();
    if (object == nullptr) {
        say(_("No focus"));
        return;
    }
    const text::TextRangePtr range = object->makeTextRange(text::Position::Caret);
    if (range == nullptr) {
        say(_("No caret"));
        return;
    }
    reportFormattingFor(*range, /*browseMode=*/false);
}

void TextCommands::reportFormattingAtReview(input::Gesture& /*gesture*/) {
    text::TextRange* position = reviewOrComplain();
    if (position == nullptr) {
        return;
    }
    reportFormattingFor(*position, /*browseMode=*/false);
}

void TextCommands::reportFormatting(input::Gesture& gesture) {
    // NVDA's `alus+shift+f`: the review cursor, and twice shows it in browse
    // mode -- which needs phase 6's document, so twice reports it again and
    // says nothing false.
    reportFormattingAtReview(gesture);
}

void TextCommands::reportOrShowFormattingAtCaret(input::Gesture& gesture) {
    reportFormattingAtCaret(gesture);
}

// ---------------------------------------------------------------------------
// The settings the user cycles
// ---------------------------------------------------------------------------

void TextCommands::cycleParagraphStyle(input::Gesture& /*gesture*/) {
    service_.cycleParagraphStyle();
}

void TextCommands::toggleReportLineIndentation(input::Gesture& /*gesture*/) {
    service_.cycleLineIndentationReporting();
}

}  // namespace alus::nav
