// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// `alus-settings` — the settings window, as its own process.
//
// WHY ITS OWN PROCESS, which is the opposite of what NVDA does and of what
// this file originally did.
//
// The window was built in the daemon first, the way NVDA builds its wx
// dialogs: in process, on the screen reader's own main loop, so that the
// gesture dialog can show the live gesture maps. It built in 83 ms and
// presented in 122 ms, and then the screen reader froze for 7.5 seconds and
// the window could not be read at all. The log says exactly why
// (claude/FAZA9.md N79):
//
//     AT-SPI get_role failed: Did not receive a reply.
//     core stuck for 516 ms, recovery started
//
// ALUS reads the desktop by making SYNCHRONOUS AT-SPI calls from its main
// loop. An AT-SPI call about an object reaches the process that owns the
// object, and is answered from THAT process's main loop. When the object is
// ALUS's own window, the two are the same loop -- which is blocked inside the
// call that is waiting for itself. Every property read of the settings window
// therefore waits the full 500 ms call timeout and returns nothing. 516 ms is
// not a coincidence; it is the timeout.
//
// There is no way to fix that from the client side: the reply cannot arrive
// until the loop runs, and the loop cannot run until the reply arrives. The
// only cure is for the window to belong to a DIFFERENT process, which is what
// this file is. ALUS then reads it exactly as it reads GNOME Settings, with
// the machinery phases 4 to 8 already proved on real applications.
//
// What this costs, and what it does not:
//
//   * It does NOT cost the gesture dialog its data. Four of the five gesture
//     layers are FILES (user, locale, driver, built-in) and the command
//     catalogue is a file, so this process reads the same bytes the daemon
//     read. The fifth layer, what app modules and plugins bind, is live in the
//     daemon -- and is fetched over the bus, or left out with a line in the
//     dialog if the daemon is not running (gui::Service::remoteAppMap).
//   * It DOES cost a write/notice round trip: a setting changed here lands in
//     ~/.config/alus/alus.ini, and the daemon notices through a file monitor
//     and re-reads just that section. Slower than a function call and
//     immeasurably so to a user, and it has a property the in-process version
//     did not: the settings survive the daemon being restarted under them.
//
// One instance: the second invocation hands its page to the first and exits,
// so the launcher, `alus+control+g` and `alus-cli settings` all raise one
// window rather than stacking three.

#include "alus/a11y/service.hpp"
#include "alus/braille/service.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/core/paths.hpp"
#include "alus/gui/model.hpp"
#include "alus/gui/window.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/i18n/service.hpp"
#include "alus/input/manager.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/service.hpp"
#include "alus/script/catalog.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/service.hpp"

#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr const char* kBusName = "org.alus.Settings";
constexpr const char* kObjectPath = "/org/alus/Settings";
constexpr const char* kInterface = "org.alus.Settings";

constexpr const char* kIntrospection = R"XML(
<node>
  <interface name='org.alus.Settings'>
    <method name='ShowPage'>
      <arg type='s' name='page' direction='in'/>
    </method>
  </interface>
</node>
)XML";

struct Options {
    gchar* page = nullptr;
    gchar* configDir = nullptr;
    gchar* dataDir = nullptr;
    gchar* language = nullptr;
    gboolean version = FALSE;
};

// Everything the window reads. Assembled from FILES, which is what makes a
// separate process possible at all: the configuration, the command catalogue
// and four of the five gesture layers are on disk.
struct Session {
    std::unique_ptr<alus::config::Manager> config;
    alus::script::Catalog catalog;
    std::unique_ptr<alus::core::Loop> loop;
    std::unique_ptr<alus::script::Dispatcher> dispatcher;
    std::unique_ptr<alus::input::GestureManager> gestures;
    std::unique_ptr<alus::gui::Window> window;
};

Session* session() {
    static Session value;
    return &value;
}

void handleCall(GDBusConnection* /*connection*/, const gchar* /*sender*/,
                const gchar* /*path*/, const gchar* /*interface*/, const gchar* method,
                GVariant* parameters, GDBusMethodInvocation* invocation,
                gpointer /*userData*/) {
    if (g_strcmp0(method, "ShowPage") != 0) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_UNKNOWN_METHOD, "%s", method);
        return;
    }
    const gchar* page = nullptr;
    g_variant_get(parameters, "(&s)", &page);
    const std::string pageId =
        (page != nullptr && *page != '\0') ? std::string{page} : std::string{"general"};
    if (alus::gui::findPage(pageId) == nullptr) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_INVALID_ARGS,
                                              "no such settings page: %s", pageId.c_str());
        return;
    }
    const bool opened = pageId == "gestures" ? session()->window->openGestures()
                                             : session()->window->open(pageId);
    if (!opened) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                              "could not show the settings");
        return;
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

const GDBusInterfaceVTable kVTable = {handleCall, nullptr, nullptr, {nullptr}};

// Hands the page to the already-running instance. Returns true when it was
// taken, in which case this process has nothing left to do.
bool handOver(const std::string& page) {
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (bus == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBusName, kObjectPath, kInterface, "ShowPage",
        g_variant_new("(s)", page.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
        &error);
    g_object_unref(bus);
    if (reply != nullptr) {
        g_variant_unref(reply);
        return true;
    }
    if (error != nullptr) {
        g_error_free(error);
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    alus::i18n::initializeFromEnvironment();

    Options options;
    const GOptionEntry entries[] = {
        {"page", 'p', 0, G_OPTION_ARG_STRING, &options.page,
         "Which settings page to show", "ID"},
        {"config-dir", 0, 0, G_OPTION_ARG_STRING, &options.configDir,
         "Configuration directory", "DIR"},
        {"data-dir", 0, 0, G_OPTION_ARG_STRING, &options.dataDir, "Data directory",
         "DIR"},
        {"language", 0, 0, G_OPTION_ARG_STRING, &options.language, "Language code",
         "CODE"},
        {"version", 'v', 0, G_OPTION_ARG_NONE, &options.version,
         "Print the version and exit", nullptr},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
    };

    GOptionContext* context = g_option_context_new("- ALUS settings");
    g_option_context_add_main_entries(context, entries, nullptr);
    GError* error = nullptr;
    const gboolean parsed = g_option_context_parse(context, &argc, &argv, &error);
    g_option_context_free(context);
    if (parsed == FALSE) {
        std::fprintf(stderr, "alus-settings: %s\n",
                     error != nullptr ? error->message : "could not parse the arguments");
        if (error != nullptr) {
            g_error_free(error);
        }
        return 2;
    }
    // Every gchar* GOption filled is copied out and freed HERE, before any
    // early return. The alternative -- g_free at the end of main -- leaks on
    // `--version`, on a bad page and on the hand-over path, which is exactly
    // what valgrind found (FAZA9.md N87). A process about to exit leaking 18
    // bytes harms nobody, but a leak the leak checker reports is a leak
    // someone has to triage, and the checker is only useful while it is quiet.
    const std::string page = options.page != nullptr ? options.page : "general";
    const std::string configDirOption =
        options.configDir != nullptr ? options.configDir : "";
    const std::string dataDirOption = options.dataDir != nullptr ? options.dataDir : "";
    const std::string languageOption =
        options.language != nullptr ? options.language : "";
    const bool wantVersion = options.version != FALSE;
    g_free(options.page);
    g_free(options.configDir);
    g_free(options.dataDir);
    g_free(options.language);
    options.page = nullptr;
    options.configDir = nullptr;
    options.dataDir = nullptr;
    options.language = nullptr;

    if (wantVersion) {
        std::printf("alus-settings %s\n", ALUS_VERSION);
        return 0;
    }
    if (alus::gui::findPage(page) == nullptr) {
        std::fprintf(stderr, "alus-settings: no such page: %s\n", page.c_str());
        return 2;
    }

    // Already open? Raise it and go. Before anything expensive.
    if (handOver(page)) {
        return 0;
    }

    alus::core::Log::setThreadName("main");
    alus::core::Log::instance().setLevel(alus::core::LogLevel::Warning);

    if (!alus::gui::isAvailable()) {
        std::fprintf(stderr, "alus-settings: %s\n",
                     alus::gui::unavailableReason().c_str());
        return 1;
    }

    Session* s = session();
    s->config = std::make_unique<alus::config::Manager>(
        !configDirOption.empty() ? alus::config::Paths::forDirectory(configDirOption)
                                 : alus::config::Paths::fromEnvironment());
    // The SAME defaults the daemon registers, from the same functions: a
    // settings window that showed a different default from the one the screen
    // reader uses would be worse than no window.
    s->config->setDefault("general", "logLevel", "info");
    alus::i18n::Service::registerDefaults(*s->config);
    alus::speech::Service::registerDefaults(*s->config);
    alus::input::Service::registerDefaults(*s->config);
    alus::a11y::Service::registerDefaults(*s->config);
    alus::nav::Service::registerDefaults(*s->config);
    alus::braille::Service::registerDefaults(*s->config);
    std::string configError;
    if (!s->config->load(&configError)) {
        ALUS_WARNING("could not read the configuration: {}", configError);
    }

    alus::i18n::Options i18nOptions;
    i18nOptions.language = !languageOption.empty()
                               ? languageOption
                               : s->config->getString("general", "language", "");
    alus::i18n::initialize(i18nOptions);

    // The command catalogue and the built-in gesture layer, from the same data
    // directory the daemon uses.
    const std::filesystem::path dataDir = alus::core::findDataDirectory(
        !dataDirOption.empty() ? std::filesystem::path{dataDirOption}
                               : std::filesystem::path{},
        "scripts.csv");
    if (!dataDir.empty()) {
        s->catalog.loadFromDataDirectory(dataDir);
    } else {
        ALUS_WARNING("no data directory: the gesture dialog will be empty");
    }

    s->loop = std::make_unique<alus::core::Loop>();
    s->dispatcher = std::make_unique<alus::script::Dispatcher>(s->loop.get(),
                                                               &s->catalog);
    s->gestures =
        std::make_unique<alus::input::GestureManager>(*s->loop, *s->dispatcher);
    s->gestures->setBuiltinMap(&s->catalog.builtinGestures());
    // The user's own map and the per-language one. The driver and app layers
    // are the daemon's and are left empty here; the dialog says so.
    std::string mapError;
    const std::filesystem::path userMap = s->config->paths().configDir / "gestures.ini";
    if (std::filesystem::exists(userMap)) {
        s->gestures->userMap().load(userMap, &mapError);
    }
    if (!dataDir.empty()) {
        const std::string language = alus::i18n::currentLanguage().substr(0, 2);
        const std::filesystem::path localeMap =
            dataDir / "locale" / language / "gestures.ini";
        if (std::filesystem::exists(localeMap)) {
            s->gestures->localeMap().load(localeMap, &mapError);
        }
    }
    s->gestures->refreshMaps();

    alus::gui::Context guiContext;
    guiContext.config = s->config.get();
    guiContext.gestures = s->gestures.get();
    guiContext.keyNames = &alus::input::KeyNames::shared();
    guiContext.catalog = &s->catalog;
    // There is no synthesizer here. A message the window wants spoken goes to
    // the RUNNING ALUS, which is the only thing that can speak -- and when
    // nothing is running, to stderr, where a developer will see it.
    guiContext.message = [](const std::string& text) {
        ALUS_INFO("settings: {}", text);
    };
    // A setting is written to the file immediately, and the daemon notices
    // through its own file monitor. No section-by-section call here: this
    // process does not own the subsystems.
    guiContext.settingChanged = [s](const std::string& /*section*/) {
        std::string saveError;
        if (!s->config->save(&saveError)) {
            ALUS_WARNING("could not save the configuration: {}", saveError);
        }
    };
    guiContext.resetAll = [s] {
        s->config->reset(/*factoryDefaults=*/true);
        std::string saveError;
        if (!s->config->save(&saveError)) {
            ALUS_WARNING("could not save the configuration: {}", saveError);
        }
    };

    s->window = std::make_unique<alus::gui::Window>(std::move(guiContext));

    // One instance. Requested AFTER the window exists, so that a hand-over
    // arriving immediately finds something to show.
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    guint registration = 0;
    if (bus != nullptr) {
        GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
        if (info != nullptr) {
            registration = g_dbus_connection_register_object(
                bus, kObjectPath, info->interfaces[0], &kVTable, nullptr, nullptr, nullptr);
            g_dbus_node_info_unref(info);
        }
        GVariant* reply = g_dbus_connection_call_sync(
            bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "RequestName", g_variant_new("(su)", kBusName, 4u), G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
        if (reply != nullptr) {
            g_variant_unref(reply);
        }
    }

    if (!(page == "gestures" ? s->window->openGestures() : s->window->open(page))) {
        std::fprintf(stderr, "alus-settings: could not show the settings\n");
        return 1;
    }

    // The window closing ends the process. A settings window that stayed
    // running invisibly would hold the bus name and the next invocation would
    // raise nothing.
    while (s->window->isOpen() || s->window->isGesturesOpen()) {
        g_main_context_iteration(g_main_context_default(), TRUE);
    }

    if (bus != nullptr) {
        if (registration != 0) {
            g_dbus_connection_unregister_object(bus, registration);
        }
        g_object_unref(bus);
    }
    s->window.reset();
    return 0;
}
