// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T8.1 and the lookup around it: which application gets which module.
//
// §12.1's key is three things (executable, accessible name, toolkit) and the
// rules may match on any of them. The exit criterion names two of the paths
// explicitly -- "by the name of the executable and by the toolkit" -- because
// they are the two that matter: one program, or one family of programs.
#include "alus/app/key.hpp"
#include "alus/app/modules.hpp"
#include "alus/app/registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <unistd.h>

using namespace alus;
using namespace alus::app;

namespace {

// A module that records nothing but its own name, which is all a lookup test
// needs to know.
ModulePtr makeNamed(std::string name, const AppKey& key, const Services& services) {
    return std::make_unique<Module>(std::move(name), key, services);
}

Registry::Rule ruleFor(std::string name, std::string executable, std::string toolkit) {
    return Registry::Rule{
        name, std::move(executable), std::move(toolkit),
        [name](const AppKey& key, const Services& services) {
            return makeNamed(name, key, services);
        }};
}

AppKey keyOf(std::string executable, std::string toolkit, std::string name = {}) {
    AppKey key;
    key.executable = std::move(executable);
    key.toolkit = std::move(toolkit);
    key.name = std::move(name);
    // A live process, so that the module is not collected as dead mid-test.
    key.processId = static_cast<int>(::getpid());
    return key;
}

}  // namespace

TEST_CASE("[T8.1] a module is found by the name of the executable", "[app][lookup]") {
    Registry registry{Services{}};
    REQUIRE(registry.add(ruleFor("libreoffice", "soffice.bin", "")));

    Module* module = registry.moduleFor(keyOf("soffice.bin", "gail"));
    REQUIRE(module != nullptr);
    CHECK(module->moduleName() == "libreoffice");
    CHECK_FALSE(module->isGeneric());
    // The same application asked for twice is the same module: the running
    // table is keyed by process, as NVDA's is.
    CHECK(registry.moduleFor(keyOf("soffice.bin", "gail")) == module);
    CHECK(registry.moduleCount() == 1);
}

TEST_CASE("[T8.1] a module is found by the toolkit alone", "[app][lookup]") {
    Registry registry{Services{}};
    REQUIRE(registry.add(ruleFor("electron", "", "chromium")));

    // An executable nobody has heard of, on a toolkit ALUS knows. This is the
    // half NVDA's windowClassName cannot express and the reason §12.1 makes
    // the toolkit the discriminator.
    Module* module = registry.moduleFor(keyOf("some-unknown-app", "chromium"));
    REQUIRE(module != nullptr);
    CHECK(module->moduleName() == "electron");

    // And a different toolkit does not match it. A different process, too:
    // the running table is keyed by pid, so asking about this one again would
    // hand back the module it just made.
    AppKey elsewhere = keyOf("another-app", "gtk", "Another");
    elsewhere.processId = static_cast<int>(::getpid()) + 1;
    Module* other = registry.moduleFor(elsewhere);
    REQUIRE(other != nullptr);
    CHECK(other->isGeneric());
}

TEST_CASE("the more specific rule wins, and a tie goes to the newer one",
          "[app][lookup]") {
    Registry registry{Services{}};
    REQUIRE(registry.add(ruleFor("by-toolkit", "", "chromium")));
    REQUIRE(registry.add(ruleFor("by-executable", "code", "")));
    REQUIRE(registry.add(ruleFor("by-both", "code", "chromium")));

    // executable + toolkit beats either alone.
    const Registry::Rule* rule = registry.ruleFor(keyOf("code", "chromium"));
    REQUIRE(rule != nullptr);
    CHECK(rule->moduleName == "by-both");

    // The executable alone beats the toolkit alone: a rule for THIS program is
    // more specific than one for its whole family.
    rule = registry.ruleFor(keyOf("code", "gtk"));
    REQUIRE(rule != nullptr);
    CHECK(rule->moduleName == "by-executable");

    // A tie is broken by registration order, newest first -- which is what
    // lets a plugin override a built-in rule.
    REQUIRE(registry.add(ruleFor("newer-toolkit-rule", "", "chromium")));
    rule = registry.ruleFor(keyOf("something-else", "chromium"));
    REQUIRE(rule != nullptr);
    CHECK(rule->moduleName == "newer-toolkit-rule");
}

TEST_CASE("a rule that matches everything, or has no factory, is refused",
          "[app][lookup]") {
    Registry registry{Services{}};
    // A rule with neither an executable nor a toolkit would shadow the generic
    // module for every application on the desktop.
    CHECK_FALSE(registry.add(ruleFor("everything", "", "")));
    CHECK_FALSE(registry.add(Registry::Rule{"no-factory", "code", "", nullptr}));
    CHECK_FALSE(registry.add(ruleFor("", "code", "")));
    CHECK(registry.ruleCount() == 0);
}

TEST_CASE("an application with no rule still gets a module", "[app][lookup]") {
    Registry registry{Services{}};
    // The reason this matters: the profile trigger, the sleep mode and the
    // status line all hang off a module existing, and most applications have
    // no rule of their own.
    Module* module = registry.moduleFor(keyOf("gsd-power", "gtk", "gsd-power"));
    REQUIRE(module != nullptr);
    CHECK(module->isGeneric());
    CHECK(module->moduleName() == "gsd-power");
    // It answers to the base class, so a gesture map section written for every
    // app module reaches it.
    CHECK(module->answersTo(std::string{Module::kBaseClass}));
}

TEST_CASE("an application with no readable executable falls back to its name",
          "[app][lookup]") {
    Registry registry{Services{}};
    // A Flatpak application: /proc/<pid>/exe is in another namespace, so
    // `keyFor` puts the lower-cased accessible name in the executable slot.
    a11y::AppInfo info;
    info.name = "Sandboxed App";
    info.toolkitName = "GTK";
    info.processId = -1;  // nothing to read
    const AppKey key = keyFor(info);
    CHECK(key.executable == "sandboxed app");
    // The toolkit is lower-cased, because it is a lookup key. Measured reason:
    // Chrome reports "Chromium" with a capital C and GTK reports "gtk".
    CHECK(key.toolkit == "gtk");
    // A dead process gets no module at all -- there is nothing to adjust.
    CHECK(registry.moduleFor(key) == nullptr);
}

TEST_CASE("the executable aliases", "[app][lookup]") {
    // NVDA's EXECUTABLE_NAMES_TO_APP_MODS, as the Linux equivalents.
    CHECK(canonicalExecutable("soffice") == "soffice.bin");
    CHECK(canonicalExecutable("code-insiders") == "code");
    CHECK(canonicalExecutable("gnome-terminal") == "gnome-terminal-server");
    // Anything unknown comes back unchanged, so it is safe to call on
    // everything.
    CHECK(canonicalExecutable("nautilus") == "nautilus");

    Registry registry{Services{}};
    REQUIRE(registry.add(ruleFor("libreoffice", "soffice.bin", "")));
    // The alias is applied BEFORE the rules, so one rule serves every
    // spelling: `soffice`, `libreoffice` and `soffice.bin` are one program.
    CHECK(registry.moduleFor(keyOf("soffice", "gail"))->moduleName() == "libreoffice");

    // And a run-time alias wins over the built-in table, which is how a plugin
    // (or a user) corrects it.
    registry.addAlias("soffice", "something-else");
    CHECK(registry.resolveExecutable("soffice") == "something-else");
}

TEST_CASE("executableForPid reads /proc, and says nothing when it cannot",
          "[app][lookup]") {
    // This process: the one executable name a test can know.
    const std::string self = executableForPid(static_cast<int>(::getpid()));
    CHECK_FALSE(self.empty());
    CHECK(self.find("test-app") != std::string::npos);
    // pid 1 belongs to init, which this process may not read in a container;
    // either answer is correct, so the assertion is only that it does not
    // throw. A pid that cannot exist must come back empty rather than guessed.
    (void)executableForPid(1);
    CHECK(executableForPid(0).empty());
    CHECK(executableForPid(-1).empty());
}

TEST_CASE("the built-in rules are the applications phase 8 promises",
          "[app][lookup][modules]") {
    Registry registry{Services{}};
    const std::size_t added = registerBuiltinModules(registry);
    // The phase 8 table of phase-plan.md: LibreOffice, three terminals, Electron,
    // Nautilus -- and gnome-shell, added in phase 10 (phase-10.md N121). The
    // browsers are deliberately absent (§9.4).
    CHECK(added == 7);
    CHECK(registry.ruleCount() == 7);

    const auto moduleName = [&registry](const AppKey& key) -> std::string {
        const Registry::Rule* rule = registry.ruleFor(key);
        return rule != nullptr ? rule->moduleName : std::string{"(generic)"};
    };
    CHECK(moduleName(keyOf("soffice.bin", "gail")) == "libreoffice");
    CHECK(moduleName(keyOf("gnome-terminal-server", "gtk")) == "gnome-terminal");
    CHECK(moduleName(keyOf("konsole", "qt")) == "konsole");
    CHECK(moduleName(keyOf("nautilus", "gtk")) == "nautilus");
    CHECK(moduleName(keyOf("code", "chromium")) == "electron");
    CHECK(moduleName(keyOf("gnome-shell", "clutter")) == "gnome_shell");
    CHECK(moduleName(keyOf("firefox", "gtk")) == "(generic)");
}

TEST_CASE("the Electron rule declines for a browser", "[app][lookup][modules]") {
    Registry registry{Services{}};
    registerBuiltinModules(registry);

    // Measured on the target system: Google Chrome and VS Code report the
    // same toolkit string ("Chromium"), and must be treated oppositely -- a
    // browser keeps browse mode, an Electron application must not have it.
    Module* chrome =
        registry.moduleFor(keyOf("chrome", "chromium", "Google Chrome"));
    REQUIRE(chrome != nullptr);
    CHECK(chrome->isGeneric());

    // A second process, so that the running table does not hand back the
    // module it made for Chrome.
    AppKey code = keyOf("code", "chromium", "Code");
    code.processId = static_cast<int>(::getpid());
    // Same pid is the same application as far as the table is concerned, so
    // the test asks the rule rather than the table here.
    const Registry::Rule* rule = registry.ruleFor(code);
    REQUIRE(rule != nullptr);
    CHECK(rule->moduleName == "electron");
}

TEST_CASE("a module whose process is gone is collected", "[app][lookup]") {
    Registry registry{Services{}};
    REQUIRE(registry.add(ruleFor("ghost", "ghost-app", "")));

    AppKey key = keyOf("ghost-app", "gtk");
    // A pid that cannot be running: Linux pids are at most 2^22 by default,
    // and nothing can be alive at this one.
    key.processId = 0x7FFFFFF0;
    Module* module = registry.moduleFor(key);
    REQUIRE(module != nullptr);
    CHECK(registry.moduleCount() == 1);
    CHECK_FALSE(module->isAlive());

    CHECK(registry.cleanup() == 1);
    CHECK(registry.moduleCount() == 0);
}
