// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// An object that owns scripts.
//
// Port of NVDA's `baseObject.ScriptableObject` (01-ARHITEKTURA.md §5.3). An
// object declares which NVDA CLASS NAMES it answers to — "the class chain" —
// because that is what a gesture map section names:
//
//     [globalCommands.GlobalCommands]
//         zoomOut = kb:nvda+shift+'
//
// A binding applies to an object when the object answers to that class name,
// which is how a binding written for a base class reaches a subclass, exactly
// as Python's MRO does it in NVDA.
#pragma once

#include "alus/script/script.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace alus::script {

class Catalog;

class ScriptableObject {
public:
    // `classChain` is most derived first. The names are NVDA's
    // `module.Class` spelling, since they are what gesture maps address.
    explicit ScriptableObject(std::vector<std::string> classChain,
                              const Catalog* catalog = nullptr);
    virtual ~ScriptableObject();

    ScriptableObject(const ScriptableObject&) = delete;
    ScriptableObject& operator=(const ScriptableObject&) = delete;

    const std::vector<std::string>& scriptClasses() const noexcept { return classChain_; }
    bool answersTo(std::string_view location) const;

    // Registers an implementation. Metadata comes from the catalogue row for
    // (most derived class that has one, `name`); anything set in `spec`
    // overrides it. Use the ALUS_SCRIPT macro below rather than calling this
    // directly.
    void bindScript(std::string name, ScriptFunction function, ScriptSpec spec = {});

    const Script* scriptByName(std::string_view name) const;
    std::vector<const ScriptInfo*> scripts() const;
    std::size_t scriptCount() const noexcept { return scripts_.size(); }

    // NVDA: `treeInterceptor.passThrough`. Consulted by step 6 of the lookup.
    virtual bool passThrough() const { return false; }

    // NVDA: `BrowseModeTreeInterceptor.getAlternativeScript`. A hook for phase
    // 6; the default answers with what was found.
    virtual const Script* alternativeScript(const input::Gesture& gesture,
                                            const Script* found) const;

protected:
    const Catalog* catalog() const noexcept { return catalog_; }

    // Replaces the class chain after construction. The object model needs it:
    // which NVDA classes an a11y::Object answers to depends on the overlay
    // behaviours chosen for it, and those are chosen once the object exists
    // and can be asked about its role and attributes (01-ARHITEKTURA.md §6.2).
    void setScriptClasses(std::vector<std::string> classChain);

private:
    std::vector<std::string> classChain_;
    const Catalog* catalog_;
    // The ScriptInfo objects this object owns. Their ADDRESSES are script
    // identities (repeat counting), so they must never move: hence the
    // indirection.
    std::vector<std::unique_ptr<ScriptInfo>> infos_;
    std::map<std::string, Script, std::less<>> scripts_;
};

// Declares a script inside a ScriptableObject subclass. The method must take
// `alus::input::Gesture&`:
//
//     ALUS_SCRIPT(toggleInputHelp,
//                 .description = N_("Turns input help on or off."),
//                 .category = N_("Input"),
//                 .gestures = {"kb:alus+1"});
//
// Fields left out are filled from data/scripts.csv, so NVDA's own description
// and category are not retyped (see Catalog).
#define ALUS_SCRIPT(method, ...)                                                      \
    bindScript(#method,                                                               \
               [this](::alus::input::Gesture& gesture) { this->method(gesture); },     \
               ::alus::script::ScriptSpec{__VA_ARGS__})

}  // namespace alus::script
