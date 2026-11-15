// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/behaviors.hpp"

#include "alus/a11y/events.hpp"
#include "alus/a11y/object.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace alus::a11y {
namespace {

// NVDA: ProgressBar.progressValueCache, keyed by output kind and screen
// position. A progress bar is often a brand new accessible on every update
// (web progress elements are), so the object cannot be the key; where it is on
// the screen can.
std::map<std::string, double>& progressCache() {
    static std::map<std::string, double> cache;
    return cache;
}

std::string progressKey(const char* kind, const Rect& location) {
    const int x = location.x + location.width / 2;
    const int y = location.y + location.height / 2;
    return std::string{kind} + "," + std::to_string(x) + "," + std::to_string(y);
}

// NVDA strips "%" and NUL before float(): a GTK progress bar's value text is
// "42 %" and a Windows one is "42%\0".
std::optional<double> parsePercentage(std::string_view text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (const char c : text) {
        if (c == '%' || c == '\0' || c == ' ') {
            continue;
        }
        cleaned.push_back(c == ',' ? '.' : c);
    }
    if (cleaned.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const double value = std::stod(cleaned, &consumed);
        if (consumed != cleaned.size()) {
            return std::nullopt;
        }
        return std::clamp(value, 0.0, 100.0);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool isBlank(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    });
}

bool isInForeground(const Object& object) {
    const ObjectPtr foreground = object.registry().api().foreground();
    if (foreground == nullptr) {
        return true;
    }
    // Walk up from the object; bounded, like every other walk.
    ObjectPtr current = object.parent();
    for (int depth = 0; current != nullptr && depth < 64; ++depth) {
        if (current->sameAs(*foreground)) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------

void ProgressBarBehavior::clearCache() { progressCache().clear(); }

std::size_t ProgressBarBehavior::cacheSize() { return progressCache().size(); }

bool ProgressBarBehavior::handleEvent(Object& object, const core::Event& event) {
    if (event.name != events::kValueChange) {
        return false;
    }
    const Settings& settings = object.registry().settings();
    const StateSet& states = object.states();
    if (settings.progressBarOutput == ProgressOutput::Off ||
        states.has(State::Invisible) || states.has(State::Offscreen)) {
        // Fall through to the object's own handling, which announces a value
        // change on the focus. NVDA does the same (it calls super()).
        return false;
    }
    const std::optional<double> percentage = parsePercentage(object.value());
    if (!percentage.has_value()) {
        ALUS_DEBUG_WARNING("progress bar with an unreadable value: {}", object.value());
        return false;
    }
    if (!settings.reportBackgroundProgressBars && !isInForeground(object)) {
        // Deliberately silent AND handled: a download in another window must
        // not interrupt what the user is doing.
        return true;
    }

    const Rect& location = object.location();
    const auto& hooks = object.registry().presentation();
    bool beep = false;
    bool speak = false;

    if (settings.progressBarOutput == ProgressOutput::Beep ||
        settings.progressBarOutput == ProgressOutput::Both) {
        const std::string key = progressKey("beep", location);
        const auto it = progressCache().find(key);
        if (it == progressCache().end() ||
            std::abs(*percentage - it->second) >=
                static_cast<double>(settings.beepPercentageInterval)) {
            beep = true;
            progressCache()[key] = *percentage;
        }
    }
    if (settings.progressBarOutput == ProgressOutput::Speak ||
        settings.progressBarOutput == ProgressOutput::Both) {
        const std::string key = progressKey("speech", location);
        const auto it = progressCache().find(key);
        if (it == progressCache().end() ||
            std::abs(*percentage - it->second) >=
                static_cast<double>(settings.speechPercentageInterval)) {
            speak = true;
            progressCache()[key] = *percentage;
        }
    }
    if ((beep || speak) && hooks.reportProgress) {
        hooks.reportProgress(*percentage, beep, speak);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

std::string DialogBehavior::dialogText(const Object& object, bool allowFocusedDescendants) {
    const std::vector<ObjectPtr>& children = object.children();
    std::vector<std::string> parts;
    const int childCount = static_cast<int>(children.size());
    for (int index = 0; index < childCount; ++index) {
        const ObjectPtr& child = children[static_cast<std::size_t>(index)];
        if (child == nullptr) {
            continue;
        }
        const StateSet& childStates = child->states();
        const Role childRole = child->role();
        if (childStates.has(State::Invisible) || childStates.has(State::Unavailable)) {
            continue;
        }
        switch (childRole) {
            // Containers worth descending into: their children hold the text.
            case Role::OptionPane:
            case Role::PropertyPage:
            case Role::Pane:
            case Role::Panel:
            case Role::Window:
            case Role::Grouping:
            case Role::Paragraph:
            case Role::Section:
            case Role::TextFrame:
            case Role::Unknown: {
                // A child that is itself a dialog with focusable descendants
                // is not descended into, or a property page inside a dialog is
                // reported twice.
                const bool childIsDialog =
                    child->hasBehavior("NVDAObjects.behaviors.Dialog");
                const std::string childText = dialogText(*child, !childIsDialog);
                if (!childText.empty()) {
                    parts.push_back(childText);
                }
                continue;
            }
            default:
                break;
        }
        if (!allowFocusedDescendants && childStates.has(State::Focused)) {
            // NVDA returns None here, meaning "there is a focused control in
            // this dialog, so its text is not a message".
            return {};
        }
        const bool isReadOnlyEntry = childRole == Role::EditableText &&
                                     childStates.has(State::ReadOnly) &&
                                     !childStates.has(State::MultiLine);
        if (!(childRole == Role::StaticText || childRole == Role::Label ||
              childRole == Role::Link || isReadOnlyEntry)) {
            continue;
        }
        // A text object directly after a grouping is that grouping's own
        // label, and a graphic may sit between the two.
        if (index > 0 && children[static_cast<std::size_t>(index) - 1] != nullptr &&
            children[static_cast<std::size_t>(index) - 1]->role() == Role::Grouping) {
            continue;
        }
        if (index > 1 && children[static_cast<std::size_t>(index) - 1] != nullptr &&
            children[static_cast<std::size_t>(index) - 2] != nullptr &&
            children[static_cast<std::size_t>(index) - 1]->role() == Role::Graphic &&
            children[static_cast<std::size_t>(index) - 2]->role() == Role::Grouping) {
            continue;
        }
        const std::string childName = child->name();
        if (!childName.empty() && index < childCount - 1) {
            const ObjectPtr& following = children[static_cast<std::size_t>(index) + 1];
            if (following != nullptr && following->name() == childName) {
                switch (following->role()) {
                    case Role::Graphic:
                    case Role::StaticText:
                    case Role::Separator:
                    case Role::Window:
                    case Role::Pane:
                    case Role::Button:
                        break;
                    default:
                        // This is almost certainly the label OF the next
                        // object, so it is not part of the message.
                        continue;
                }
            }
        }
        std::string childText;
        if (child->hasInterface(Interface::Text)) {
            childText = child->textRange(0, -1);
        }
        if (childText.empty()) {
            childText = childName;
        }
        if (!childText.empty()) {
            parts.push_back(childText);
        }
    }
    std::string text;
    for (const std::string& part : parts) {
        if (!text.empty()) {
            text += "\n";
        }
        text += part;
    }
    return text;
}

std::optional<std::string> DialogBehavior::description(const Object& object) const {
    // NVDA: only when the object does not already give a useful description.
    // The guard reads the PLATFORM description, not object.description(),
    // which would recurse into this very override.
    const std::string& platform = object.platformDescription();
    if (!platform.empty() && !isBlank(platform)) {
        return std::nullopt;
    }
    std::string text = dialogText(object);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::string> DialogBehavior::value(const Object&) const {
    // NVDA: `value = None` on Dialog.
    return std::string{};
}

// ---------------------------------------------------------------------------
// FocusableUnfocusableContainer
// ---------------------------------------------------------------------------

std::optional<bool> FocusableUnfocusableContainerBehavior::isFocusable(const Object&) const {
    return true;
}

std::optional<bool> FocusableUnfocusableContainerBehavior::setFocus(Object& object) {
    if (const ObjectPtr target = object.firstFocusableDescendant()) {
        return target->setFocus();
    }
    // No focusable descendant: say so with false rather than pretending.
    return false;
}

// ---------------------------------------------------------------------------
// ToolTip
// ---------------------------------------------------------------------------

std::optional<Role> ToolTipBehavior::role(const Object&) const { return Role::ToolTip; }

bool ToolTipBehavior::handleEvent(Object& object, const core::Event& event) {
    if (event.name != events::kShow && event.name != events::kWindowCreate) {
        return false;
    }
    if (!object.registry().settings().reportTooltips) {
        return true;
    }
    if (const auto& hooks = object.registry().presentation(); hooks.speakObject) {
        hooks.speakObject(object, OutputReason::Focus);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Notification
// ---------------------------------------------------------------------------

bool NotificationBehavior::handleEvent(Object& object, const core::Event& event) {
    if (event.name != events::kAlert && event.name != events::kShow &&
        event.name != events::kWindowCreate) {
        return false;
    }
    if (!object.registry().settings().reportHelpBalloons) {
        return true;
    }
    if (const auto& hooks = object.registry().presentation(); hooks.speakObject) {
        hooks.speakObject(object, OutputReason::Focus);
    }
    return true;
}

// ---------------------------------------------------------------------------
// NamelessItem
// ---------------------------------------------------------------------------

namespace {

constexpr int kLabelDepth = 4;
constexpr std::size_t kMaxLabels = 8;
constexpr int kMaxChildrenPerLevel = 16;

void collectLabels(const Object& object, int depth, std::vector<std::string>& out) {
    if (depth > kLabelDepth || out.size() >= kMaxLabels) {
        return;
    }
    const std::vector<ObjectPtr>& children = object.children();
    int seen = 0;
    for (const ObjectPtr& child : children) {
        if (child == nullptr || ++seen > kMaxChildrenPerLevel || out.size() >= kMaxLabels) {
            break;
        }
        if (child->states().has(State::Invisible)) {
            continue;
        }
        const Role role = child->role();
        if (role == Role::Label || role == Role::StaticText) {
            const std::string& text = child->name();
            // Nautilus puts an icon and a label of the same name side by
            // side; a label repeated is said once.
            if (!text.empty() && std::find(out.begin(), out.end(), text) == out.end()) {
                out.push_back(text);
            }
            continue;
        }
        collectLabels(*child, depth + 1, out);
    }
}

bool isGtk(const Object& object) {
    const std::string& toolkit = object.appInfo().toolkitName;
    return toolkit.size() >= 3 && (toolkit[0] == 'G' || toolkit[0] == 'g') &&
           (toolkit[1] == 'T' || toolkit[1] == 't') && (toolkit[2] == 'K' || toolkit[2] == 'k');
}

}  // namespace

std::string NamelessItemBehavior::labelText(const Object& object) {
    std::vector<std::string> labels;
    collectLabels(object, 1, labels);
    std::string out;
    for (const std::string& label : labels) {
        if (!out.empty()) {
            out += ' ';
        }
        out += label;
    }
    return out;
}

std::optional<std::string> NamelessItemBehavior::name(const Object& object) const {
    // Only when the item has nothing of its own: no name, and no label
    // pointing at it (the platform answer handles LABELLED_BY).
    if (!object.platformName().empty() || !isGtk(object)) {
        return std::nullopt;
    }
    Backend& backend = object.registry().backend();
    const std::vector<Ref> labels = backend.relation(object.ref(), PlatformRelation::LabelledBy);
    for (const Ref label : labels) {
        backend.release(label);
    }
    if (!labels.empty()) {
        return std::nullopt;
    }
    std::string text = labelText(object);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

// ---------------------------------------------------------------------------
// The Api-stage chooser
// ---------------------------------------------------------------------------

void chooseDefaultBehaviors(Object& object, BehaviorList& list) {
    // platformRole(), not role(): the chooser runs BEFORE the behaviours are
    // attached, and this is the role after the attribute refinement and
    // before any behaviour override -- exactly the key §6.2 asks for, and
    // what makes `xml-roles="tooltip"` on a plain section reach the ToolTip
    // behaviour.
    switch (object.platformRole()) {
        case Role::ProgressBar:
        case Role::BusyIndicator:
            list.push_back(behaviorEntry<ProgressBarBehavior>(
                "NVDAObjects.behaviors.ProgressBar"));
            break;
        case Role::Dialog:
            list.push_back(behaviorEntry<DialogBehavior>("NVDAObjects.behaviors.Dialog"));
            break;
        case Role::Alert:
            // An alert is both: its text is assembled like a dialog's, and it
            // announces itself when it shows. Notification first, because the
            // event handling is what matters when it appears.
            list.push_back(
                behaviorEntry<NotificationBehavior>("NVDAObjects.behaviors.Notification"));
            list.push_back(behaviorEntry<DialogBehavior>("NVDAObjects.behaviors.Dialog"));
            break;
        case Role::ToolTip:
        case Role::HelpBalloon:
            list.push_back(behaviorEntry<ToolTipBehavior>("NVDAObjects.behaviors.ToolTip"));
            break;
        case Role::ListItem:
        case Role::TreeViewItem:
        case Role::TableRow:
            list.push_back(behaviorEntry<NamelessItemBehavior>("alus.behaviors.NamelessItem"));
            break;
        default:
            break;
    }

    // An ARIA application or a container with a focusable descendant that the
    // author never made focusable. NVDA applies this in its web support; the
    // key on Linux is the same attribute.
    const Attributes& attributes = object.attributes();
    const auto xmlRoles = attributes.find("xml-roles");
    const bool isAriaApplication =
        xmlRoles != attributes.end() && xmlRoles->second.find("application") != std::string::npos;
    if (isAriaApplication && !object.states().has(State::Focusable)) {
        list.push_back(behaviorEntry<FocusableUnfocusableContainerBehavior>(
            "NVDAObjects.behaviors.FocusableUnfocusableContainer"));
    }
}

}  // namespace alus::a11y
