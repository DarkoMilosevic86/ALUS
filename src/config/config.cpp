// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/config/config.hpp"

#include "alus/core/log.hpp"

#include <glib.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

namespace alus::config {
namespace {

std::string defaultKey(std::string_view section, std::string_view key) {
    return std::format("{}/{}", section, key);
}

// GKeyFile wants NUL-terminated strings; string_view does not promise one.
std::string zero(std::string_view value) {
    return std::string{value};
}

bool parseBool(std::string_view value, bool fallback) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

}  // namespace

// ---------------------------------------------------------------- Paths ----

Paths Paths::forDirectory(std::filesystem::path configDir) {
    Paths paths;
    paths.configDir = std::move(configDir);
    paths.mainFile = paths.configDir / "alus.ini";
    paths.profilesDir = paths.configDir / "profiles";
    return paths;
}

Paths Paths::fromEnvironment() {
    if (const char* override = g_getenv("ALUS_CONFIG_DIR"); override != nullptr && *override) {
        return forDirectory(std::filesystem::path{override});
    }
    return forDirectory(std::filesystem::path{g_get_user_config_dir()} / "alus");
}

// ---------------------------------------------------------------- Layer ----

// One ini file: the base configuration or one activated profile.
struct Manager::Layer {
    std::string name;  // empty for the base configuration
    std::filesystem::path path;
    GKeyFile* file = nullptr;
    bool dirty = false;

    Layer() : file(g_key_file_new()) {}

    ~Layer() {
        if (file != nullptr) {
            g_key_file_free(file);
        }
    }

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

    bool loadFromDisk(std::string* error) {
        GError* gerror = nullptr;
        const gboolean ok = g_key_file_load_from_file(
            file, path.c_str(),
            GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS), &gerror);
        if (ok == FALSE) {
            // A missing file is the normal first run, not a failure.
            const bool missing = gerror != nullptr && gerror->domain == G_FILE_ERROR &&
                                 gerror->code == G_FILE_ERROR_NOENT;
            if (!missing && error != nullptr) {
                *error = gerror != nullptr ? gerror->message : "unknown error";
            }
            if (gerror != nullptr) {
                g_error_free(gerror);
            }
            return missing;
        }
        return true;
    }

    bool saveToDisk(std::string* error) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        GError* gerror = nullptr;
        if (g_key_file_save_to_file(file, path.c_str(), &gerror) == FALSE) {
            if (error != nullptr) {
                *error = gerror != nullptr ? gerror->message : "unknown error";
            }
            if (gerror != nullptr) {
                g_error_free(gerror);
            }
            return false;
        }
        dirty = false;
        return true;
    }

    bool hasKey(std::string_view section, std::string_view key) const {
        return g_key_file_has_key(file, zero(section).c_str(), zero(key).c_str(), nullptr) != FALSE;
    }

    std::optional<std::string> getString(std::string_view section, std::string_view key) const {
        gchar* value =
            g_key_file_get_string(file, zero(section).c_str(), zero(key).c_str(), nullptr);
        if (value == nullptr) {
            return std::nullopt;
        }
        std::string result{value};
        g_free(value);
        return result;
    }

    void setString(std::string_view section, std::string_view key, std::string_view value) {
        g_key_file_set_string(file, zero(section).c_str(), zero(key).c_str(),
                              zero(value).c_str());
        dirty = true;
    }
};

// -------------------------------------------------------------- Manager ----

Manager::Manager(Paths paths) : paths_(std::move(paths)) {
    base_ = std::make_unique<Layer>();
    base_->path = paths_.mainFile;
    // NVDA: BASE_ONLY_SECTIONS. These describe the installation, not the
    // situation, so a profile must not override them.
    baseOnlySections_ = {"general", "update", "development", "addons"};
}

Manager::~Manager() = default;

void Manager::setDefault(std::string section, std::string key, std::string value) {
    defaults_[defaultKey(section, key)] = std::move(value);
}

bool Manager::load(std::string* error) {
    return base_->loadFromDisk(error);
}

bool Manager::save(std::string* error) {
    bool ok = true;
    if (base_->dirty && !base_->saveToDisk(error)) {
        ok = false;
    }
    for (auto& profile : profiles_) {
        if (profile->dirty && !profile->saveToDisk(error)) {
            ok = false;
        }
    }
    return ok;
}

void Manager::reset(bool factoryDefaults) {
    // NVDA's reset discards unsaved changes: the point is to return to the
    // configuration on disk (or to the factory defaults).
    profiles_.clear();
    activeTriggers_.clear();
    suspended_.reset();

    base_ = std::make_unique<Layer>();
    base_->path = paths_.mainFile;
    if (!factoryDefaults) {
        std::string error;
        if (!base_->loadFromDisk(&error)) {
            ALUS_ERROR("could not reload configuration: {}", error);
        }
    }
    notifyProfileSwitch();
}

const Manager::Layer* Manager::findLayerWith(std::string_view section,
                                             std::string_view key) const {
    const bool baseOnly = isBaseOnlySection(section);
    if (!baseOnly) {
        // Most recently activated profile wins.
        for (auto it = profiles_.rbegin(); it != profiles_.rend(); ++it) {
            if ((*it)->hasKey(section, key)) {
                return it->get();
            }
        }
    }
    if (base_->hasKey(section, key)) {
        return base_.get();
    }
    return nullptr;
}

Manager::Layer* Manager::topWritableLayer(std::string_view section) {
    if (!isBaseOnlySection(section) && !profiles_.empty()) {
        return profiles_.back().get();
    }
    return base_.get();
}

bool Manager::has(std::string_view section, std::string_view key) const {
    if (findLayerWith(section, key) != nullptr) {
        return true;
    }
    return defaults_.find(defaultKey(section, key)) != defaults_.end();
}

std::string Manager::getString(std::string_view section, std::string_view key,
                               std::string_view fallback) const {
    if (const Layer* layer = findLayerWith(section, key); layer != nullptr) {
        if (auto value = layer->getString(section, key)) {
            return *value;
        }
    }
    const auto it = defaults_.find(defaultKey(section, key));
    if (it != defaults_.end()) {
        return it->second;
    }
    return std::string{fallback};
}

bool Manager::getBool(std::string_view section, std::string_view key, bool fallback) const {
    const std::string value = getString(section, key);
    return value.empty() ? fallback : parseBool(value, fallback);
}

std::int64_t Manager::getInt(std::string_view section, std::string_view key,
                             std::int64_t fallback) const {
    const std::string value = getString(section, key);
    std::int64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return fallback;
    }
    return parsed;
}

double Manager::getDouble(std::string_view section, std::string_view key,
                          double fallback) const {
    const std::string value = getString(section, key);
    double parsed = 0.0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return fallback;
    }
    return parsed;
}

void Manager::setString(std::string_view section, std::string_view key,
                        std::string_view value) {
    topWritableLayer(section)->setString(section, key, value);
}

void Manager::setBool(std::string_view section, std::string_view key, bool value) {
    setString(section, key, value ? "true" : "false");
}

void Manager::setInt(std::string_view section, std::string_view key, std::int64_t value) {
    setString(section, key, std::format("{}", value));
}

void Manager::setDouble(std::string_view section, std::string_view key, double value) {
    setString(section, key, std::format("{}", value));
}

void Manager::addBaseOnlySection(std::string section) {
    baseOnlySections_.insert(std::move(section));
}

bool Manager::isBaseOnlySection(std::string_view section) const {
    return baseOnlySections_.find(section) != baseOnlySections_.end();
}

// ------------------------------------------------------------- profiles ----

std::filesystem::path Manager::profilePath(const std::string& name) const {
    return paths_.profilesDir / (name + ".ini");
}

std::vector<std::string> Manager::listProfiles() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator{paths_.profilesDir, ec}) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".ini") {
            continue;
        }
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool Manager::createProfile(const std::string& name, std::string* error) {
    if (name.empty()) {
        if (error != nullptr) {
            *error = "a profile name may not be empty";
        }
        return false;
    }
    const std::filesystem::path path = profilePath(name);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        if (error != nullptr) {
            *error = std::format("profile {} already exists", name);
        }
        return false;
    }
    Layer layer;
    layer.name = name;
    layer.path = path;
    return layer.saveToDisk(error);
}

bool Manager::deleteProfile(const std::string& name, std::string* error) {
    deactivateProfile(name);
    for (auto it = triggersToProfiles_.begin(); it != triggersToProfiles_.end();) {
        it = it->second == name ? triggersToProfiles_.erase(it) : std::next(it);
    }
    std::error_code ec;
    if (!std::filesystem::remove(profilePath(name), ec)) {
        if (error != nullptr) {
            *error = std::format("profile {} does not exist", name);
        }
        return false;
    }
    return true;
}

bool Manager::renameProfile(const std::string& from, const std::string& to,
                            std::string* error) {
    std::error_code ec;
    if (std::filesystem::exists(profilePath(to), ec)) {
        if (error != nullptr) {
            *error = std::format("profile {} already exists", to);
        }
        return false;
    }
    std::filesystem::rename(profilePath(from), profilePath(to), ec);
    if (ec) {
        if (error != nullptr) {
            *error = ec.message();
        }
        return false;
    }
    for (auto& [spec, profile] : triggersToProfiles_) {
        if (profile == from) {
            profile = to;
        }
    }
    for (auto& layer : profiles_) {
        if (layer->name == from) {
            layer->name = to;
            layer->path = profilePath(to);
        }
    }
    return true;
}

Manager::Layer* Manager::loadProfileLayer(const std::string& name, std::string* error) {
    auto layer = std::make_unique<Layer>();
    layer->name = name;
    layer->path = profilePath(name);
    if (!layer->loadFromDisk(error)) {
        return nullptr;
    }
    Layer* raw = layer.get();
    profiles_.push_back(std::move(layer));
    return raw;
}

bool Manager::activateProfile(const std::string& name, std::string* error) {
    const auto active = std::find_if(profiles_.begin(), profiles_.end(),
                                     [&name](const auto& layer) { return layer->name == name; });
    if (active != profiles_.end()) {
        return true;  // already active; activating twice is not an error
    }
    if (loadProfileLayer(name, error) == nullptr) {
        return false;
    }
    ALUS_DEBUG("profile {} activated", name);
    notifyProfileSwitch();
    return true;
}

bool Manager::deactivateProfile(const std::string& name) {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(),
                                 [&name](const auto& layer) { return layer->name == name; });
    if (it == profiles_.end()) {
        return false;
    }
    if ((*it)->dirty) {
        std::string error;
        if (!(*it)->saveToDisk(&error)) {
            ALUS_ERROR("could not save profile {}: {}", name, error);
        }
    }
    profiles_.erase(it);
    ALUS_DEBUG("profile {} deactivated", name);
    notifyProfileSwitch();
    return true;
}

std::vector<std::string> Manager::activeProfiles() const {
    std::vector<std::string> names;
    names.reserve(profiles_.size());
    for (const auto& layer : profiles_) {
        names.push_back(layer->name);
    }
    return names;
}

void Manager::notifyProfileSwitch() {
    // Inside an atomic switch nothing is announced; the guard does it once on
    // the way out. The flag, not a counter of deferred calls: ten deferred
    // switches are still one switch.
    if (atomicDepth_ > 0) {
        atomicPending_ = true;
        return;
    }
    ++profileSwitches_;
    postProfileSwitch_.notify();
}

// ------------------------------------------------- atomic profile switch ----

Manager::AtomicSwitch::AtomicSwitch(Manager* manager) : manager_(manager) {
    if (manager_ != nullptr) {
        ++manager_->atomicDepth_;
    }
}

Manager::AtomicSwitch::AtomicSwitch(AtomicSwitch&& other) noexcept
    : manager_(other.manager_) {
    other.manager_ = nullptr;
}

Manager::AtomicSwitch& Manager::AtomicSwitch::operator=(AtomicSwitch&& other) noexcept {
    if (this != &other) {
        finish();
        manager_ = other.manager_;
        other.manager_ = nullptr;
    }
    return *this;
}

Manager::AtomicSwitch::~AtomicSwitch() { finish(); }

void Manager::AtomicSwitch::finish() {
    Manager* manager = manager_;
    manager_ = nullptr;
    if (manager == nullptr) {
        return;
    }
    if (manager->atomicDepth_ > 0) {
        --manager->atomicDepth_;
    }
    if (manager->atomicDepth_ > 0 || !manager->atomicPending_) {
        return;
    }
    // Clear BEFORE notifying: a handler that activates a profile of its own
    // must not re-arm the flag it is being called for.
    manager->atomicPending_ = false;
    ++manager->profileSwitches_;
    manager->postProfileSwitch_.notify();
}

Manager::AtomicSwitch Manager::atomicProfileSwitch() { return AtomicSwitch{this}; }

// ------------------------------------------------------------- triggers ----

void Manager::mapTrigger(std::string spec, std::string profileName) {
    triggersToProfiles_[std::move(spec)] = std::move(profileName);
}

bool Manager::unmapTrigger(const std::string& spec) {
    return triggersToProfiles_.erase(spec) > 0;
}

std::optional<std::string> Manager::profileForTrigger(const std::string& spec) const {
    const auto it = triggersToProfiles_.find(spec);
    if (it == triggersToProfiles_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool Manager::triggerEnter(const std::string& spec) {
    if (!triggersEnabled_) {
        return false;
    }
    const auto profile = profileForTrigger(spec);
    if (!profile) {
        return false;  // NVDA: a trigger with no profile does nothing
    }
    if (std::find(activeTriggers_.begin(), activeTriggers_.end(), spec) !=
        activeTriggers_.end()) {
        return true;
    }
    std::string error;
    if (!activateProfile(*profile, &error)) {
        ALUS_ERROR("error entering trigger {}, profile {}: {}", spec, *profile, error);
        return false;
    }
    activeTriggers_.push_back(spec);
    return true;
}

bool Manager::triggerExit(const std::string& spec) {
    const auto it = std::find(activeTriggers_.begin(), activeTriggers_.end(), spec);
    if (it == activeTriggers_.end()) {
        return false;
    }
    const auto profile = profileForTrigger(spec);
    activeTriggers_.erase(it);
    if (!profile) {
        return false;
    }
    // Another live trigger may point at the same profile; it stays until the
    // last one leaves.
    for (const std::string& other : activeTriggers_) {
        if (profileForTrigger(other) == profile) {
            return true;
        }
    }
    deactivateProfile(*profile);
    return true;
}

void Manager::setTriggersEnabled(bool enabled) {
    if (enabled == triggersEnabled_) {
        return;
    }
    if (!enabled) {
        const std::vector<std::string> active = activeTriggers_;
        for (auto it = active.rbegin(); it != active.rend(); ++it) {
            triggerExit(*it);
        }
    }
    triggersEnabled_ = enabled;
}

void Manager::suspendTriggers() {
    if (suspended_.has_value()) {
        return;
    }
    suspended_ = activeTriggers_;
    for (auto it = suspended_->rbegin(); it != suspended_->rend(); ++it) {
        triggerExit(*it);
    }
}

void Manager::resumeTriggers() {
    if (!suspended_.has_value()) {
        return;
    }
    const std::vector<std::string> toRestore = *suspended_;
    suspended_.reset();
    for (const std::string& spec : toRestore) {
        triggerEnter(spec);
    }
}

// ----------------------------------------------------------- subsystems ----

void SubsystemRegistry::add(Subsystem* subsystem) {
    if (subsystem == nullptr) {
        return;
    }
    subsystems_.push_back(subsystem);
}

bool SubsystemRegistry::remove(Subsystem* subsystem) {
    const auto it = std::find(subsystems_.begin(), subsystems_.end(), subsystem);
    if (it == subsystems_.end()) {
        return false;
    }
    subsystems_.erase(it);
    return true;
}

void SubsystemRegistry::clear() {
    subsystems_.clear();
}

void SubsystemRegistry::initializeAll() {
    for (Subsystem* subsystem : subsystems_) {
        try {
            ALUS_DEBUG("initializing {}", subsystem->name());
            subsystem->initialize();
        } catch (...) {
            // One broken subsystem must not stop the others from coming back.
            core::Log::instance().exception(std::format("could not initialize {}", subsystem->name()));
        }
    }
    initialized_ = true;
}

void SubsystemRegistry::terminateAll() {
    // Reverse order, as in NVDA's resetConfiguration: what was built last is
    // torn down first.
    for (auto it = subsystems_.rbegin(); it != subsystems_.rend(); ++it) {
        try {
            ALUS_DEBUG("terminating {}", (*it)->name());
            (*it)->terminate();
        } catch (...) {
            core::Log::instance().exception(std::format("could not terminate {}", (*it)->name()));
        }
    }
    initialized_ = false;
}

void resetConfiguration(Manager& manager, SubsystemRegistry& subsystems, bool factoryDefaults) {
    ALUS_INFO("resetting configuration (factoryDefaults={})", factoryDefaults);
    subsystems.terminateAll();
    manager.reset(factoryDefaults);
    subsystems.initializeAll();
    manager.postConfigReset().notify();
    ALUS_INFO("reverted to saved configuration");
}

}  // namespace alus::config
