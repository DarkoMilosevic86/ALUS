// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// alus-cli — the speech stack from the command line.
//
// This is the tool the phase 2 exit criteria call for:
//
//   alus-cli speak "Проба говора на српском" --rate 20
//   alus-cli bench-speech --iterations 100
//
// It is not a toy: it runs the real pipeline (presenter → manager → driver) in
// a real core loop, which means it also serves as the manual test for anything
// that cannot be unit tested, and as the way to measure a user's own machine.
//
// WHAT bench-speech CAN AND CANNOT MEASURE. Phase 0 established (finding N14)
// that the only practical way to time real audio is to capture the output sink
// with pw-record, which is what spikes/s2-speech.c does. That belongs in a
// measurement harness, not in the screen reader. So bench-speech measures the
// SSIP path — and says which number is which — while the audio-onset budget
// stays owned by the spike: 11.5 ms p95 to first sample, 29.0 ms p95 to
// silence after cancel, on the target system.
#include "alus/a11y/atspi_backend.hpp"
#include "alus/core/queue.hpp"
#include "alus/script/script.hpp"
#include "alus/input/gesture.hpp"
#include "alus/speech/synth.hpp"
#include "alus/speech/dictionaries.hpp"
#include "alus/nav/service.hpp"
#include "alus/a11y/service.hpp"
#include "alus/a11y/object_speech.hpp"
#include "alus/a11y/object.hpp"
#include "alus/a11y/registry.hpp"
#include "alus/a11y/text_range.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/gui/model.hpp"
#include "alus/i18n/gettext.hpp"
#include "alus/i18n/service.hpp"

#include "alus/gui/service.hpp"
#include "alus/nav/browse_mode.hpp"
#include "alus/nav/document_range.hpp"
#include "alus/nav/quick_nav.hpp"
#include "alus/nav/screen_review.hpp"
#include "alus/text/segment.hpp"
#include "alus/audio/tones.hpp"
#include "alus/audio/wave_player.hpp"
#include "alus/braille/louis.hpp"
#include "alus/app/modules.hpp"
#include "alus/app/plugin.hpp"
#include "alus/app/service.hpp"
#include "alus/braille/display.hpp"
#include "alus/braille/settings.hpp"
#include "alus/config/config.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/input/service.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/speech/service.hpp"
#include "alus/speech/ssip_synth.hpp"
#include "alus/text/symbols.hpp"
#include "alus/ui/message.hpp"

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using alus::config::Manager;
using alus::core::Log;
using alus::core::LogLevel;
using alus::core::Loop;
using alus::speech::Presenter;
using alus::speech::Priority;
using alus::speech::Service;
using alus::speech::SpeechSequence;
using Clock = std::chrono::steady_clock;

struct Options {
    gboolean version = FALSE;
    gboolean help = FALSE;
    gboolean noAudio = FALSE;
    gint rate = -1;
    gint pitch = -1;
    gint volume = -1;
    gint iterations = 20;
    gchar* voice = nullptr;
    gchar* module = nullptr;
    gchar* language = nullptr;
    gchar* symbolLevel = nullptr;
    gchar* logLevel = nullptr;
    gchar* configDir = nullptr;
    gchar* dataDir = nullptr;
};

void usage() {
    std::puts(
        "Usage: alus-cli <command> [options]\n"
        "\n"
        "Commands:\n"
        "  speak <text>        speak the text and exit when it has been spoken\n"
        "  bench-speech        measure the SSIP path (see --iterations)\n"
        "  list-voices         the voices the driver offers\n"
        "  list-modules        the speech-dispatcher output modules\n"
        "  process <text>      run the text through dictionaries and symbols only,\n"
        "                      print the result, say nothing\n"
        "  beep [hz] [ms]      play a tone through the ALUS audio path\n"
        "  input-info          whether the keyboard is available, and why not\n"
        "  gesture <id>        which command a gesture runs, e.g. kb:alus+1\n"
        "  list-commands [cat] the command table (data/scripts.csv)\n"
        "  a11y-info           whether the object model works, and what it sees\n"
        "  a11y-tree [depth] [app]  the accessible tree, as ALUS reads it (default\n"
        "                      depth 3; app limits it to one application)\n"
        "  text-info           what the text layer sees: units, formatting, screen review\n"
        "  browse-info         whether browse mode works here, and what quick nav costs\n"
        "  browse-walk [title] [lines]\n"
        "                      run h, the arrows, f, t and the character keys over\n"
        "                      a live page and print what would be said (no keys\n"
        "                      taken); lines: how far down to walk (default 8)\n"
        "  braille-info        whether braille works here: liblouis, the tables, brltty\n"
        "  app-info            which app module every running application would get,\n"
        "                      and which plugins are installed\n"
        "  gui-info            whether the settings window can open here, and which\n"
        "                      translations are installed\n"
        "  settings [page]     ask the RUNNING ALUS to show its settings\n"
        "  autostart [--enable|--disable]\n"
        "                      does ALUS start with the session, and does anything\n"
        "                      else take the keyboard first\n"
        "\n"
        "Options:\n"
        "  --rate/--pitch/--volume N   0-100\n"
        "  --voice NAME, --module NAME\n"
        "  --language CODE             e.g. sr, en\n"
        "  --symbol-level LEVEL        none, some, most, all, char\n"
        "  --iterations N              bench-speech repetitions\n"
        "  --no-audio                  do not open the audio device\n"
        "  --log-level LEVEL, --config-dir DIR, --data-dir DIR\n");
}

// Runs the loop until `done` is set or the deadline passes. Returns false on
// timeout, which is itself a result worth reporting: a synthesizer that never
// finishes is the failure mode risk R14 is about.
bool runUntil(Loop& loop, const bool& done, std::chrono::milliseconds timeout) {
    if (done) {
        return true;
    }
    bool timedOut = false;
    const Loop::TimerId timer = loop.callLater(timeout,
                                               [&loop, &timedOut] {
                                                   timedOut = true;
                                                   loop.quit();
                                               },
                                               "cli-timeout");
    loop.run();
    if (!timedOut) {
        loop.cancelLater(timer);
    }
    return !timedOut;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto position = static_cast<std::size_t>(
        fraction * static_cast<double>(values.size() - 1) + 0.5);
    return values[std::min(position, values.size() - 1)];
}

void report(const char* label, const std::vector<double>& values, const char* note) {
    if (values.empty()) {
        std::printf("%-34s %s\n", label, "no samples");
        return;
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    std::printf("%-34s p50 %7.2f  p95 %7.2f  p99 %7.2f  min %7.2f  max %7.2f  n=%zu  %s\n", label,
                percentile(sorted, 0.50), percentile(sorted, 0.95), percentile(sorted, 0.99),
                sorted.front(), sorted.back(), sorted.size(), note);
}

int commandSpeak(Loop& loop, Service& service, const std::string& text) {
    Presenter* presenter = service.presenter();
    if (presenter == nullptr) {
        std::fputs("alus-cli: speech is not available\n", stderr);
        return 1;
    }
    bool done = false;
    SpeechSequence sequence;
    sequence.emplace_back(text);
    // A callback at the end of the sequence: the manager turns it into an index
    // mark and runs it on the main thread when the speech reaches it. Waiting
    // for that is the only correct way to know the text has been spoken —
    // `end` would be ~294 ms late (finding N12).
    sequence.emplace_back(alus::speech::callback(
        [&done, &loop] {
            done = true;
            loop.quit();
        },
        "cli-done"));
    presenter->speak(std::move(sequence));
    // Generous: a long sentence at a slow rate takes a while.
    if (!runUntil(loop, done, std::chrono::seconds{60})) {
        std::fputs("alus-cli: timed out waiting for the speech to finish\n", stderr);
        return 1;
    }
    return 0;
}

int commandBench(Loop& loop, Service& service, int iterations) {
    alus::speech::SpeechManager* manager = service.manager();
    alus::speech::ISynth* synth = service.synth();
    if (manager == nullptr || synth == nullptr) {
        std::fputs("alus-cli: speech is not available\n", stderr);
        return 1;
    }
    std::printf("driver: %s", std::string{synth->name()}.c_str());
    if (auto* ssip = dynamic_cast<alus::speech::SsipSynth*>(synth)) {
        std::printf(", module: %s, timing guaranteed: %s, index mark offset: %lld ms",
                    ssip->outputModule().c_str(), ssip->timingIsGuaranteed() ? "yes" : "no",
                    static_cast<long long>(ssip->indexMarkOffset().count()));
    }
    std::printf("\n\n");

    std::vector<double> toFirstMark;
    std::vector<double> toLastMark;
    std::vector<double> toDone;
    unsigned lost = 0;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        bool done = false;
        bool haveFirst = false;
        Clock::time_point start;
        std::optional<double> firstMark;
        std::optional<double> lastMark;

        auto indexHandle = synth->indexReached.add(
            [&](alus::speech::ISynth*, int) {
                const double elapsed =
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                if (!haveFirst) {
                    haveFirst = true;
                    firstMark = elapsed;
                }
                lastMark = elapsed;
            },
            "bench-index");
        auto doneHandle = synth->doneSpeaking.add(
            [&](alus::speech::ISynth*) {
                toDone.push_back(
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            },
            "bench-done");

        SpeechSequence sequence;
        // A mark at the START of the utterance is the safe shape (the N16
        // matrix), and it is what tells us when synthesis actually began.
        sequence.emplace_back(alus::speech::callback([] {}, "bench-start"));
        sequence.emplace_back(std::string{"Проба говора на српском језику."});
        sequence.emplace_back(alus::speech::callback(
            [&done, &loop] {
                done = true;
                loop.quit();
            },
            "bench-end"));

        start = Clock::now();
        manager->speak(std::move(sequence), Priority::Normal);
        if (!runUntil(loop, done, std::chrono::seconds{10})) {
            ++lost;
        }
        if (firstMark.has_value()) {
            toFirstMark.push_back(*firstMark);
        }
        if (lastMark.has_value()) {
            toLastMark.push_back(*lastMark);
        }
        manager->cancel();
    }

    std::printf("All times in milliseconds, from the speak() call.\n\n");
    report("speak -> first index mark", toFirstMark, "synthesis started");
    report("speak -> last index mark", toLastMark, "utterance spoken");
    report("speak -> end notification", toDone, "NOT an audio event (N12)");
    std::printf("\nutterances that did not complete: %u of %d\n", lost, iterations);
    std::printf(
        "\nThe audio-onset and cancel-to-silence budgets need an audio tap and are\n"
        "owned by spikes/s2-speech.c: 11.5 ms p95 to the first sample and 29.0 ms p95\n"
        "to silence on the target system. Finding N10: the `begin` notification\n"
        "arrives BEFORE the audio, so it cannot stand in for the onset.\n");
    return lost == 0 ? 0 : 1;
}

int commandListVoices(Service& service) {
    alus::speech::ISynth* synth = service.synth();
    if (synth == nullptr) {
        return 1;
    }
    const std::vector<alus::speech::VoiceInfo> voices = synth->availableVoices();
    std::printf("%zu voices\n", voices.size());
    for (const alus::speech::VoiceInfo& voice : voices) {
        std::printf("  %-32s %-12s %s\n", voice.name.c_str(), voice.language.c_str(),
                    voice.variant.c_str());
    }
    return 0;
}

int commandListModules(Service& service) {
    auto* ssip = dynamic_cast<alus::speech::SsipSynth*>(service.synth());
    if (ssip == nullptr) {
        std::fputs("alus-cli: not using the speech-dispatcher driver\n", stderr);
        return 1;
    }
    std::printf("current module: %s\n", ssip->outputModule().c_str());
    for (const std::string& module : ssip->availableModules()) {
        std::printf("  %s\n", module.c_str());
    }
    return 0;
}

int commandProcess(Service& service, const std::string& text, const std::string& language,
                   alus::text::SymbolLevel level) {
    Presenter* presenter = service.presenter();
    if (presenter == nullptr) {
        return 1;
    }
    const std::string out = presenter->processText(language, text, level, false);
    std::printf("%s\n", out.c_str());
    return 0;
}

// --------------------------------------------------------------------------
// Phase 3: input diagnostics.
//
// These exist because the failure they report is INVISIBLE. If the compositor
// refuses ALUS the keyboard, every key simply does nothing (risks.md R1,
// findings N7 and N8), and a user who cannot see the terminal has no other way
// to tell that apart from a broken binding. So there is a command that says it.
// --------------------------------------------------------------------------

int commandInputInfo(alus::input::Service& input) {
    std::printf("%s\n", input.describe().c_str());
    alus::input::IInputBackend* backend = input.backend();
    if (backend == nullptr) {
        std::puts("input: no backend");
        return 1;
    }
    const alus::input::BackendHealth health = backend->health();
    if (health == alus::input::BackendHealth::Working) {
        std::puts("keyboard: AVAILABLE");
    } else {
        std::printf("keyboard: NOT AVAILABLE (%s)\n",
                    std::string(alus::input::backendHealthName(health)).c_str());
    }
    if (auto* manager = input.manager()) {
        const auto& stats = manager->stats();
        std::printf("keys seen %llu, consumed %llu, scripts queued %llu\n",
                    static_cast<unsigned long long>(stats.keysSeen),
                    static_cast<unsigned long long>(stats.keysConsumed),
                    static_cast<unsigned long long>(stats.scriptsQueued));
    }
    if (auto* a11y = dynamic_cast<alus::input::A11yManagerBackend*>(backend)) {
        std::printf("bus name: %s\n",
                    a11y->busName().empty() ? "(none)" : a11y->busName().c_str());
        if (!a11y->nameHolder().empty()) {
            std::printf("held by: %s\n", a11y->nameHolder().c_str());
        }
        std::printf("key events: %llu received, %llu duplicates dropped (N33)\n",
                    static_cast<unsigned long long>(a11y->eventsReceived()),
                    static_cast<unsigned long long>(a11y->duplicatesDropped()));
        if (!a11y->lastError().empty()) {
            std::printf("last error: %s\n", a11y->lastError().c_str());
        }
    }
    return health == alus::input::BackendHealth::Working ? 0 : 1;
}

// --------------------------------------------------------------------------
// The object model, from the shell. Same reason as input-info: a screen reader
// that cannot read the desktop looks exactly like one that works, until the
// user presses a key. These commands say which it is, and they only READ --
// no window is activated, no action performed, no key taken, so they are safe
// to run while the screen reader is running.
// --------------------------------------------------------------------------

int commandA11yInfo(Loop& loop) {
    alus::a11y::AtspiBackend::Options backendOptions;
    // The default context, for the reason in AtspiBackend::initialize.
    backendOptions.mainContext = loop.context();
    alus::a11y::AtspiBackend backend{backendOptions};
    std::string error;
    if (!backend.initialize(&error)) {
        std::printf("object model: NOT AVAILABLE\n%s\n", error.c_str());
        return 1;
    }
    std::printf("AT-SPI:       %s\n", alus::a11y::AtspiBackend::platformVersion().c_str());

    alus::a11y::Registry registry{loop, backend};
    if (!registry.start(&error)) {
        std::printf("object model: NO EVENTS\n%s\n", error.c_str());
        return 1;
    }
    const alus::a11y::ObjectPtr desktop = registry.desktop();
    if (desktop == nullptr) {
        std::puts("object model: NO DESKTOP (the bus is there, the registry is not)");
        return 1;
    }
    std::printf("desktop:      %s, %d application(s)\n", desktop->name().c_str(),
                desktop->childCount());
    for (const alus::a11y::ObjectPtr& application : desktop->children()) {
        if (application == nullptr) {
            continue;
        }
        const alus::a11y::AppInfo info = application->appInfo();
        std::printf("  %-28s toolkit %-10s pid %d\n", application->name().c_str(),
                    info.toolkitName.empty() ? "(none)" : info.toolkitName.c_str(),
                    info.processId);
    }
    if (const alus::a11y::ObjectPtr window = registry.activeWindow()) {
        std::printf("active window: %s (%s)\n", window->name().c_str(),
                    alus::a11y::roleName(window->role()));
    } else {
        std::puts("active window: (none found)");
    }
    if (const alus::a11y::ObjectPtr& focus = registry.api().focus()) {
        std::printf("focus:        %s (%s), %zu ancestor(s)\n", focus->name().c_str(),
                    alus::a11y::roleName(focus->role()),
                    registry.api().focusAncestors().size());
    } else {
        std::puts("focus:        (none found)");
    }
    std::printf("AT-SPI calls: %llu, errors %llu\n",
                static_cast<unsigned long long>(backend.callCount()),
                static_cast<unsigned long long>(backend.errorCount()));
    std::puts("object model: WORKING");
    registry.stop();
    return 0;
}

void printTree(alus::a11y::Object& object, int depth, int indent, std::size_t& budget) {
    if (budget == 0) {
        return;
    }
    --budget;
    std::printf("%*s%s \"%s\"", indent, "", alus::a11y::roleName(object.role()),
                object.name().c_str());
    if (!object.value().empty()) {
        std::printf(" = %s", object.value().c_str());
    }
    const alus::a11y::StateSet& states = object.states();
    if (states.has(alus::a11y::State::Focused)) {
        std::printf(" [focused]");
    }
    if (states.has(alus::a11y::State::Unavailable)) {
        std::printf(" [unavailable]");
    }
    if (object.presentationType() == alus::a11y::PresentationType::Layout) {
        std::printf(" [layout]");
    }
    std::printf("\n");
    if (depth <= 0) {
        return;
    }
    for (const alus::a11y::ObjectPtr& child : object.children()) {
        if (child != nullptr) {
            printTree(*child, depth - 1, indent + 2, budget);
        }
    }
}

// `alus-cli app-info`: which app module each running application would get.
//
// The question phase 8 cannot answer from a test, because the answer depends on
// what is running: the lookup key of §12.1 is the executable, the accessible
// name and the toolkit, and the only way to know what a real desktop reports is
// to ask it. Printing the key next to the module that would be chosen is also
// how a wrong rule is diagnosed -- a module that does not load is almost always
// an executable name that is not what the user thought it was (gnome-terminal
// is `gnome-terminal-server`; LibreOffice is `soffice.bin`).
//
// Reads only. It creates no module and takes no key, so it is safe to run while
// ALUS or Orca is running.
int commandAppInfo(Loop& loop, alus::config::Manager& config) {
    alus::app::Services services;
    services.config = &config;
    alus::app::Registry modules{services};
    const std::size_t rules = alus::app::registerBuiltinModules(modules);
    std::printf("app module rules: %zu\n", rules);

    // The plugins first, because they are the part that can be wrong in a way
    // the user installed on purpose.
    alus::app::PluginHost plugins{services};
    for (const std::filesystem::path& directory :
         alus::app::Service::standardPluginDirectories()) {
        plugins.addDirectory(directory);
        std::error_code ec;
        std::printf("plugin dir:       %s%s\n", directory.c_str(),
                    std::filesystem::is_directory(directory, ec) ? "" : " (does not exist)");
    }
    // NOT loaded: loading a plugin runs third-party code, and a diagnostic
    // command must not do that. The names come from the directories.
    std::size_t installed = 0;
    for (const std::filesystem::path& directory :
         alus::app::Service::standardPluginDirectories()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec)) {
            continue;
        }
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(directory, ec)) {
            const bool bare = entry.is_regular_file(ec) && entry.path().extension() == ".so";
            const bool addon =
                entry.is_directory(ec) &&
                std::filesystem::is_regular_file(
                    entry.path() / (entry.path().filename().string() + ".so"), ec);
            if (!bare && !addon) {
                continue;
            }
            ++installed;
            const std::string name = bare ? entry.path().stem().string()
                                          : entry.path().filename().string();
            std::printf("  plugin %-22s %s%s\n", name.c_str(), entry.path().c_str(),
                        plugins.isMarkedForRemoval(name) ? " [marked for removal]" : "");
        }
    }
    if (installed == 0) {
        std::puts("  (no plugins installed)");
    }

    alus::a11y::AtspiBackend::Options backendOptions;
    backendOptions.mainContext = loop.context();
    alus::a11y::AtspiBackend backend{backendOptions};
    std::string error;
    if (!backend.initialize(&error)) {
        std::printf("\nrunning applications: NOT AVAILABLE\n%s\n", error.c_str());
        return 1;
    }
    alus::a11y::Registry registry{loop, backend};
    if (!registry.start(&error)) {
        std::printf("\nrunning applications: NO EVENTS\n%s\n", error.c_str());
        return 1;
    }
    const alus::a11y::ObjectPtr desktop = registry.desktop();
    if (desktop == nullptr) {
        std::puts("\nrunning applications: NO DESKTOP");
        registry.stop();
        return 1;
    }
    std::puts("\nrunning applications (key -> module):");
    for (const alus::a11y::ObjectPtr& application : desktop->children()) {
        if (application == nullptr) {
            continue;
        }
        const alus::app::AppKey key = alus::app::keyFor(application->appInfo());
        // The module, not merely the rule that matched: a rule's factory may
        // DECLINE, and then the generic module takes over. The Electron rule
        // does exactly that for a browser, so printing the rule here would say
        // "electron" next to Google Chrome -- which is the opposite of what
        // happens.
        const alus::app::Module* module = modules.moduleFor(key);
        std::printf("  %-52s -> %s\n", key.describe().c_str(),
                    module == nullptr ? "(none)"
                    : module->isGeneric() ? "generic"
                                          : module->moduleName().c_str());
    }
    registry.stop();
    return 0;
}

// `alus-cli braille-info`: whether BRAILLE can work on this machine, and what
// it would show.
//
// The four questions phase 7 cannot answer from a test, because they are
// questions about this machine:
//
//   1. is liblouis there, which version, and where are its tables;
//   2. does the configured output table translate -- and specifically, does
//      `sr-Cyrl.ctb` translate Cyrillic, which is T7.1;
//   3. is brltty running, and what size is the display it offers;
//   4. does the round trip hold: text -> cells -> text, which is the one
//      measurement that proves the whole translation path at once.
//
// Reads only, and it does NOT take over the display: it opens the BrlAPI
// connection, asks the size and closes it again, so it is safe to run while
// ALUS is running.
int commandBrailleInfo(Loop& loop, alus::config::Manager& config) {
    (void)loop;
    alus::braille::Louis& louis = alus::braille::Louis::shared();
    louis.initialize();
    const alus::braille::Settings settings = alus::braille::settingsFrom(config);

    std::printf("liblouis:     %s\n", louis.version().c_str());
    std::printf("tables:       %s\n", louis.registry().systemDirectory().c_str());

    std::string outputTable = settings.translationTable;
    if (outputTable.empty() || outputTable == "auto") {
        outputTable = louis.registry().defaultTableForLanguage(settings.language, false);
    }
    std::string inputTable = settings.inputTable;
    if (inputTable.empty() || inputTable == "auto") {
        inputTable = louis.registry().defaultTableForLanguage(settings.language, true);
    }
    std::printf("output table: %s (%s)\n", outputTable.c_str(),
                louis.registry().resolve(outputTable).empty() ? "NOT FOUND" : "found");
    std::printf("input table:  %s\n", inputTable.c_str());
    std::printf("language:     %s, table says %s\n", settings.language.c_str(),
                louis.tableLanguage(outputTable).c_str());

    // The translation path, end to end, on the two scripts this project cares
    // about most.
    struct Sample {
        const char* table;
        const char* text;
    };
    const Sample samples[] = {
        {"sr-Cyrl.ctb", "Ћирилица"},
        {"sr-Cyrl.ctb", "Приступ Линуксу говором"},
        {outputTable.c_str(), "braille works"},
    };
    std::printf("\ntranslation:\n");
    for (const Sample& sample : samples) {
        const std::vector<std::string> tables{sample.table, "braille-patterns.cti"};
        const alus::braille::Translation translation = louis.translate(tables, sample.text);
        if (!translation.ok) {
            std::printf("  %-14s %s  FAILED\n", sample.table, sample.text);
            continue;
        }
        std::string unicode;
        for (const std::uint8_t cell : translation.cells) {
            unicode += alus::braille::unicodeBrailleFor(cell);
        }
        const std::string back = louis.backTranslate(tables, translation.cells);
        std::printf("  %-14s %s\n", sample.table, sample.text);
        std::printf("  %-14s -> %s\n", "", unicode.c_str());
        std::printf("  %-14s <- %s (round trip %s)\n", "", back.c_str(),
                    back == sample.text ? "exact" : "LOSSY");
    }

    std::printf("\ndisplay:\n");
    if (!alus::braille::hasDisplaySupport()) {
        std::printf("  support:    NONE (built without libbrlapi)\n");
        louis.terminate();
        return 1;
    }
    std::string error;
    alus::braille::DisplayPtr display =
        alus::braille::makeDisplay(settings.display, settings.brlapiHost, &error);
    if (!error.empty()) {
        std::printf("  brltty:     NOT AVAILABLE\n  reason:     %s\n", error.c_str());
        std::printf(
            "\n  Start brltty to use a display. For a test without hardware:\n"
            "    brltty -b no -d /dev/null -x no -n\n");
        louis.terminate();
        return 1;
    }
    const alus::braille::DisplayDimensions dimensions = display->dimensions();
    std::printf("  driver:     %s (%s)\n", std::string{display->name()}.c_str(),
                display->model().empty() ? "unknown model" : display->model().c_str());
    std::printf("  size:       %d rows x %d cols = %d cells\n", dimensions.numRows,
                dimensions.numCols, dimensions.size());
    std::printf("  wrap:       %s\n",
                std::string{alus::braille::textWrapName(settings.textWrap)}.c_str());
    std::printf("  tether:     %s\n",
                std::string{alus::braille::tetherName(settings.tetherTo)}.c_str());
    std::printf("  typing:     %s\n",
                settings.handleBrailleInput ? "ALUS translates the dots"
                                            : "brltty types (default; see N18)");
    display->terminate();
    louis.terminate();
    return 0;
}

// `alus-cli browse-info`: whether BROWSE MODE can work on this desktop, and
// what it costs.
//
// The four questions phase 6 cannot answer from a test, because they are
// questions about the browser that is running right now:
//
//   1. does the focused document expose `Collection`? Without it quick
//      navigation is one D-Bus call that answers nothing (§9.1 point 2);
//   2. is the document's own text U+FFFC per child, as N47 measured? That is
//      what decides whether the compound range was the right call;
//   3. does `GetMatchesFrom` with sortBy=FLOW actually answer, and how fast
//      (T6.7's p95 < 20 ms);
//   4. how many leaves and lines the document has, which is the cost of
//      everything that walks it.
//
// Reads only: it never moves a caret, never takes the keyboard and never
// activates a window, so it is safe to run while ALUS is running.
// How many nodes a subtree holds, depth- and budget-limited. Used only to
// rank documents, so an approximate answer is the right answer: the budget
// is what keeps this from walking a thousand-node page twice.
std::size_t subtreeSize(const alus::a11y::Object& node, int depth, std::size_t* budget) {
    if (depth <= 0 || *budget == 0) {
        return 0;
    }
    --*budget;
    std::size_t total = 1;
    for (const alus::a11y::ObjectPtr& child : node.children()) {
        if (child != nullptr) {
            total += subtreeSize(*child, depth - 1, budget);
        }
    }
    return total;
}

// The biggest web document open anywhere on this desktop. Breadth first and
// budgeted: a desktop with a dozen applications must not turn this into a
// full tree walk.
//
// Ranked by SUBTREE SIZE and not by child count: a web document root has
// exactly one child (the body `section`), so counting children ranks a chat
// widget above a Wikipedia article.
alus::a11y::ObjectPtr biggestDocument(alus::a11y::Registry& registry) {
    alus::a11y::ObjectPtr best;
    std::size_t bestSize = 0;
    std::vector<alus::a11y::ObjectPtr> queue;
    if (const alus::a11y::ObjectPtr& desktop = registry.api().desktopObject();
        desktop != nullptr) {
        queue.push_back(desktop);
    }
    for (std::size_t head = 0; head < queue.size() && head < 4000; ++head) {
        const alus::a11y::ObjectPtr node = queue[head];
        if (node == nullptr) {
            continue;
        }
        if (alus::nav::BrowseMode::isDocumentRoot(*node)) {
            // Its subtree is the page; nothing below it is another document.
            std::size_t budget = 4000;
            if (const std::size_t size = subtreeSize(*node, 24, &budget); size > bestSize) {
                bestSize = size;
                best = node;
            }
            continue;
        }
        const std::vector<alus::a11y::ObjectPtr>& children = node->children();
        const std::size_t limit = std::min<std::size_t>(children.size(), 60);
        for (std::size_t i = 0; i < limit; ++i) {
            if (children[i] != nullptr) {
                queue.push_back(children[i]);
            }
        }
    }
    return best;
}

// The first web document whose name contains `title`. For `browse-walk`,
// which has to run against ONE known page (tests/fixtures/browse-mode.html)
// and not against whichever page happens to be biggest.
alus::a11y::ObjectPtr documentNamed(alus::a11y::Registry& registry, const std::string& title) {
    std::vector<alus::a11y::ObjectPtr> queue;
    if (const alus::a11y::ObjectPtr& desktop = registry.api().desktopObject();
        desktop != nullptr) {
        queue.push_back(desktop);
    }
    for (std::size_t head = 0; head < queue.size() && head < 4000; ++head) {
        const alus::a11y::ObjectPtr node = queue[head];
        if (node == nullptr) {
            continue;
        }
        if (alus::nav::BrowseMode::isDocumentRoot(*node)) {
            if (node->name().find(title) != std::string::npos) {
                return node;
            }
            continue;
        }
        const std::vector<alus::a11y::ObjectPtr>& children = node->children();
        const std::size_t limit = std::min<std::size_t>(children.size(), 60);
        for (std::size_t i = 0; i < limit; ++i) {
            if (children[i] != nullptr) {
                queue.push_back(children[i]);
            }
        }
    }
    return nullptr;
}

// `alus-cli browse-walk [title]`: browse mode over a live page, driven with
// the same scripts the keys run, and what WOULD be spoken printed after each.
//
// It exists because browse mode met a real browser for the first time in the
// second live pass (docs/history/orca-comparison.md) and failed in three ways at once
// -- `h` found the same heading every time, the down arrow read "U+FFFC", the
// up arrow said "edge of document" -- and every one of those had to be
// diagnosed by asking a blind user to press keys and then reading a log. This
// reproduces them with no keyboard, no grab and no sound: the synthesizer at
// the bottom is a NullSynth that records.
int commandBrowseWalk(Loop& loop, const std::string& titleAndLines) {
    // A trailing number is how many lines to walk down (default 8): a form
    // or a table needs more than the first screen.
    std::string title = titleAndLines;
    int lines = 8;
    if (const std::size_t space = title.find_last_of(' ');
        !title.empty() && title.find_first_not_of("0123456789", space == std::string::npos
                                                                     ? 0
                                                                     : space + 1) ==
                              std::string::npos) {
        const std::size_t start = space == std::string::npos ? 0 : space + 1;
        lines = std::max(1, std::atoi(title.c_str() + start));
        title = space == std::string::npos ? std::string{} : title.substr(0, space);
    }
    alus::a11y::AtspiBackend::Options backendOptions;
    backendOptions.mainContext = loop.context();
    alus::a11y::AtspiBackend backend{backendOptions};
    std::string error;
    if (!backend.initialize(&error)) {
        std::fprintf(stderr, "alus-cli: %s\n", error.c_str());
        return 1;
    }
    alus::a11y::Registry registry{loop, backend};
    if (!registry.start(&error)) {
        std::fprintf(stderr, "alus-cli: %s\n", error.c_str());
        return 1;
    }
    const alus::a11y::ObjectPtr root =
        title.empty() ? biggestDocument(registry) : documentNamed(registry, title);
    if (root == nullptr) {
        std::fprintf(stderr, "alus-cli: no web document%s%s%s is open\n",
                     title.empty() ? "" : " named \"", title.c_str(), title.empty() ? "" : "\"");
        return 1;
    }
    std::printf("document: %s \"%s\" (%s)\n", root->path().c_str(), root->name().c_str(),
                root->appInfo().name.c_str());

    // The speech layer, with a synthesizer that records instead of speaking.
    Manager config{alus::config::Paths::fromEnvironment()};
    alus::a11y::Service::registerDefaults(config);
    alus::nav::Service::registerDefaults(config);
    alus::text::SymbolRegistry symbols{Service::resolveDataDirectory() / "symbols"};
    alus::speech::Dictionaries dictionaries;
    alus::speech::NullSynth synth{true};
    alus::speech::SpeechManager speechManager{loop};
    speechManager.setSynth(&synth);
    alus::speech::Presenter presenter{speechManager, symbols, dictionaries};
    alus::speech::Presenter::Settings settings;
    settings.language = "sr";
    presenter.setSettings(settings);
    alus::ui::initialize(&presenter);
    alus::a11y::installSpeechPresentation(registry, presenter);
    alus::nav::Service nav{loop, config, [&registry] { return &registry; }, nullptr,
                           [&presenter] { return &presenter; }};
    nav.initialize();

    alus::nav::TreeInterceptorPtr interceptor = alus::nav::BrowseMode::chooseForDocument(root, nav);
    auto* browse = dynamic_cast<alus::nav::BrowseMode*>(interceptor.get());
    if (browse == nullptr) {
        std::puts("browse mode: NOT CHOSEN for this document");
        nav.terminate();
        alus::ui::initialize(nullptr);
        return 1;
    }

    const auto drain = [&loop, &speechManager] {
        for (int cycle = 0; cycle < 2000; ++cycle) {
            loop.iterateUntilIdle();
            if (!speechManager.isSpeaking() && !speechManager.hasPendingSpeech() &&
                loop.queue().empty()) {
                return;
            }
        }
    };
    const auto run = [&](const char* script, int times) {
        const alus::script::Script* found = browse->scriptByName(script);
        for (int i = 0; i < times; ++i) {
            synth.clear();
            if (found == nullptr) {
                std::printf("  %-22s (no such script on browse mode)\n", script);
                return;
            }
            alus::input::KeyboardGesture::Init init;
            alus::input::KeyboardGesture gesture{init};
            found->function(gesture);
            drain();
            std::string said;
            for (const std::string& piece : synth.spokenText()) {
                said += (said.empty() ? "" : " | ") + piece;
            }
            // U+FFFC made visible: it is the defect this command was written
            // to show, and printed raw it is an invisible box.
            for (std::size_t at = said.find("\xEF\xBF\xBC"); at != std::string::npos;
                 at = said.find("\xEF\xBF\xBC")) {
                said.replace(at, 3, "<U+FFFC>");
            }
            std::printf("  %-22s %s\n", script, said.empty() ? "(silence)" : said.c_str());
        }
    };

    std::puts("\n-- quick navigation --");
    run("nextHeading", 5);
    run("previousHeading", 2);
    std::puts("\n-- by line, from the top --");
    run("topOfDocument", 1);
    run("moveByLine_forward", lines);
    run("moveByLine_back", 3);
    std::puts("\n-- form fields and tables, from the top --");
    run("topOfDocument", 1);
    run("nextFormField", 12);
    run("topOfDocument", 1);
    run("nextTable", 3);
    std::puts("\n-- by character --");
    run("moveByCharacter_forward", 5);
    run("moveByCharacter_back", 3);

    interceptor.reset();
    nav.terminate();
    alus::ui::initialize(nullptr);
    registry.stop();
    return 0;
}

int commandBrowseInfo(Loop& loop) {
    alus::a11y::AtspiBackend::Options backendOptions;
    backendOptions.mainContext = loop.context();
    alus::a11y::AtspiBackend backend{backendOptions};
    std::string error;
    if (!backend.initialize(&error)) {
        std::printf("browse mode:  NOT AVAILABLE (no object model)\n%s\n", error.c_str());
        return 1;
    }
    alus::a11y::Registry registry{loop, backend};
    if (!registry.start(&error)) {
        std::printf("browse mode:  NO EVENTS\n%s\n", error.c_str());
        return 1;
    }

    const alus::a11y::ObjectPtr focus = registry.api().focus();
    if (focus == nullptr) {
        std::puts("focus:        (none found)");
        std::puts("");
        std::puts("Focus a web page in Firefox or Chromium, or a document in");
        std::puts("LibreOffice Writer, and run this again.");
        return 1;
    }

    // Walk up for the document root, exactly as the interceptor registry does.
    alus::a11y::ObjectPtr root = focus;
    for (int depth = 0; depth < 64 && root != nullptr; ++depth) {
        if (alus::nav::BrowseMode::isDocumentRoot(*root)) {
            break;
        }
        root = root->parent();
    }
    std::printf("application:  %s (%s %s)\n", focus->appInfo().name.c_str(),
                focus->appInfo().toolkitName.c_str(),
                focus->appInfo().toolkitVersion.c_str());
    std::printf("focus:        %s \"%s\"\n", alus::a11y::roleName(focus->role()),
                focus->name().c_str());

    // No document under the focus? Measure the biggest page that IS open and
    // say so. The focus is on the window frame whenever the user has not
    // clicked into the page -- which is most of the time, and which used to
    // make this command refuse to measure anything at all. What it reports
    // then is still a real document from a real browser; only the caret
    // questions are about the focused one.
    bool focused = true;
    if (root == nullptr || !alus::nav::BrowseMode::isDocumentRoot(*root)) {
        focused = false;
        root = biggestDocument(registry);
        if (root == nullptr) {
            std::puts("document:     NONE in the focus chain, and no web document is open");
            std::puts("");
            std::puts("Open a page in Firefox or Chromium, or a document in");
            std::puts("LibreOffice Writer, and run this again.");
            return 1;
        }
    }
    std::printf("document:     %s \"%s\"%s\n", root->path().c_str(), root->name().c_str(),
                focused ? "" : "   (NOT the focused object: the biggest page open)");
    std::printf("  editable:   %s%s\n",
                root->states().has(alus::a11y::State::Editable) ? "yes" : "no",
                root->states().has(alus::a11y::State::Editable)
                    ? "  (NOT intercepted: a compose field is a text editor)"
                    : "");
    std::printf("  busy:       %s\n",
                root->states().has(alus::a11y::State::Busy) ? "yes (still loading)" : "no");

    // Question 1: Collection.
    const bool collection = root->hasInterface(alus::a11y::Interface::Collection);
    std::printf("  Collection: %s\n",
                collection ? "yes -- quick navigation is one D-Bus call per key"
                           : "NO -- quick navigation will answer nothing (§9.1)");

    // Question 2: is the root's own text placeholders?
    const int characters = root->characterCount();
    // CLAMPED to the character count, and not a flat 120: Firefox returns an
    // EMPTY string when the end offset is past the end of the text, where
    // Chrome clamps and answers (phase-6.md N94). Asking for 120 characters of
    // an 11-character document therefore reported "0 of them U+FFFC" in
    // Firefox -- which looked like Firefox contradicting N47, and did not.
    const std::string own =
        characters > 0 ? root->textRange(0, std::min(characters, 120)) : std::string{};
    int placeholders = 0;
    for (std::size_t at = 0; at + 2 < own.size(); at += 3) {
        if (own.compare(at, 3, "\xEF\xBF\xBC") == 0) {
            ++placeholders;
        }
    }
    std::printf("  own text:   %d characters, %d of them U+FFFC%s\n", characters,
                placeholders,
                (placeholders > 0 && characters > 0)
                    ? "  (N47: the root lists its children, it is not the body)"
                    : "");

    // Question 4: the leaves and the lines.
    const alus::text::FormatReportSettings settings;
    int leaves = 0;
    int embedded = 0;
    alus::a11y::ObjectPtr leaf = alus::nav::DocumentLeaves::first(root);
    for (; leaf != nullptr && leaves < 20000; ++leaves) {
        if (alus::nav::DocumentLeaves::isEmbeddedLeaf(*leaf)) {
            ++embedded;
        }
        leaf = alus::nav::DocumentLeaves::next(root, leaf);
    }
    std::printf("  leaves:     %d (%d of them embedded objects)\n", leaves, embedded);

    alus::text::TextRangePtr whole =
        alus::nav::DocumentTextRange::make(root, settings, alus::text::Position::All);
    if (whole == nullptr) {
        std::puts("  range:      COULD NOT BE MADE");
        return 1;
    }
    const std::string text = whole->text();
    std::printf("  text:       %zu bytes\n", text.size());

    // The field stream: the thing §9.3 is about. Counted, not printed, because
    // a whole page of it is unreadable -- and the COUNTS are what say whether
    // it is balanced.
    const alus::text::TextWithFields fields = whole->getTextWithFields(settings);
    int starts = 0;
    int ends = 0;
    int pieces = 0;
    for (const alus::text::TextOrField& item : fields) {
        if (item.isText()) {
            ++pieces;
        } else if (item.command->kind() == alus::text::FieldCommand::Kind::ControlStart) {
            ++starts;
        } else if (item.command->kind() == alus::text::FieldCommand::Kind::ControlEnd) {
            ++ends;
        }
    }
    std::printf("  fields:     %d controlStart, %d controlEnd, %d text pieces -- %s\n",
                starts, ends, pieces,
                starts == ends ? "BALANCED" : "UNBALANCED (a bug: §9.3)");

    const std::optional<int> lines = whole->unitCount(alus::text::TextUnit::Line);
    std::printf("  lines:      %s\n",
                lines.has_value() ? std::to_string(*lines).c_str() : "unknown");

    // Question 3: the quick-nav latency, measured here because only a real
    // browser can answer it (T6.7).
    if (!collection) {
        std::puts("");
        std::puts("quick nav:    not measured (no Collection interface)");
        return 0;
    }
    std::puts("");
    std::puts("quick navigation, 20 presses each (T6.7 asks for p95 < 20 ms):");
    const alus::nav::QuickNav quickNav{root};
    struct Row {
        const char* key;
        alus::nav::ElementType type;
    };
    const Row rows[] = {
        {"h  heading", alus::nav::ElementType::Heading},
        {"k  link", alus::nav::ElementType::Link},
        {"f  form field", alus::nav::ElementType::FormField},
        {"l  list", alus::nav::ElementType::List},
        {"t  table", alus::nav::ElementType::Table},
        {"d  landmark", alus::nav::ElementType::Landmark},
        {"b  button", alus::nav::ElementType::Button},
        {"g  graphic", alus::nav::ElementType::Graphic},
    };
    for (const Row& row : rows) {
        std::vector<double> samples;
        alus::a11y::ObjectPtr at = root;
        int found = 0;
        for (int press = 0; press < 20; ++press) {
            const auto before = std::chrono::steady_clock::now();
            alus::a11y::ObjectPtr next = quickNav.find(row.type, at, /*forwards=*/true);
            const auto after = std::chrono::steady_clock::now();
            samples.push_back(
                std::chrono::duration<double, std::milli>(after - before).count());
            if (next == nullptr) {
                break;
            }
            at = next;
            ++found;
        }
        std::sort(samples.begin(), samples.end());
        const double p95 =
            samples.empty()
                ? 0.0
                : samples[std::min(samples.size() - 1,
                                   static_cast<std::size_t>(
                                       static_cast<double>(samples.size()) * 0.95))];
        std::printf("  %-14s %2d found, p95 %6.2f ms  %s\n", row.key, found, p95,
                    p95 < 20.0 ? "" : "<-- OVER BUDGET");
    }
    return 0;
}

// `alus-cli text-info`: what the text layer can do with the focused object and
// with the active window. Reads only -- it never moves a caret, never takes
// the keyboard and never activates a window, so it is safe to run while the
// screen reader is running.
int commandTextInfo(Loop& loop) {
    std::printf("segmenter:    %s%s\n",
                alus::text::Segmenter::icuAvailable() ? "ICU " : "RULE-BASED FALLBACK",
                alus::text::Segmenter::icuAvailable()
                    ? alus::text::Segmenter::icuVersion().c_str()
                    : " (Japanese and Thai word navigation need libicu-dev)");

    alus::a11y::AtspiBackend::Options backendOptions;
    backendOptions.mainContext = loop.context();
    alus::a11y::AtspiBackend backend{backendOptions};
    std::string error;
    if (!backend.initialize(&error)) {
        std::printf("text layer:   NOT AVAILABLE (no object model)\n%s\n", error.c_str());
        return 1;
    }
    alus::a11y::Registry registry{loop, backend};
    if (!registry.start(&error)) {
        std::printf("text layer:   NO EVENTS\n%s\n", error.c_str());
        return 1;
    }

    const alus::a11y::ObjectPtr focus = registry.api().focus();
    if (focus == nullptr) {
        std::puts("focus:        (none found)");
    } else {
        std::printf("focus:        %s (%s)%s\n", focus->name().c_str(),
                    alus::a11y::roleName(focus->role()),
                    focus->hasNavigableText() ? ", navigable text" : ", no navigable text");
        const alus::text::TextRangePtr caret =
            focus->makeTextRange(alus::text::Position::Caret);
        if (caret == nullptr) {
            std::puts("caret:        (the object has no text interface)");
        } else {
            std::printf("characters:   %d\n", focus->characterCount());
            // Which granularities the toolkit answers for itself. This is the
            // measurement §7.2 is about: lines and paragraphs come from the
            // toolkit, words and sentences from the segmenter.
            struct Granularity {
                const char* name;
                alus::a11y::TextGranularity value;
            };
            static const Granularity kGranularities[] = {
                {"char", alus::a11y::TextGranularity::Character},
                {"word", alus::a11y::TextGranularity::Word},
                {"sentence", alus::a11y::TextGranularity::Sentence},
                {"line", alus::a11y::TextGranularity::Line},
                {"paragraph", alus::a11y::TextGranularity::Paragraph},
            };
            std::printf("granularity: ");
            for (const Granularity& granularity : kGranularities) {
                const bool answered =
                    backend.stringAtOffset(focus->ref(), 0, granularity.value).has_value();
                std::printf(" %s=%s", granularity.name, answered ? "yes" : "no");
            }
            std::printf("\n");

            for (const auto& [label, unit] :
                 {std::pair<const char*, alus::text::TextUnit>{
                      "character", alus::text::TextUnit::Character},
                  {"word", alus::text::TextUnit::Word},
                  {"line", alus::text::TextUnit::Line},
                  {"sentence", alus::text::TextUnit::Sentence},
                  {"paragraph", alus::text::TextUnit::Paragraph}}) {
                alus::text::TextRangePtr range = caret->copy();
                if (!range->expand(unit)) {
                    std::printf("%-12s (not supported)\n", label);
                    continue;
                }
                std::string text = range->text();
                if (text.size() > 60) {
                    text = text.substr(0, 57) + "...";
                }
                for (char& character : text) {
                    if (character == '\n' || character == '\r') {
                        character = ' ';
                    }
                }
                std::printf("%-12s \"%s\"\n", label, text.c_str());
            }
            const alus::a11y::AttributeRun run =
                backend.attributeRun(focus->ref(),
                                     static_cast<int>(caret->bookmark().start), true);
            const alus::text::FormatField field =
                alus::a11y::normaliseTextAttributes(run.attributes);
            std::printf("attributes:   %zu raw, %zu normalised, run [%d,%d)\n",
                        run.attributes.size(), field.values().size(), run.start, run.end);
            for (const auto& [key, value] : field.values()) {
                std::printf("  %-20s %s\n", key.c_str(), value.c_str());
            }
        }
    }

    // Flat object review over the active window (§10): what "screen review"
    // means on Linux, and how much it costs.
    const alus::a11y::ObjectPtr window = registry.activeWindow();
    if (window == nullptr) {
        std::puts("screen review: (no active window found)");
    } else {
        const auto started = std::chrono::steady_clock::now();
        const std::uint64_t callsBefore = backend.callCount();
        auto screen = alus::nav::ScreenReviewRange::make(window, alus::text::Position::First);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        if (screen == nullptr) {
            std::printf("screen review: no content in \"%s\"\n", window->name().c_str());
        } else {
            std::printf("screen review: %zu pieces, %d rows, %zu objects visited, "
                        "%llu calls, %lld us%s\n",
                        screen->pieces().size(), screen->lineCount().value_or(0),
                        screen->visitedObjects(),
                        static_cast<unsigned long long>(backend.callCount() - callsBefore),
                        static_cast<long long>(elapsed.count()),
                        screen->hitLimit() ? " (BUDGET REACHED)" : "");
            alus::text::TextRangePtr row = screen->copy();
            for (int index = 0; index < 3; ++index) {
                if (!row->expand(alus::text::TextUnit::Line)) {
                    break;
                }
                std::string text = row->text();
                if (text.size() > 70) {
                    text = text.substr(0, 67) + "...";
                }
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                    text.pop_back();
                }
                std::printf("  row %d: \"%s\"\n", index + 1, text.c_str());
                if (row->move(alus::text::TextUnit::Line, 1) == 0) {
                    break;
                }
            }
        }
    }

    std::printf("AT-SPI calls: %llu, errors %llu\n",
                static_cast<unsigned long long>(backend.callCount()),
                static_cast<unsigned long long>(backend.errorCount()));
    std::puts("text layer:   WORKING");
    registry.stop();
    return 0;
}

int commandA11yTree(Loop& loop, const std::string& argument) {
    alus::a11y::AtspiBackend::Options backendOptions;
    backendOptions.mainContext = loop.context();
    alus::a11y::AtspiBackend backend{backendOptions};
    std::string error;
    if (!backend.initialize(&error)) {
        std::fprintf(stderr, "alus-cli: %s\n", error.c_str());
        return 1;
    }
    alus::a11y::Registry registry{loop, backend};
    if (!registry.start(&error)) {
        std::fprintf(stderr, "alus-cli: %s\n", error.c_str());
        return 1;
    }
    // `a11y-tree [depth] [application]`. The application filter exists
    // because the walk is depth first from the desktop and bounded, and
    // gnome-shell alone spends the whole budget: asking about one window --
    // ALUS's own settings window, which is how N111 was chased -- printed
    // 2000 shell panels and never reached it.
    std::string depthText = argument;
    std::string application;
    if (const std::size_t space = argument.find(' '); space != std::string::npos) {
        depthText = argument.substr(0, space);
        application = argument.substr(space + 1);
    }
    const int depth = depthText.empty() ? 3 : std::max(1, std::atoi(depthText.c_str()));
    // Bounded, because a web page has more objects than a terminal has lines.
    std::size_t budget = 2000;
    if (const alus::a11y::ObjectPtr desktop = registry.desktop()) {
        if (application.empty()) {
            printTree(*desktop, depth, 0, budget);
        } else {
            bool found = false;
            for (const alus::a11y::ObjectPtr& app : desktop->children()) {
                if (app != nullptr && app->name() == application) {
                    printTree(*app, depth, 0, budget);
                    found = true;
                }
            }
            if (!found) {
                std::fprintf(stderr, "alus-cli: no application named '%s' on the desktop\n",
                             application.c_str());
            }
        }
    }
    std::printf("%zu object(s) printed, %llu AT-SPI calls\n", 2000 - budget,
                static_cast<unsigned long long>(backend.callCount()));
    registry.stop();
    return 0;
}

int commandGesture(alus::input::Service& input, const std::string& identifier) {
    if (identifier.empty()) {
        std::fputs("alus-cli: gesture needs an identifier, e.g. kb:alus+1\n", stderr);
        return 2;
    }
    auto* manager = input.manager();
    auto* dispatcher = input.dispatcher();
    if (manager == nullptr || dispatcher == nullptr) {
        std::fputs("alus-cli: the input subsystem is not running\n", stderr);
        return 1;
    }
    const std::optional<alus::input::ParsedKeyboardGesture> parsed =
        alus::input::parseKeyboardGesture(identifier);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "alus-cli: '%s' is not a keyboard gesture identifier\n",
                     identifier.c_str());
        return 2;
    }
    std::printf("gesture:   %s\n",
                alus::input::normalizeGestureIdentifier(identifier).c_str());
    std::printf("main key:  %s (keysym 0x%04x)\n", parsed->mainKeyName.c_str(),
                parsed->keysym);
    if (parsed->keysym == 0) {
        std::puts("WARNING: no keyboard can produce that key; it cannot be grabbed");
    }

    alus::input::KeyboardGesture::Init init;
    init.event.keysym = parsed->keysym;
    init.baseKeysym = parsed->keysym;
    init.modifiers = parsed->modifiers;
    init.isAlusKey = parsed->mainKeyName == alus::input::KeyNames::kAlusKeyName;
    init.config.layout = parsed->layout.value_or(manager->config().layout);
    auto gesture = std::make_shared<alus::input::KeyboardGesture>(std::move(init));
    std::printf("display:   %s\n", gesture->displayName().c_str());

    const std::optional<alus::script::Script> script = dispatcher->findScript(*gesture);
    if (!script.has_value()) {
        std::puts("command:   (none — this gesture is unbound)");
        return 1;
    }
    const auto& trace = dispatcher->lastTrace();
    std::printf("command:   %s\n", script->info->name.c_str());
    std::printf("category:  %s\n", script->info->category.c_str());
    std::printf("found at:  step %d, %s, %s map\n", trace.step, trace.location.c_str(),
                trace.fromGlobalMap ? "global" : "built-in");
    if (!script->info->description.empty()) {
        std::printf("describes: %s\n", script->info->description.c_str());
    }
    return 0;
}

int commandListCommands(alus::input::Service& input, const std::string& category) {
    const std::vector<const alus::script::ScriptInfo*> all = input.catalog().all();
    std::size_t shown = 0;
    for (const alus::script::ScriptInfo* info : all) {
        if (!category.empty() && info->category != category) {
            continue;
        }
        ++shown;
        std::printf("%-28s %-22s %-34s %s\n", info->name.c_str(), info->category.c_str(),
                    info->gestures.empty() ? "(unbound)" : info->gestures.front().c_str(),
                    info->description.c_str());
    }
    std::printf("\n%zu of %zu commands%s\n", shown, all.size(),
                category.empty() ? "" : (" in category '" + category + "'").c_str());
    return shown > 0 ? 0 : 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 9: the settings window and the installation
// ---------------------------------------------------------------------------

// `alus-cli settings [page]`: open the settings window.
//
// A thin wrapper around `alus-settings`, which is a program of its own because
// a window in the screen reader's own process cannot be read at all (see
// src/gui/main.cpp). Kept as a subcommand because every other diagnostic in
// this tool is one, and `alus-cli settings braille` is what a person following
// a bug report will type.
int commandSettings(const std::string& page) {
    const std::string wanted = page.empty() ? std::string{"general"} : page;
    if (alus::gui::findPage(wanted) == nullptr) {
        std::fprintf(stderr, "alus-cli: no such settings page: %s\n", wanted.c_str());
        std::fputs("  pages: ", stderr);
        for (const std::string& id : alus::gui::pageIds()) {
            std::fprintf(stderr, "%s ", id.c_str());
        }
        std::fputs("\n", stderr);
        return 2;
    }

    // Beside this executable first, so an uninstalled build opens its own
    // settings window rather than an older installed one.
    std::string program;
    std::error_code code;
    const std::filesystem::path self =
        std::filesystem::read_symlink("/proc/self/exe", code);
    if (!code) {
        const std::filesystem::path beside = self.parent_path() / "alus-settings";
        if (std::filesystem::exists(beside, code)) {
            program = beside.string();
        }
    }
    if (program.empty()) {
        if (gchar* found = g_find_program_in_path("alus-settings")) {
            program = found;
            g_free(found);
        }
    }
    if (program.empty()) {
        std::fputs("alus-cli: alus-settings is not installed\n"
                   "  It is built only when libgtk-4-dev and libadwaita-1-dev "
                   "are present.\n",
                   stderr);
        return 1;
    }

    // execvp and not a spawn: this process has nothing left to do, and
    // replacing it means the exit status and the terminal belong to the window.
    const char* argv[] = {program.c_str(), "--page", wanted.c_str(), nullptr};
    ::execv(program.c_str(), const_cast<char**>(argv));
    std::fprintf(stderr, "alus-cli: could not run %s: %s\n", program.c_str(),
                 std::strerror(errno));
    return 1;
}

// `alus-cli gui-info`: whether the settings window can open here, and what the
// installation looks like.
//
// The same shape as every other *-info command in this tool, and for the same
// reason: "the settings window does not open" has to have an answer that does
// not require opening it, and "ALUS does not start with my session" has to
// have an answer that does not require logging out.
int commandGuiInfo() {
    // Asked through gui::Service, which is the same code path the shortcuts
    // take: a diagnostic that answered the question differently from the thing
    // it is diagnosing would be worse than no diagnostic.
    alus::config::Manager probe{alus::config::Paths::fromEnvironment()};
    alus::gui::Service service{probe, nullptr, nullptr, nullptr, {}, {}};

    std::printf("Settings window\n");
    const std::string program = service.settingsProgram();
    std::printf("  alus-settings:     %s\n",
                program.empty() ? "NOT FOUND -- built only with libgtk-4-dev and "
                                  "libadwaita-1-dev installed"
                                : program.c_str());
    const bool available = service.available();
    std::printf("  can open here:     %s\n", available ? "yes" : "no");
    if (!available) {
        std::printf("  why not:           %s\n", service.unavailableReason().c_str());
    }
    std::printf("  its own process:   yes -- a window in the screen reader's own\n"
                "                     process cannot be read at all (phase-9.md N79)\n");
    std::printf("  pages:             ");
    bool first = true;
    for (const std::string& id : alus::gui::pageIds()) {
        std::printf("%s%s", first ? "" : ", ", id.c_str());
        first = false;
    }
    std::printf("\n");

    std::printf("\nTranslation\n");
    std::printf("  language:          %s\n", alus::i18n::currentLanguage().c_str());
    std::printf("  catalogue:         %s\n",
                alus::i18n::haveCatalog() ? "loaded" : "not found (speaking English)");
    std::printf("  looked in:         %s\n",
                alus::i18n::localeDirectory().string().c_str());
    const std::vector<std::string> languages = alus::i18n::availableLanguages();
    std::printf("  installed:         ");
    if (languages.empty()) {
        std::printf("none\n");
    } else {
        first = true;
        for (const std::string& language : languages) {
            std::printf("%s%s", first ? "" : ", ", language.c_str());
            first = false;
        }
        std::printf("\n");
    }
    return 0;
}

// `alus-cli autostart [--enable|--disable]`: does ALUS start with the session,
// and does anything else.
//
// risks.md R12 is explicit that the Orca conflict is OFFERED and never
// resolved silently, because masking another screen reader's autostart is not
// a decision an installer gets to make for a user who may be relying on it.
// So the default is --status and it only reports.
int commandAutostart(const std::string& argument) {
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        std::fputs("alus-cli: no $HOME\n", stderr);
        return 1;
    }
    const fs::path autostartDir = fs::path{home} / ".config" / "autostart";
    const fs::path ours = autostartDir / "alus.desktop";
    // Masking an entry from /etc/xdg/autostart is done by a same-named file in
    // the user's own directory with Hidden=true. That is the XDG way and it is
    // reversible by deleting one file, which matters for a change to another
    // screen reader.
    const fs::path orcaMask = autostartDir / "orca-autostart.desktop";

    std::error_code code;
    const bool oursEnabled = fs::exists(ours, code);
    const bool orcaSystem = fs::exists("/etc/xdg/autostart/orca-autostart.desktop", code);
    const bool orcaMasked = fs::exists(orcaMask, code);

    if (argument == "--enable" || argument == "--disable") {
        const bool enable = argument == "--enable";
        fs::create_directories(autostartDir, code);
        if (enable) {
            // Copied from the installed location, so the Exec path is the one
            // this build installed.
            const fs::path installed =
                fs::path{ALUS_DATADIR} / "applications" / "alus.desktop";
            if (!fs::exists(installed, code)) {
                std::fprintf(stderr,
                             "alus-cli: %s is not installed; run `meson install` "
                             "first\n",
                             installed.string().c_str());
                return 1;
            }
            fs::copy_file(installed, ours, fs::copy_options::overwrite_existing, code);
            if (code) {
                std::fprintf(stderr, "alus-cli: could not write %s: %s\n",
                             ours.string().c_str(), code.message().c_str());
                return 1;
            }
            std::printf("ALUS will start with your session: %s\n", ours.string().c_str());
            if (orcaSystem && !orcaMasked) {
                // NOT done for them. Both screen readers would start, and the
                // second to ask the compositor for the keyboard gets nothing
                // (N8) -- a screen reader that says it started and then
                // answers no key at all.
                std::printf(
                    "\nAnother screen reader also starts with your session, and only "
                    "one\nof them can take the keyboard. To stop it, run:\n\n"
                    "  printf '[Desktop Entry]\\nType=Application\\nHidden=true\\n' > "
                    "%s\n\nTo undo that later, delete that file.\n",
                    orcaMask.string().c_str());
            }
        } else {
            fs::remove(ours, code);
            std::printf("ALUS will not start with your session.\n");
        }
        return 0;
    }

    std::printf("Autostart\n");
    std::printf("  ALUS:              %s\n",
                oursEnabled ? ours.string().c_str() : "not enabled");
    std::printf("  another reader:    %s\n",
                !orcaSystem ? "none installed"
                            : (orcaMasked ? "installed but masked for you"
                                          : "INSTALLED AND ENABLED -- only one screen "
                                            "reader can take the keyboard"));
    std::printf("  systemd unit:      %s\n",
                fs::exists(fs::path{home} / ".config" / "systemd" / "user" /
                               "default.target.wants" / "alus.service",
                           code)
                    ? "enabled"
                    : "not enabled (systemctl --user enable alus.service)");
    std::printf("\nThe desktop's own screen-reader switch\n");
    std::printf("  This is what the installed autostart entry follows:\n"
                "    gsettings get org.gnome.desktop.a11y.applications "
                "screen-reader-enabled\n");
    std::printf("\n  alus-cli autostart --enable    start ALUS with the session\n");
    std::printf("  alus-cli autostart --disable   do not\n");
    return 0;
}

int main(int argc, char* argv[]) {
    // Before anything that can produce text: --help is text too. The
    // configuration is not read yet, so this binds from the environment, and
    // --language re-binds below.
    alus::i18n::initializeFromEnvironment();

    Options options;
    const GOptionEntry entries[] = {
        // GLib's own --help is turned off below because it would list only
        // the options; usage() lists the commands, which is what is asked for.
        {"help", 'h', 0, G_OPTION_ARG_NONE, &options.help, "Show the commands and options",
         nullptr},
        {"version", 'v', 0, G_OPTION_ARG_NONE, &options.version, "Print the version and exit",
         nullptr},
        {"rate", 0, 0, G_OPTION_ARG_INT, &options.rate, "Speech rate, 0-100", "N"},
        {"pitch", 0, 0, G_OPTION_ARG_INT, &options.pitch, "Pitch, 0-100", "N"},
        {"volume", 0, 0, G_OPTION_ARG_INT, &options.volume, "Volume, 0-100", "N"},
        {"voice", 0, 0, G_OPTION_ARG_STRING, &options.voice, "Voice name", "NAME"},
        {"module", 0, 0, G_OPTION_ARG_STRING, &options.module,
         "speech-dispatcher output module", "NAME"},
        {"language", 0, 0, G_OPTION_ARG_STRING, &options.language, "Language code", "CODE"},
        {"symbol-level", 0, 0, G_OPTION_ARG_STRING, &options.symbolLevel,
         "none, some, most, all, char", "LEVEL"},
        {"iterations", 0, 0, G_OPTION_ARG_INT, &options.iterations,
         "bench-speech repetitions", "N"},
        {"no-audio", 0, 0, G_OPTION_ARG_NONE, &options.noAudio,
         "Do not open the audio device", nullptr},
        {"log-level", 0, 0, G_OPTION_ARG_STRING, &options.logLevel, "Log level", "LEVEL"},
        {"config-dir", 0, 0, G_OPTION_ARG_STRING, &options.configDir, "Configuration directory",
         "DIR"},
        {"data-dir", 0, 0, G_OPTION_ARG_STRING, &options.dataDir, "ALUS data directory", "DIR"},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
    };

    GOptionContext* context = g_option_context_new("<command> [text] - ALUS speech from the shell");
    g_option_context_add_main_entries(context, entries, nullptr);
    g_option_context_set_help_enabled(context, FALSE);
    GError* error = nullptr;
    const gboolean parsed = g_option_context_parse(context, &argc, &argv, &error);
    g_option_context_free(context);
    if (parsed == FALSE) {
        std::fprintf(stderr, "alus-cli: %s\n",
                     error != nullptr ? error->message : "could not parse the arguments");
        if (error != nullptr) {
            g_error_free(error);
        }
        return 2;
    }
    // One place every return path passes through; see the same lambda in
    // src/daemon/main.cpp for why (phase-9.md N87). `settings` never reaches it,
    // because it execv()s -- and the kernel frees the whole image there.
    const auto freeOptions = [&options] {
        g_free(options.voice);
        g_free(options.module);
        g_free(options.language);
        g_free(options.symbolLevel);
        g_free(options.logLevel);
        g_free(options.configDir);
        g_free(options.dataDir);
        options.voice = nullptr;
        options.module = nullptr;
        options.language = nullptr;
        options.symbolLevel = nullptr;
        options.logLevel = nullptr;
        options.configDir = nullptr;
        options.dataDir = nullptr;
    };

    if (options.version != FALSE) {
        std::printf("alus-cli %s\n", ALUS_VERSION);
        freeOptions();
        return 0;
    }
    if (options.help != FALSE) {
        usage();
        freeOptions();
        return 0;
    }
    if (argc < 2) {
        usage();
        freeOptions();
        return 2;
    }

    Log::setThreadName("main");
    Log::instance().setLevel(LogLevel::Warning);
    if (options.logLevel != nullptr) {
        const std::string level = options.logLevel;
        if (level == "debug") {
            Log::instance().setLevel(LogLevel::Debug);
        } else if (level == "io") {
            Log::instance().setLevel(LogLevel::Io);
        } else if (level == "info") {
            Log::instance().setLevel(LogLevel::Info);
        } else if (level == "error") {
            Log::instance().setLevel(LogLevel::Error);
        } else if (level == "off") {
            Log::instance().setLevel(LogLevel::Off);
        }
    }

    const std::string command = argv[1];
    std::string text;
    for (int i = 2; i < argc; ++i) {
        if (!text.empty()) {
            text += " ";
        }
        text += argv[i];
    }

    Manager config{options.configDir != nullptr
                       ? alus::config::Paths::forDirectory(options.configDir)
                       : alus::config::Paths::fromEnvironment()};
    Service::registerDefaults(config);
    config.setDefault("general", "language", "en");
    std::string configError;
    if (!config.load(&configError)) {
        ALUS_DEBUG("no configuration read: {}", configError);
    }
    if (options.rate >= 0) {
        config.setInt("speech", "rate", options.rate);
    }
    if (options.pitch >= 0) {
        config.setInt("speech", "pitch", options.pitch);
    }
    if (options.volume >= 0) {
        config.setInt("speech", "volume", options.volume);
    }
    if (options.voice != nullptr) {
        config.setString("speech", "voice", options.voice);
    }
    if (options.module != nullptr) {
        config.setString("speech", "outputModule", options.module);
    }
    if (options.language != nullptr) {
        config.setString("general", "language", options.language);
        // --language picks the SYNTHESIZER's language and ALUS's own, which
        // is what a diagnostic wants: `alus-cli --language sr list-commands`
        // has to show the Serbian command table, not the English one with a
        // Serbian voice.
        alus::i18n::Options i18nOptions;
        i18nOptions.language = options.language;
        alus::i18n::initialize(i18nOptions);
    }
    if (options.symbolLevel != nullptr) {
        config.setString("speech", "symbolLevel", options.symbolLevel);
    }

    Loop loop;
    alus::speech::ServiceOptions serviceOptions;
    serviceOptions.enableAudio = options.noAudio == FALSE;
    if (options.dataDir != nullptr) {
        serviceOptions.dataDir = options.dataDir;
    }
    if (command == "process" || command == "input-info" || command == "gesture" ||
        command == "list-commands" || command == "a11y-info" || command == "a11y-tree" ||
        command == "text-info" || command == "browse-info" || command == "browse-walk" ||
        command == "braille-info" || command == "app-info" ||
        command == "settings" || command == "gui-info" || command == "autostart") {
        // No synthesizer and no audio device: these commands only print.
        serviceOptions.noSpeech = true;
        serviceOptions.enableAudio = false;
    }
    Service service{loop, config, serviceOptions};
    service.initialize();

    int status = 2;
    if (command == "speak") {
        if (text.empty()) {
            std::fputs("alus-cli: speak needs some text\n", stderr);
            status = 2;
        } else {
            status = commandSpeak(loop, service, text);
        }
    } else if (command == "bench-speech") {
        status = commandBench(loop, service, std::max(1, options.iterations));
    } else if (command == "list-voices") {
        status = commandListVoices(service);
    } else if (command == "list-modules") {
        status = commandListModules(service);
    } else if (command == "process") {
        alus::text::SymbolLevel level = alus::text::SymbolLevel::All;
        if (options.symbolLevel != nullptr) {
            if (const auto parsedLevel = alus::text::parseSymbolLevel(options.symbolLevel)) {
                level = *parsedLevel;
            }
        }
        status = commandProcess(service, text, config.getString("general", "language", "en"),
                                level);
    } else if (command == "beep") {
        double hz = 440.0;
        int milliseconds = 200;
        if (!text.empty()) {
            const std::vector<std::string> parts = [&text] {
                std::vector<std::string> out;
                std::size_t start = 0;
                for (;;) {
                    const std::size_t position = text.find(' ', start);
                    if (position == std::string::npos) {
                        out.push_back(text.substr(start));
                        return out;
                    }
                    out.push_back(text.substr(start, position - start));
                    start = position + 1;
                }
            }();
            if (!parts.empty()) {
                hz = std::strtod(parts[0].c_str(), nullptr);
            }
            if (parts.size() > 1) {
                milliseconds = std::atoi(parts[1].c_str());
            }
        }
        alus::audio::beep(hz, milliseconds);
        if (alus::audio::WavePlayer* player = alus::audio::soundsPlayer()) {
            player->sync();
        }
        status = 0;
    } else if (command == "a11y-info") {
        status = commandA11yInfo(loop);
    } else if (command == "a11y-tree") {
        status = commandA11yTree(loop, text);
    } else if (command == "browse-walk") {
        status = commandBrowseWalk(loop, text);
    } else if (command == "text-info") {
        status = commandTextInfo(loop);
    } else if (command == "browse-info") {
        status = commandBrowseInfo(loop);
    } else if (command == "braille-info") {
        status = commandBrailleInfo(loop, config);
    } else if (command == "app-info") {
        status = commandAppInfo(loop, config);
    } else if (command == "settings") {
        status = commandSettings(text);
    } else if (command == "gui-info") {
        status = commandGuiInfo();
    } else if (command == "autostart") {
        status = commandAutostart(text);
    } else if (command == "input-info" || command == "gesture" ||
               command == "list-commands") {
        // Diagnostics only: nothing here takes the keyboard, so running it
        // while the screen reader is running is safe.
        alus::input::InputServiceOptions inputOptions;
        inputOptions.noInput = command != "input-info";
        if (options.dataDir != nullptr) {
            inputOptions.dataDir = options.dataDir;
        }
        alus::input::Service input{loop, config, {}, inputOptions};
        input.initialize();
        if (command == "input-info") {
            status = commandInputInfo(input);
        } else if (command == "gesture") {
            status = commandGesture(input, text);
        } else {
            status = commandListCommands(input, text);
        }
        input.terminate();
    } else if (command == "help" || command == "--help") {
        usage();
        status = 0;
    } else {
        std::fprintf(stderr, "alus-cli: unknown command %s\n", command.c_str());
        usage();
        status = 2;
    }

    service.terminate();
    alus::audio::terminate();
    freeOptions();
    return status;
}
