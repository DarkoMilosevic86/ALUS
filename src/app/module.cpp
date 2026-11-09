// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/app/module.hpp"

#include "alus/a11y/registry.hpp"
#include "alus/core/log.hpp"

#include <filesystem>
#include <format>
#include <system_error>
#include <utility>

namespace alus::app {
namespace {

std::vector<std::string> withBaseClass(std::vector<std::string> chain) {
    // Every module answers to `appModuleHandler.AppModule`, last. A gesture
    // map section with that name therefore reaches every application, which is
    // how NVDA lets one binding apply to all of them.
    const std::string base{Module::kBaseClass};
    if (std::find(chain.begin(), chain.end(), base) == chain.end()) {
        chain.push_back(base);
    }
    return chain;
}

}  // namespace

Module::Module(std::string moduleName, AppKey key, Services services,
               std::vector<std::string> classChain, const script::Catalog* catalog)
    : script::ScriptableObject(withBaseClass(std::move(classChain)), catalog),
      moduleName_(std::move(moduleName)),
      key_(std::move(key)),
      services_(std::move(services)) {}

Module::~Module() = default;

// ------------------------------------------------------------- mechanism 3 ----

void Module::bindEvent(std::string name, EventHandler handler) {
    if (name.empty() || handler == nullptr) {
        ALUS_WARNING("app module {}: refusing an empty event binding", moduleName_);
        return;
    }
    eventHandlers_[std::move(name)] = std::move(handler);
}

bool Module::hasEventHandler(std::string_view name) const {
    return eventHandlers_.find(name) != eventHandlers_.end();
}

Module::Disposition Module::handleEvent(std::string_view name, const a11y::ObjectPtr& object) {
    const auto it = eventHandlers_.find(name);
    if (it == eventHandlers_.end()) {
        return Disposition::Continue;
    }
    return it->second(object);
}

// ------------------------------------------------------------- mechanism 5 ----

a11y::ObjectPtr Module::statusBar() const {
    // NVDA's `api.getStatusBar`: the status bar is a child of the foreground
    // window, usually the last one. The search is bounded (see findDescendant)
    // because this runs on a key press and a spreadsheet has a million
    // descendants.
    const a11y::ObjectPtr window = activeWindow();
    if (window == nullptr) {
        return nullptr;
    }
    // Last child first: that is where every toolkit puts it, and checking
    // backwards turns the common case into one comparison.
    const std::vector<a11y::ObjectPtr>& children = window->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (*it != nullptr && (*it)->role() == a11y::Role::StatusBar) {
            return *it;
        }
    }
    return findDescendant(window, a11y::Role::StatusBar);
}

std::string Module::statusBarText() const {
    const a11y::ObjectPtr bar = statusBar();
    if (bar == nullptr) {
        return {};
    }
    // NVDA: the names of the status bar's children, in order, joined with a
    // space; the bar's own name when it has no children (a one-field status
    // bar is one object in GTK and three in Qt).
    std::string text;
    const auto append = [&text](const std::string& piece) {
        if (piece.empty()) {
            return;
        }
        if (!text.empty()) {
            text += ' ';
        }
        text += piece;
    };
    for (const a11y::ObjectPtr& child : bar->children()) {
        if (child == nullptr) {
            continue;
        }
        append(child->name());
        // A status field's content is its VALUE in Qt and its NAME in GTK, so
        // both are asked for -- the alternative is a status line that is
        // silent in half the applications on the desktop.
        if (child->name().empty()) {
            append(child->value());
        }
    }
    if (text.empty()) {
        append(bar->name());
        append(bar->value());
    }
    return text;
}

std::string Module::productName() const {
    // The application's own name is the honest answer: on Linux there is no
    // equivalent of NVDA's `FileVersionInfo`, and inventing one from the
    // executable would be a guess shown to the user.
    if (!key_.name.empty()) {
        return key_.name;
    }
    return key_.executable;
}

std::string Module::productVersion() const {
    // Deliberately empty rather than guessed. A module that knows where its
    // application keeps its version (LibreOffice does) overrides this.
    return {};
}

std::vector<std::string> Module::devInfo() const {
    std::vector<std::string> lines;
    lines.push_back(std::format("module: {}{}", moduleName_, generic_ ? " (generic)" : ""));
    lines.push_back(std::format("application: {}", key_.describe()));
    lines.push_back(std::format("scripts: {}, event handlers: {}", scriptCount(),
                                eventHandlers_.size()));
    lines.push_back(std::format("sleep mode: {}", sleepMode_ ? "on" : "off"));
    const std::string status = statusBarText();
    lines.push_back(std::format("status line: {}", status.empty() ? "(none)" : status));
    return lines;
}

// ----------------------------------------------------------- the application ----

bool Module::isAlive() const {
    if (key_.processId <= 0) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(std::format("/proc/{}", key_.processId), ec);
}

a11y::ObjectPtr Module::applicationObject() const {
    a11y::Registry* registry = services_.a11y;
    if (registry == nullptr || key_.processId <= 0) {
        return nullptr;
    }
    // From the focus first: that is one comparison, and it is the answer
    // whenever this module is the current one -- which is whenever a script of
    // its own is running.
    if (const a11y::ObjectPtr& focus = registry->api().focus()) {
        if (focus->processId() == key_.processId) {
            if (a11y::ObjectPtr application = focus->application()) {
                return application;
            }
        }
    }
    const a11y::ObjectPtr desktop = registry->api().desktopObject();
    if (desktop == nullptr) {
        return nullptr;
    }
    for (const a11y::ObjectPtr& application : desktop->children()) {
        if (application != nullptr && application->processId() == key_.processId) {
            return application;
        }
    }
    return nullptr;
}

a11y::ObjectPtr Module::activeWindow() const {
    const a11y::ObjectPtr application = applicationObject();
    if (application == nullptr) {
        return nullptr;
    }
    // ACTIVE is a platform state with no ALUS equivalent -- NVDA has no such
    // state and nothing announces it -- so it is read from the platform
    // directly, exactly as a11y::Registry::activeWindow does.
    a11y::Backend& backend = application->backend();
    a11y::ObjectPtr fallback;
    for (const a11y::ObjectPtr& window : application->children()) {
        if (window == nullptr) {
            continue;
        }
        const std::uint64_t bits = backend.states(window->ref());
        if ((bits & a11y::platformStateBit(a11y::PlatformState::Active)) != 0) {
            return window;
        }
        if (fallback == nullptr) {
            fallback = window;
        }
    }
    // A single-window application whose window does not claim ACTIVE (which
    // several Qt applications do not) still has a status bar.
    return fallback;
}

a11y::ObjectPtr Module::findDescendant(const a11y::ObjectPtr& root, a11y::Role role,
                                       int maxDepth) const {
    if (root == nullptr || maxDepth < 0) {
        return nullptr;
    }
    if (root->role() == role) {
        return root;
    }
    if (maxDepth == 0) {
        return nullptr;
    }
    for (const a11y::ObjectPtr& child : root->children()) {
        if (a11y::ObjectPtr found = findDescendant(child, role, maxDepth - 1)) {
            return found;
        }
    }
    return nullptr;
}

void Module::say(const std::string& text) const {
    if (services_.message) {
        services_.message(text);
    }
}

}  // namespace alus::app
