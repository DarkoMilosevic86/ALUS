// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 8 fixture: the phase 4 mock platform, plus a real app layer on
// top of it.
//
// WHY THE REAL SERVICE AND NOT A HAND-WIRED REGISTRY. Every one of the seven
// exit criteria is a statement about a seam between the app layer and a layer
// below it -- the overlay stages, the event chain, the focus hooks, the
// profile switch. A test that called `Module::chooseOverlayClasses` directly
// would assert that the module works and prove nothing about whether it is
// ever asked. So the fixture installs `app::Service` against the mock
// platform, and the tests drive the platform.
//
// WHAT CANNOT BE FAKED, and how the tests work around it: the lookup key's
// primary component is `basename(/proc/<pid>/exe)`, which for a test process
// is always the test binary. So:
//
//   * the executable half of the lookup (T8.1) is tested by handing
//     `Registry::moduleFor` an `AppKey` directly, which is what `keyFor` would
//     have produced on a real desktop;
//   * everything that must go through the whole service uses a rule keyed on
//     the TOOLKIT, which the mock platform can report freely.
//
// That split is not a workaround for a weak design: a toolkit-keyed rule is
// the thing §12.1 adds over NVDA, and it is what a test should exercise.
#pragma once

#include "alus/a11y/registry.hpp"
#include "alus/app/modules.hpp"
#include "alus/app/service.hpp"
#include "support.hpp"  // tests/a11y/support.hpp -- the mock platform fixture

#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace alus::testing {

// A configuration directory per test, so that nothing here touches the
// developer's own ~/.config/alus.
class TempConfigDir {
public:
    TempConfigDir(std::string label = "app") {
        static int counter = 0;
        dir_ = std::filesystem::temp_directory_path() /
               ("alus-" + label + "-test-" + std::to_string(++counter) + "-" +
                std::to_string(::getpid()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_ / "profiles");
    }

    ~TempConfigDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempConfigDir(const TempConfigDir&) = delete;
    TempConfigDir& operator=(const TempConfigDir&) = delete;

    const std::filesystem::path& dir() const noexcept { return dir_; }
    config::Paths paths() const { return config::Paths::forDirectory(dir_); }

private:
    std::filesystem::path dir_;
};

class AppFixture : public A11yFixture {
public:
    // `withBuiltinModules` is off by default: a test that asserts on a lookup
    // wants the rules it registered and no others.
    explicit AppFixture(bool withBuiltinModules = false)
        : config(tempDir.paths()) {
        app::Service::registerDefaults(config);
        service = std::make_unique<app::Service>(
            config, [this] { return &registry; }, [] { return nullptr; }, nullptr,
            [this](const std::string& text) { said.push_back(text); });
        service->initialize();
        if (withBuiltinModules) {
            app::registerBuiltinModules(*service->modules());
        }
    }

    ~AppFixture() {
        // Before the mock platform goes: a module's terminate may ask about
        // its application.
        service.reset();
    }

    app::Registry& modules() { return *service->modules(); }
    app::PluginHost& plugins() { return *service->plugins(); }

    // Gives an application node an identity the app layer can key on. The
    // process id is this process by default, so that `Module::isAlive` is true
    // and `Registry::cleanup` does not collect the module mid-test.
    a11y::Ref addApplication(std::string name, std::string toolkit, int processId = 0) {
        const a11y::Ref ref = backend.addApplication(std::move(name), std::move(toolkit));
        backend.node(ref).app.processId =
            processId != 0 ? processId : static_cast<int>(::getpid());
        return ref;
    }

    TempConfigDir tempDir;
    config::Manager config;
    std::unique_ptr<app::Service> service;
    std::vector<std::string> said;
};

}  // namespace alus::testing
