// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/gui/service.hpp.

#include "alus/gui/service.hpp"

#include "alus/core/log.hpp"
#include "alus/gui/model.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/input/gesture.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/dispatch.hpp"

#include <gio/gio.h>
#include <glib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace alus::gui {
namespace {

// The inherited command table has one script per upstream settings panel.
// ALUS has SEVEN pages where upstream has fifteen panels, because most of what
// those panels configure is either one of ALUS's pages or a v1.0 non-goal
// (vision.md §5: the mouse, the add-on store, input composition and the
// dictionary editors are all deferred).
//
// An empty page is DELIBERATE and is not a gap to be filled with the nearest
// page: `alus+control+m` opening the keyboard settings because ALUS has no
// mouse settings would be worse than it saying there are none. The script
// reports the absence, which is what §3.8 asks of an expected failure.
const std::vector<ScriptPage>& table() {
    static const std::vector<ScriptPage> value = {
        // The seven pages ALUS has.
        {"activateGeneralSettingsDialog", "general"},
        {"activateAdvancedSettings", "general"},
        {"activateSynthesizerDialog", "speech"},
        {"activateVoiceDialog", "speech"},
        {"activateBrailleSettingsDialog", "braille"},
        {"activateBrailleDisplayDialog", "braille"},
        {"activateKeyboardSettingsDialog", "keyboard"},
        {"activateObjectPresentationDialog", "presentation"},
        {"activateReviewCursorDialog", "review"},
        {"activateDocumentFormattingDialog", "formatting"},
        {"activateInputGesturesDialog", "gestures"},
        // Upstream's `showGui` opens its MENU, and ALUS has no menu: it has
        // this window, which is the same thing one level down. So the key
        // opens the window on its first page rather than saying there is no
        // such panel -- the user asked for "the ALUS settings", and that is
        // what they get. Its key is ALUS's own choice and not upstream's; see
        // data/gestures-alus.ini.
        {"showGui", "general"},
        // Bound keys with no page in v1.0. Listed rather than omitted, so the
        // command can say which setting it is that does not exist, and so
        // tests/gui can assert that no shortcut silently opens the wrong page.
        {"activateAudioSettingsDialog", ""},
        {"activateMouseSettingsDialog", ""},
        {"activateBrowseModeDialog", ""},
        {"activateConfigProfilesDialog", ""},
        {"activateAddonStoreSettings", ""},
        {"activatePrivacyAndSecuritySettings", ""},
        {"activateInputCompositionDialog", ""},
        {"activateSpeechSymbolsDialog", ""},
        {"activateDefaultDictionaryDialog", ""},
        {"activateVoiceDictionaryDialog", ""},
        {"activateTemporaryDictionaryDialog", ""},
    };
    return value;
}

}  // namespace

const std::vector<ScriptPage>& scriptPages() { return table(); }

std::string pageForScript(std::string_view script) {
    const auto it = std::find_if(table().begin(), table().end(),
                                 [script](const ScriptPage& entry) {
                                     return entry.script == script;
                                 });
    return it == table().end() ? std::string{} : it->page;
}

// ---------------------------------------------------------------------------
// The scripts.
//
// One ScriptableObject answering to `globalCommands.GlobalCommands`, which is
// the section the generated built-in gesture map uses -- so every one of these
// picks up NVDA's own key and NVDA's own description with no transcription
// (see script::Catalog), and `alus+control+g` works because upstream binds it.
// ---------------------------------------------------------------------------

class Service::Commands final : public script::ScriptableObject {
public:
    Commands(Service& service, const script::Catalog* catalog)
        : script::ScriptableObject({"globalCommands.GlobalCommands"}, catalog),
          service_(service) {
        ALUS_SCRIPT(activateGeneralSettingsDialog);
        ALUS_SCRIPT(activateAdvancedSettings);
        ALUS_SCRIPT(activateSynthesizerDialog);
        ALUS_SCRIPT(activateVoiceDialog);
        ALUS_SCRIPT(activateBrailleSettingsDialog);
        ALUS_SCRIPT(activateBrailleDisplayDialog);
        ALUS_SCRIPT(activateKeyboardSettingsDialog);
        ALUS_SCRIPT(activateObjectPresentationDialog);
        ALUS_SCRIPT(activateReviewCursorDialog);
        ALUS_SCRIPT(activateDocumentFormattingDialog);
        ALUS_SCRIPT(activateInputGesturesDialog);
        ALUS_SCRIPT(activateAudioSettingsDialog);
        ALUS_SCRIPT(activateMouseSettingsDialog);
        ALUS_SCRIPT(activateBrowseModeDialog);
        ALUS_SCRIPT(activateConfigProfilesDialog);
        ALUS_SCRIPT(showGui);
    }

private:
    // Every one of these is the same two lines; the script NAME is what
    // chooses the page, through the table above. Written out rather than
    // generated with a macro because ALUS_SCRIPT needs a real method to take
    // the address of, and because a reader looking for
    // `activateBrailleSettingsDialog` should find it.
    void open(std::string_view script) { service_.openByScript(script); }

    void activateGeneralSettingsDialog(input::Gesture&) {
        open("activateGeneralSettingsDialog");
    }
    void showGui(input::Gesture&) { open("showGui"); }
    void activateAdvancedSettings(input::Gesture&) { open("activateAdvancedSettings"); }
    void activateSynthesizerDialog(input::Gesture&) { open("activateSynthesizerDialog"); }
    void activateVoiceDialog(input::Gesture&) { open("activateVoiceDialog"); }
    void activateBrailleSettingsDialog(input::Gesture&) {
        open("activateBrailleSettingsDialog");
    }
    void activateBrailleDisplayDialog(input::Gesture&) {
        open("activateBrailleDisplayDialog");
    }
    void activateKeyboardSettingsDialog(input::Gesture&) {
        open("activateKeyboardSettingsDialog");
    }
    void activateObjectPresentationDialog(input::Gesture&) {
        open("activateObjectPresentationDialog");
    }
    void activateReviewCursorDialog(input::Gesture&) {
        open("activateReviewCursorDialog");
    }
    void activateDocumentFormattingDialog(input::Gesture&) {
        open("activateDocumentFormattingDialog");
    }
    void activateInputGesturesDialog(input::Gesture&) {
        open("activateInputGesturesDialog");
    }
    void activateAudioSettingsDialog(input::Gesture&) {
        open("activateAudioSettingsDialog");
    }
    void activateMouseSettingsDialog(input::Gesture&) {
        open("activateMouseSettingsDialog");
    }
    void activateBrowseModeDialog(input::Gesture&) { open("activateBrowseModeDialog"); }
    void activateConfigProfilesDialog(input::Gesture&) {
        open("activateConfigProfilesDialog");
    }

    Service& service_;
};

// ---------------------------------------------------------------------------

Service::Service(config::Manager& manager, input::GestureManager* gestures,
                 const input::KeyNames* keyNames, const script::Catalog* catalog,
                 Hooks hooks, ServiceOptions options)
    : manager_(manager),
      gestures_(gestures),
      keyNames_(keyNames),
      catalog_(catalog),
      hooks_(std::move(hooks)),
      options_(options) {}

Service::~Service() = default;

void Service::initialize() {
    if (options_.noGui) {
        ALUS_INFO("gui: disabled on the command line");
        return;
    }
    // Only the LOOKUP, not the window: the window is another process and
    // starting it costs a fork, which the user asks for by pressing a key.
    // What is done at start-up is finding the program, so the log says at
    // start-up whether the settings would open -- which is the question a bug
    // report has to answer.
    const std::string path = settingsProgram();
    if (path.empty()) {
        ALUS_INFO("gui: not available: {}", unavailableReason());
    } else {
        ALUS_DEBUG("gui: settings program {}", path);
    }
}

void Service::terminate() {}

script::ScriptableObject* Service::scriptable() {
    if (!commands_) {
        commands_ = std::make_unique<Commands>(*this, catalog_);
    }
    return commands_.get();
}

// Where `alus-settings` is.
//
// Beside this executable FIRST, so an uninstalled build opens the settings
// window it was just built with rather than an older installed one -- the
// difference matters while the settings pages are still changing. Then $PATH.
std::string Service::settingsProgram() const {
    if (!program_.empty()) {
        return program_ == "-" ? std::string{} : program_;
    }
    std::error_code code;
    const std::filesystem::path self =
        std::filesystem::read_symlink("/proc/self/exe", code);
    if (!code) {
        const std::filesystem::path beside = self.parent_path() / "alus-settings";
        if (std::filesystem::exists(beside, code)) {
            program_ = beside.string();
            return program_;
        }
    }
    if (gchar* found = g_find_program_in_path("alus-settings")) {
        program_ = found;
        g_free(found);
        return program_;
    }
    program_ = "-";
    return {};
}

bool Service::available() const {
    return !options_.noGui && !settingsProgram().empty() && hasDisplay();
}

bool Service::hasDisplay() {
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* x11 = std::getenv("DISPLAY");
    return (wayland != nullptr && *wayland != '\0') || (x11 != nullptr && *x11 != '\0');
}

std::string Service::unavailableReason() const {
    if (options_.noGui) {
        return "disabled on the command line (--no-gui)";
    }
    if (!hasDisplay()) {
        return "no display: neither WAYLAND_DISPLAY nor DISPLAY is set";
    }
    if (settingsProgram().empty()) {
        return "alus-settings is not installed";
    }
    return {};
}

Service::OpenResult Service::openPage(std::string_view pageId) {
    if (findPage(pageId) == nullptr) {
        return OpenResult::NoSuchPage;
    }
    if (options_.noGui) {
        return OpenResult::Disabled;
    }
    const std::string program = settingsProgram();
    if (program.empty() || !hasDisplay()) {
        return OpenResult::NotAvailable;
    }

    const std::string page{pageId};
    // ASYNCHRONOUS, and that is the whole point of this phase's main finding:
    // the screen reader's main loop must not wait for a window. g_spawn_async
    // returns as soon as the child is forked, the child raises an existing
    // window if there is one, and ALUS goes back to reading the desktop.
    //
    // G_SPAWN_DO_NOT_REAP_CHILD is NOT set, so GLib reaps the child itself and
    // ALUS does not accumulate zombies over a session in which the user opens
    // the settings twenty times.
    const char* argv[] = {program.c_str(), "--page", page.c_str(), nullptr};
    GError* error = nullptr;
    const gboolean started = g_spawn_async(
        nullptr, const_cast<char**>(argv), nullptr,
        static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL),
        nullptr, nullptr, nullptr, &error);
    if (started == FALSE) {
        ALUS_WARNING("gui: could not start {}: {}", program,
                     error != nullptr ? error->message : "unknown error");
        if (error != nullptr) {
            g_error_free(error);
        }
        return OpenResult::NotAvailable;
    }
    page_ = page;
    ALUS_INFO("gui: opening the {} settings", page);
    return OpenResult::Opened;
}

Service::OpenResult Service::openGestures() { return openPage("gestures"); }

std::string Service::currentPage() const { return page_; }

void Service::openByScript(std::string_view script) {
    const std::string page = pageForScript(script);
    if (page.empty()) {
        // A key upstream binds to a panel ALUS does not have. Said, not
        // silent: a shortcut that does nothing is indistinguishable from a
        // key the compositor swallowed (§3.8, and findings N1/N7/N15/N18).
        if (hooks_.message) {
            // Translators: spoken when a settings shortcut names a settings
            // panel this version of ALUS does not have.
            hooks_.message(_("There are no such settings in this version"));
        }
        ALUS_INFO("gui: {} has no page in this version", script);
        return;
    }

    const OpenResult result =
        page == "gestures" ? openGestures() : openPage(page);
    switch (result) {
        case OpenResult::Opened:
            return;
        case OpenResult::NoSuchPage:
            ALUS_WARNING("gui: {} names page '{}', which does not exist", script, page);
            if (hooks_.message) {
                hooks_.message(_("There are no such settings in this version"));
            }
            return;
        case OpenResult::Disabled:
        case OpenResult::NotAvailable:
            if (hooks_.message) {
                // Translators: spoken when the settings window cannot be shown;
                // %s is the reason, e.g. "no display".
                hooks_.message(i18n::substitute(
                    _("The settings window is not available: %s"), unavailableReason()));
            }
            ALUS_WARNING("gui: cannot open {}: {}", page, unavailableReason());
            return;
    }
}

}  // namespace alus::gui
