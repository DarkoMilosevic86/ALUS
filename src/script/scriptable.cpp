// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).

#include "alus/script/scriptable.hpp"

#include "alus/core/log.hpp"
#include "alus/script/catalog.hpp"

#include <algorithm>
#include <utility>

namespace alus::script {

ScriptableObject::ScriptableObject(std::vector<std::string> classChain, const Catalog* catalog)
    : classChain_(std::move(classChain)), catalog_(catalog) {}

ScriptableObject::~ScriptableObject() = default;

void ScriptableObject::setScriptClasses(std::vector<std::string> classChain) {
    classChain_ = std::move(classChain);
}

bool ScriptableObject::answersTo(std::string_view location) const {
    return std::find(classChain_.begin(), classChain_.end(), location) != classChain_.end();
}

void ScriptableObject::bindScript(std::string name, ScriptFunction function, ScriptSpec spec) {
    auto info = std::make_unique<ScriptInfo>();
    info->name = name;
    info->location = classChain_.empty() ? std::string() : classChain_.front();

    // The catalogue row is looked up along the class chain, so a script
    // implemented on a subclass still finds NVDA's description for the base
    // class that documents it.
    const ScriptInfo* fromCatalog = nullptr;
    if (catalog_ != nullptr) {
        for (const std::string& location : classChain_) {
            fromCatalog = catalog_->find(location, name);
            if (fromCatalog != nullptr) {
                info->location = location;
                break;
            }
        }
    }
    if (fromCatalog != nullptr) {
        info->description = fromCatalog->description;
        info->category = fromCatalog->category;
        info->gestures = fromCatalog->gestures;
        info->canPropagate = fromCatalog->canPropagate;
        info->bypassInputHelp = fromCatalog->bypassInputHelp;
        info->allowInSleepMode = fromCatalog->allowInSleepMode;
        info->speakOnDemand = fromCatalog->speakOnDemand;
        info->resumeSayAllMode = fromCatalog->resumeSayAllMode;
    }
    // Whatever the binding site states wins: an ALUS-specific script has no
    // catalogue row, and a script whose behaviour deliberately differs from
    // NVDA's says so here, in the code, where it can be reviewed.
    if (!spec.description.empty()) {
        info->description = spec.description;
    }
    if (!spec.category.empty()) {
        info->category = spec.category;
    }
    if (!spec.gestures.empty()) {
        info->gestures = spec.gestures;
    }
    info->canPropagate = info->canPropagate || spec.canPropagate;
    info->bypassInputHelp = info->bypassInputHelp || spec.bypassInputHelp;
    info->allowInSleepMode = info->allowInSleepMode || spec.allowInSleepMode;
    info->speakOnDemand = info->speakOnDemand || spec.speakOnDemand;
    info->ignoreTreeInterceptorPassThrough =
        info->ignoreTreeInterceptorPassThrough || spec.ignoreTreeInterceptorPassThrough;
    info->needsKeyPassThrough = info->needsKeyPassThrough || spec.needsKeyPassThrough;
    if (spec.resumeSayAllMode != SayAllCursor::None) {
        info->resumeSayAllMode = spec.resumeSayAllMode;
    }
    if (fromCatalog == nullptr && catalog_ != nullptr && info->description.empty()) {
        // Not an error: NVDA has undocumented scripts too, and hides them from
        // the gesture dialog. Logged at debug level so the extractor's
        // coverage can be checked without noise in normal runs.
        ALUS_DEBUG("script {}.{}: no catalogue row and no description", info->location, name);
    }

    Script script;
    script.function = std::move(function);
    script.info = info.get();
    infos_.push_back(std::move(info));
    scripts_[std::move(name)] = std::move(script);
}

const Script* ScriptableObject::scriptByName(std::string_view name) const {
    const auto it = scripts_.find(name);
    return it == scripts_.end() ? nullptr : &it->second;
}

std::vector<const ScriptInfo*> ScriptableObject::scripts() const {
    std::vector<const ScriptInfo*> result;
    result.reserve(scripts_.size());
    for (const auto& [name, script] : scripts_) {
        result.push_back(script.info);
    }
    return result;
}

const Script* ScriptableObject::alternativeScript(const input::Gesture& /*gesture*/,
                                                  const Script* found) const {
    return found;
}

}  // namespace alus::script
