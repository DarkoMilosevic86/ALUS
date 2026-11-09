// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/overlay_registry.hpp"

#include "alus/a11y/behaviors.hpp"
#include "alus/a11y/object.hpp"
#include "alus/core/log.hpp"

#include <algorithm>
#include <array>

namespace alus::a11y {

Behavior::~Behavior() = default;

void Behavior::bindScripts(Object& /*object*/) {}

std::optional<std::string> Behavior::name(const Object&) const { return std::nullopt; }
std::optional<std::string> Behavior::description(const Object&) const { return std::nullopt; }
std::optional<std::string> Behavior::value(const Object&) const { return std::nullopt; }
std::optional<Role> Behavior::role(const Object&) const { return std::nullopt; }
std::optional<bool> Behavior::isFocusable(const Object&) const { return std::nullopt; }
std::optional<bool> Behavior::isPresentableFocusAncestor(const Object&) const {
    return std::nullopt;
}
bool Behavior::handleEvent(Object&, const core::Event&) { return false; }
std::optional<bool> Behavior::setFocus(Object&) { return std::nullopt; }

bool removeBehavior(BehaviorList& list, std::string_view name) {
    const auto it = std::find_if(list.begin(), list.end(), [name](const BehaviorEntry& entry) {
        return entry.name == name;
    });
    if (it == list.end()) {
        return false;
    }
    list.erase(it);
    return true;
}

bool hasBehavior(const BehaviorList& list, std::string_view name) {
    return std::any_of(list.begin(), list.end(),
                       [name](const BehaviorEntry& entry) { return entry.name == name; });
}

void prependBehavior(BehaviorList& list, BehaviorEntry entry) {
    list.insert(list.begin(), std::move(entry));
}

OverlayRegistry::OverlayRegistry() = default;

OverlayRegistry::~OverlayRegistry() { *alive_ = false; }

core::HandlerHandle OverlayRegistry::add(Stage stage, Chooser chooser, std::string name) {
    const std::uint64_t id = ++nextId_;
    entries_.push_back(Entry{id, stage, std::move(name), std::move(chooser)});
    // The handle may outlive the registry (a test that destroys the registry
    // first), so it checks before it reaches back in.
    std::weak_ptr<bool> alive = alive_;
    return core::HandlerHandle{[this, id, alive] {
        const auto locked = alive.lock();
        if (!locked || !*locked) {
            return;
        }
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [id](const Entry& entry) { return entry.id == id; });
        if (it != entries_.end()) {
            entries_.erase(it);
        }
    }};
}

std::size_t OverlayRegistry::chooserCount() const { return entries_.size(); }

BehaviorList OverlayRegistry::proposalsFor(Object& object) const {
    BehaviorList list;
    // The three stages in NVDA's order. Within a stage, registration order.
    for (const Stage stage : {Stage::Api, Stage::AppModule, Stage::Plugin}) {
        for (const Entry& entry : entries_) {
            if (entry.stage != stage) {
                continue;
            }
            entry.chooser(object, list);
        }
    }
    return list;
}

std::vector<std::unique_ptr<Behavior>> OverlayRegistry::choose(Object& object) const {
    const BehaviorList proposals = proposalsFor(object);
    std::vector<std::unique_ptr<Behavior>> behaviors;
    behaviors.reserve(proposals.size());
    for (const BehaviorEntry& entry : proposals) {
        if (!entry.factory) {
            ALUS_DEBUG_WARNING("overlay behaviour {} has no factory", entry.name);
            continue;
        }
        if (auto behavior = entry.factory()) {
            behaviors.push_back(std::move(behavior));
        }
    }
    return behaviors;
}

void OverlayRegistry::installDefaults() {
    add(Stage::Api, [](Object& object, BehaviorList& list) { chooseDefaultBehaviors(object, list); },
        "a11y.defaults")
        .release();
}

}  // namespace alus::a11y
