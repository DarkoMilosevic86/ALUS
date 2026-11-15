// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The ALUS daemon.
//
// Port of NVDA's startup and shutdown path (`core.py:684-1088`), reduced to what
// phase 1 can honestly do: bring up the log, the configuration, the watchdog and
// the core loop, hold a single-instance lock, and go down cleanly on a signal.
// Phase 2 adds speech: alus::speech::Service is the second restartable
// Subsystem, so SIGHUP now also rebuilds the synthesizer connection in place.
// Phase 3 adds input, which is the third, and which reaches speech only
// through the hooks assembled here — the input library does not link against
// the speech library at all. Phase 4 adds the object model, the fourth: it is
// what finally joins the other three, because it is the thing that has
// something to say. It reaches speech the same way input does (through hooks),
// and it hands input the two answers phase 3 was waiting for — the focus
// object for the script dispatch context, and whether the focused control is
// editable, which is what the character echo needs. Braille joins later, the
// same way.
//
// One thing about input is spelled out because a blind user cannot see it: if
// the compositor refuses ALUS the keyboard (risks.md R1, or another screen
// reader holding the bus name, N8), that is ANNOUNCED. The alternative is a
// screen reader that starts, says "ALUS started", and then answers no key —
// the worst failure mode this project has, and the one phase 0 found four
// times (N1, N7, N15, N18).
//
// Signals:
//   SIGTERM, SIGINT — clean shutdown
//   SIGHUP          — resetConfiguration() in place, without restarting the
//                     process (the requirement from architecture.md §3.9)
#include "alus/a11y/service.hpp"
#include "alus/app/service.hpp"
#include "alus/audio/wave_player.hpp"
#include "alus/braille/louis.hpp"
#include "alus/braille/service.hpp"
#include "alus/commands/system.hpp"
#include "alus/config/commands.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/core/watchdog.hpp"
#include "alus/i18n/i18n.hpp"
#include "alus/gui/service.hpp"
#include "alus/speech/global_commands.hpp"
#include "alus/i18n/service.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/say_all.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/service.hpp"
#include "alus/ui/message.hpp"

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

using alus::config::Manager;
using alus::config::resetConfiguration;
using alus::config::Subsystem;
using alus::config::SubsystemRegistry;
using alus::core::Log;
using alus::core::LogLevel;
using alus::core::Loop;
using alus::core::Watchdog;

constexpr const char* kBusName = "org.alus.Alus";

struct Options {
    gboolean version = FALSE;
    gboolean noSpeech = FALSE;
    gboolean noInput = FALSE;
    gboolean noA11y = FALSE;
    gboolean noNav = FALSE;
    gboolean noBraille = FALSE;
    gboolean noApp = FALSE;
    gboolean noPlugins = FALSE;
    gboolean noGui = FALSE;
    gboolean noWatchdog = FALSE;
    gboolean allowMultiple = FALSE;
    gint exitAfter = 0;
    gchar* logLevel = nullptr;
    gchar* language = nullptr;
    gchar* logFile = nullptr;
    gchar* configDir = nullptr;
};

std::optional<LogLevel> parseLogLevel(std::string_view name) {
    if (name == "debug") {
        return LogLevel::Debug;
    }
    if (name == "io") {
        return LogLevel::Io;
    }
    if (name == "debugwarning") {
        return LogLevel::DebugWarning;
    }
    if (name == "info") {
        return LogLevel::Info;
    }
    if (name == "warning") {
        return LogLevel::Warning;
    }
    if (name == "error") {
        return LogLevel::Error;
    }
    if (name == "critical") {
        return LogLevel::Critical;
    }
    if (name == "off") {
        return LogLevel::Off;
    }
    return std::nullopt;
}

// Re-applies the log level from the configuration. NVDA does the same thing in
// resetConfiguration (`logHandler.setLogLevelFromConfig`), and it gives the
// daemon one genuinely restartable subsystem from phase 1 onwards.
class LogSubsystem final : public Subsystem {
public:
    LogSubsystem(Manager& manager, std::optional<LogLevel> commandLineLevel)
        : manager_(manager), commandLineLevel_(commandLineLevel) {}

    std::string_view name() const override { return "log"; }

    void initialize() override {
        // The command line wins over the configuration file, as in NVDA.
        if (commandLineLevel_.has_value()) {
            Log::instance().setLevel(*commandLineLevel_);
            return;
        }
        const std::string configured = manager_.getString("general", "logLevel", "info");
        if (const auto level = parseLogLevel(configured)) {
            Log::instance().setLevel(*level);
        } else {
            ALUS_WARNING("unknown log level in configuration: {}", configured);
        }
    }

    void terminate() override {}

private:
    Manager& manager_;
    std::optional<LogLevel> commandLineLevel_;
};

// Single instance, the way every session daemon does it: own a well-known name
// on the session bus. Losing the race means another ALUS is already speaking.
class SingleInstance {
public:
    enum class Result { Acquired, AlreadyRunning, NoBus };

    Result acquire() {
        GError* error = nullptr;
        bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (bus_ == nullptr) {
            ALUS_WARNING("no session bus ({}); running without a single-instance lock",
                         error != nullptr ? error->message : "unknown error");
            if (error != nullptr) {
                g_error_free(error);
            }
            return Result::NoBus;
        }

        // DBUS_NAME_FLAG_DO_NOT_QUEUE: do not wait in line behind the running
        // instance, fail immediately instead.
        constexpr guint32 kDoNotQueue = 4;
        GVariant* reply = g_dbus_connection_call_sync(
            bus_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "RequestName", g_variant_new("(su)", kBusName, kDoNotQueue), G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
        if (reply == nullptr) {
            ALUS_WARNING("could not request the bus name ({}); continuing without the lock",
                         error != nullptr ? error->message : "unknown error");
            if (error != nullptr) {
                g_error_free(error);
            }
            return Result::NoBus;
        }

        guint32 code = 0;
        g_variant_get(reply, "(u)", &code);
        g_variant_unref(reply);

        constexpr guint32 kPrimaryOwner = 1;
        if (code != kPrimaryOwner) {
            return Result::AlreadyRunning;
        }
        owned_ = true;
        return Result::Acquired;
    }

    ~SingleInstance() {
        if (bus_ != nullptr) {
            if (owned_) {
                GVariant* reply = g_dbus_connection_call_sync(
                    bus_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                    "ReleaseName", g_variant_new("(s)", kBusName), G_VARIANT_TYPE("(u)"),
                    G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
                if (reply != nullptr) {
                    g_variant_unref(reply);
                }
            }
            g_object_unref(bus_);
        }
    }

    SingleInstance() = default;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

private:
    GDBusConnection* bus_ = nullptr;
    bool owned_ = false;
};

// What the signal sources need to reach.
struct SignalContext {
    Loop* loop = nullptr;
    Manager* manager = nullptr;
    SubsystemRegistry* subsystems = nullptr;
};

gboolean onShutdownSignal(void* data) {
    auto* context = static_cast<SignalContext*>(data);
    ALUS_INFO("shutdown signal received");
    context->loop->quit();
    return G_SOURCE_CONTINUE;
}

gboolean onReloadSignal(void* data) {
    auto* context = static_cast<SignalContext*>(data);
    ALUS_INFO("SIGHUP received, reloading the configuration");
    // Through the queue, so the reset happens inside a pump cycle on the main
    // thread and never in the middle of one.
    context->loop->queueFunction(
        [context] { resetConfiguration(*context->manager, *context->subsystems); }, true,
        "resetConfiguration");
    return G_SOURCE_CONTINUE;
}

void addSignalSource(int signalNumber, Loop& loop, GSourceFunc callback, void* data) {
    GSource* source = g_unix_signal_source_new(signalNumber);
    g_source_set_callback(source, callback, data, nullptr);
    g_source_attach(source, loop.context());
    g_source_unref(source);
}

}  // namespace

int main(int argc, char* argv[]) {
    // FIRST, before the options are even parsed. setlocale has to run before
    // any GTK call and before the first translated string, and --help itself
    // is text the user reads. The configuration is not loaded yet, so this
    // binds from the environment; i18n::Service re-binds with
    // [general] language once it is.
    alus::i18n::initializeFromEnvironment();

    Options options;

    const GOptionEntry entries[] = {
        {"version", 'v', 0, G_OPTION_ARG_NONE, &options.version, "Print the version and exit",
         nullptr},
        {"no-speech", 0, 0, G_OPTION_ARG_NONE, &options.noSpeech,
         "Start without speech (diagnostics)", nullptr},
        {"no-input", 0, 0, G_OPTION_ARG_NONE, &options.noInput,
         "Start without taking the keyboard (diagnostics)", nullptr},
        {"no-nav", 0, 0, G_OPTION_ARG_NONE, &options.noNav,
         "Run without the text and review layer (no review cursor, no say all)", nullptr},
        {"no-braille", 0, 0, G_OPTION_ARG_NONE, &options.noBraille,
         "Run without braille (no translation, no display)", nullptr},
        {"no-a11y", 0, 0, G_OPTION_ARG_NONE, &options.noA11y,
         "Start without the object model (diagnostics)", nullptr},
        {"no-app", 0, 0, G_OPTION_ARG_NONE, &options.noApp,
         "Run without app modules and plugins", nullptr},
        {"no-plugins", 0, 0, G_OPTION_ARG_NONE, &options.noPlugins,
         "Run app modules but load no plugins", nullptr},
        {"no-gui", 0, 0, G_OPTION_ARG_NONE, &options.noGui,
         "Do not open the settings window, even when asked", nullptr},
        {"no-watchdog", 0, 0, G_OPTION_ARG_NONE, &options.noWatchdog,
         "Do not start the freeze watchdog (debugging)", nullptr},
        {"allow-multiple", 0, 0, G_OPTION_ARG_NONE, &options.allowMultiple,
         "Start even if another instance is running", nullptr},
        {"exit-after", 0, 0, G_OPTION_ARG_INT, &options.exitAfter,
         "Exit after N seconds (testing)", "N"},
        {"log-level", 0, 0, G_OPTION_ARG_STRING, &options.logLevel,
         "debug, io, debugwarning, info, warning, error, critical, off", "LEVEL"},
        {"log-file", 0, 0, G_OPTION_ARG_STRING, &options.logFile, "Write the log to this file",
         "PATH"},
        {"language", 0, 0, G_OPTION_ARG_STRING, &options.language,
         "Speak this language, overriding [general] language", "CODE"},
        {"config-dir", 0, 0, G_OPTION_ARG_STRING, &options.configDir,
         "Configuration directory (default: ~/.config/alus)", "DIR"},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
    };

    GOptionContext* optionContext = g_option_context_new("- the ALUS screen reader");
    g_option_context_add_main_entries(optionContext, entries, nullptr);
    GError* error = nullptr;
    const gboolean parsed = g_option_context_parse(optionContext, &argc, &argv, &error);
    g_option_context_free(optionContext);
    if (parsed == FALSE) {
        std::fprintf(stderr, "alus-daemon: %s\n",
                     error != nullptr ? error->message : "could not parse the arguments");
        if (error != nullptr) {
            g_error_free(error);
        }
        return 2;
    }

    // Every gchar* GOption filled, freed in ONE place that every return path
    // passes through. The g_free calls used to be at the end of main only, so
    // `--version` and the already-running exit leaked whatever strings the
    // command line had set -- six bytes, and six bytes the leak checker
    // reports is six bytes somebody has to triage (phase-9.md N87).
    const auto freeOptions = [&options] {
        g_free(options.logLevel);
        g_free(options.language);
        g_free(options.logFile);
        g_free(options.configDir);
        options.logLevel = nullptr;
        options.language = nullptr;
        options.logFile = nullptr;
        options.configDir = nullptr;
    };

    if (options.version != FALSE) {
        std::printf("alus-daemon %s\n", ALUS_VERSION);
        freeOptions();
        return 0;
    }

    Log::setThreadName("main");
    std::optional<LogLevel> commandLineLevel;
    if (options.logLevel != nullptr) {
        commandLineLevel = parseLogLevel(options.logLevel);
        if (!commandLineLevel.has_value()) {
            std::fprintf(stderr, "alus-daemon: unknown log level: %s\n", options.logLevel);
            return 2;
        }
        Log::instance().setLevel(*commandLineLevel);
    }
    if (options.logFile != nullptr) {
        std::string openError;
        if (!Log::instance().openFile(options.logFile, &openError)) {
            std::fprintf(stderr, "alus-daemon: could not open the log file: %s\n",
                         openError.c_str());
            return 2;
        }
    }

    SingleInstance instance;
    if (options.allowMultiple == FALSE) {
        if (instance.acquire() == SingleInstance::Result::AlreadyRunning) {
            std::fprintf(stderr,
                         "alus-daemon: another instance is already running (bus name %s)\n",
                         kBusName);
            freeOptions();
            return 1;
        }
    }

    ALUS_INFO("ALUS {} starting", ALUS_VERSION);

    Manager manager{options.configDir != nullptr
                        ? alus::config::Paths::forDirectory(options.configDir)
                        : alus::config::Paths::fromEnvironment()};
    manager.setDefault("general", "logLevel", "info");
    alus::i18n::Service::registerDefaults(manager);
    alus::speech::Service::registerDefaults(manager);
    alus::input::Service::registerDefaults(manager);
    alus::a11y::Service::registerDefaults(manager);
    alus::nav::Service::registerDefaults(manager);
    alus::braille::Service::registerDefaults(manager);
    alus::app::Service::registerDefaults(manager);
    std::string configError;
    if (!manager.load(&configError)) {
        ALUS_WARNING("could not read the configuration: {}", configError);
    }

    Watchdog watchdog;
    Loop loop{&watchdog};

    SubsystemRegistry subsystems;
    // BEFORE the log and before everything below it: each of those may speak
    // during its own initialize(), and a message spoken before the catalogue
    // is bound comes out in English.
    alus::i18n::Service i18nService{
        manager, options.language != nullptr ? options.language : std::string{}};
    subsystems.add(&i18nService);
    LogSubsystem logSubsystem{manager, commandLineLevel};
    subsystems.add(&logSubsystem);

    // Registration order is initialisation order, and teardown is the reverse.
    // Speech comes after the log so that a failure to reach speech-dispatcher
    // is logged at the level the user configured.
    alus::speech::ServiceOptions speechOptions;
    speechOptions.noSpeech = options.noSpeech != FALSE;
    if (options.noSpeech != FALSE) {
        ALUS_INFO("speech disabled on the command line");
    }
    alus::speech::Service speech{loop, manager, speechOptions};
    subsystems.add(&speech);

    // Input comes after speech, because the first thing it may have to do is
    // say that it does not work.
    //
    // The object model does not exist yet at this point, and two of input's
    // hooks need it. They are given a pointer that is filled in below, which
    // is also what keeps the answers honest with --no-a11y: there is no object
    // model, so no field is editable and nothing is protected.
    alus::a11y::Service* objectModel = nullptr;
    alus::input::GestureManager::Hooks inputHooks;
    inputHooks.cancelSpeech = [&speech] {
        if (auto* presenter = speech.presenter()) {
            presenter->cancelSpeech();
        }
    };
    inputHooks.pauseSpeech = [&speech](bool pause) {
        if (auto* presenter = speech.presenter()) {
            presenter->pauseSpeech(pause);
        }
    };
    inputHooks.speakText = [&speech](const std::string& text, bool symbolLevelAll) {
        auto* presenter = speech.presenter();
        if (presenter == nullptr) {
            return;
        }
        if (symbolLevelAll) {
            // Input help must say the punctuation keys out loud, or a
            // punctuation key explains itself as silence.
            presenter->speakText(text, alus::text::SymbolLevel::All);
        } else {
            alus::ui::message(text);
        }
    };
    inputHooks.speakSpelling = [&speech](const std::string& text) {
        // Phase 5's real spelling path: the punctuation named, the pitch
        // raised for a capital, and character mode so that "b" is a letter
        // and not a word (phase-4.md O11, now closed).
        if (auto* presenter = speech.presenter()) {
            presenter->speakSpelling(text);
        }
    };
    inputHooks.isSpeechPaused = [&speech] {
        auto* presenter = speech.presenter();
        return presenter != nullptr && presenter->isPaused();
    };
    inputHooks.isRecovering = [&watchdog] { return watchdog.isAttemptingRecovery(); };
    // Phase 3's handover, item 3: the two answers the object model owes input.
    // The character echo defaults to "only in edit controls" (O12), so until
    // this hook existed it stayed quiet everywhere.
    inputHooks.isFocusEditable = [&objectModel] {
        auto* api = objectModel != nullptr ? objectModel->api() : nullptr;
        return api != nullptr && api->isFocusEditable();
    };
    inputHooks.isTypingProtected = [&objectModel] {
        auto* api = objectModel != nullptr ? objectModel->api() : nullptr;
        return api != nullptr && api->isTypingProtected();
    };
    // Phase 5's answer to the last hook phase 3 left empty (phase-4.md handover
    // point 5). The text layer does not exist yet either, so the same
    // late-bound pointer trick is used.
    alus::nav::Service* textLayer = nullptr;
    inputHooks.isSayAllRunning = [&textLayer] {
        if (textLayer == nullptr) {
            return false;
        }
        alus::nav::SayAll* sayAll = textLayer->sayAll();
        return sayAll != nullptr && sayAll->isRunning();
    };

    alus::input::InputServiceOptions inputOptions;
    inputOptions.noInput = options.noInput != FALSE;
    if (options.noInput != FALSE) {
        ALUS_INFO("input disabled on the command line");
    }
    // `alus+q`. The only thing that owns the loop is this function, so the
    // command reaches it as a hook (input/manager.hpp).
    //
    // IT WAITS FOR THE WORD TO BE SPOKEN. The synthesizer is another process:
    // quitting the moment the key is pressed closes the connection in the
    // middle of the utterance, so a blind user never learns whether the key
    // worked -- and the next thing they hear is nothing at all, which is also
    // what a crash sounds like. So the loop is asked again every 100 ms until
    // the speech queue is empty, with a cap, because a synthesizer that never
    // reports an index must not be able to keep ALUS alive (the same reason
    // SpeechManager has a lost-index grace at all).
    inputHooks.quit = [&loop, &speech] {
        static constexpr int kMaxWaits = 30;  // 3 s
        auto* waits = new int{0};
        const auto step = [&loop, &speech, waits](auto&& self) -> void {
            alus::speech::SpeechManager* speechManager = speech.manager();
            const bool stillSpeaking = speechManager != nullptr &&
                                       (speechManager->isSpeaking() ||
                                        speechManager->hasPendingSpeech());
            if (!stillSpeaking || ++*waits >= kMaxWaits) {
                if (*waits >= kMaxWaits) {
                    ALUS_INFO("quit: the farewell did not finish in {} ms; stopping anyway",
                              kMaxWaits * 100);
                }
                delete waits;
                loop.quit();
                return;
            }
            loop.callLater(std::chrono::milliseconds{100},
                           [self] { self(self); }, "quit-wait");
        };
        loop.callLater(std::chrono::milliseconds{100}, [step] { step(step); }, "quit-wait");
    };
    alus::input::Service input{loop, manager, inputHooks, inputOptions};
    subsystems.add(&input);

    // The object model comes last, because it uses all three: it speaks
    // through speech, it supplies input's dispatch context and grab set, and
    // it runs in the core loop's first pump stage.
    alus::a11y::A11yServiceOptions a11yOptions;
    a11yOptions.noA11y = options.noA11y != FALSE;
    if (options.noA11y != FALSE) {
        ALUS_INFO("the object model is disabled on the command line");
    }
    alus::a11y::Service a11y{loop, manager, options.noInput != FALSE ? nullptr : &input,
                             [&speech] { return speech.presenter(); }, a11yOptions};
    objectModel = &a11y;
    subsystems.add(&a11y);

    // The text and review layer comes last of all: it is the layer above the
    // object model and it needs all three below it (nav/service.hpp).
    alus::nav::NavServiceOptions navOptions;
    navOptions.noNav = options.noNav != FALSE;
    if (options.noNav != FALSE) {
        ALUS_INFO("the text and review layer is disabled on the command line");
    }
    alus::nav::Service nav{loop, manager, [&a11y] { return a11y.registry(); },
                           options.noInput != FALSE ? nullptr : &input,
                           [&speech] { return speech.presenter(); }, navOptions};
    textLayer = &nav;
    subsystems.add(&nav);

    // Braille comes after the text layer, because it reads from all four
    // below it (braille/service.hpp) and because it must be able to show what
    // speech has just said -- which means being initialised after the thing
    // that says it.
    alus::braille::ServiceOptions brailleOptions;
    brailleOptions.noBraille = options.noBraille != FALSE;
    if (options.noBraille != FALSE) {
        ALUS_INFO("braille is disabled on the command line");
    }
    alus::braille::Service braille{loop,
                                   manager,
                                   [&a11y] { return a11y.registry(); },
                                   [&nav] { return &nav; },
                                   options.noInput != FALSE ? nullptr : &input,
                                   [&speech] { return speech.presenter(); },
                                   brailleOptions};
    subsystems.add(&braille);

    // The app layer comes last of all, and that is not a detail: it adjusts
    // what every layer below it does, so each of them must already exist for
    // it to reach (app/service.hpp lists the seams). It is also the layer that
    // loads third-party code, which must not be able to stop the screen reader
    // from coming up -- so it is the last thing that happens before the loop
    // runs, and a plugin that refuses to load costs a log line and nothing
    // else.
    alus::app::ServiceOptions appOptions;
    appOptions.noApp = options.noApp != FALSE;
    appOptions.noPlugins = options.noPlugins != FALSE;
    if (options.noApp != FALSE) {
        ALUS_INFO("app modules and plugins are disabled on the command line");
    } else if (options.noPlugins != FALSE) {
        ALUS_INFO("plugins are disabled on the command line");
    }
    alus::app::Service app{manager,
                           [&a11y] { return a11y.registry(); },
                           [&nav] { return &nav; },
                           options.noInput != FALSE ? nullptr : &input,
                           [](const std::string& text) { alus::ui::message(text); },
                           appOptions};
    // Phase 7's handover, item 1: a plugin's braille table directory wins over
    // the system one. A function and not a pointer, so that the app layer does
    // not link liblouis and braille can be restarted under it.
    app.setBrailleTableSourceHook([&braille](const std::filesystem::path& directory) {
        alus::braille::Handler* handler = braille.handler();
        if (handler == nullptr) {
            return false;
        }
        braille.louis().registry().addSource("plugin", directory);
        return true;
    });
    subsystems.add(&app);

    // The GUI is last, and it is the only subsystem nothing below it knows
    // about. It reads the configuration, the gesture maps and the command
    // catalogue; it is read by nothing. It is also the only one whose absence
    // is normal -- no display, or a build with no gtk4 -- so it must come up
    // after everything that works without it.
    alus::gui::ServiceOptions guiOptions;
    guiOptions.noGui = options.noGui != FALSE;
    alus::gui::Service::Hooks guiHooks;
    guiHooks.message = [](const std::string& text) { alus::ui::message(text); };
    // A setting changed in the window re-reads ONE section rather than
    // restarting everything: changing the speech rate must not take the
    // braille display down (§3.9, and rule 3 of phase-plan.md).
    guiHooks.reloadSection = [&manager, &speech, &a11y, &nav, &braille, &input,
                              &i18nService](const std::string& section) {
        std::string saveError;
        if (!manager.save(&saveError)) {
            ALUS_WARNING("could not save the configuration: {}", saveError);
        }
        if (section == "speech" || section == "general") {
            speech.applyConfiguration();
        }
        if (section == "keyboard" || section == "general") {
            input.applyConfiguration();
        }
        if (section.rfind("presentation", 0) == 0 || section == "annotations" ||
            section == "objectNavigation" || section == "documentFormatting") {
            a11y.applyConfiguration();
        }
        if (section == "reviewCursor" || section == "documentNavigation" ||
            section == "editableText" || section == "documentFormatting" ||
            section == "virtualBuffers") {
            nav.applyConfiguration();
        }
        if (section == "braille" || section == "documentFormatting" ||
            section == "general") {
            braille.applyConfiguration();
        }
        // The language is the one setting that reaches every layer at once,
        // because it changes the CATALOGUE and not a value: every label every
        // subsystem is holding was translated when it was read.
        if (section == "general") {
            i18nService.initialize();
        }
    };
    guiHooks.resetAll = [&manager, &subsystems] {
        // The real thing and not a narrower cousin: a factory reset has to
        // bring every subsystem back up, which is what resetConfiguration is.
        alus::config::resetConfiguration(manager, subsystems, /*factoryDefaults=*/true);
    };
    // ---- the commands that need speech AND the script layer ----
    //
    // After the GUI's own, and like them they reach the dispatcher through the
    // context the lookup assembles per key press. The getters are called on
    // every press rather than captured: the speech service rebuilds its
    // presenter and its settings ring whenever the synthesizer changes, and a
    // captured pointer would be the OLD one (rule 3 of phase-plan.md).
    alus::speech::GlobalCommandsContext speechCommandContext;
    speechCommandContext.presenter = [&speech] { return speech.presenter(); };
    speechCommandContext.ring = [&speech] { return speech.settingsRing(); };
    speechCommandContext.config = &manager;
    speechCommandContext.dispatcher = [&input] { return input.dispatcher(); };
    alus::speech::GlobalCommands speechCommands{std::move(speechCommandContext),
                                                &input.catalog()};

    alus::config::CommandsContext configCommandContext;
    configCommandContext.manager = &manager;
    configCommandContext.dispatcher = [&input] { return input.dispatcher(); };
    configCommandContext.reset = [&manager, &subsystems](bool factoryDefaults) {
        alus::config::resetConfiguration(manager, subsystems, factoryDefaults);
    };
    alus::config::Commands configCommands{std::move(configCommandContext), &input.catalog()};

    alus::commands::SystemCommands::Context systemCommandContext;
    systemCommandContext.dispatcher = [&input] { return input.dispatcher(); };
    alus::commands::SystemCommands systemCommands{std::move(systemCommandContext),
                                                  &input.catalog()};

    if (options.noInput == FALSE) {
        input.addContextContributor([&speechCommands, &configCommands, &systemCommands](
                                        alus::script::DispatchContext& context) {
            context.globalCommands.push_back(&speechCommands);
            context.globalCommands.push_back(&configCommands);
            context.globalCommands.push_back(&systemCommands);
        });
    }

    alus::gui::Service gui{manager,
                           options.noInput != FALSE ? nullptr : input.manager(),
                           &alus::input::KeyNames::shared(),
                           &input.catalog(),
                           std::move(guiHooks),
                           guiOptions};
    subsystems.add(&gui);
    // The settings scripts reach the dispatcher the same way every other
    // layer's do: through the context the lookup assembles per key press.
    if (options.noInput == FALSE) {
        input.addContextContributor([&gui](alus::script::DispatchContext& context) {
            context.globalCommands.push_back(gui.scriptable());
        });
    }

    subsystems.initializeAll();
    input.attachWatchdog(&watchdog);

    SignalContext signalContext{&loop, &manager, &subsystems};
    addSignalSource(SIGTERM, loop, &onShutdownSignal, &signalContext);
    addSignalSource(SIGINT, loop, &onShutdownSignal, &signalContext);
    addSignalSource(SIGHUP, loop, &onReloadSignal, &signalContext);

    if (options.noWatchdog == FALSE) {
        watchdog.start();
    }

    if (options.exitAfter > 0) {
        ALUS_INFO("will exit after {} s", options.exitAfter);
        loop.callLater(std::chrono::seconds{options.exitAfter}, [&loop] { loop.quit(); },
                       "exit-after");
    }

    if (options.noSpeech == FALSE) {
        // NVDA announces itself on startup, and for the same reason: the user
        // needs to know the screen reader is running before anything else
        // happens.
        // Translators: spoken as the screen reader finishes starting.
        // "ALUS" is the product name and is NOT translated.
        alus::ui::message(_("ALUS started"));
        if (options.noA11y == FALSE && a11y.health() != alus::a11y::A11yHealth::Working) {
            // Said, not only logged, for the same reason as the keyboard
            // below: a screen reader that cannot read the desktop looks
            // exactly like one that is working, until the user presses a key.
            // Translators: spoken at start-up when the accessibility bus
            // cannot be reached; %s names the reason.
            alus::ui::message(alus::i18n::substitute(
                _("The object model is not available: %s"),
                alus::a11y::a11yHealthName(a11y.health())));
            ALUS_ERROR("{}", a11y.describe());
        }
        if (options.noInput == FALSE &&
            input.health() != alus::input::BackendHealth::Working) {
            // Said, not only logged. A user who cannot see the terminal would
            // otherwise have no way to tell this apart from a key that does
            // nothing.
            // Translators: spoken at start-up when the compositor refused
            // the keyboard; %s names the reason. This one matters more than
            // it looks: without it a screen reader that gets no keys looks
            // exactly like one that is working (findings N1, N7, N15, N18).
            alus::ui::message(alus::i18n::substitute(
                _("Keyboard input is not available: %s"),
                alus::input::backendHealthName(input.health())));
            ALUS_ERROR("{}", input.describe());
        }
    }

    // The settings window writes ~/.config/alus/alus.ini from ANOTHER process
    // (src/gui/main.cpp says why it has to), so the daemon learns about a
    // changed setting from the file rather than from a function call.
    //
    // A file monitor and not a D-Bus notification, for two reasons: it works
    // when the settings window was open across a restart of the daemon, and it
    // also catches the user editing the file by hand, which is how ALUS was
    // configured before this phase and must keep working.
    GFileMonitor* configMonitor = nullptr;
    GFile* configFile = g_file_new_for_path(manager.paths().mainFile.c_str());
    GError* monitorError = nullptr;
    configMonitor = g_file_monitor_file(configFile, G_FILE_MONITOR_WATCH_MOVES, nullptr,
                                        &monitorError);
    g_object_unref(configFile);
    if (configMonitor == nullptr) {
        ALUS_INFO("not watching the configuration file ({}); a setting changed in the "
                  "settings window will need a restart",
                  monitorError != nullptr ? monitorError->message : "unknown error");
        if (monitorError != nullptr) {
            g_error_free(monitorError);
        }
    } else {
        // Coalesced by GIO itself to one signal per rate limit; 300 ms so that
        // a window writing several settings in a row costs one reload rather
        // than one per switch.
        g_file_monitor_set_rate_limit(configMonitor, 300);
        struct ReloadContext {
            alus::config::Manager* manager;
            alus::config::SubsystemRegistry* subsystems;
        };
        static ReloadContext reloadContext{&manager, &subsystems};
        g_signal_connect(
            configMonitor, "changed",
            G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event,
                           gpointer data) {
                if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
                    event != G_FILE_MONITOR_EVENT_CREATED &&
                    event != G_FILE_MONITOR_EVENT_MOVED_IN &&
                    event != G_FILE_MONITOR_EVENT_RENAMED) {
                    return;
                }
                auto* reload = static_cast<ReloadContext*>(data);
                // ALUS's own save (alus+v, alus+control+c): the running
                // settings already ARE the file. Re-reading meant restarting
                // speech, input, browse mode -- and losing the focus and the
                // place in the page (N143).
                if (reload->manager->isOwnWrite()) {
                    ALUS_DEBUG("the configuration file changed by ALUS's own save; not re-read");
                    return;
                }
                ALUS_INFO("the configuration file changed; re-reading it");
                // The whole thing, in the NVDA way: terminate in reverse
                // order, reload, bring everything back. Narrower reloads are
                // what the in-process window used; from a file there is no way
                // to know WHICH section changed, and guessing would leave a
                // subsystem holding a stale setting.
                alus::config::resetConfiguration(*reload->manager, *reload->subsystems);
            }),
            &reloadContext);
    }

    // One pump so the empty queue is drained and the cycle is proven to run.
    loop.requestPump(true);
    loop.run();

    if (configMonitor != nullptr) {
        g_file_monitor_cancel(configMonitor);
        g_object_unref(configMonitor);
    }

    ALUS_INFO("shutting down after {} pump cycles", loop.tickCount());
    // The subsystems go down FIRST, with the watchdog still watching: taking
    // the keyboard grabs back and closing the synthesizer are exactly the
    // calls that can block on something outside this process, and a freeze
    // during shutdown is a freeze (phase 3 holds grabs, so it is also the
    // moment the user's keyboard is at stake — finding N19).
    subsystems.terminateAll();
    watchdog.stop();
    alus::audio::terminate();

    freeOptions();
    Log::instance().closeFile();
    return 0;
}
