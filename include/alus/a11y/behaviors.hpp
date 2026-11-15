// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The platform-neutral behaviours of §6.3.
//
// `NVDAObjects/behaviors.py` is the most useful file in NVDA to port, because
// it knows nothing about Windows. Phase 4 takes the five that need no text
// layer; `EditableText`, `LiveText` and `Terminal` follow in phase 5, when
// there is a TextRange for them to diff.
//
// Each one keeps NVDA's class name, so a gesture map section or an app module
// written for `NVDAObjects.behaviors.ProgressBar` addresses it unchanged.
#pragma once

#include "alus/a11y/behavior.hpp"

#include <map>
#include <string>

namespace alus::a11y {

class Object;

// NVDA: behaviors.ProgressBar. A beep whose pitch is the percentage, a spoken
// percentage, or both; and never for a background window unless asked.
//
// The cache of last-reported values is keyed by the object's screen position,
// exactly as NVDA keys it ("beep,x,y"): a progress bar is often a fresh
// accessible on every update, and the position is what stays the same.
class ProgressBarBehavior final : public Behavior {
public:
    std::string_view className() const override { return "NVDAObjects.behaviors.ProgressBar"; }
    bool handleEvent(Object& object, const core::Event& event) override;

    // For the tests: the cache is process-wide, as NVDA's class attribute is.
    static void clearCache();
    static std::size_t cacheSize();
};

// NVDA: behaviors.Dialog. A dialog's description is assembled from the text of
// its children, because a GTK or Qt dialog puts its message in labels, not in
// its own description.
class DialogBehavior final : public Behavior {
public:
    std::string_view className() const override { return "NVDAObjects.behaviors.Dialog"; }
    std::optional<std::string> description(const Object& object) const override;
    // NVDA sets `value = None` for dialogs: a dialog has no value, and
    // whatever the toolkit put there is noise.
    std::optional<std::string> value(const Object& object) const override;

    // NVDA: Dialog.getDialogText. Public because phase 6 reuses it for
    // ARIA dialogs, and because it is worth testing on its own.
    static std::string dialogText(const Object& object, bool allowFocusedDescendants = true);
};

// ALUS's own (N139): a GTK list item, tree item or table row with no name
// takes one from the labels inside it. GTK names neither a `GtkListBoxRow`
// nor a `GtkListItem` from its child, so a row an application filled with
// labels of its own is read as just "list item" (the elements list's rows
// were, N133). GNOME's own applications name their rows; this is for the
// rest. GTK only: a web page's list item has no name either, but its text is
// read as text, and naming it would say everything twice.
class NamelessItemBehavior final : public Behavior {
public:
    std::string_view className() const override { return "alus.behaviors.NamelessItem"; }
    std::optional<std::string> name(const Object& object) const override;

    // The labels inside `object`, a few levels down, joined by spaces.
    static std::string labelText(const Object& object);
};

// NVDA: behaviors.FocusableUnfocusableContainer. A container the author forgot
// to make focusable, focused through its first focusable descendant.
class FocusableUnfocusableContainerBehavior final : public Behavior {
public:
    std::string_view className() const override {
        return "NVDAObjects.behaviors.FocusableUnfocusableContainer";
    }
    std::optional<bool> isFocusable(const Object& object) const override;
    std::optional<bool> setFocus(Object& object) override;
};

// NVDA: behaviors.ToolTip. Announced when it shows, and only if the user asked
// for tooltips.
class ToolTipBehavior final : public Behavior {
public:
    std::string_view className() const override { return "NVDAObjects.behaviors.ToolTip"; }
    std::optional<Role> role(const Object& object) const override;
    bool handleEvent(Object& object, const core::Event& event) override;
};

// NVDA: behaviors.Notification. A toast or a notification-area message: spoken
// on `alert` or `show`, and never focused.
class NotificationBehavior final : public Behavior {
public:
    std::string_view className() const override { return "NVDAObjects.behaviors.Notification"; }
    bool handleEvent(Object& object, const core::Event& event) override;
};

// The Api-stage chooser: which of the above an object gets. Installed by
// OverlayRegistry::installDefaults().
void chooseDefaultBehaviors(Object& object, BehaviorList& list);

}  // namespace alus::a11y
