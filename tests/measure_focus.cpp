// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// measure-focus: what one focus announcement costs on the LIVE desktop, call
// by call.
//
//     ./build/tests/measure-focus [noapp|nonav|nobraille]
//     ./build/tests/measure-focus elements
//     ./build/tests/measure-focus webfocus
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
//
// `elements` measures the elements list (alus+f7) instead: it finds the first
// showing web document of a running browser -- not the focus, so it can be
// run from a terminal -- and gathers each type's entries the way
// BrowseMode::showElementsList does, printing the time and calls per type.
// `webfocus` replays the focus as above, but on the twentieth link of that
// document rather than on the focus.
#include "alus/a11y/registry.hpp"
#include "alus/a11y/service.hpp"
#include "alus/app/service.hpp"
#include "alus/braille/service.hpp"
#include "alus/config/config.hpp"
#include "alus/core/cached.hpp"
#include "alus/core/log.hpp"
#include "alus/core/loop.hpp"
#include "alus/nav/browse_mode.hpp"
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

void printSpent(const CallCounts& before, const CallCounts& after) {
    for (const auto& [name, count] : spentBetween(before, after)) {
        std::printf(" %s×%llu", name.c_str(), static_cast<unsigned long long>(count));
    }
    std::printf("\n");
}

// The first showing web document, breadth first under the browsers.
a11y::ObjectPtr findWebDocument(a11y::Registry& registry) {
    std::vector<a11y::ObjectPtr> queue;
    if (const a11y::ObjectPtr desktop = registry.desktop()) {
        for (const a11y::ObjectPtr& app : desktop->children()) {
            if (app == nullptr) {
                continue;
            }
            const std::string name = app->name();
            if (name.find("hrom") != std::string::npos || name.find("irefox") != std::string::npos) {
                queue.push_back(app);
            }
        }
    }
    for (std::size_t index = 0; index < queue.size() && index < 4000; ++index) {
        const a11y::ObjectPtr object = queue[index];
        if (object->role() == a11y::Role::Document &&
            nav::BrowseMode::isDocumentRoot(*object) &&
            !object->states().has(a11y::State::Invisible)) {
            return object;
        }
        for (const a11y::ObjectPtr& child : object->children()) {
            if (child != nullptr) {
                queue.push_back(child);
            }
        }
    }
    return nullptr;
}

int measureElements(a11y::Registry& registry, nav::Service& navService) {
    const a11y::ObjectPtr root = findWebDocument(registry);
    if (root == nullptr) {
        std::puts("measure-focus: no showing web document in a running browser");
        return 1;
    }
    std::printf("document: %s\n", root->name().c_str());
    nav::TreeInterceptorPtr interceptor = nav::BrowseMode::chooseForDocument(root, navService);
    auto* browse = dynamic_cast<nav::BrowseMode*>(interceptor.get());
    if (browse == nullptr) {
        std::puts("measure-focus: no browse mode for it");
        return 1;
    }
    a11y::Backend& backend = registry.backend();
    const std::pair<const char*, nav::ElementType> kinds[] = {
        {"link", nav::ElementType::Link},         {"heading", nav::ElementType::Heading},
        {"formField", nav::ElementType::FormField}, {"button", nav::ElementType::Button},
        {"landmark", nav::ElementType::Landmark},
    };
    for (int round = 0; round < 2; ++round) {
        registry.releaseCycleObjects();
        core::invalidateAllCaches();
        const auto roundStarted = std::chrono::steady_clock::now();
        const std::uint64_t roundCalls = backend.callCount();
        for (const auto& [id, type] : kinds) {
            const CallCounts before = backend.callsByName();
            const std::uint64_t callsBefore = backend.callCount();
            const auto started = std::chrono::steady_clock::now();
            std::vector<a11y::ObjectPtr> objects;
            for (nav::ElementListItem& item : browse->elementsList(type, 0)) {
                if (type != nav::ElementType::Link && type != nav::ElementType::Heading) {
                    (void)a11y::roleLabel(item.object->role());
                }
                objects.push_back(std::move(item.object));
            }
            const auto listed = std::chrono::steady_clock::now();
            (void)browse->initialElementIndex(type, objects);
            const auto took = [](auto from, auto to) {
                return static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count());
            };
            std::printf("round %d %-9s %4zu entries, %4lld ms (+%lld ms initial), %llu calls:",
                        round, id, objects.size(), took(started, listed),
                        took(listed, std::chrono::steady_clock::now()),
                        static_cast<unsigned long long>(backend.callCount() - callsBefore));
            printSpent(before, backend.callsByName());
        }
        std::printf("round %d total: %lld ms, %llu calls\n", round,
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - roundStarted)
                                               .count()),
                    static_cast<unsigned long long>(backend.callCount() - roundCalls));
    }
    return 0;
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
    if (off == "elements" && registry != nullptr) {
        status = measureElements(*registry, navService);
    } else {
    a11y::ObjectPtr focus = registry != nullptr ? registry->api().focus() : nullptr;
    if (off == "webfocus" && registry != nullptr) {
        focus.reset();
        if (const a11y::ObjectPtr root = findWebDocument(*registry)) {
            const nav::QuickNav quickNav{root};
            const std::vector<a11y::ObjectPtr> links = quickNav.all(nav::ElementType::Link, 20);
            if (!links.empty()) {
                focus = links.back();
            }
        }
    }
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
    }

    appService.terminate();
    brailleService.terminate();
    navService.terminate();
    a11yService.terminate();
    speechService.terminate();
    return status;
}
