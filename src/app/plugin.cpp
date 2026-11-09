// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/app/plugin.hpp"

#include "alus/a11y/mapping.hpp"
#include "alus/a11y/role.hpp"
#include "alus/core/log.hpp"
#include "alus/script/catalog.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <string>
#include <system_error>
#include <utility>

#ifndef ALUS_VERSION
#define ALUS_VERSION "0.0.0"
#endif

namespace alus::app {
namespace {

// ---------------------------------------------------------------------------
// Version comparison
//
// "major.minor.patch", missing components read as 0, anything unparseable read
// as 0 -- a plugin whose min_alus_version is nonsense is treated as "any",
// because refusing it would be a harsher answer than the mistake deserves and
// the log line says what was read.
// ---------------------------------------------------------------------------

std::array<int, 3> parseVersion(std::string_view text) {
    std::array<int, 3> parts{0, 0, 0};
    std::size_t index = 0;
    while (index < parts.size() && !text.empty()) {
        const std::size_t dot = text.find('.');
        const std::string_view piece = text.substr(0, dot);
        int value = 0;
        const auto result = std::from_chars(piece.data(), piece.data() + piece.size(), value);
        if (result.ec == std::errc{}) {
            parts[index] = value;
        }
        if (dot == std::string_view::npos) {
            break;
        }
        text.remove_prefix(dot + 1);
        ++index;
    }
    return parts;
}

bool versionAtLeast(std::string_view have, std::string_view want) {
    return parseVersion(have) >= parseVersion(want);
}

// The one string every host function needs to return something for. A C API
// cannot return "nothing", and returning NULL would put the burden of a null
// check on every plugin; the empty string is the documented answer
// (alus-plugin.h, "STRING LIFETIME").
constexpr const char* kEmpty = "";

// Strings handed back to a plugin have to outlive the call. They are kept here,
// one slot per query, which is exactly the lifetime alus-plugin.h promises:
// "valid only until the plugin returns from the call it got them in".
thread_local std::string g_returned;

const char* returned(std::string value) {
    g_returned = std::move(value);
    return g_returned.c_str();
}

a11y::Object* objectFrom(AlusObject* handle) {
    return reinterpret_cast<a11y::Object*>(handle);
}

a11y::BehaviorList* listFrom(AlusBehaviorList* handle) {
    return reinterpret_cast<a11y::BehaviorList*>(handle);
}

const a11y::BehaviorList* listFrom(const AlusBehaviorList* handle) {
    return reinterpret_cast<const a11y::BehaviorList*>(handle);
}

// ---------------------------------------------------------------------------
// The host API: the C functions a plugin is handed.
//
// Every one of them starts by turning the opaque handle back into something
// real and returns a harmless answer when it cannot. A plugin that passes
// rubbish gets a log line, not a crash: this is the boundary with third-party
// code, and it is the one place in ALUS where defensive checks on every
// argument are the right thing rather than noise.
// ---------------------------------------------------------------------------

const char* apiAlusVersion() { return ALUS_VERSION; }

void apiLog(AlusHost* host, AlusLogLevel level, const char* message) {
    PluginHost::Loaded* loaded = PluginHost::fromHandle(host);
    const std::string name = loaded != nullptr ? loaded->name : "?";
    const std::string text = message != nullptr ? message : "";
    switch (level) {
        case ALUS_LOG_DEBUG:
            ALUS_DEBUG("plugin {}: {}", name, text);
            break;
        case ALUS_LOG_INFO:
            ALUS_INFO("plugin {}: {}", name, text);
            break;
        case ALUS_LOG_WARNING:
            ALUS_WARNING("plugin {}: {}", name, text);
            break;
        case ALUS_LOG_ERROR:
            ALUS_ERROR("plugin {}: {}", name, text);
            break;
    }
}

void apiMessage(AlusHost* host, const char* text) {
    PluginHost::Loaded* loaded = PluginHost::fromHandle(host);
    if (loaded == nullptr || loaded->owner == nullptr || text == nullptr) {
        return;
    }
    const Services& services = loaded->owner->services();
    if (services.message) {
        services.message(text);
    }
}

int apiRegisterScript(AlusHost* host, const char* name, const char* description,
                      const char* category, const char* gesture, AlusScriptFunc run,
                      void* userData) {
    PluginHost::Loaded* loaded = PluginHost::fromHandle(host);
    if (loaded == nullptr || name == nullptr || *name == '\0' || run == nullptr) {
        return -1;
    }
    if (loaded->scripts == nullptr) {
        return -1;
    }
    if (loaded->scripts->scriptByName(name) != nullptr) {
        ALUS_WARNING("plugin {}: script {} is registered twice", loaded->name, name);
        return -2;
    }
    script::ScriptSpec spec;
    spec.description = description != nullptr ? description : "";
    spec.category = category != nullptr ? category : std::format("Plugin: {}", loaded->name);
    if (gesture != nullptr && *gesture != '\0') {
        spec.gestures.emplace_back(gesture);
    }
    // The C function pointer and its user data are copied into the lambda: the
    // plugin may free whatever it built them from as soon as this returns.
    loaded->scripts->addScript(
        name, [run, userData](input::Gesture&) { run(userData); }, std::move(spec));
    ALUS_DEBUG("plugin {}: script {} registered ({})", loaded->name, name,
               gesture != nullptr && *gesture != '\0' ? gesture : "no gesture");
    return 0;
}

int apiRegisterEventHandler(AlusHost* host, const char* eventName, AlusEventFunc handler,
                            void* userData) {
    PluginHost::Loaded* loaded = PluginHost::fromHandle(host);
    if (loaded == nullptr || eventName == nullptr || *eventName == '\0' || handler == nullptr) {
        return -1;
    }
    loaded->events.push_back(
        PluginHost::EventRegistration{eventName, handler, userData});
    ALUS_DEBUG("plugin {}: handler for event {}", loaded->name, eventName);
    return 0;
}

int apiAddBrailleTableSource(AlusHost* host, const char* directory) {
    PluginHost::Loaded* loaded = PluginHost::fromHandle(host);
    if (loaded == nullptr || loaded->owner == nullptr || directory == nullptr) {
        return -1;
    }
    const Services& services = loaded->owner->services();
    if (!services.addBrailleTableSource) {
        // Braille is not running (--no-braille, or no liblouis). Not an error
        // in the plugin, so it is told apart from a bad directory.
        return -2;
    }
    std::error_code ec;
    const std::filesystem::path path{directory};
    if (!std::filesystem::is_directory(path, ec)) {
        ALUS_WARNING("plugin {}: braille table directory {} does not exist", loaded->name,
                     directory);
        return -1;
    }
    if (!services.addBrailleTableSource(path)) {
        return -2;
    }
    ALUS_INFO("plugin {}: braille table source {}", loaded->name, directory);
    return 0;
}

const char* apiObjectName(AlusObject* handle) {
    a11y::Object* object = objectFrom(handle);
    return object != nullptr ? returned(object->name()) : kEmpty;
}

const char* apiObjectRoleName(AlusObject* handle) {
    a11y::Object* object = objectFrom(handle);
    return object != nullptr ? a11y::roleName(object->role()) : kEmpty;
}

const char* apiObjectAttribute(AlusObject* handle, const char* key) {
    a11y::Object* object = objectFrom(handle);
    if (object == nullptr || key == nullptr) {
        return kEmpty;
    }
    const a11y::Attributes& attributes = object->attributes();
    const auto it = attributes.find(key);
    return it != attributes.end() ? returned(it->second) : kEmpty;
}

const char* apiObjectToolkit(AlusObject* handle) {
    a11y::Object* object = objectFrom(handle);
    return object != nullptr ? returned(object->appInfo().toolkitName) : kEmpty;
}

void apiBehaviorsPrepend(AlusBehaviorList* handle, const char* className) {
    a11y::BehaviorList* list = listFrom(handle);
    if (list == nullptr || className == nullptr || *className == '\0') {
        return;
    }
    std::string name{className};
    a11y::prependBehavior(*list, a11y::BehaviorEntry{name, [name] {
                                                         return std::make_unique<NamedBehavior>(
                                                             name);
                                                     }});
}

int apiBehaviorsRemove(AlusBehaviorList* handle, const char* className) {
    a11y::BehaviorList* list = listFrom(handle);
    if (list == nullptr || className == nullptr) {
        return 0;
    }
    return a11y::removeBehavior(*list, className) ? 1 : 0;
}

int apiBehaviorsContains(const AlusBehaviorList* handle, const char* className) {
    const a11y::BehaviorList* list = listFrom(handle);
    if (list == nullptr || className == nullptr) {
        return 0;
    }
    return a11y::hasBehavior(*list, className) ? 1 : 0;
}

const AlusHostApi kHostApi = {
    sizeof(AlusHostApi),
    &apiAlusVersion,
    &apiLog,
    &apiMessage,
    &apiRegisterScript,
    &apiRegisterEventHandler,
    &apiAddBrailleTableSource,
    &apiObjectName,
    &apiObjectRoleName,
    &apiObjectAttribute,
    &apiObjectToolkit,
    &apiBehaviorsPrepend,
    &apiBehaviorsRemove,
    &apiBehaviorsContains,
};

}  // namespace

// ------------------------------------------------------------ NamedBehavior ----

NamedBehavior::NamedBehavior(std::string className) : className_(std::move(className)) {}
NamedBehavior::~NamedBehavior() = default;

// ------------------------------------------------------------ PluginScripts ----

PluginScripts::PluginScripts(std::string pluginName, const script::Catalog* catalog)
    : script::ScriptableObject({classNameFor(pluginName)}, catalog) {}

PluginScripts::~PluginScripts() = default;

std::string PluginScripts::classNameFor(std::string_view pluginName) {
    // `[plugins.spellcheck]` is what a user writes in gestures.ini to rebind
    // this plugin's keys. NVDA's own spelling for a global plugin is
    // `globalPlugins.<name>.GlobalPlugin`; ALUS shortens it because there is
    // no module/class distinction to carry over, and because the user has to
    // type it.
    return std::format("plugins.{}", pluginName);
}

void PluginScripts::addScript(std::string name, script::ScriptFunction function,
                              script::ScriptSpec spec) {
    bindScript(std::move(name), std::move(function), std::move(spec));
}

// --------------------------------------------------------------- PluginHost ----

PluginHost::PluginHost(Services services, const script::Catalog* catalog)
    : services_(std::move(services)), catalog_(catalog) {}

PluginHost::~PluginHost() { unloadAll(); }

const AlusHostApi& PluginHost::hostApi() { return kHostApi; }

PluginHost::Loaded* PluginHost::fromHandle(AlusHost* host) {
    return reinterpret_cast<Loaded*>(host);
}

void PluginHost::addDirectory(std::filesystem::path directory) {
    if (directory.empty()) {
        return;
    }
    if (std::find(directories_.begin(), directories_.end(), directory) != directories_.end()) {
        return;
    }
    directories_.push_back(std::move(directory));
}

std::vector<std::filesystem::path> PluginHost::discover() const {
    std::vector<std::filesystem::path> found;
    std::vector<std::string> seen;
    std::error_code ec;
    for (const std::filesystem::path& directory : directories_) {
        if (!std::filesystem::is_directory(directory, ec)) {
            // A user with no add-ons is the normal case, not a warning.
            continue;
        }
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(directory, ec)) {
            std::filesystem::path candidate;
            if (entry.is_regular_file(ec) && entry.path().extension() == ".so") {
                // A bare `.so` in a plugins directory: the development and
                // system-package shape.
                candidate = entry.path();
            } else if (entry.is_directory(ec)) {
                // An add-on directory: `<name>/<name>.so`, which is §12.3's
                // user layout and the one the two-phase uninstall deletes
                // whole.
                const std::filesystem::path inside =
                    entry.path() / (entry.path().filename().string() + ".so");
                if (std::filesystem::is_regular_file(inside, ec)) {
                    candidate = inside;
                }
            }
            if (candidate.empty()) {
                continue;
            }
            // THE FIRST DIRECTORY WINS, by stem: the scratchpad's copy of a
            // plugin shadows the installed one, which is what makes a
            // scratchpad useful at all (§12.3).
            const std::string stem = candidate.stem().string();
            if (std::find(seen.begin(), seen.end(), stem) != seen.end()) {
                ALUS_DEBUG("plugin {} shadowed by an earlier directory", candidate.string());
                continue;
            }
            seen.push_back(stem);
            found.push_back(candidate);
        }
    }
    return found;
}

// ----------------------------------------------- the two-phase uninstall ----

std::size_t PluginHost::processPendingRemovals() {
    std::size_t removed = 0;
    std::error_code ec;
    for (const std::filesystem::path& directory : directories_) {
        if (!std::filesystem::is_directory(directory, ec)) {
            continue;
        }
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(directory, ec)) {
            std::filesystem::path marker;
            std::filesystem::path target;
            if (entry.is_directory(ec)) {
                marker = entry.path() / kRemovalMarker;
                target = entry.path();
            } else if (entry.is_regular_file(ec) && entry.path().extension() == ".so") {
                // A bare `.so`: the marker sits beside it, named after it, so
                // that two plugins in one directory are marked independently.
                marker = entry.path().parent_path() /
                         (entry.path().filename().string() + std::string{kRemovalMarker});
                target = entry.path();
            }
            if (marker.empty() || !std::filesystem::exists(marker, ec)) {
                continue;
            }
            const std::uintmax_t count = std::filesystem::remove_all(target, ec);
            if (ec) {
                // Logged and left alone: the marker stays, so the next start
                // tries again. A plugin the user cannot uninstall is bad; a
                // half-deleted one is worse.
                ALUS_ERROR("could not remove the plugin at {}: {}", target.string(),
                           ec.message());
                continue;
            }
            // For a bare `.so` the marker is outside the target.
            std::filesystem::remove(marker, ec);
            ALUS_INFO("plugin removed: {} ({} files)", target.string(), count);
            ++removed;
        }
    }
    return removed;
}

bool PluginHost::markForRemoval(std::string_view name, std::string* error) {
    const Loaded* loaded = find(name);
    std::filesystem::path path;
    if (loaded != nullptr) {
        path = loaded->path;
    } else {
        // A plugin that is installed but was refused (a wrong ABI version) is
        // exactly the one a user most wants to uninstall, and it is not in
        // plugins_. So the directories are searched too.
        for (const std::filesystem::path& candidate : discover()) {
            if (candidate.stem() == name) {
                path = candidate;
                break;
            }
        }
    }
    if (path.empty()) {
        if (error != nullptr) {
            *error = std::format("no plugin named {}", name);
        }
        return false;
    }

    // An add-on directory is marked inside itself; a bare `.so` beside itself.
    const bool isAddonDirectory = path.parent_path().filename() == path.stem();
    const std::filesystem::path marker =
        isAddonDirectory
            ? path.parent_path() / kRemovalMarker
            : path.parent_path() / (path.filename().string() + std::string{kRemovalMarker});

    std::error_code ec;
    std::filesystem::create_directories(marker.parent_path(), ec);
    std::FILE* file = std::fopen(marker.c_str(), "we");
    if (file == nullptr) {
        if (error != nullptr) {
            *error = std::format("could not write {}", marker.string());
        }
        return false;
    }
    // The content is for a human reading the directory; nothing parses it.
    std::fputs("ALUS: this plugin is uninstalled at the next start.\n", file);
    std::fclose(file);
    ALUS_INFO("plugin {} marked for removal; it goes at the next start", name);
    return true;
}

bool PluginHost::isMarkedForRemoval(std::string_view name) const {
    std::error_code ec;
    for (const std::filesystem::path& directory : directories_) {
        const std::filesystem::path addon = directory / name / kRemovalMarker;
        if (std::filesystem::exists(addon, ec)) {
            return true;
        }
        const std::filesystem::path bare =
            directory / (std::string{name} + ".so" + std::string{kRemovalMarker});
        if (std::filesystem::exists(bare, ec)) {
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------- loading ----

bool PluginHost::checkVersions(const AlusPluginV1& plugin, std::string* error) const {
    if (plugin.abi_version != ALUS_PLUGIN_ABI_V1) {
        if (error != nullptr) {
            *error = std::format("ABI version {} is not supported (this ALUS speaks {})",
                                 plugin.abi_version, ALUS_PLUGIN_ABI_V1);
        }
        return false;
    }
    if (plugin.name == nullptr || *plugin.name == '\0') {
        if (error != nullptr) {
            *error = "the plugin has no name";
        }
        return false;
    }
    if (plugin.init == nullptr) {
        if (error != nullptr) {
            *error = "the plugin has no init function";
        }
        return false;
    }
    if (plugin.min_alus_version != nullptr && *plugin.min_alus_version != '\0' &&
        !versionAtLeast(ALUS_VERSION, plugin.min_alus_version)) {
        if (error != nullptr) {
            *error = std::format("needs ALUS {} or newer; this is {}", plugin.min_alus_version,
                                 ALUS_VERSION);
        }
        return false;
    }
    return true;
}

bool PluginHost::load(const std::filesystem::path& path, std::string* error) {
    // RTLD_LOCAL (the default) and not RTLD_GLOBAL: a plugin's symbols must
    // not leak into the process, or two plugins with the same helper function
    // silently share one of them.
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        if (error != nullptr) {
            const char* reason = dlerror();
            *error = reason != nullptr ? reason : "dlopen failed";
        }
        return false;
    }

    // The cast through a void* is what POSIX gives us; the alternative is a
    // reinterpret_cast of an object pointer to a function pointer, which is
    // the same thing with fewer words about it.
    dlerror();
    void* symbol = dlsym(handle, ALUS_PLUGIN_ENTRY_POINT);
    if (symbol == nullptr) {
        if (error != nullptr) {
            *error = std::format("no {} entry point", ALUS_PLUGIN_ENTRY_POINT);
        }
        dlclose(handle);
        return false;
    }
    AlusPluginEntryFunc entry = nullptr;
    std::memcpy(&entry, &symbol, sizeof(entry));

    const AlusPluginV1* plugin = entry();
    if (plugin == nullptr) {
        if (error != nullptr) {
            *error = std::format("{} returned nothing", ALUS_PLUGIN_ENTRY_POINT);
        }
        dlclose(handle);
        return false;
    }
    // THE VERSION CHECK BEFORE ANYTHING IS CALLED (T8.5). `plugin->init` is
    // not touched until this passes.
    if (!checkVersions(*plugin, error)) {
        dlclose(handle);
        return false;
    }
    if (find(plugin->name) != nullptr) {
        if (error != nullptr) {
            *error = std::format("a plugin named {} is already loaded", plugin->name);
        }
        dlclose(handle);
        return false;
    }

    auto loaded = std::make_unique<Loaded>();
    loaded->owner = this;
    loaded->name = plugin->name;
    loaded->path = path;
    loaded->abiVersion = plugin->abi_version;
    loaded->minVersion = plugin->min_alus_version != nullptr ? plugin->min_alus_version : "";
    loaded->plugin = plugin;
    loaded->handle = handle;
    loaded->scripts = std::make_unique<PluginScripts>(loaded->name, catalog_);

    // In the list BEFORE init, so that a plugin registering a script from
    // inside init finds its own entry through the handle it was given.
    Loaded* raw = loaded.get();
    plugins_.push_back(std::move(loaded));

    const int result = plugin->init(reinterpret_cast<AlusHost*>(raw), &kHostApi);
    if (result != 0) {
        if (error != nullptr) {
            *error = std::format("init returned {}", result);
        }
        // Everything it managed to register goes with it.
        unload(*raw);
        plugins_.pop_back();
        return false;
    }

    // Its scripts' own gestures become bindings, under the plugin's class
    // name, so the user can rebind them in gestures.ini like any other key.
    for (const script::ScriptInfo* info : raw->scripts->scripts()) {
        for (const std::string& gesture : info->gestures) {
            gestures_.add(gesture, info->location, info->name);
        }
    }

    ALUS_INFO("plugin {} loaded from {} ({} scripts, {} event handlers)", raw->name,
              path.string(), raw->scripts->scriptCount(), raw->events.size());
    return true;
}

std::size_t PluginHost::loadAll() {
    std::size_t count = 0;
    for (const std::filesystem::path& path : discover()) {
        std::string error;
        if (load(path, &error)) {
            ++count;
        } else {
            // A refused plugin is an ERROR and not a warning: the user
            // installed it on purpose and it is not running.
            ALUS_ERROR("plugin {} refused: {}", path.string(), error);
        }
    }
    return count;
}

void PluginHost::unload(Loaded& loaded) {
    // The order is the whole point (see plugin.hpp): the plugin's own
    // terminate, then every registration, then dlclose. A handler whose code
    // has been unmapped is the one crash with no stack trace.
    if (loaded.plugin != nullptr && loaded.plugin->terminate != nullptr) {
        loaded.plugin->terminate();
    }
    loaded.events.clear();
    loaded.scripts.reset();
    loaded.plugin = nullptr;
    if (loaded.handle != nullptr) {
        dlclose(loaded.handle);
        loaded.handle = nullptr;
    }
}

void PluginHost::unloadAll() {
    // In reverse load order, the way every teardown in ALUS goes.
    while (!plugins_.empty()) {
        unload(*plugins_.back());
        plugins_.pop_back();
    }
    gestures_.clear();
}

const PluginHost::Loaded* PluginHost::find(std::string_view name) const {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [name](const std::unique_ptr<Loaded>& loaded) {
                                     return loaded != nullptr && loaded->name == name;
                                 });
    return it != plugins_.end() ? it->get() : nullptr;
}

// --------------------------------------------- what the app service plugs in ----

void PluginHost::chooseOverlayClasses(a11y::Object& object, a11y::BehaviorList& list) {
    for (const std::unique_ptr<Loaded>& loaded : plugins_) {
        if (loaded == nullptr || loaded->plugin == nullptr ||
            loaded->plugin->choose_overlays == nullptr) {
            continue;
        }
        loaded->plugin->choose_overlays(reinterpret_cast<AlusObject*>(&object),
                                        reinterpret_cast<AlusBehaviorList*>(&list));
    }
}

Module::Disposition PluginHost::handleEvent(std::string_view name,
                                            const a11y::ObjectPtr& object) {
    for (const std::unique_ptr<Loaded>& loaded : plugins_) {
        if (loaded == nullptr) {
            continue;
        }
        for (const EventRegistration& registration : loaded->events) {
            if (registration.event != name || registration.handler == nullptr) {
                continue;
            }
            const AlusEventDisposition disposition = registration.handler(
                reinterpret_cast<AlusObject*>(object.get()), registration.userData);
            if (disposition == ALUS_EVENT_CONSUME) {
                ALUS_DEBUG("plugin {} consumed event {}", loaded->name, name);
                return Module::Disposition::Consume;
            }
        }
    }
    return Module::Disposition::Continue;
}

std::vector<script::ScriptableObject*> PluginHost::scriptObjects() const {
    std::vector<script::ScriptableObject*> objects;
    objects.reserve(plugins_.size());
    for (const std::unique_ptr<Loaded>& loaded : plugins_) {
        if (loaded != nullptr && loaded->scripts != nullptr &&
            loaded->scripts->scriptCount() > 0) {
            objects.push_back(loaded->scripts.get());
        }
    }
    return objects;
}

}  // namespace alus::app
