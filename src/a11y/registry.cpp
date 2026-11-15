// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/a11y/registry.hpp"

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <format>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace alus::a11y {
namespace {

// How many objects the initial focus scan may look at. AT-SPI has no "who has
// the focus" call, so this walk is the only way to answer alus+tab before the
// first focus event arrives -- and it must be a bounded cost at start-up, not
// a tree walk of every open application.
constexpr int kInitialFocusBudget = 512;

// How long a newly activated window may take to send its own focus event
// before ALUS looks for the focus itself (N130). GTK's arrives within a few
// milliseconds; long enough not to race it, short enough not to feel late.
constexpr std::chrono::milliseconds kForegroundFocusWait{250};

// "states×40 parent×31 …": the calls made between two snapshots of
// Backend::callsByName, most frequent first, for the "slow event" line.
std::string callBreakdown(const std::vector<std::pair<std::string, std::uint64_t>>& before,
                          const std::vector<std::pair<std::string, std::uint64_t>>& after) {
    std::vector<std::pair<std::string, std::uint64_t>> spent;
    for (const auto& [name, count] : after) {
        const auto old = std::find_if(before.begin(), before.end(),
                                      [&name](const auto& entry) { return entry.first == name; });
        const std::uint64_t was = old == before.end() ? 0 : old->second;
        if (count > was) {
            spent.emplace_back(name, count - was);
        }
    }
    std::sort(spent.begin(), spent.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::string out;
    for (const auto& [name, count] : spent) {
        if (!out.empty()) {
            out += ' ';
        }
        out += std::format("{}×{}", name, count);
    }
    return out;
}

}  // namespace

Registry::Registry(core::Loop& loop, Backend& backend, Options options)
    : loop_(loop),
      backend_(backend),
      options_(std::move(options)),
      limiter_(limiterConfig(options_.maxEventsPerApplication, options_.maxFocusEvents)),
      dispatcher_(&loop),
      api_(*this) {
    overlays_.installDefaults();
    installDispatchProviders();
}

Registry::~Registry() {
    stop();
    // stop() is a no-op when the registry was never started; the objects a
    // test or a tree walk created still have to go.
    releaseObjects();
}

void Registry::installDispatchProviders() {
    // Level 4, the terminal level: the object itself (§3.3). Levels 1-3 are
    // the global plugins, the app module and the tree interceptor, and they
    // arrive with phases 6 and 8; the chain runs without them meanwhile.
    dispatcher_.setTerminalProvider(
        [this](const core::Event& event) -> std::vector<core::EventDispatcher::TerminalHandler> {
            std::vector<core::EventDispatcher::TerminalHandler> handlers;
            const auto it = objects_.find(static_cast<Ref>(event.object));
            if (it == objects_.end()) {
                return handlers;
            }
            if (ObjectPtr object = it->second.lock()) {
                handlers.push_back([object](const core::Event& dispatched) {
                    object->handleEvent(dispatched);
                });
            }
            return handlers;
        });
}

bool Registry::start(std::string* error) {
    if (running_) {
        return true;
    }
    if (!backend_.subscribe(subscribedEventTypes(),
                            [this](PlatformEvent event) { onPlatformEvent(std::move(event)); },
                            error)) {
        return false;
    }
    running_ = true;

    if (const Ref desktopRef = backend_.desktop(); desktopRef != 0) {
        api_.setDesktopObject(adoptObject(desktopRef));
    } else {
        ALUS_WARNING("the accessibility bus has no desktop object");
    }

    if (const ObjectPtr focus = findInitialFocus()) {
        ALUS_DEBUG("initial focus: {} ({})", focus->name(), roleName(focus->role()));
        // Not announced: the user did not do anything, and ALUS saying the
        // focused control at start-up would talk over "ALUS started". The
        // state is set so that the first alus+tab answers.
        handleGainFocus(focus);
    } else {
        ALUS_DEBUG("no focused object found at start-up");
    }
    return true;
}

void Registry::stop() {
    if (foregroundTimer_ != 0) {
        loop_.cancelLater(foregroundTimer_);
        foregroundTimer_ = 0;
    }
    pendingForeground_.reset();
    if (!running_) {
        return;
    }
    backend_.unsubscribe();
    api_.clear();
    releaseObjects();
    running_ = false;
}

void Registry::releaseObjects() {
    // THE CYCLES HAVE TO BE BROKEN BY HAND. An object caches its parent, and
    // that parent caches its children: two strong references in a ring. Within
    // a session the ring is cut at the end of every pump cycle, when the
    // property caches are invalidated (§3.1). At shutdown there is no next
    // cycle, so dropping the registry's own references would leave the ring
    // alive as garbage -- with a DANGLING REFERENCE to this registry inside
    // it, because ~Object asks the registry for the backend in order to hand
    // its platform reference back. That is a use-after-free with a delay on
    // it, and it is what this function exists to prevent.
    //
    // The duplicate-focus marker is one more strong reference of exactly that
    // kind, so it goes first. It is a single object rather than a ring, but
    // one object outliving its registry is the same use-after-free (N40).
    lastQueuedFocus_.reset();
    std::vector<ObjectPtr> alive;
    alive.reserve(objects_.size());
    for (const auto& [ref, weak] : objects_) {
        if (ObjectPtr object = weak.lock()) {
            alive.push_back(std::move(object));
        }
    }
    for (const ObjectPtr& object : alive) {
        object->cacheScope().invalidate();
    }
    cycleObjects_.clear();
    objects_.clear();
    // `alive` goes out of scope here, and the objects go with it: no rings
    // left, every platform reference handed back.
}

ObjectPtr Registry::objectFor(Ref ref) {
    if (ref == 0) {
        return nullptr;
    }
    const auto it = objects_.find(ref);
    if (it != objects_.end()) {
        if (ObjectPtr existing = it->second.lock()) {
            // Held for the rest of the cycle, even if the only other holder
            // was a property cache about to be invalidated.
            cycleObjects_.push_back(existing);
            return existing;
        }
        objects_.erase(it);
    }
    auto object = std::make_shared<Object>(*this, ref);
    objects_.emplace(ref, object);
    // The overlay behaviours are chosen once, here, with the object able to
    // answer about its role, attributes and interfaces (§6.2).
    object->setBehaviors(overlays_.choose(*object));
    cycleObjects_.push_back(object);
    // Mechanism 2 of §12.2, in NVDA's own moment: after the overlay classes
    // and the script class chain, before anyone has asked the object anything.
    // Held in cycleObjects_ first, so a hook that looks at the object's
    // ancestry cannot destroy it from under itself.
    if (objectInit_) {
        objectInit_(*object);
    }
    return object;
}

ObjectPtr Registry::adoptObject(Ref ref) {
    if (ref == 0) {
        return nullptr;
    }
    ObjectPtr object = objectFor(ref);
    // objectFor took a reference of its own; the caller's is now spent.
    backend_.release(ref);
    return object;
}

ObjectPtr Registry::desktop() { return api_.desktopObject(); }

std::size_t Registry::cachedObjectCount() const {
    return static_cast<std::size_t>(
        std::count_if(objects_.begin(), objects_.end(),
                      [](const auto& entry) { return !entry.second.expired(); }));
}

void Registry::releaseCycleObjects() { cycleObjects_.clear(); }

void Registry::sweepObjectCache() {
    for (auto it = objects_.begin(); it != objects_.end();) {
        it = it->second.expired() ? objects_.erase(it) : std::next(it);
    }
}

void Registry::primeFromEvent(const ObjectPtr& object) {
    if (object == nullptr) {
        return;
    }
    // AT-SPI delivers a few property values WITH an event (§6.1's
    // `properties`), and libatspi keeps them only for the duration of the
    // callback. So the one chance to have them for free is here, and it is
    // taken for focus events, where the 50 ms budget lives. Everything these
    // getters read is read again a moment later by the presentation layer --
    // from this object's own one-cycle cache, at no cost (§3.5).
    object->attributes();
    object->location();
    object->name();
    object->role();
    object->states();
}

bool Registry::onPlatformEvent(PlatformEvent event) {
    const std::string_view name = alusEventName(event.type, event.detail1);
    // The arrival of every platform event, at IO level. Without it a gap in
    // the log could not tell "nothing arrived" from "it arrived and was
    // dropped" -- which is exactly what the Super-key investigation needed
    // (phase-10.md, N120).
    if (core::Log::instance().isEnabledFor(core::LogLevel::Io)) {
        ALUS_IO("event in: {} ({}) app {:#x}", event.type,
                name.empty() ? std::string_view{"ignored"} : name,
                static_cast<std::uintptr_t>(event.application));
    }
    if (name.empty()) {
        // Not an event ALUS has a use for. The references still have to go
        // back, or every mouse move leaks one accessible.
        if (event.source != 0) {
            backend_.release(event.source);
        }
        if (event.object != 0) {
            backend_.release(event.object);
        }
        return false;
    }

    auto* payload = new EventPayload{};
    payload->atspiType = std::move(event.type);
    payload->detail1 = event.detail1;
    payload->detail2 = event.detail2;
    payload->text = std::move(event.text);
    payload->relatedObject = static_cast<std::uintptr_t>(event.object);

    // The platform references live exactly as long as the event does: the
    // deleter hands them back whether the event is dispatched or discarded.
    // Without this the limiter dropping 990 of 1000 flood events would drop
    // 990 references on the floor (T4.3 is also a leak test under valgrind).
    const Ref source = event.source;
    const Ref related = event.object;
    Backend* backend = &backend_;
    std::shared_ptr<EventPayload> owned(payload, [backend, source, related](EventPayload* p) {
        if (source != 0) {
            backend->release(source);
        }
        if (related != 0) {
            backend->release(related);
        }
        delete p;
    });

    core::RawEvent raw;
    raw.name = std::string{name};
    raw.object = static_cast<core::ObjectId>(source);
    raw.source = event.application;
    raw.payload = std::move(owned);
    // An item becoming selected is where the user is when the focus does not
    // move -- gnome-shell's search results, list boxes driven from an entry.
    // It must survive the flood that usually comes with it (N121).
    raw.priority = payload->atspiType == "object:state-changed:selected" && payload->detail1 != 0;

    // A focus event is rare and urgent; everything else may be one of a
    // thousand. Only the urgent one is worth creating an object for here, and
    // it is the only one that can use the event's delivered properties.
    const bool urgent = name == events::kGainFocus || name == events::kForeground;
    if (urgent) {
        const ObjectPtr object = objectFor(source);
        primeFromEvent(object);
        if (name == events::kGainFocus) {
            // NVDA: eventHandler.lastQueuedFocusObject, set HERE and not at
            // dispatch time. An app module asks it "have I already been told
            // about this object", and the answer has to cover the events still
            // sitting in the limiter.
            lastQueuedFocus_ = object;
        }
    }
    if (!limiter_.addEvent(std::move(raw))) {
        return false;
    }
    // AND ASK FOR A PUMP. Without this the event sits in the limiter until
    // something else happens to pump -- a key press, a timer -- and a focus
    // change made with the mouse would be announced late or not at all. This
    // is invariant 1 of §3.1: a platform event requests a cycle, and a focus
    // event requests it immediately, because response time to focus is the
    // one latency the user feels.
    loop_.requestPump(urgent);
    return true;
}

void Registry::queueEvent(std::string name, const ObjectPtr& object,
                          std::shared_ptr<EventPayload> payload) {
    core::Event event;
    event.name = std::move(name);
    event.object = object != nullptr ? object->id() : 0;
    event.payload = std::move(payload);
    dispatcher_.queueEvent(std::move(event));
}

std::size_t Registry::pumpEvents() {
    // The focused object and its ancestors are exempt from the per-application
    // cap: whatever else an application is doing, what the user is touching is
    // never dropped (§3.4).
    std::vector<core::ObjectId> alwaysAllowed;
    if (const ObjectPtr& focus = api_.focus(); focus != nullptr) {
        alwaysAllowed.push_back(focus->id());
    }
    for (const ObjectPtr& ancestor : api_.focusAncestors()) {
        if (ancestor != nullptr) {
            alwaysAllowed.push_back(ancestor->id());
        }
    }
    if (watched_ != nullptr) {
        alwaysAllowed.push_back(watched_->id());
    }

    const std::size_t emitted = limiter_.pumpAll(
        [this](const core::RawEvent& raw) {
            // The object is created here, after the limiter decided this event
            // is worth the work -- a flood of 1000 events must not cost 1000
            // objects.
            ObjectPtr object = objectFor(static_cast<Ref>(raw.object));
            if (object == nullptr) {
                return false;
            }
            auto payload = std::static_pointer_cast<EventPayload>(raw.payload);

            // How long one event took, focus to announcement, when it was
            // long enough to hear (N127: about 200 ms from an arrow key to
            // speech in Nautilus, with nothing logged in between). Almost all
            // of it is synchronous AT-SPI calls into the application, so the
            // line names the object and its role; a pattern across runs says
            // which property is the expensive one.
            struct SlowEventTrace {
                std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
                const core::RawEvent& raw;
                const ObjectPtr& object;
                const Backend& backend;
                std::uint64_t callsBefore;
                // Taken only when the line can be written: a copy of a few
                // dozen counters per event is nothing, but it is not free.
                std::vector<std::pair<std::string, std::uint64_t>> byNameBefore =
                    core::Log::instance().isEnabledFor(core::LogLevel::Debug)
                        ? backend.callsByName()
                        : std::vector<std::pair<std::string, std::uint64_t>>{};
                ~SlowEventTrace() {
                    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started);
                    if (took.count() >= 50) {
                        // The call count tells many cheap calls from one
                        // expensive one (N128); the breakdown says which
                        // calls they were (N129).
                        ALUS_DEBUG("slow event: {} for {} ({}) took {} ms, {} AT-SPI calls: {}",
                                   raw.name, object->path(), roleName(object->role()),
                                   took.count(), backend.callCount() - callsBefore,
                                   callBreakdown(byNameBefore, backend.callsByName()));
                    }
                }
            } slowEventTrace{std::chrono::steady_clock::now(), raw, object, backend_,
                             backend_.callCount()};

            if (raw.name == events::kGainFocus) {
                if (!object->isAlive()) {
                    // Refusing makes the limiter try an older focus event,
                    // which is the whole point of its second stage.
                    ALUS_DEBUG("focus event for a dead object; falling back");
                    return false;
                }
                // Generalised hook 1 (§12.2): the app module gets to say the
                // event is spurious. Before handleGainFocus, because the point
                // is that NOTHING about this event happens -- no ancestor
                // chain, no foreground, no announcement.
                if (focusEventFilter_ && !focusEventFilter_(object)) {
                    ALUS_DEBUG("focus event for {} refused by the app module",
                               object->path());
                    return false;
                }
                if (!handleGainFocus(object)) {
                    ALUS_DEBUG("focus event for {} ({}) not handled", object->path(),
                               roleName(object->role()));
                    return false;
                }
            } else if (raw.name == events::kForeground) {
                if (!object->isAlive()) {
                    return false;
                }
                api_.setForegroundObject(object);
                // N136: GTK 4 sends the window's focus event BEFORE
                // `window:activate` (alus-elements: focused at .280,
                // activated at .284). handleGainFocus has then already
                // dispatched the foreground -- and its cancel -- before the
                // focus was spoken; dispatching this one again cancelled the
                // title, the list and the item just queued, which the user
                // heard as speech cut off whenever Nautilus, the elements
                // list or the settings window opened. On Windows, NVDA's
                // order is the other way round.
                if (focusIsInside(*object)) {
                    ALUS_DEBUG("window:activate after its focus; already announced");
                    return true;
                }
                armForegroundFocusCheck(object);
            } else if (raw.name == events::kActiveDescendantChange) {
                // The container's active descendant changed. When the
                // container has the focus, that descendant is what the user is
                // now on, so it becomes a focus event -- which is how a list
                // box that keeps the focus on itself is usable at all.
                const ObjectPtr& focus = api_.focus();
                const bool containerHasFocus =
                    focus != nullptr && focus->sameAs(*object);
                if (containerHasFocus && payload != nullptr &&
                    payload->relatedObject != 0) {
                    if (ObjectPtr descendant =
                            objectFor(static_cast<Ref>(payload->relatedObject))) {
                        if (descendant->isAlive() && handleGainFocus(descendant)) {
                            core::Event event;
                            event.name = std::string{events::kGainFocus};
                            event.object = descendant->id();
                            event.payload = raw.payload;
                            dispatcher_.executeEvent(event);
                            eventDispatched_.notify(event, descendant);
                            ++dispatched_;
                            return true;
                        }
                    }
                }
            } else if (raw.name == events::kCaret && caretMovesTheFocus(object)) {
                // N132: the caret moved in an editable control that has the
                // focus while ALUS believed the focus was elsewhere -- an
                // address bar that was focused before ALUS could see it, so
                // ctrl+l sends no focus event. The user is typing there, and
                // the field is where they are.
                ALUS_DEBUG("caret moved in focused {} ({}); it is the focus",
                           object->path(), roleName(object->role()));
                if (handleGainFocus(object)) {
                    core::Event event;
                    event.name = std::string{events::kGainFocus};
                    event.object = object->id();
                    dispatcher_.executeEvent(event);
                    eventDispatched_.notify(event, object);
                    ++dispatched_;
                    return true;
                }
            } else if (raw.name == events::kStateChange && payload != nullptr &&
                       payload->detail1 != 0 &&
                       payload->atspiType == "object:state-changed:selected" &&
                       selectionFollowsFocus(object) && handleGainFocus(object)) {
                // See selectionFollowsFocus: the selected item IS where the
                // user is, announced as a focus.
                core::Event event;
                event.name = std::string{events::kGainFocus};
                event.object = object->id();
                event.payload = raw.payload;
                dispatcher_.executeEvent(event);
                eventDispatched_.notify(event, object);
                ++dispatched_;
                return true;
            }

            core::Event event;
            event.name = raw.name;
            event.object = raw.object;
            event.payload = raw.payload;
            dispatcher_.executeEvent(event);
            // After the chain, never inside it: see eventDispatched().
            eventDispatched_.notify(event, object);
            ++dispatched_;
            return true;
        },
        alwaysAllowed);
    if (emitted > 0) {
        announceShownChoice();
    }
    return emitted;
}

bool Registry::selectionFollowsFocus(const ObjectPtr& object) const {
    // GTK 4 lists move the SELECTION and not the focus (phase-10.md N127,
    // Nautilus 48): opening a folder focuses the list ("list") and selects
    // the first file; type-ahead selects the file whose name was typed; the
    // first arrow after opening selects the next row. Only `selected` comes,
    // so the first file was never said and letters said nothing. NVDA treats
    // a selection inside the focused list as the focus (IAccessible's
    // `selection` event), and Orca does (`on_selected_changed`); so does this,
    // when:
    //   * the object is an item, alive, selected now, and not the focus;
    //   * the focus is its list -- an ancestor a few levels up, through
    //     GTK's wrapper panels -- or another item of the same list;
    //   * and the focus is not itself still selected: that is a selection
    //     being EXTENDED (ctrl+a, shift+arrows), where the user stays put.
    const ObjectPtr& focus = api_.focus();
    if (object == nullptr || focus == nullptr || focus->sameAs(*object)) {
        return false;
    }
    const auto isItem = [](Role role) {
        return role == Role::ListItem || role == Role::TableRow || role == Role::TreeViewItem ||
               role == Role::Icon;
    };
    if (!isItem(object->role()) || !object->isAlive() ||
        !object->states().has(State::Selected)) {
        return false;
    }
    const bool focusGone = !focus->isAlive();
    if (focusGone || isItem(focus->role())) {
        if (focus->states().has(State::Selected)) {
            return false;
        }
        const ObjectPtr parent = object->parent();
        const ObjectPtr focusParent = focusGone ? nullptr : focus->parent();
        if (parent != nullptr && focusParent != nullptr && parent->sameAs(*focusParent)) {
            return true;
        }
        // Entering a folder in Nautilus REPLACES the rows: the focused row is
        // removed, GTK sends no focus event (the list already had it), and
        // the first arrow selects a row of the new folder -- a dead focus
        // answers no parent (log 16:13:43, N128: the first file was never
        // said). The chain remembered when the focus was set still names the
        // list and the view around it; a row up to two levels below either
        // is in that list. The sidebar is further away than that.
        const std::vector<ObjectPtr>& chain = api_.focusAncestors();
        constexpr std::size_t kNearAncestors = 2;
        constexpr int kLevels = 2;
        ObjectPtr above = object->parent();
        for (int level = 0; level < kLevels && above != nullptr; ++level) {
            for (std::size_t back = 1; back <= kNearAncestors && back <= chain.size(); ++back) {
                const ObjectPtr& near = chain[chain.size() - back];
                if (near != nullptr && near->sameAs(*above)) {
                    return true;
                }
            }
            above = above->parent();
        }
        return false;
    }
    constexpr int kMaxLevels = 4;
    ObjectPtr ancestor = object->parent();
    for (int level = 0; level < kMaxLevels && ancestor != nullptr; ++level) {
        if (ancestor->sameAs(*focus)) {
            return true;
        }
        ancestor = ancestor->parent();
    }
    return false;
}

void Registry::announceShownChoice() {
    // A combo box that reports its choice through neither Value nor
    // Selection -- GTK 4's AdwComboRow and GtkDropDown -- changes it with no
    // event of its own: the arrows send `selected` for items of a hidden
    // list, and the value ALUS derives (Object::value, shownChoiceText) is
    // simply different afterwards. So after every cycle that dispatched
    // anything, the focused combo box's value is compared with the one last
    // seen and spoken when it moved (phase-10.md N124). A combo box with Value
    // or Selection is left alone: its own events already announce it.
    const ObjectPtr& focus = api_.focus();
    if (focus == nullptr || focus->role() != Role::ComboBox ||
        focus->hasInterface(Interface::Value) || focus->hasInterface(Interface::Selection)) {
        shownChoiceOwner_ = 0;
        return;
    }
    const std::string& value = focus->value();
    if (shownChoiceOwner_ != focus->id()) {
        shownChoiceOwner_ = focus->id();
        shownChoice_ = value;
        return;
    }
    if (value == shownChoice_ || value.empty()) {
        return;
    }
    shownChoice_ = value;
    if (presentation_.speakPropertyChange) {
        PropertyChange change;
        change.value = true;
        presentation_.speakPropertyChange(*focus, change, OutputReason::Change);
    }
}

bool Registry::handleGainFocus(const ObjectPtr& object) {
    // Port of NVDA's eventHandler.doPreGainFocus.
    if (object == nullptr) {
        return false;
    }
    if (!api_.setFocusObject(object)) {
        return false;
    }

    // The chain changed at the very top, so the window changed too.
    if (api_.focusDifferenceLevel() <= 1) {
        ObjectPtr newForeground = activeWindow();
        if (newForeground == nullptr) {
            ALUS_DEBUG_WARNING("no active window; using the focus ancestry");
            const std::vector<ObjectPtr>& ancestors = api_.focusAncestors();
            newForeground = ancestors.size() > 1 ? ancestors[1] : object;
        }
        if (!api_.setForegroundObject(newForeground)) {
            return false;
        }
        core::Event event;
        event.name = std::string{events::kForeground};
        event.object = newForeground->id();
        dispatcher_.executeEvent(event);
    }

    // focusEntered for the genuinely new ancestors only. This is what
    // focusDifferenceLevel is for, and why tabbing inside one dialog does not
    // announce the dialog again.
    const std::vector<ObjectPtr>& ancestors = api_.focusAncestors();
    for (std::size_t index = static_cast<std::size_t>(std::max(0, api_.focusDifferenceLevel()));
         index < ancestors.size(); ++index) {
        const ObjectPtr& ancestor = ancestors[index];
        if (ancestor == nullptr) {
            continue;
        }
        core::Event event;
        event.name = std::string{events::kFocusEntered};
        event.object = ancestor->id();
        dispatcher_.executeEvent(event);
    }

    // Phase 3's handover, item 1: the grab set depends on which scripts could
    // run, and that depends on the focus (O6). Without this the object
    // navigation keys exist and do nothing.
    if (focusChanged_) {
        focusChanged_();
    }
    return true;
}

ObjectPtr Registry::activeWindow() {
    const ObjectPtr desktopObject = api_.desktopObject();
    if (desktopObject == nullptr) {
        return nullptr;
    }
    // Desktop -> applications -> windows. The active window carries
    // ATSPI_STATE_ACTIVE, which is the one state no screen reader announces
    // and every screen reader needs.
    for (const ObjectPtr& application : desktopObject->children()) {
        if (application == nullptr) {
            continue;
        }
        for (const ObjectPtr& window : application->children()) {
            if (window == nullptr) {
                continue;
            }
            const std::uint64_t bits = backend_.states(window->ref());
            if ((bits & platformStateBit(PlatformState::Active)) != 0) {
                return window;
            }
        }
    }
    return nullptr;
}

ObjectPtr Registry::findInitialFocus() {
    const ObjectPtr window = activeWindow();
    if (window == nullptr) {
        return nullptr;
    }
    if (ObjectPtr focused = findFocusedIn(window)) {
        return focused;
    }
    // The window itself is a usable answer: alus+tab then says which window
    // the user is in, which beats silence.
    return window;
}

ObjectPtr Registry::findFocusedIn(const ObjectPtr& window) {
    if (window == nullptr) {
        return nullptr;
    }
    if (window->states().has(State::Focused)) {
        return window;
    }
    // BREADTH first, with a budget: a web page has more objects than this is
    // worth, and the focused control (a document, an address bar) sits a few
    // levels down, while depth first spent the budget inside the page.
    //
    // DOCUMENTS LAST (N132): Chrome keeps FOCUSED on the page's document while
    // the address bar has the real focus, which is where a new window puts
    // it. Taking the document made ALUS browse a page the keys were not going
    // to: ctrl+l moved nothing (the address bar already had the focus), so no
    // event ever corrected it, and the letters typed into the address bar
    // were eaten as quick-nav keys. So the window's own controls are searched
    // first, and a document -- with everything inside it -- only after them.
    int budget = kInitialFocusBudget;
    std::deque<ObjectPtr> queue;
    std::deque<ObjectPtr> documents;
    for (const ObjectPtr& child : window->children()) {
        queue.push_back(child);
    }
    ObjectPtr focusedDocument;
    while (budget-- > 0) {
        if (queue.empty()) {
            if (documents.empty()) {
                break;
            }
            // Outside the documents nothing has the focus. A focused document
            // is the answer; otherwise the focus is somewhere inside one.
            if (focusedDocument != nullptr) {
                return focusedDocument;
            }
            for (const ObjectPtr& child : documents.front()->children()) {
                queue.push_back(child);
            }
            documents.pop_front();
            continue;
        }
        ObjectPtr current = std::move(queue.front());
        queue.pop_front();
        if (current == nullptr) {
            continue;
        }
        const bool isDocument = current->role() == Role::Document;
        if (current->states().has(State::Focused)) {
            if (!isDocument) {
                return current;
            }
            if (focusedDocument == nullptr) {
                focusedDocument = current;
            }
        }
        if (isDocument) {
            documents.push_back(std::move(current));
            continue;
        }
        for (const ObjectPtr& child : current->children()) {
            queue.push_back(child);
        }
    }
    if (focusedDocument != nullptr) {
        return focusedDocument;
    }
    if (budget <= 0) {
        ALUS_DEBUG("focus scan of {} gave up after {} objects", window->name(),
                   kInitialFocusBudget);
    }
    return nullptr;
}

bool Registry::caretMovesTheFocus(const ObjectPtr& object) const {
    if (object == nullptr || !object->isAlive()) {
        return false;
    }
    const StateSet& states = object->states();
    if (!states.has(State::Focused) || !states.has(State::Editable)) {
        return false;
    }
    const ObjectPtr& focus = api_.focus();
    if (focus == nullptr) {
        return true;
    }
    if (focus->sameAs(*object)) {
        return false;
    }
    // The focus inside this field (a combo box's entry reporting for it) or
    // this field inside the focus is the same place.
    for (const ObjectPtr& ancestor : api_.focusAncestors()) {
        if (ancestor != nullptr && ancestor->sameAs(*object)) {
            return false;
        }
    }
    const ObjectPtr parent = object->parent();
    return parent == nullptr || !parent->sameAs(*focus);
}

bool Registry::focusIsInside(const Object& window) const {
    const ObjectPtr& focus = api_.focus();
    if (focus == nullptr) {
        return false;
    }
    if (focus->sameAs(window)) {
        return true;
    }
    for (const ObjectPtr& ancestor : api_.focusAncestors()) {
        if (ancestor != nullptr && ancestor->sameAs(window)) {
            return true;
        }
    }
    return false;
}

void Registry::armForegroundFocusCheck(const ObjectPtr& window) {
    // NVDA gets the focus from the system when a window comes up; AT-SPI
    // leaves it to the toolkit, and Chrome sends `window:activate` and then
    // nothing at all until a key moves the focus -- so ALUS said nothing when
    // Chrome opened (N130). GTK sends its focus event within a few
    // milliseconds of the activation, so the check waits briefly and does
    // nothing when the focus has arrived meanwhile.
    if (foregroundTimer_ != 0) {
        loop_.cancelLater(foregroundTimer_);
    }
    pendingForeground_ = window;
    foregroundTimer_ = loop_.callLater(kForegroundFocusWait,
                                       [this] { checkFocusAfterForeground(); },
                                       "foreground-focus-check");
}

void Registry::checkFocusAfterForeground() {
    foregroundTimer_ = 0;
    const ObjectPtr window = std::move(pendingForeground_);
    pendingForeground_.reset();
    if (!running_ || window == nullptr || !window->isAlive()) {
        return;
    }
    const ObjectPtr& foreground = api_.foreground();
    if (foreground == nullptr || !foreground->sameAs(*window)) {
        return;  // another window came up since
    }
    if (focusIsInside(*window)) {
        return;  // the toolkit did send its focus event
    }
    ObjectPtr target = findFocusedIn(window);
    if (target == nullptr) {
        // Nothing reports FOCUSED yet: say the window, which is where the
        // user is, instead of nothing.
        target = window;
    }
    ALUS_DEBUG("no focus event after {} came to the foreground; focusing {} ({})",
               window->name(), target->name(), roleName(target->role()));
    if (!handleGainFocus(target)) {
        return;
    }
    core::Event event;
    event.name = std::string{events::kGainFocus};
    event.object = target->id();
    dispatcher_.executeEvent(event);
    eventDispatched_.notify(event, target);
    ++dispatched_;
}

void Registry::setAppModuleProvider(AppModuleProvider provider) {
    appModuleProvider_ = std::move(provider);
}

script::ScriptableObject* Registry::appModuleFor(const AppInfo& info) const {
    return appModuleProvider_ ? appModuleProvider_(info) : nullptr;
}

void Registry::setObjectInitHook(ObjectInitHook hook) { objectInit_ = std::move(hook); }

void Registry::setFocusEventFilter(FocusEventFilter filter) {
    focusEventFilter_ = std::move(filter);
}

void Registry::setContainerOverride(ContainerOverride override) {
    containerOverride_ = std::move(override);
}

ObjectPtr Registry::containerFor(const Object& object) const {
    return containerOverride_ ? containerOverride_(object) : nullptr;
}

void Registry::setFocusChangedHook(std::function<void()> hook) {
    focusChanged_ = std::move(hook);
}

void Registry::setNavigatorChangedHook(NavigatorChangedHook hook) {
    navigatorChanged_ = std::move(hook);
}

void Registry::notifyNavigatorChanged(const ObjectPtr& object, bool isFocus) const {
    if (navigatorChanged_) {
        navigatorChanged_(object, isFocus);
    }
}

}  // namespace alus::a11y
