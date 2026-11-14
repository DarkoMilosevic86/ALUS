// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Overlay classes, by composition.
//
// NVDA synthesises a class per object (`DynamicNVDAObjectType`): the API layer,
// the app module and every global plugin get to add, remove and reorder entries
// in `clsList`, and Python builds a type from the result. C++ cannot build
// types at run time, so ALUS composes BEHAVIOUR OBJECTS instead
// (architecture.md §6.2) and keeps the contract that matters:
//
//   * the list is ordered and the FIRST entry wins;
//   * each stage may insert at the front, append, or REMOVE what an earlier
//     stage proposed (NVDA's `clsList.remove(...)`);
//   * a behaviour that declines to answer passes the question on, ending at
//     the plain object -- which is what `super()` does in NVDA.
//
// A behaviour answers questions (properties), handles events, and contributes
// its NVDA class name to the object's script class chain, so a gesture map
// section written for `NVDAObjects.behaviors.Terminal` reaches it.
#pragma once

#include "alus/a11y/role.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alus::core {
struct Event;
}

namespace alus::a11y {

class Object;

class Behavior {
public:
    virtual ~Behavior();

    Behavior(const Behavior&) = delete;
    Behavior& operator=(const Behavior&) = delete;

    // The NVDA class this behaviour stands for, in NVDA's own spelling, e.g.
    // "NVDAObjects.behaviors.ProgressBar". It becomes part of the object's
    // script class chain.
    virtual std::string_view className() const = 0;

    // ---- property overrides ----
    // std::nullopt means "I have nothing to say": the next behaviour, and
    // finally the platform, answers. This is NVDA's `super().name`.
    virtual std::optional<std::string> name(const Object& object) const;
    virtual std::optional<std::string> description(const Object& object) const;
    virtual std::optional<std::string> value(const Object& object) const;
    virtual std::optional<Role> role(const Object& object) const;
    virtual std::optional<bool> isFocusable(const Object& object) const;
    virtual std::optional<bool> isPresentableFocusAncestor(const Object& object) const;

    // ---- behaviour ----
    // Returns true when the behaviour has dealt with the event and the object's
    // own handling must NOT run (NVDA: not calling super()).
    virtual bool handleEvent(Object& object, const core::Event& event);

    // Scripts the behaviour contributes to the object that owns it.
    //
    // Phase 5 needed this and phase 4 did not: NVDA's `editableText.EditableText`
    // is a mixin that brings ITS OWN SCRIPTS to the object, and a gesture map
    // section written for that class must reach them. A behaviour is the ALUS
    // equivalent of such a mixin (§6.2), so it gets the same ability.
    //
    // Called by Object::setBehaviors, after the class chain is rebuilt, so a
    // script bound here is addressable under the behaviour's own class name.
    // The behaviour lives as long as the object, so capturing `this` is safe.
    virtual void bindScripts(Object& object);
    // nullopt leaves focusing to Component::grab_focus.
    virtual std::optional<bool> setFocus(Object& object);

protected:
    Behavior() = default;
};

// A behaviour that has not been built yet. The name is its identity in the
// list, so a later stage can remove it by name, as NVDA removes a class.
struct BehaviorEntry {
    std::string name;
    std::function<std::unique_ptr<Behavior>()> factory;
};

using BehaviorList = std::vector<BehaviorEntry>;

// Helper for the common case: a behaviour with a default constructor.
template <class T>
BehaviorEntry behaviorEntry(std::string name) {
    return BehaviorEntry{std::move(name), [] { return std::make_unique<T>(); }};
}

// NVDA: `clsList.remove(cls)`, by name. Returns true if it was there.
bool removeBehavior(BehaviorList& list, std::string_view name);
bool hasBehavior(const BehaviorList& list, std::string_view name);
// NVDA: `clsList[0:0] = (cls,)` -- hand it to the front, where it wins.
void prependBehavior(BehaviorList& list, BehaviorEntry entry);

}  // namespace alus::a11y
