// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// gnome-shell: the activities overview, its search, the window switcher.
//
// WHY THIS MODULE EXISTS (FAZA10.md N121). Typing into the overview's search
// finds things -- "datot" brings up Files -- and gnome-shell marks the first
// result as the one Enter will start, but it does that with the SELECTED
// state while the keyboard focus stays in the entry. No focus event, so ALUS
// said nothing about the result until an arrow key moved the real focus onto
// it. The window switcher (alt+tab) does the same. Orca's gnome-shell script
// handles it the same way this does: a selected item becomes the locus of
// focus (`on_selected_changed` -> `set_locus_of_focus`), because gnome-shell
// "fails to implement the selection interface but fires state-changed
// selected".
//
// Once the result is the focus, the rest comes for free: gnome-shell reuses
// the result objects and renames them as the search narrows, and a name
// change on the focus is announced by the object layer -- so "Files" is said
// when the selected result BECOMES Files, without another selected event.
#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/app/modules.hpp"
#include "alus/core/log.hpp"

#include <memory>
#include <utility>

namespace alus::app {
namespace {

class GnomeShellModule final : public Module {
public:
    GnomeShellModule(AppKey key, Services services)
        : Module("gnome_shell", std::move(key), std::move(services),
                 {"appModules.gnome_shell.AppModule"}) {}

    void initialize() override {
        bindEvent(std::string{a11y::events::kStateChange},
                  [this](const a11y::ObjectPtr& object) { return onStateChange(object); });
    }

private:
    Disposition onStateChange(const a11y::ObjectPtr& object) {
        if (object == nullptr || services().a11y == nullptr) {
            return Disposition::Continue;
        }
        // Only an item that IS selected now: the same event with detail1 == 0
        // is the previous item letting go.
        if (!object->states().has(a11y::State::Selected) ||
            object->states().has(a11y::State::Invisible)) {
            return Disposition::Continue;
        }
        // Orca's exception too: the shell's own window claims things it
        // should not after the switcher closes.
        switch (object->role()) {
            case a11y::Role::Window:
            case a11y::Role::Frame:
            case a11y::Role::Application:
                return Disposition::Continue;
            default:
                break;
        }
        if (services().a11y->redirectFocus(object)) {
            ALUS_DEBUG("gnome-shell: selected {} becomes the focus", object->path());
            // Announced as a focus; the state change itself adds nothing.
            return Disposition::Consume;
        }
        return Disposition::Continue;
    }
};

}  // namespace

void registerGnomeShellModule(Registry& registry) {
    registry.add(Registry::Rule{
        "gnome_shell", "gnome-shell", "",
        [](const AppKey& key, const Services& services) -> ModulePtr {
            return std::make_unique<GnomeShellModule>(key, services);
        }});
}

}  // namespace alus::app
