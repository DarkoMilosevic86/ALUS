// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// gnome-shell: the activities overview, its search, the window switcher.
//
// WHY THIS MODULE EXISTS (FAZA10.md N121). Typing into the overview's search
// finds things -- "datot" brings up Files -- and gnome-shell marks the result
// Enter will start with the SELECTED state while the keyboard focus stays in
// the entry. Measured on GNOME 48:
//
//   * after the first letter, ONE `object:state-changed:selected` for the
//     first result; after every later letter, no new one -- the existing
//     result objects are RENAMED as the search narrows;
//   * on the first letter, two focus events in a row: the entry, then the
//     shell's own window ("Main stage"). Keeping the second one left ALUS
//     believing the focus was on a window: the letters were not echoed, and
//     the window and the overview panel were announced in front of the result
//     -- long enough for the next key to cut the result off.
//
// So, as NVDA does for the Windows start menu's search (the suggestion is
// spoken, the focus stays in the edit field) and as Orca's gnome-shell script
// does for the window claim:
//
//   1. a focus event for the shell's window is refused (Orca: "spurious focus
//      claim from the gnome-shell window");
//   2. a result that becomes selected while the focus is in an editable field
//      is SPOKEN, and the focus stays where the keyboard is, so the letters
//      typed after it are still echoed and edited as text;
//   3. that result is remembered, and when gnome-shell renames it the new
//      name is spoken -- which is how "Terminal" turning into "Files" is heard.
//
// When the user arrows onto a result the real focus moves there, and the
// object layer announces it as any focus; nothing here speaks then.
#include "alus/a11y/events.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/app/modules.hpp"
#include "alus/core/log.hpp"

#include <memory>
#include <string>
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
        bindEvent(std::string{a11y::events::kNameChange},
                  [this](const a11y::ObjectPtr& object) { return onNameChange(object); });
    }

    bool shouldAllowFocusEvent(const a11y::ObjectPtr& object) const override {
        return object == nullptr || object->role() != a11y::Role::Window;
    }

private:
    // The focus is in a field the user is typing into, and the object is
    // somewhere else: the case where a selection has to be spoken because no
    // focus event will speak it.
    bool typingElsewhere(const a11y::ObjectPtr& object) const {
        const a11y::ObjectPtr& focus = services().a11y->api().focus();
        return focus != nullptr && !focus->sameAs(*object) &&
               focus->states().has(a11y::State::Editable);
    }

    void speak(const a11y::ObjectPtr& object) {
        const a11y::PresentationHooks& hooks = services().a11y->presentation();
        if (!hooks.speakObject) {
            return;
        }
        spokenName_ = object->name();
        hooks.speakObject(*object, a11y::OutputReason::Focus);
    }

    Disposition onStateChange(const a11y::ObjectPtr& object) {
        if (object == nullptr || services().a11y == nullptr) {
            return Disposition::Continue;
        }
        // Only an item that IS selected now: the same event with detail1 == 0
        // is the previous item letting go.
        if (!object->states().has(a11y::State::Selected) ||
            object->states().has(a11y::State::Invisible) ||
            object->role() == a11y::Role::Window || !typingElsewhere(object)) {
            return Disposition::Continue;
        }
        selected_ = object;
        services().a11y->setWatchedObject(object);
        ALUS_DEBUG("gnome-shell: selected result {} spoken, focus stays in the entry",
                   object->path());
        speak(object);
        return Disposition::Consume;
    }

    Disposition onNameChange(const a11y::ObjectPtr& object) {
        if (object == nullptr || selected_ == nullptr || services().a11y == nullptr ||
            !object->sameAs(*selected_)) {
            return Disposition::Continue;
        }
        if (!object->states().has(a11y::State::Selected) || !typingElsewhere(object)) {
            forget();
            return Disposition::Continue;
        }
        // gnome-shell renames every result several times per letter; the same
        // name twice is not news.
        if (object->name().empty() || object->name() == spokenName_) {
            return Disposition::Consume;
        }
        speak(object);
        return Disposition::Consume;
    }

    void forget() {
        selected_.reset();
        services().a11y->setWatchedObject(nullptr);
    }

    void terminate() override {
        if (services().a11y != nullptr) {
            forget();
        }
    }

    a11y::ObjectPtr selected_;
    std::string spokenName_;
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
