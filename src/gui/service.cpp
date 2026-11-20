// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/gui/service.hpp.

#include "alus/gui/service.hpp"

#include "alus/core/log.hpp"
#include "alus/gui/gesture_model.hpp"
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
        // Upstream's `showGui` opens its MENU, and so does ALUS's now
        // (alus-menu, N152). The page here is the fallback when the menu
        // program cannot be started: the settings window on its first page.
        // Its key is ALUS's own choice and not upstream's; see
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
    // N152: NVDA's alus+n menu. The settings window still opens when the
    // menu program cannot be started.
    void showGui(input::Gesture&) {
        if (!service_.openMenu()) {
            open("showGui");
        }
    }
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

// ---------------------------------------------------------------------------
// The ALUS menu (N152)
// ---------------------------------------------------------------------------

namespace {

std::string menuProgram() {
    std::error_code code;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", code);
    if (!code) {
        for (const std::filesystem::path& candidate :
             {self.parent_path() / "alus-menu",
              self.parent_path().parent_path() / "src" / "alus-menu"}) {
            if (std::filesystem::exists(candidate, code)) {
                return candidate.string();
            }
        }
    }
    if (gchar* found = g_find_program_in_path("alus-menu")) {
        std::string path = found;
        g_free(found);
        return path;
    }
    return {};
}

struct MenuWatch {
    std::string output;
    std::function<void(const std::string&)> done;
};

std::string escapeHtml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace

bool Service::openMenu() {
    if (options_.noGui || !hasDisplay()) {
        return false;
    }
    const std::string program = menuProgram();
    if (program.empty()) {
        ALUS_INFO("gui: alus-menu is not installed; opening the settings");
        return false;
    }
    const std::string language = i18n::currentLanguage();
    std::vector<const char*> argv{program.c_str()};
    if (!language.empty()) {
        argv.push_back("--language");
        argv.push_back(language.c_str());
    }
    argv.push_back(nullptr);
    gint out = -1;
    GError* error = nullptr;
    if (g_spawn_async_with_pipes(nullptr, const_cast<char**>(argv.data()), nullptr,
                                 G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr, &out,
                                 nullptr, &error) == FALSE) {
        ALUS_WARNING("gui: could not start {}: {}", program,
                     error != nullptr ? error->message : "unknown error");
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }
    GIOChannel* channel = g_io_channel_unix_new(out);
    g_io_channel_set_close_on_unref(channel, TRUE);
    g_io_channel_set_encoding(channel, nullptr, nullptr);
    g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, nullptr);
    auto* watch = new MenuWatch{{}, [this](const std::string& choice) { onMenuChoice(choice); }};
    g_io_add_watch_full(
        channel, G_PRIORITY_DEFAULT, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
        +[](GIOChannel* source, GIOCondition condition, gpointer data) -> gboolean {
            auto* self = static_cast<MenuWatch*>(data);
            if ((condition & G_IO_IN) != 0) {
                gchar buffer[256];
                gsize got = 0;
                const GIOStatus status =
                    g_io_channel_read_chars(source, buffer, sizeof buffer, &got, nullptr);
                if (got > 0) {
                    self->output.append(buffer, got);
                }
                if (status == G_IO_STATUS_NORMAL || status == G_IO_STATUS_AGAIN) {
                    // One line is the whole answer; the child may still be
                    // showing a dialog of its own, so do not wait for it.
                    if (self->output.find('\n') == std::string::npos) {
                        return TRUE;
                    }
                }
            } else if ((condition & (G_IO_HUP | G_IO_ERR)) == 0) {
                return TRUE;
            }
            const std::string choice = self->output.substr(0, self->output.find('\n'));
            if (!choice.empty() && self->done) {
                self->done(choice);
            }
            return FALSE;
        },
        watch, +[](gpointer data) { delete static_cast<MenuWatch*>(data); });
    g_io_channel_unref(channel);
    ALUS_INFO("gui: ALUS menu opened");
    return true;
}

void Service::onMenuChoice(const std::string& choice) {
    ALUS_INFO("gui: ALUS menu chose {}", choice);
    if (choice == "settings") {
        openPage("general");
    } else if (choice == "gestures") {
        openGestures();
    } else if (choice == "commands") {
        const std::string html = commandsReferenceHtml();
        const char* runtime = g_get_user_runtime_dir();
        const std::filesystem::path dir = std::filesystem::path{runtime} / "alus";
        std::error_code code;
        std::filesystem::create_directories(dir, code);
        const std::filesystem::path file = dir / "commands.html";
        if (g_file_set_contents(file.c_str(), html.data(), static_cast<gssize>(html.size()),
                                nullptr) == FALSE) {
            ALUS_WARNING("gui: could not write {}", file.string());
            return;
        }
        gchar* uri = g_filename_to_uri(file.c_str(), nullptr, nullptr);
        if (uri != nullptr) {
            GError* error = nullptr;
            if (g_app_info_launch_default_for_uri(uri, nullptr, &error) == FALSE) {
                ALUS_WARNING("gui: could not open the commands reference: {}",
                             error != nullptr ? error->message : "unknown error");
                if (error != nullptr) {
                    g_error_free(error);
                }
            }
            g_free(uri);
        }
    } else if (hooks_.menuAction) {
        hooks_.menuAction(choice);
    }
}

std::string Service::commandsReferenceHtml() const {
    // Translators: the title of the commands quick reference page that the
    // ALUS menu opens.
    const std::string title = _("ALUS commands quick reference");
    std::string html = "<!doctype html>\n<html lang=\"" +
                       escapeHtml(i18n::currentLanguage().substr(0, 2)) +
                       "\">\n<head>\n<meta charset=\"utf-8\">\n<title>" + escapeHtml(title) +
                       "</title>\n<style>body{font-family:sans-serif;max-width:60em;margin:auto;"
                       "padding:1em;line-height:1.5}table{border-collapse:collapse;width:100%}"
                       "th,td{border:1px solid #888;padding:.3em .5em;text-align:left;"
                       "vertical-align:top}</style>\n</head>\n<body>\n<h1>" +
                       escapeHtml(title) + "</h1>\n";
    // Translators: the introduction of the commands quick reference page.
    html += "<p>" +
            escapeHtml(_("Every command ALUS has and the keys that run it now, including keys "
                         "you assigned yourself. Commands with no key can be given one in the "
                         "Input gestures dialog.")) +
            "</p>\n";
    if (catalog_ == nullptr || gestures_ == nullptr) {
        return html + "</body>\n</html>\n";
    }
    const Model model = buildModel(*catalog_, *gestures_, keyNames_);
    for (const std::string& category : model.categories) {
        html += "<h2>" + escapeHtml(category) + "</h2>\n<table>\n<tr><th>" +
                // Translators: a column of the commands quick reference.
                escapeHtml(_("Command")) + "</th><th>" +
                // Translators: a column of the commands quick reference.
                escapeHtml(_("Keys")) + "</th></tr>\n";
        for (const Command* command : model.inCategory(category)) {
            std::string keys;
            for (const Binding& binding : command->bindings) {
                keys += (keys.empty() ? "" : ", ") + binding.display;
            }
            if (keys.empty()) {
                // Translators: the commands quick reference, for a command
                // with no key.
                keys = _("none");
            }
            html += "<tr><td>" + escapeHtml(command->description) + "</td><td>" +
                    escapeHtml(keys) + "</td></tr>\n";
        }
        html += "</table>\n";
    }
    return html + "</body>\n</html>\n";
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
