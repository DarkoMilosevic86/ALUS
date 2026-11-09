// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/commands.hpp.

#include "alus/braille/commands.hpp"

#include "alus/a11y/api.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/braille/input.hpp"
#include "alus/braille/service.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/gesture.hpp"
#include "alus/input/key_names.hpp"
#include "alus/nav/review.hpp"
#include "alus/nav/service.hpp"
#include "alus/ui/message.hpp"

#include <algorithm>

namespace alus::braille {
namespace {

std::string shapeLabel(std::uint8_t shape) {
    switch (shape) {
        case kCursorDot8:
            // Translators: a braille cursor shape.
            return _("dot 8");
        case kCursorAllDots:
            // Translators: a braille cursor shape.
            return _("all dots");
        default:
            // Translators: a braille cursor shape.
            return _("dots 7 and 8");
    }
}

std::uint8_t nextShape(std::uint8_t shape) {
    switch (shape) {
        case kCursorDots78:
            return kCursorDot8;
        case kCursorDot8:
            return kCursorAllDots;
        default:
            return kCursorDots78;
    }
}

std::string onOff(bool value) {
    // Translators: a setting that is on or off.
    return value ? _("on") : _("off");
}

}  // namespace

Commands::Commands(Service& service, const script::Catalog* catalog)
    // It answers to NVDA's GlobalCommands, which is what makes the generated
    // command table's keys and descriptions apply (see the header).
    : script::ScriptableObject({kGlobalCommandsClass}, catalog), service_(service) {
    ALUS_SCRIPT(braille_scrollBack);
    ALUS_SCRIPT(braille_scrollForward);
    ALUS_SCRIPT(braille_previousLine);
    ALUS_SCRIPT(braille_nextLine);
    ALUS_SCRIPT(braille_toFocus);
    ALUS_SCRIPT(braille_routeTo);
    ALUS_SCRIPT(braille_reportFormatting);
    ALUS_SCRIPT(braille_selectRange);
    ALUS_SCRIPT(braille_dots);
    ALUS_SCRIPT(braille_enter);
    ALUS_SCRIPT(braille_eraseLastCell);
    ALUS_SCRIPT(braille_translate);
    ALUS_SCRIPT(braille_toggleShift);
    ALUS_SCRIPT(braille_toggleControl);
    ALUS_SCRIPT(braille_toggleAlt);
    ALUS_SCRIPT(braille_toggleWindows);
    ALUS_SCRIPT(braille_toggleAlusKey);
    ALUS_SCRIPT(braille_toggleControlShift);
    ALUS_SCRIPT(braille_toggleAltShift);
    ALUS_SCRIPT(braille_toggleWindowsShift);
    ALUS_SCRIPT(braille_toggleAlusKeyShift);
    ALUS_SCRIPT(braille_toggleControlAlt);
    ALUS_SCRIPT(braille_toggleControlAltShift);
    ALUS_SCRIPT(braille_toggleTether);
    ALUS_SCRIPT(toggleBrailleMode);
    ALUS_SCRIPT(braille_toggleShowCursor);
    ALUS_SCRIPT(braille_cycleCursorShape);
    ALUS_SCRIPT(braille_cycleShowSelection);
    ALUS_SCRIPT(braille_cycleShowMessages);
    ALUS_SCRIPT(braille_toggleFocusContextPresentation);
    ALUS_SCRIPT(braille_cycleReviewRoutingMovesSystemCaret);
    ALUS_SCRIPT(braille_cycleUnicodeNormalization);
    ALUS_SCRIPT(braille_toggleSpeakOnRouting);
    ALUS_SCRIPT(toggleSpeakingOnNavigatingByUnit);
    ALUS_SCRIPT(toggleBrailleAutoScroll);
    ALUS_SCRIPT(increaseBrailleAutoScrollRate);
    ALUS_SCRIPT(decreaseBrailleAutoScrollRate);
}

Commands::~Commands() = default;

void Commands::say(const std::string& text) const { ui::message(text); }

std::vector<int> Commands::cellsOf(input::Gesture& gesture) {
    if (auto* display = dynamic_cast<DisplayGesture*>(&gesture)) {
        return display->cellIndexes();
    }
    return {};
}

// ---------------------------------------------------------------------------
// The display window
// ---------------------------------------------------------------------------

void Commands::braille_scrollBack(input::Gesture& /*gesture*/) {
    if (Handler* handler = service_.handler()) {
        handler->scrollBack();
    }
}

void Commands::braille_scrollForward(input::Gesture& /*gesture*/) {
    if (Handler* handler = service_.handler()) {
        handler->scrollForward();
    }
}

void Commands::braille_previousLine(input::Gesture& /*gesture*/) {
    Handler* handler = service_.handler();
    if (handler == nullptr) {
        return;
    }
    std::vector<RegionPtr>& regions = handler->currentBuffer().regions;
    if (!regions.empty()) {
        regions.back()->previousLine(/*toStart=*/true);
    }
}

void Commands::braille_nextLine(input::Gesture& /*gesture*/) {
    Handler* handler = service_.handler();
    if (handler == nullptr) {
        return;
    }
    std::vector<RegionPtr>& regions = handler->currentBuffer().regions;
    if (!regions.empty()) {
        regions.back()->nextLine();
    }
}

void Commands::braille_toFocus(input::Gesture& /*gesture*/) {
    Handler* handler = service_.handler();
    a11y::Registry* registry = service_.registry();
    if (handler == nullptr || registry == nullptr) {
        return;
    }
    handler->setTether(TetherTo::Focus, /*automatic=*/true);
    if (handler->tether() == TetherTo::Review) {
        // Still pinned to review: the user asked for the focus, so move the
        // review cursor there rather than silently doing nothing.
        if (nav::Service* navService = service_.nav()) {
            if (nav::Review* review = navService->review()) {
                const a11y::ObjectPtr focus = registry->api().focus();
                if (focus != nullptr) {
                    registry->api().setNavigatorObject(focus, /*isFocus=*/true);
                    review->clear();
                }
            }
        }
        handler->handleReviewMove(/*shouldAutoTether=*/false);
        return;
    }
    const a11y::ObjectPtr focus = registry->api().focus();
    Buffer& buffer = handler->mainBuffer();
    Region* last = buffer.regions.empty() ? nullptr : buffer.regions.back().get();
    const a11y::ObjectPtr shown = last != nullptr ? last->object() : nullptr;
    if (last != nullptr && shown != nullptr && focus != nullptr && shown->sameAs(*focus)) {
        // The display is already showing the focus; just bring it into view.
        buffer.focus(last);
        if (last->brailleCursorPos.has_value()) {
            buffer.scrollTo(last, *last->brailleCursorPos);
        } else if (last->brailleSelectionStart.has_value()) {
            buffer.scrollTo(last, *last->brailleSelectionStart);
        }
        handler->update();
        return;
    }
    handler->handleGainFocus(focus, /*shouldAutoTether=*/false);
}

// ---------------------------------------------------------------------------
// The routing keys
// ---------------------------------------------------------------------------

void Commands::braille_routeTo(input::Gesture& gesture) {
    const std::vector<int> cells = cellsOf(gesture);
    if (cells.empty()) {
        return;
    }
    if (Handler* handler = service_.handler()) {
        handler->routeTo(cells.front());
    }
}

void Commands::braille_reportFormatting(input::Gesture& gesture) {
    const std::vector<int> cells = cellsOf(gesture);
    Handler* handler = service_.handler();
    nav::Service* navService = service_.nav();
    if (cells.empty() || handler == nullptr) {
        return;
    }
    const text::TextRangePtr range = handler->rangeForWindowPos(cells.front());
    if (range == nullptr || navService == nullptr) {
        // Translators: reported when there is no formatting information for
        // the text under a braille cell.
        say(_("No formatting information"));
        return;
    }
    // The text layer already has the formatting command; braille only decides
    // WHERE to ask about.
    text::TextRangePtr unit = range->copy();
    unit->expand(text::TextUnit::Character);
    navService->speakRange(*unit, text::TextUnit::Character, a11y::OutputReason::Query);
}

void Commands::braille_selectRange(input::Gesture& gesture) {
    const std::vector<int> cells = cellsOf(gesture);
    Handler* handler = service_.handler();
    if (cells.size() < 2 || handler == nullptr) {
        return;
    }
    const auto [minIt, maxIt] = std::minmax_element(cells.begin(), cells.end());
    text::TextRangePtr start = handler->rangeForWindowPos(*minIt);
    const text::TextRangePtr end = handler->rangeForWindowPos(*maxIt);
    if (start == nullptr || end == nullptr) {
        // Translators: reported when selection via routing keys is not
        // possible.
        say(_("Cannot select from braille routing keys"));
        return;
    }
    start->setEndPoint(*end, text::EndPointPair::EndToEnd);
    if (!start->updateSelection()) {
        // Translators: reported when the focused control does not support
        // selecting from the braille display.
        say(_("Selection not supported here"));
    }
}

// ---------------------------------------------------------------------------
// Braille input
// ---------------------------------------------------------------------------

void Commands::braille_dots(input::Gesture& gesture) {
    BrailleInput* input = service_.input();
    auto* dots = dynamic_cast<InputGesture*>(&gesture);
    if (input == nullptr || dots == nullptr) {
        return;
    }
    input->input(dots->dots());
}

void Commands::braille_enter(input::Gesture& /*gesture*/) {
    if (BrailleInput* input = service_.input()) {
        input->enter();
    }
}

void Commands::braille_eraseLastCell(input::Gesture& /*gesture*/) {
    if (BrailleInput* input = service_.input()) {
        input->eraseLastCell();
    }
}

void Commands::braille_translate(input::Gesture& /*gesture*/) {
    if (BrailleInput* input = service_.input()) {
        input->translate();
    }
}

// ---------------------------------------------------------------------------
// The emulated modifiers
// ---------------------------------------------------------------------------

void Commands::toggleModifiers(const std::vector<std::string>& modifiers) {
    if (BrailleInput* input = service_.input()) {
        input->toggleModifiers(modifiers);
    }
}

void Commands::braille_toggleShift(input::Gesture&) { toggleModifiers({"shift"}); }
void Commands::braille_toggleControl(input::Gesture&) { toggleModifiers({"control"}); }
void Commands::braille_toggleAlt(input::Gesture&) { toggleModifiers({"alt"}); }
void Commands::braille_toggleWindows(input::Gesture&) { toggleModifiers({"windows"}); }
void Commands::braille_toggleAlusKey(input::Gesture&) {
    toggleModifiers({std::string{input::KeyNames::kAlusKeyName}});
}
void Commands::braille_toggleControlShift(input::Gesture&) {
    toggleModifiers({"control", "shift"});
}
void Commands::braille_toggleAltShift(input::Gesture&) { toggleModifiers({"alt", "shift"}); }
void Commands::braille_toggleWindowsShift(input::Gesture&) {
    toggleModifiers({"windows", "shift"});
}
void Commands::braille_toggleAlusKeyShift(input::Gesture&) {
    toggleModifiers({std::string{input::KeyNames::kAlusKeyName}, "shift"});
}
void Commands::braille_toggleControlAlt(input::Gesture&) {
    toggleModifiers({"control", "alt"});
}
void Commands::braille_toggleControlAltShift(input::Gesture&) {
    toggleModifiers({"control", "alt", "shift"});
}

// ---------------------------------------------------------------------------
// The settings the user cycles with a key
// ---------------------------------------------------------------------------

void Commands::braille_toggleTether(input::Gesture& /*gesture*/) {
    Handler* handler = service_.handler();
    if (handler == nullptr) {
        return;
    }
    Settings settings = service_.settings();
    // focus -> review -> auto -> focus, which is NVDA's cycle order.
    switch (settings.tetherTo) {
        case TetherTo::Focus:
            settings.tetherTo = TetherTo::Review;
            break;
        case TetherTo::Review:
            settings.tetherTo = TetherTo::Auto;
            break;
        case TetherTo::Auto:
            settings.tetherTo = TetherTo::Focus;
            break;
    }
    service_.updateSettings(settings);
    if (settings.tetherTo != TetherTo::Auto) {
        handler->setTether(settings.tetherTo, /*automatic=*/false);
        if (settings.tetherTo == TetherTo::Review) {
            handler->handleReviewMove(/*shouldAutoTether=*/false);
        } else if (a11y::Registry* registry = service_.registry()) {
            handler->handleGainFocus(registry->api().focus(), /*shouldAutoTether=*/false);
        }
    }
    // Translators: reports which position braille is tethered to; %s is
    // "focus" or "review".
    say(i18n::substitute(_("Braille tethered %s"), tetherLabel(settings.tetherTo)));
}

void Commands::toggleBrailleMode(input::Gesture& /*gesture*/) {
    Handler* handler = service_.handler();
    if (handler == nullptr) {
        return;
    }
    Settings settings = service_.settings();
    settings.mode =
        settings.mode == Mode::FollowCursors ? Mode::SpeechOutput : Mode::FollowCursors;
    service_.updateSettings(settings);
    handler->dismissMessage(/*shouldUpdate=*/false);
    handler->clearSpeechRegions();
    handler->mainBuffer().clear();
    // Translators: reported when switching braille modes; %s names the
    // mode.
    say(i18n::substitute(_("Braille mode %s"), modeLabel(settings.mode)));
    if (settings.mode == Mode::SpeechOutput) {
        return;
    }
    if (handler->tether() == TetherTo::Review) {
        handler->handleReviewMove(handler->shouldAutoTether());
        return;
    }
    if (a11y::Registry* registry = service_.registry()) {
        handler->handleGainFocus(registry->api().focus());
    }
}

void Commands::braille_toggleShowCursor(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    settings.showCursor = !settings.showCursor;
    service_.updateSettings(settings);
    // Translators: reported when the braille cursor is turned on or off;
    // %s is "on" or "off".
    say(i18n::substitute(_("Braille cursor %s"), onOff(settings.showCursor)));
}

void Commands::braille_cycleCursorShape(input::Gesture& /*gesture*/) {
    Handler* handler = service_.handler();
    Settings settings = service_.settings();
    if (!settings.showCursor) {
        // Translators: reported when the braille cursor shape cannot be
        // changed because the cursor is off.
        say(_("Cannot cycle braille cursor shape. The braille cursor is turned off"));
        return;
    }
    const bool review = handler != nullptr && handler->tether() == TetherTo::Review;
    std::uint8_t& shape = review ? settings.cursorShapeReview : settings.cursorShapeFocus;
    shape = nextShape(shape);
    service_.updateSettings(settings);
    say(shapeLabel(shape));
}

void Commands::braille_cycleShowSelection(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    settings.showSelection = !settings.showSelection;
    service_.updateSettings(settings);
    // Translators: reported when braille selection indication is cycled;
    // %s is "on" or "off".
    say(i18n::substitute(_("Braille show selection %s"), onOff(settings.showSelection)));
}

void Commands::braille_cycleShowMessages(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    switch (settings.showMessages) {
        case ShowMessages::Disabled:
            settings.showMessages = ShowMessages::UseTimeout;
            break;
        case ShowMessages::UseTimeout:
            settings.showMessages = ShowMessages::ShowIndefinitely;
            break;
        case ShowMessages::ShowIndefinitely:
            settings.showMessages = ShowMessages::Disabled;
            break;
    }
    service_.updateSettings(settings);
    say(std::string{"Braille show messages "} +
        std::string{showMessagesName(settings.showMessages)});
}

void Commands::braille_toggleFocusContextPresentation(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    switch (settings.focusContextPresentation) {
        case FocusContext::ChangedContext:
            settings.focusContextPresentation = FocusContext::Fill;
            break;
        case FocusContext::Fill:
            settings.focusContextPresentation = FocusContext::Scroll;
            break;
        case FocusContext::Scroll:
            settings.focusContextPresentation = FocusContext::ChangedContext;
            break;
    }
    service_.updateSettings(settings);
    say(std::string{"Braille focus context presentation "} +
        std::string{focusContextName(settings.focusContextPresentation)});
}

void Commands::braille_cycleReviewRoutingMovesSystemCaret(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    if (settings.tetherTo == TetherTo::Focus) {
        // Translators: reported when the action is unavailable because
        // braille is tethered to focus.
        say(_("Action unavailable. Braille is tethered to focus"));
        return;
    }
    switch (settings.reviewRoutingMovesCaret) {
        case ReviewRoutingMovesCaret::Never:
            settings.reviewRoutingMovesCaret = ReviewRoutingMovesCaret::Always;
            break;
        case ReviewRoutingMovesCaret::Always:
            settings.reviewRoutingMovesCaret = ReviewRoutingMovesCaret::OnlyWhenAutoTethered;
            break;
        case ReviewRoutingMovesCaret::OnlyWhenAutoTethered:
            settings.reviewRoutingMovesCaret = ReviewRoutingMovesCaret::Never;
            break;
    }
    service_.updateSettings(settings);
    say(std::string{"Braille move system caret when routing review cursor "} +
        std::string{reviewRoutingLabel(settings.reviewRoutingMovesCaret)});
}

void Commands::braille_cycleUnicodeNormalization(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    settings.unicodeNormalization = !settings.unicodeNormalization;
    service_.updateSettings(settings);
    say(std::string{"Braille unicode normalization "} + onOff(settings.unicodeNormalization));
}

void Commands::braille_toggleSpeakOnRouting(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    settings.speakOnRouting = !settings.speakOnRouting;
    service_.updateSettings(settings);
    say(std::string{"Speak character when routing braille cursor "} +
        onOff(settings.speakOnRouting));
}

void Commands::toggleSpeakingOnNavigatingByUnit(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    settings.speakOnNavigatingByUnit = !settings.speakOnNavigatingByUnit;
    service_.updateSettings(settings);
    say(std::string{"Speak when navigating by line or paragraph with braille "} +
        onOff(settings.speakOnNavigatingByUnit));
}

void Commands::toggleBrailleAutoScroll(input::Gesture& /*gesture*/) {
    Handler* handler = service_.handler();
    if (handler == nullptr) {
        return;
    }
    const bool enable = !handler->isAutoScrolling();
    handler->setAutoScroll(enable);
    // Translators: reported when braille automatic scroll is turned on or
    // off; %s is "on" or "off".
    say(i18n::substitute(_("Braille automatic scroll %s"), onOff(enable)));
}

void Commands::increaseBrailleAutoScrollRate(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    settings.autoScrollRate = std::min(settings.autoScrollRate + 1, 60);
    service_.updateSettings(settings);
    if (Handler* handler = service_.handler(); handler != nullptr && handler->isAutoScrolling()) {
        handler->setAutoScroll(true);
    }
    say(std::to_string(settings.autoScrollRate));
}

void Commands::decreaseBrailleAutoScrollRate(input::Gesture& /*gesture*/) {
    Settings settings = service_.settings();
    settings.autoScrollRate = std::max(settings.autoScrollRate - 1, 1);
    service_.updateSettings(settings);
    if (Handler* handler = service_.handler(); handler != nullptr && handler->isAutoScrolling()) {
        handler->setAutoScroll(true);
    }
    say(std::to_string(settings.autoScrollRate));
}

}  // namespace alus::braille
