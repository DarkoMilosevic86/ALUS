// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/commands.hpp"

#include "alus/a11y/object.hpp"
#include "alus/a11y/api.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/script/dispatch.hpp"

#include <utility>

namespace alus::a11y {
namespace {

// NVDA: the depth at which a flattened-view walk gives up.
constexpr int kFlowDepthLimit = 100;

}  // namespace

ObjectCommands::ObjectCommands(Registry& registry, const script::Catalog* catalog,
                               script::Dispatcher* dispatcher, config::Manager* config)
    : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
      registry_(registry),
      dispatcher_(dispatcher),
      config_(config) {
    // The descriptions, the categories and the key assignments come from
    // data/scripts.csv and data/gestures-builtin.ini, so nothing is repeated
    // here: the catalogue row for (globalCommands.GlobalCommands, <name>) is
    // NVDA's own.
    ALUS_SCRIPT(reportCurrentFocus);
    ALUS_SCRIPT(navigatorObject_current);
    ALUS_SCRIPT(navigatorObject_parent);
    ALUS_SCRIPT(navigatorObject_firstChild);
    ALUS_SCRIPT(navigatorObject_next);
    ALUS_SCRIPT(navigatorObject_previous);
    ALUS_SCRIPT(navigatorObject_nextInFlow);
    ALUS_SCRIPT(navigatorObject_previousInFlow);
    ALUS_SCRIPT(navigatorObject_toFocus);
    ALUS_SCRIPT(navigatorObject_moveFocus);
    ALUS_SCRIPT(navigatorObject_currentDimensions);
    ALUS_SCRIPT(review_activate);
    ALUS_SCRIPT(title);
    ALUS_SCRIPT(toggleCaretMovesReviewCursor);
    ALUS_SCRIPT(toggleFocusMovesNavigatorObject);
    ALUS_SCRIPT(toggleReportDynamicContentChanges);
    ALUS_SCRIPT(toggleProgressBarOutput);
    ALUS_SCRIPT(reportFocusObjectAccelerator);
    ALUS_SCRIPT(reportDetailsSummary);
    ALUS_SCRIPT(navigatorObject_devInfo);
    ALUS_SCRIPT(reportLinkDestination);
}

void ObjectCommands::say(const std::string& text) const {
    if (const auto& message = registry_.presentation().message) {
        message(text);
    }
}

void ObjectCommands::spell(const std::string& text) const {
    if (const auto& speller = registry_.presentation().spell) {
        speller(text);
        return;
    }
    say(text);
}

void ObjectCommands::describeCharacters(const std::string& text) const {
    if (const auto& describe = registry_.presentation().describeCharacters) {
        describe(text);
        return;
    }
    spell(text);
}

void ObjectCommands::sayOrSpell(const std::string& text) const {
    // NVDA: once says it, twice spells it, three times describes each
    // character. The third press is what phase 5 made different from the
    // second (phase-4.md O11).
    if (repeatCount() >= 2) {
        describeCharacters(text);
        return;
    }
    spell(text);
}

int ObjectCommands::repeatCount() const {
    return dispatcher_ != nullptr ? dispatcher_->lastScriptRepeatCount() : 0;
}

ObjectPtr ObjectCommands::navigatorOrComplain() const {
    const ObjectPtr navigator = registry_.api().navigator();
    if (navigator == nullptr) {
        // Translators: the user asked about the navigator object and there is
        // none.
        say(_("No navigator object"));
        return nullptr;
    }
    return navigator;
}

void ObjectCommands::moveTo(const ObjectPtr& target, const char* edgeMessage) {
    if (target == nullptr) {
        // §3.8: an expected refusal is spoken, not an error sound.
        say(edgeMessage);
        return;
    }
    if (!registry_.api().setNavigatorObject(target)) {
        say(edgeMessage);
        return;
    }
    if (const auto& speak = registry_.presentation().speakObject) {
        speak(*target, OutputReason::Focus);
    }
}

void ObjectCommands::reportCurrentFocus(input::Gesture& /*gesture*/) {
    const ObjectPtr focus = registry_.api().focus();
    if (focus == nullptr) {
        // Translators: there is no focused object.
        say(_("No focus"));
        return;
    }
    if (repeatCount() == 0) {
        if (const auto& speak = registry_.presentation().speakObject) {
            speak(*focus, OutputReason::Query);
        }
        return;
    }
    // Pressed twice: spell it. Three times: describe each character.
    sayOrSpell(focus->name());
}

void ObjectCommands::navigatorObject_current(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    if (repeatCount() == 0) {
        if (const auto& speak = registry_.presentation().speakObject) {
            speak(*navigator, OutputReason::Query);
        }
        return;
    }
    sayOrSpell(navigator->name());
}

void ObjectCommands::navigatorObject_parent(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    // Translators: there is no object containing this one.
    moveTo(navigator->parent(), _("No containing object"));
}

void ObjectCommands::navigatorObject_firstChild(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    // Translators: there is nothing inside this object.
    moveTo(navigator->firstChild(), _("No objects inside"));
}

void ObjectCommands::navigatorObject_next(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    // Translators: this is the last object.
    moveTo(navigator->next(), _("No next"));
}

void ObjectCommands::navigatorObject_previous(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    // Translators: this is the first object.
    moveTo(navigator->previous(), _("No previous"));
}

void ObjectCommands::navigatorObject_nextInFlow(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    // The flattened view: into this object, else past it, else out of it and
    // past its first ancestor that has somewhere to go.
    ObjectPtr target = navigator->firstChild();
    if (target == nullptr) {
        target = navigator->next();
    }
    if (target == nullptr) {
        ObjectPtr parent = navigator->parent();
        for (int depth = 0; parent != nullptr && depth < kFlowDepthLimit; ++depth) {
            if (ObjectPtr sibling = parent->next()) {
                target = std::move(sibling);
                break;
            }
            parent = parent->parent();
        }
    }
    moveTo(target, "No next");
}

void ObjectCommands::navigatorObject_previousInFlow(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    ObjectPtr target = navigator->previous();
    if (target != nullptr) {
        // The previous object in a flattened view is the DEEPEST last
        // descendant of the previous sibling, not the sibling itself.
        for (int depth = 0; depth < kFlowDepthLimit; ++depth) {
            ObjectPtr last = target->lastChild();
            if (last == nullptr) {
                break;
            }
            target = std::move(last);
        }
    } else {
        target = navigator->parent();
    }
    moveTo(target, "No previous");
}

void ObjectCommands::navigatorObject_toFocus(input::Gesture& /*gesture*/) {
    const ObjectPtr focus = registry_.api().focus();
    if (focus == nullptr) {
        say(_("No focus"));
        return;
    }
    if (!registry_.api().setNavigatorObject(focus, true)) {
        say(_("No focus"));
        return;
    }
    // Translators: the review cursor was moved to the object with focus.
    say(_("Move to focus"));
    if (const auto& speak = registry_.presentation().speakObject) {
        speak(*focus, OutputReason::Query);
    }
}

void ObjectCommands::navigatorObject_moveFocus(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    // Translators: the keyboard focus was moved to the navigator object.
    say(_("Move focus"));
    if (!navigator->setFocus()) {
        // The object refused the focus. Saying so is the whole reason
        // Object::setFocus returns bool (§6.4, T4.9).
        // Translators: the object could not take the keyboard focus.
        say(_("Cannot focus this object"));
    }
}

void ObjectCommands::navigatorObject_currentDimensions(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    const Rect& location = navigator->location();
    if (location.empty()) {
        // Translators: the object has no position on the screen.
        say(_("No location information"));
        return;
    }
    // NVDA reports this as a percentage of the screen; until there is a screen
    // size to divide by (phase 10's screen review), the pixels are the honest
    // answer.
    say(std::to_string(location.width) + " by " + std::to_string(location.height) +
        " pixels at " + std::to_string(location.x) + ", " + std::to_string(location.y));
}

void ObjectCommands::review_activate(input::Gesture& /*gesture*/) {
    ObjectPtr target = navigatorOrComplain();
    if (target == nullptr) {
        return;
    }
    // NVDA walks up from the navigator object until something can be
    // activated, because the thing with the action is often the parent of the
    // label the review cursor is on.
    for (int depth = 0; target != nullptr && depth < kFlowDepthLimit; ++depth) {
        if (target->actionCount() > 0) {
            std::string actionName = target->actionName(0);
            if (target->doAction(0)) {
                // Translators: the action at the navigator object was
                // performed. The toolkit's own word for it is used when it has
                // one ("click", "press"), and "Activate" otherwise.
                say(actionName.empty() ? std::string{_("Activate")} : actionName);
                return;
            }
        }
        target = target->parent();
    }
    // Translators: nothing at the navigator object could be activated.
    say(_("No action"));
}

void ObjectCommands::title(input::Gesture& /*gesture*/) {
    const ObjectPtr foreground = registry_.api().foreground();
    std::string text = foreground != nullptr ? foreground->name() : std::string{};
    if (text.empty() && foreground != nullptr) {
        text = foreground->appInfo().name;
    }
    if (text.empty()) {
        // Translators: the active window has no title.
        text = _("No title");
    }
    if (repeatCount() == 0) {
        say(text);
        return;
    }
    // NVDA copies the title to the clipboard on the third press; the clipboard
    // belongs to phase 9's GUI work, so the third press describes the
    // characters instead, which is at least not the same as the second.
    sayOrSpell(text);
}


// ---------------------------------------------------------------------------
// The four toggles.
//
// Each one changes the LIVE settings and then writes the configuration, in
// that order and never only the second: a11y::Settings is what the
// presentation path reads on every focus change (settings.hpp says why it is
// a struct and not a lookup), so a toggle that wrote only the file would be
// heard at the next restart and nowhere else.
//
// The section and key names are NVDA's own, which is also what
// a11y/settings.cpp registers the defaults under -- so a factory reset and
// resetConfiguration both find what these wrote.
// ---------------------------------------------------------------------------

void ObjectCommands::toggleCaretMovesReviewCursor(input::Gesture& /*gesture*/) {
    Settings& settings = registry_.settings();
    settings.reviewCursorFollowCaret = !settings.reviewCursorFollowCaret;
    if (config_ != nullptr) {
        config_->setBool("reviewCursor", "followCaret", settings.reviewCursorFollowCaret);
    }
    say(settings.reviewCursorFollowCaret
            // Translators: reported when the review cursor is set to follow
            // the system caret (alus+6).
            ? _("caret moves review cursor on")
            // Translators: reported when the review cursor no longer follows
            // the system caret.
            : _("caret moves review cursor off"));
}

void ObjectCommands::toggleFocusMovesNavigatorObject(input::Gesture& /*gesture*/) {
    Settings& settings = registry_.settings();
    settings.reviewCursorFollowFocus = !settings.reviewCursorFollowFocus;
    if (config_ != nullptr) {
        config_->setBool("reviewCursor", "followFocus", settings.reviewCursorFollowFocus);
    }
    say(settings.reviewCursorFollowFocus
            // Translators: reported when the navigator object is set to
            // follow the focus (alus+7).
            ? _("focus moves navigator object on")
            // Translators: reported when the navigator object no longer
            // follows the focus.
            : _("focus moves navigator object off"));
}

void ObjectCommands::toggleReportDynamicContentChanges(input::Gesture& /*gesture*/) {
    Settings& settings = registry_.settings();
    settings.reportDynamicContentChanges = !settings.reportDynamicContentChanges;
    if (config_ != nullptr) {
        config_->setBool("presentation", "reportDynamicContentChanges",
                         settings.reportDynamicContentChanges);
    }
    say(settings.reportDynamicContentChanges
            // Translators: reported when text that changes by itself -- a
            // terminal, a chat window, a log -- is announced again (alus+5).
            ? _("report dynamic content changes on")
            // Translators: reported when text that changes by itself is no
            // longer announced.
            : _("report dynamic content changes off"));
}

void ObjectCommands::toggleProgressBarOutput(input::Gesture& /*gesture*/) {
    Settings& settings = registry_.settings();
    // Upstream's cycle, in upstream's order: both -> off -> speak -> beep ->
    // both. It is not alphabetical and not the enumeration's order, and it is
    // what a user who knows NVDA counts presses against.
    std::string spoken;
    switch (settings.progressBarOutput) {
        case ProgressOutput::Both:
            settings.progressBarOutput = ProgressOutput::Off;
            // Translators: a progress bar reporting mode (alus+u): nothing is
            // reported.
            spoken = _("No progress bar updates");
            break;
        case ProgressOutput::Off:
            settings.progressBarOutput = ProgressOutput::Speak;
            // Translators: a progress bar reporting mode: the percentage is
            // spoken.
            spoken = _("Speak progress bar updates");
            break;
        case ProgressOutput::Speak:
            settings.progressBarOutput = ProgressOutput::Beep;
            // Translators: a progress bar reporting mode: a beep that rises
            // in pitch as the bar fills.
            spoken = _("Beep for progress bar updates");
            break;
        case ProgressOutput::Beep:
            settings.progressBarOutput = ProgressOutput::Both;
            // Translators: a progress bar reporting mode: both the beep and
            // the spoken percentage.
            spoken = _("Beep and speak progress bar updates");
            break;
    }
    if (config_ != nullptr) {
        config_->setString("presentation.progressBarUpdates", "progressBarOutputMode",
                           progressOutputName(settings.progressBarOutput));
    }
    say(spoken);
}


// ---------------------------------------------------------------------------
// Three questions about what has the focus.
// ---------------------------------------------------------------------------

void ObjectCommands::reportFocusObjectAccelerator(input::Gesture& /*gesture*/) {
    const ObjectPtr focus = registry_.api().focus();
    if (focus == nullptr || focus->keyboardShortcut().empty()) {
        // Translators: reported when the focused control has no keyboard
        // shortcut of its own (shift+numpad2).
        say(_("No shortcut key"));
        return;
    }
    // AT-SPI hands over the accelerator as the platform's own text ("Alt+F",
    // "<Control>o"), and ALUS says it as it was given.
    //
    // NOT split into spoken key labels, which is what upstream does
    // (shortcutKeys.speakKeyboardShortcuts): the labels in data/key-labels.ini
    // are keyed by ALUS GESTURE names, and an accelerator is a string from
    // another toolkit's vocabulary -- GTK writes "<Control>o", Qt writes
    // "Ctrl+O". Mapping the two is phase 10 work; saying the toolkit's own
    // text is right in every case and wrong in none.
    say(focus->keyboardShortcut());
}

void ObjectCommands::reportDetailsSummary(input::Gesture& /*gesture*/) {
    const ObjectPtr focus = registry_.api().focus();
    const std::vector<ObjectPtr> targets =
        focus != nullptr ? focus->details() : std::vector<ObjectPtr>{};
    if (targets.empty()) {
        // Translators: reported when the focused control points at no
        // additional details (alus+d).
        say(_("No additional details"));
        return;
    }
    // The next target on a repeat press, the first one otherwise. Upstream
    // cycles the same way, and for the same reason: an object may point at
    // several details and there is no other way to reach the second.
    const core::ObjectId origin = focus->id();
    if (origin == detailsOrigin_ && repeatCount() > 0) {
        detailsIndex_ = (detailsIndex_ + 1) % targets.size();
    } else {
        detailsIndex_ = 0;
    }
    detailsOrigin_ = origin;

    const ObjectPtr& target = targets[detailsIndex_];
    std::string summary = target->name();
    if (summary.empty()) {
        summary = target->description();
    }
    if (summary.empty() && target->hasNavigableText()) {
        summary = target->textRange(0, target->characterCount());
    }
    if (summary.empty()) {
        // There IS a details target; it just says nothing ALUS can read. That
        // is a different answer from "no additional details" and must not be
        // reported as it, or the user would go looking for a relation that is
        // there.
        ALUS_DEBUG_WARNING("details target {} has no name, description or text",
                           target->path());
        // Translators: reported when the details a control points at exist
        // but carry no readable text.
        say(_("No details summary"));
        return;
    }
    say(summary);
}

void ObjectCommands::navigatorObject_devInfo(input::Gesture& /*gesture*/) {
    const ObjectPtr navigator = navigatorOrComplain();
    if (navigator == nullptr) {
        return;
    }
    std::string block = "developer info for the navigator object:";
    for (const std::string& line : navigator->devInfo()) {
        block += "\n  ";
        block += line;
    }
    // To the LOG and not to speech, which is upstream's choice too: this is
    // twenty lines of platform detail for a bug report, and no synthesizer
    // should be made to read it out. Upstream opens its log viewer at this
    // point; ALUS has none, so the one thing that must not be left out is
    // telling the user WHERE it went -- otherwise the key looks dead.
    ALUS_INFO("{}", block);
    // Translators: reported when the developer information about an object has
    // been written to the log (alus+f1).
    say(_("Developer info written to the log"));
}


void ObjectCommands::reportLinkDestination(input::Gesture& /*gesture*/) {
    // THE NAVIGATOR OBJECT FIRST, and that is what makes this work in browse
    // mode: the navigator object follows the browse-mode caret (browse_mode.cpp
    // sets it on every jump), so the object under the reading cursor is the
    // one asked. The focus is the fallback, for a link reached with tab in a
    // dialog or a focus-mode form.
    Api& api = registry_.api();
    ObjectPtr object = api.navigator();
    if (object == nullptr) {
        object = api.focus();
    }
    if (object == nullptr) {
        // Translators: reported when there is nothing under the cursor to ask
        // about.
        say(_("No navigator object"));
        return;
    }

    const bool isLink = object->role() == Role::Link || object->states().has(State::Linked);
    const std::string destination = object->linkDestination();
    if (!destination.empty()) {
        say(destination);
        return;
    }
    if (isLink) {
        // Translators: reported when the thing under the cursor is a link but
        // the application does not say where it points.
        say(_("Link has no apparent destination"));
        return;
    }
    // Translators: reported when the thing under the cursor is not a link at
    // all (alus+k).
    say(_("Not a link."));
}

}  // namespace alus::a11y
