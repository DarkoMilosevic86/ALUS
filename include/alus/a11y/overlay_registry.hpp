// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Who gets to decide what an object behaves like.
//
// NVDA's `chooseNVDAObjectOverlayClasses(obj, clsList)` is called in three
// stages, in this order, and each one sees what the previous one proposed:
//
//     1. the API layer   (NVDAObjects/IAccessible/__init__.py and friends)
//     2. the app module  (appModules/<name>.py)
//     3. every global plugin
//
// ALUS keeps that contract exactly (01-ARHITEKTURA.md §6.2); only the result
// differs, being a list of behaviour objects rather than a synthesised class.
// The choosing keys on Linux are not NVDA's `windowClassName`:
//
//     (Application::get_toolkit_name, AtspiRole)   primary
//     Accessible::get_attributes()                secondary (xml-roles, tag, id)
//     Accessible::get_interfaces()                tertiary (Table? Text? Document?)
#pragma once

#include "alus/a11y/behavior.hpp"
#include "alus/core/extension_points.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace alus::a11y {

class Object;

class OverlayRegistry {
public:
    // The list is IN/OUT: insert, append, or remove what an earlier stage
    // proposed.
    using Chooser = std::function<void(Object&, BehaviorList&)>;

    enum class Stage {
        // The platform layer: the platform-neutral behaviours of §6.3.
        Api,
        // One app module, chosen by toolkit and application name (phase 8).
        AppModule,
        // Global plugins (phase 8).
        Plugin,
    };

    OverlayRegistry();
    ~OverlayRegistry();

    OverlayRegistry(const OverlayRegistry&) = delete;
    OverlayRegistry& operator=(const OverlayRegistry&) = delete;

    // While the returned handle lives, the chooser is registered. `release()`
    // keeps it for the life of the process, which is what the built-in
    // choosers want.
    [[nodiscard]] core::HandlerHandle add(Stage stage, Chooser chooser, std::string name);

    // Installs the platform-neutral Api-stage chooser: the five behaviours
    // phase 4 owns (ProgressBar, Dialog, FocusableUnfocusableContainer,
    // ToolTip, Notification). Called by Registry unless a test asks for a
    // bare registry.
    void installDefaults();

    // NVDA: the whole `DynamicNVDAObjectType.__call__` path, minus the type
    // synthesis. Runs the three stages in order and builds the behaviours.
    std::vector<std::unique_ptr<Behavior>> choose(Object& object) const;
    // The list before it is built; for tests and for the log.
    BehaviorList proposalsFor(Object& object) const;

    std::size_t chooserCount() const;

private:
    struct Entry {
        std::uint64_t id = 0;
        Stage stage = Stage::Api;
        std::string name;
        Chooser chooser;
    };

    std::vector<Entry> entries_;
    std::uint64_t nextId_ = 0;
    // `add` hands out handles that outlive this object in tests; the shared
    // state keeps that safe.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace alus::a11y
