// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The global state: what has the focus, what the user is looking at.
//
// Port of NVDA's `api.py` (01-ARHITEKTURA.md §6.4), transferred as it stands:
// the focus object AND ITS ANCESTOR CHAIN, `getFocusDifferenceLevel()`, the
// navigator object, the review position, the foreground object, the mouse
// object and the desktop object.
//
// THE CONTRACT THAT MUST NOT BE LOST: THE SETTERS RETURN bool. `setFocusObject`
// may refuse -- the object died between the event and the call, which happens
// every day -- and the caller must look at the answer. T4.9 is exactly this.
//
// `setFocusObject` is also where the ancestor chain is REUSED rather than
// rebuilt: it walks up from the new focus looking for an object the old chain
// already contains, and keeps the old objects above that point. That is what
// makes `focusDifferenceLevel` meaningful, and it is the basis of the
// focusEntered events (only the genuinely new ancestors are announced).
#pragma once

#include "alus/a11y/object.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace alus::a11y {

class Registry;

class Api {
public:
    explicit Api(Registry& registry);
    ~Api();

    Api(const Api&) = delete;
    Api& operator=(const Api&) = delete;

    // ---- focus ----
    const ObjectPtr& focus() const noexcept { return focus_; }
    // Outermost first (the desktop end), as NVDA returns it. The focus itself
    // is NOT in the list.
    const std::vector<ObjectPtr>& focusAncestors() const noexcept { return ancestors_; }
    // How much of the chain changed: the number of ancestors the new focus
    // shares with the old one. 0 means "everything is new".
    int focusDifferenceLevel() const noexcept { return focusDifferenceLevel_; }
    // Fires loseFocus on the old focus, builds the new ancestor chain, fires
    // focusExited for the old ancestors that are gone. Returns false when the
    // object is null or dead.
    bool setFocusObject(ObjectPtr object);

    // ---- foreground ----
    const ObjectPtr& foreground() const noexcept { return foreground_; }
    // Does NOT raise a window: it records which window ALUS believes is in
    // front (NVDA's own note on this function).
    bool setForegroundObject(ObjectPtr object);

    // ---- navigator ----
    // The object the numpad walks over, independent of the focus. Falls back
    // to the focus when nothing has been set.
    ObjectPtr navigator() const;
    // `isFocus` is true when the navigator moved because the focus moved; it
    // reaches the becomeNavigatorObject event, which phase 5's review cursor
    // needs in order to follow (or not follow) the focus.
    bool setNavigatorObject(ObjectPtr object, bool isFocus = false);

    // ---- mouse and desktop ----
    const ObjectPtr& mouseObject() const noexcept { return mouse_; }
    bool setMouseObject(ObjectPtr object);
    const ObjectPtr& desktopObject() const noexcept { return desktop_; }
    void setDesktopObject(ObjectPtr object);

    // NVDA: api.isTypingProtected(). True when the focus is a password field,
    // and the reason the character echo stays quiet there.
    bool isTypingProtected() const;
    // The answer phase 3 is waiting for (its handover item 3): is the focus a
    // field where typed characters should be echoed.
    bool isFocusEditable() const;

    // Clears everything. Called by resetConfiguration and by terminate(), so
    // that a restarted object model does not hold objects from the old one.
    void clear();

private:
    Registry& registry_;
    ObjectPtr focus_;
    std::vector<ObjectPtr> ancestors_;
    int focusDifferenceLevel_ = 0;
    ObjectPtr foreground_;
    ObjectPtr navigator_;
    ObjectPtr mouse_;
    ObjectPtr desktop_;
};

}  // namespace alus::a11y
