// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/script/dispatch.hpp"

#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/gesture.hpp"
#include "alus/script/catalog.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace alus::script {
namespace {

constexpr std::string_view kEmulationPrefix = "kb:";
// NVDA's SCRCAT_KBEMU. The category name is user interface; it is spelled the
// same here so the gesture dialog groups emulated keys as NVDA's does.
// Translators: the name of the gesture-dialog category that holds the
// emulated system keyboard keys. N_ and not _: this is a constexpr, so it
// carries the msgid and the point of use translates it.
constexpr std::string_view kEmulationCategory = N_("Emulated system keyboard keys");

}  // namespace

Dispatcher::Dispatcher(core::Loop* loop, const Catalog* catalog)
    : loop_(loop), catalog_(catalog) {}

Dispatcher::~Dispatcher() = default;

void Dispatcher::setContextProvider(ContextProvider provider) {
    provider_ = std::move(provider);
}

void Dispatcher::setGlobalMaps(std::vector<const input::GestureMap*> maps) {
    globalMaps_ = std::move(maps);
}

void Dispatcher::setBuiltinMap(const input::GestureMap* map) { builtinMap_ = map; }

void Dispatcher::setKeyEmulator(KeyEmulator emulator) { keyEmulator_ = std::move(emulator); }

void Dispatcher::setMultiPressTimeout(std::chrono::milliseconds timeout) {
    multiPressTimeout_ = timeout;
}

std::vector<Dispatcher::MapHit> Dispatcher::globalMapHits(const input::Gesture& gesture) const {
    // NVDA: getGlobalMapScripts. The result is ORDERED by resolution priority:
    // map by map, and within a map, identifier by identifier — so that
    // `kb(laptop):` beats `kb:` (T3.2) and user beats locale beats driver
    // (T3.3).
    std::vector<MapHit> hits;
    const std::vector<std::string> identifiers = gesture.normalizedIdentifiers();
    for (const input::GestureMap* map : globalMaps_) {
        if (map == nullptr) {
            continue;
        }
        for (const std::string& identifier : identifiers) {
            const std::vector<input::GestureMap::Entry>* entries = map->entriesForNormalized(identifier);
            if (entries == nullptr) {
                continue;
            }
            for (const input::GestureMap::Entry& entry : *entries) {
                hits.push_back(MapHit{entry.location, entry.script});
            }
        }
    }
    return hits;
}

const Script* Dispatcher::keyEmulationScript(std::string_view keyName) {
    const auto existing = emulationScripts_.find(keyName);
    if (existing != emulationScripts_.end()) {
        return &existing->second;
    }
    auto info = std::make_unique<ScriptInfo>();
    info->name = std::string(kEmulationPrefix) + std::string(keyName);
    info->location = "inputCore.KeyboardEmulation";
    info->category = _(std::string(kEmulationCategory).c_str());
    // Translators: the description of an emulated keyboard command, as shown
    // in input help and in the gesture dialog; %s is the key, e.g. "shift+tab".
    info->description =
        i18n::substitute(_("Emulates pressing %s on the system keyboard"), keyName);
    ScriptInfo* raw = info.get();
    emulationInfos_[std::string(keyName)] = std::move(info);

    Script script;
    script.info = raw;
    const std::string name(keyName);
    KeyEmulator emulator = keyEmulator_;
    script.function = [name, emulator](input::Gesture& gesture) {
        if (!emulator || !emulator(name, gesture)) {
            // Wayland has no way to inject a key through AT-SPI (N18). This is
            // an EXPECTED platform limit, so it degrades to a debug warning:
            // an error would play the error sound, and a screen reader that
            // chirps is unusable (§3.8, rule 5).
            ALUS_DEBUG_WARNING("key emulation of '{}' is not available on this platform", name);
        }
    };
    const auto [it, inserted] = emulationScripts_.emplace(std::string(keyName), std::move(script));
    (void)inserted;
    return &it->second;
}

const Script* Dispatcher::objectScript(ScriptableObject& object, const input::Gesture& gesture,
                                       const std::vector<MapHit>& hits, bool& unbound) {
    unbound = false;
    // 1. The global maps, in priority order.
    for (const MapHit& hit : hits) {
        if (!object.answersTo(hit.location)) {
            continue;
        }
        if (hit.script.empty()) {
            // `None = kb:h` for this class: the gesture is deliberately
            // unbound here (T3.4). NVDA stops looking at THIS object and
            // carries on with the next one.
            unbound = true;
            trace_.unboundByMap = true;
            return nullptr;
        }
        if (hit.script.rfind(kEmulationPrefix, 0) == 0) {
            trace_.fromGlobalMap = true;
            trace_.location = hit.location;
            trace_.script = hit.script;
            return keyEmulationScript(std::string_view(hit.script).substr(kEmulationPrefix.size()));
        }
        if (const Script* script = object.scriptByName(hit.script); script != nullptr) {
            trace_.fromGlobalMap = true;
            trace_.location = hit.location;
            trace_.script = hit.script;
            return script;
        }
        // A map entry naming a script this object does not have is not an
        // error: the same class name can appear in several maps.
    }

    // 2. The built-in class bindings, after the global maps.
    if (builtinMap_ != nullptr) {
        for (const std::string& identifier : gesture.normalizedIdentifiers()) {
            const std::vector<input::GestureMap::Entry>* entries =
                builtinMap_->entriesForNormalized(identifier);
            if (entries == nullptr) {
                continue;
            }
            for (const input::GestureMap::Entry& entry : *entries) {
                if (!object.answersTo(entry.location)) {
                    continue;
                }
                if (entry.script.empty()) {
                    unbound = true;
                    return nullptr;
                }
                if (const Script* script = object.scriptByName(entry.script); script != nullptr) {
                    trace_.fromGlobalMap = false;
                    trace_.location = entry.location;
                    trace_.script = entry.script;
                    return script;
                }
            }
        }
    }
    return nullptr;
}

std::optional<Script> Dispatcher::findScript(const input::Gesture& gesture) {
    trace_ = Trace{};
    DispatchContext context;
    if (provider_) {
        provider_(context);
    }
    const std::vector<MapHit> hits = globalMapHits(gesture);

    // The ten steps, in order, each with its filter. Written out rather than
    // generated: the order IS the specification, and a reader has to be able
    // to compare it with §5.1 line by line.
    struct Candidate {
        int step;
        ScriptableObject* object;
        enum class Filter { None, TreeInterceptor, Ancestor } filter;
    };
    std::vector<Candidate> candidates;
    candidates.push_back({1, context.gestureScriptable, Candidate::Filter::None});
    for (ScriptableObject* plugin : context.globalPlugins) {
        candidates.push_back({2, plugin, Candidate::Filter::None});
    }
    candidates.push_back({3, context.appModule, Candidate::Filter::None});
    candidates.push_back({4, context.brailleDisplay, Candidate::Filter::None});
    for (ScriptableObject* provider : context.visionProviders) {
        candidates.push_back({5, provider, Candidate::Filter::None});
    }
    if (context.treeInterceptor != nullptr && context.treeInterceptorReady) {
        candidates.push_back({6, context.treeInterceptor, Candidate::Filter::TreeInterceptor});
    }
    candidates.push_back({7, context.focus, Candidate::Filter::None});
    for (auto it = context.focusAncestors.rbegin(); it != context.focusAncestors.rend(); ++it) {
        candidates.push_back({8, *it, Candidate::Filter::Ancestor});
    }
    candidates.push_back({9, context.profileActivation, Candidate::Filter::None});
    for (ScriptableObject* commands : context.globalCommands) {
        candidates.push_back({10, commands, Candidate::Filter::None});
    }

    for (const Candidate& candidate : candidates) {
        if (candidate.object == nullptr) {
            continue;
        }
        bool unbound = false;
        const Script* found = objectScript(*candidate.object, gesture, hits, unbound);
        switch (candidate.filter) {
            case Candidate::Filter::TreeInterceptor: {
                // NVDA: _getTreeModeInterceptorScript.
                found = candidate.object->alternativeScript(gesture, found);
                if (found != nullptr && candidate.object->passThrough() &&
                    !found->info->ignoreTreeInterceptorPassThrough) {
                    found = nullptr;
                }
                break;
            }
            case Candidate::Filter::Ancestor:
                // NVDA: _getFocusAncestorScript. A script only applies on an
                // ancestor if it says it may propagate.
                if (found != nullptr && !found->info->canPropagate) {
                    found = nullptr;
                }
                break;
            case Candidate::Filter::None:
                break;
        }
        if (found != nullptr && found->valid()) {
            // Sleep mode (NVDA: executeGesture). Full sleep silences
            // everything; app sleep lets through only what asked for it.
            if (context.sleepMode == DispatchContext::Sleep::Full ||
                (context.sleepMode == DispatchContext::Sleep::App &&
                 !found->info->allowInSleepMode)) {
                return std::nullopt;
            }
            trace_.step = candidate.step;
            return *found;
        }
        (void)unbound;
    }
    return std::nullopt;
}

void Dispatcher::queueScript(Script script, std::shared_ptr<input::Gesture> gesture) {
    if (!script.valid() || gesture == nullptr) {
        return;
    }
    ++queuedScripts_;
    const std::string name = script.info->name;
    auto run = [this, script = std::move(script), gesture]() mutable {
        --queuedScripts_;
        executeScript(script, *gesture);
    };
    if (loop_ != nullptr) {
        // `immediate`: user input is the one thing that must not wait for the
        // delayed pump (§3.1).
        loop_->queueFunction(std::move(run), true, "script:" + name);
    } else {
        run();
    }
}

void Dispatcher::executeScript(const Script& script, input::Gesture& gesture) {
    if (!script.valid()) {
        return;
    }
    const void* identity = script.identity();
    if (scriptRunning_ && lastScriptIdentity_ == identity) {
        // NVDA does not let a script run inside itself, and passes the key on
        // instead. On Wayland `send()` cannot do that (N18); it still must not
        // recurse.
        gesture.send();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto sinceLast =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastScriptTime_);
    if (lastScriptIdentity_ == identity && lastScriptTime_.time_since_epoch().count() != 0 &&
        sinceLast <= multiPressTimeout_) {
        ++lastScriptCount_;
    } else {
        lastScriptCount_ = 0;
    }
    lastScriptIdentity_ = identity;
    lastScriptTime_ = now;
    ALUS_DEBUG("script {} ({}), repeat {}", script.info->name, script.info->location,
               lastScriptCount_);

    scriptRunning_ = true;
    currentScript_ = &script;
    try {
        script.function(gesture);
        ++executed_;
    } catch (const std::exception& error) {
        ++failed_;
        core::Log::instance().exception(
            std::string("error executing script ") + script.info->name + ": " + error.what());
    } catch (...) {
        ++failed_;
        core::Log::instance().exception("error executing script " + script.info->name);
    }
    currentScript_ = nullptr;
    scriptRunning_ = false;
}

int Dispatcher::lastScriptRepeatCount() const {
    if (lastScriptIdentity_ == nullptr) {
        return 0;
    }
    const auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - lastScriptTime_);
    if (sinceLast > multiPressTimeout_) {
        return 0;
    }
    return lastScriptCount_;
}

void Dispatcher::clearLastScript() {
    lastScriptIdentity_ = nullptr;
    lastScriptCount_ = 0;
    lastScriptTime_ = {};
}

const Script* Dispatcher::currentScript() const { return currentScript_; }

}  // namespace alus::script
