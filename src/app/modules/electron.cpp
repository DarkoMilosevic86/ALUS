// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The Electron app module: VS Code and its relatives.
//
// KEYED ON THE TOOLKIT, NOT THE EXECUTABLE, and that is the half of T8.1 the
// executable cannot do. There are hundreds of Electron applications and nobody
// is going to write a rule for each; what they share is the toolkit string
// Chromium reports, measured on the target system as:
//
//     Google Chrome   toolkit "Chromium"      (a browser)
//     VS Code         toolkit "Chromium"      (an Electron application)
//
// WHICH IS THE PROBLEM. A browser and an Electron application are the same
// toolkit and must be treated oppositely: a browser's document IS the content
// and browse mode is how it is read, while an Electron application's "document"
// is a user interface that happens to be made of HTML, where browse mode takes
// the arrow keys away from a text editor. So the rule matches the toolkit and
// the FACTORY DECLINES for a browser (Registry::moduleFor then falls back to
// the generic module, which is what a browser wants -- phase 6 measured that
// Firefox and Chromium need no module at all).
//
// WHAT IT FIXES: THE EDITOR FREEZING. VS Code exposes its editor as a document
// with a caret of its own and thousands of nodes. Intercepting it means ALUS
// rebuilding an ancestor chain on every keystroke in a file that the plain
// text layer already reads correctly, and the symptom is the one the phase 8
// manual test looks for -- an editor that stops responding while typing. NVDA
// blocks browse mode in `appModules/code.py` for the same reason. The veto in
// `nav::TreeInterceptorRegistry` exists for this (phase 6 could not express
// it: a chooser that declines only passes the question on).
#include "alus/a11y/registry.hpp"
#include "alus/app/modules.hpp"
#include "alus/core/log.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace alus::app {
namespace {

// The executables that are browsers rather than Electron applications. A
// browser's own name is the only thing that distinguishes it, which is why
// this list exists and why it is short: anything NOT on it is treated as an
// Electron application, so a new Electron app works with no change and a new
// browser needs one line.
constexpr std::array<std::string_view, 7> kBrowsers{{
    "chrome", "google-chrome", "google-chrome-stable", "chromium", "chromium-browser",
    "brave", "brave-browser",
}};

bool isBrowser(const AppKey& key) {
    if (std::find(kBrowsers.begin(), kBrowsers.end(), key.executable) != kBrowsers.end()) {
        return true;
    }
    // A sandboxed browser whose /proc is unreadable: the accessible name is
    // what is left, and "Google Chrome" is what it reports (measured).
    const std::string_view name = key.name;
    return name.find("Chrome") != std::string_view::npos ||
           name.find("Chromium") != std::string_view::npos ||
           name.find("Brave") != std::string_view::npos;
}

class ElectronModule final : public Module {
public:
    ElectronModule(AppKey key, Services services)
        : Module("electron", std::move(key), std::move(services),
                 {"appModules.electron.AppModule"}) {}

    void initialize() override {
        ALUS_INFO("electron module: browse mode is blocked for {}", key().describe());
    }

    // The whole module, in one answer. EVERY document in the process, not
    // only the editor: an Electron application's dialogs, its settings pages
    // and its welcome screen are the same HTML interface, read correctly by
    // the object and text layers and read badly by browse mode.
    bool blocksTreeInterceptor(const a11y::Object& /*object*/) const override { return true; }
};

}  // namespace

void registerElectronModule(Registry& registry) {
    registry.add(Registry::Rule{
        "electron", "", "chromium",
        [](const AppKey& key, const Services& services) -> ModulePtr {
            if (isBrowser(key)) {
                // Decline: a browser gets the generic module and keeps browse
                // mode. See the file comment.
                return nullptr;
            }
            return std::make_unique<ElectronModule>(key, services);
        }});
}

}  // namespace alus::app
