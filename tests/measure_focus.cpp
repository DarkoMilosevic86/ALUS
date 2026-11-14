// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// measure-focus: what one focus announcement costs on the LIVE desktop, call
// by call.
//
//     ./build/tests/measure-focus [noapp|nonav|nobraille]
//
// It assembles the daemon's layers the way src/daemon/main.cpp does, but
// silent (NullSynth) and without the keyboard, takes whatever has the focus
// right now, and replays a focus event for it three times: each time the
// focus first moves to the object's parent, so the replay is a move inside one
// window, which is what an arrow key in a list is. It prints the AT-SPI calls
// each replay made, most frequent first.
//
// IT CHANGES NOTHING on the desktop: no key is taken, nothing is spoken, no
// focus is moved in any application; the events are synthesised inside this
// process. Safe to run while ALUS or another screen reader is running.
//
// This is the tool that found N129: ~300 calls per arrow in Nautilus, half of
// them the application's name and toolkit asked again for every object. The
// argument switches one layer off, to see what that layer costs.
#include "alus/a11y/registry.hpp"
#include "alus/a11y/service.hpp"
#include "alus/app/service.hpp"
#include "alus/braille/service.hpp"
#include "alus/config/config.hpp"
#include "alus/core/cached.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace alus;

namespace {

using CallCounts = std::vector<std::pair<std::string, std::uint64_t>>;

CallCounts spentBetween(const CallCounts& before, const CallCounts& after) {
    CallCounts spent;
    for (const auto& [name, count] : after) {
        const auto old = std::find_if(before.begin(), before.end(),
                                      [&name](const auto& entry) { return entry.first == name; });
        const std::uint64_t was = old == before.end() ? 0 : old->second;
        if (count > was) {
            spent.emplace_back(name, count - was);
        }
    }
    std::sort(spent.begin(), spent.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return spent;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string off = argc > 1 ? argv[1] : "";
    // A configuration of its own: the defaults, never the user's settings.
    const std::filesystem::path configDir =
        std::filesystem::temp_directory_path() / "alus-measure-focus";
    std::filesystem::create_directories(configDir);
    config::Manager manager{config::Paths::forDirectory(configDir)};
    speech::Service::registerDefaults(manager);
    a11y::Service::registerDefaults(manager);
    nav::Service::registerDefaults(manager);
    braille::Service::registerDefaults(manager);
    app::Service::registerDefaults(manager);
    manager.load(nullptr);
    core::Log::instance().setLevel(core::LogLevel::Warning);

    core::Loop loop;
    speech::ServiceOptions speechOptions;
    speechOptions.noSpeech = true;
    speechOptions.enableAudio = false;
    speech::Service speechService{loop, manager, speechOptions};
    speechService.initialize();
    a11y::Service a11yService{loop, manager, nullptr,
                              [&] { return speechService.presenter(); }, {}};
    a11yService.initialize();
    nav::NavServiceOptions navOptions;
    navOptions.noNav = off == "nonav";
    nav::Service navService{loop, manager, [&] { return a11yService.registry(); }, nullptr,
                            [&] { return speechService.presenter(); }, navOptions};
    navService.initialize();
    braille::ServiceOptions brailleOptions;
    brailleOptions.noBraille = off == "nobraille";
    braille::Service brailleService{loop, manager, [&] { return a11yService.registry(); },
                                    [&] { return &navService; }, nullptr,
                                    [&] { return speechService.presenter(); }, brailleOptions};
    brailleService.initialize();
    app::ServiceOptions appOptions;
    appOptions.noApp = off == "noapp";
    app::Service appService{manager, [&] { return a11yService.registry(); },
                            [&] { return &navService; }, nullptr, [](const std::string&) {},
                            appOptions};
    appService.initialize();

    int status = 0;
    a11y::Registry* registry = a11yService.registry();
    a11y::ObjectPtr focus = registry != nullptr ? registry->api().focus() : nullptr;
    if (focus == nullptr) {
        std::puts("measure-focus: nothing has the focus (is the accessibility bus running?)");
        status = 1;
    } else {
        a11y::Backend& backend = registry->backend();
        const a11y::Ref ref = backend.addRef(focus->ref());
        std::printf("focus: %s\n", focus->name().c_str());
        focus.reset();
        for (int round = 0; round < 3; ++round) {
            // Move the focus to the parent first, so the replay is a move
            // inside the window and not the first focus of a session.
            registry->releaseCycleObjects();
            core::invalidateAllCaches();
            if (const a11y::ObjectPtr object = registry->objectFor(ref)) {
                const a11y::ObjectPtr parent = object->parent();
                registry->api().setFocusObject(parent != nullptr ? parent : registry->desktop());
            }
            registry->releaseCycleObjects();
            core::invalidateAllCaches();

            const CallCounts before = backend.callsByName();
            const std::uint64_t callsBefore = backend.callCount();
            const auto started = std::chrono::steady_clock::now();
            a11y::PlatformEvent event;
            event.type = "object:state-changed:focused";
            event.source = backend.addRef(ref);
            event.detail1 = 1;
            event.application = 1;
            registry->onPlatformEvent(std::move(event));
            registry->pumpEvents();
            const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);

            std::printf("round %d: %llu calls, %lld ms:", round,
                        static_cast<unsigned long long>(backend.callCount() - callsBefore),
                        static_cast<long long>(took.count()));
            for (const auto& [name, count] : spentBetween(before, backend.callsByName())) {
                std::printf(" %s×%llu", name.c_str(), static_cast<unsigned long long>(count));
            }
            std::printf("\n");
        }
        backend.release(ref);
    }

    appService.terminate();
    brailleService.terminate();
    navService.terminate();
    a11yService.terminate();
    speechService.terminate();
    return status;
}
