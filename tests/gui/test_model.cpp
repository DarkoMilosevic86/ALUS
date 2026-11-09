// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The settings window's contents, and T9.1.
//
// No display is needed for any of this, which is the point of splitting the
// model from the widgets (include/alus/gui/model.hpp says why). The two
// defects these catch are both invisible at build time:
//
//   * a row naming a configuration key nothing reads -- the user sees a switch
//     that does nothing, forever,
//   * a settings shortcut opening the wrong page, or silently opening nothing.

#include "alus/a11y/service.hpp"
#include "alus/braille/service.hpp"
#include "alus/config/config.hpp"
#include "alus/gui/model.hpp"
#include "alus/gui/service.hpp"
#include "alus/i18n/service.hpp"
#include "alus/input/service.hpp"
#include "alus/nav/service.hpp"
#include "alus/speech/service.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>

namespace {

// A manager with every subsystem's factory defaults registered, which is
// exactly the set of keys that EXIST. By unique_ptr because config::Manager is
// deliberately non-copyable -- two managers over one directory would each
// think they owned the file.
std::unique_ptr<alus::config::Manager> withDefaults(const std::filesystem::path& dir) {
    auto manager = std::make_unique<alus::config::Manager>(
        alus::config::Paths::forDirectory(dir));
    manager->setDefault("general", "logLevel", "info");
    alus::i18n::Service::registerDefaults(*manager);
    alus::speech::Service::registerDefaults(*manager);
    alus::input::Service::registerDefaults(*manager);
    alus::a11y::Service::registerDefaults(*manager);
    alus::nav::Service::registerDefaults(*manager);
    alus::braille::Service::registerDefaults(*manager);
    return manager;
}

}  // namespace

TEST_CASE("every settings row names a key that exists", "[gui]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "alus-gui-model-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::unique_ptr<alus::config::Manager> manager = withDefaults(dir);

    std::size_t checked = 0;
    for (const auto& [section, key] : alus::gui::allSettingKeys()) {
        INFO("row names " << section << "." << key
                          << ", which no subsystem registers a default for");
        // has() after only setDefault() answers "is this a key ALUS knows",
        // which is the question: a typo gives false here and a switch that
        // writes a setting nothing reads in the running screen reader.
        CHECK(manager->has(section, key));
        ++checked;
    }
    CHECK(checked > 70);
    std::filesystem::remove_all(dir);
}

TEST_CASE("the pages are well formed", "[gui]") {
    const std::vector<alus::gui::Page>& pages = alus::gui::pages();
    REQUIRE(pages.size() >= 7);

    std::set<std::string> ids;
    std::set<std::pair<std::string, std::string>> keys;
    for (const alus::gui::Page& page : pages) {
        INFO("page " << page.id);
        CHECK_FALSE(page.id.empty());
        CHECK(page.title != nullptr);
        // A duplicate id would make a shortcut open whichever page came first.
        CHECK(ids.insert(page.id).second);
        CHECK_FALSE(page.groups.empty());

        for (const alus::gui::Group& group : page.groups) {
            CHECK_FALSE(group.rows.empty());
            for (const alus::gui::Row& row : group.rows) {
                INFO("row " << (row.title != nullptr ? row.title : "(no title)"));
                CHECK(row.title != nullptr);
                if (row.kind == alus::gui::RowKind::Action) {
                    CHECK_FALSE(row.action.empty());
                    CHECK(row.section.empty());
                    continue;
                }
                CHECK_FALSE(row.section.empty());
                CHECK_FALSE(row.key.empty());
                // The same key twice would give two switches that fight.
                CHECK(keys.insert({row.section, row.key}).second);
                if (row.kind == alus::gui::RowKind::Choice) {
                    CHECK(row.choices.size() >= 2);
                    std::set<std::string> values;
                    for (const alus::gui::Choice& choice : row.choices) {
                        CHECK(choice.label != nullptr);
                        // An empty VALUE is legal (it means "follow the
                        // session" for the language); a duplicate is not.
                        CHECK(values.insert(choice.value).second);
                    }
                } else if (row.kind == alus::gui::RowKind::Integer) {
                    CHECK(row.minimum < row.maximum);
                    CHECK(row.step > 0);
                }
            }
        }
    }
    // The six pages 02-FAZE.md names for phase 9, plus general.
    for (const char* id : {"general", "speech", "braille", "keyboard", "presentation",
                           "formatting", "gestures"}) {
        INFO("missing page: " << id);
        CHECK(alus::gui::findPage(id) != nullptr);
    }
    CHECK(alus::gui::findPage("nonesuch") == nullptr);
}

TEST_CASE("T9.1 — every settings shortcut names a page or says it has none",
          "[gui][T9.1]") {
    // The criterion is "alus+control+g and the other shortcuts open the right
    // settings". The mapping from script name to page is data
    // (gui::scriptPages), so it is checkable without a display; what the
    // shortcut does with it is checked below.
    const std::vector<alus::gui::ScriptPage>& table = alus::gui::scriptPages();
    REQUIRE(table.size() >= 15);

    // The shortcut 02-FAZE.md names by hand.
    CHECK(alus::gui::pageForScript("activateGeneralSettingsDialog") == "general");

    // And the rest of the keys the generated command table binds.
    CHECK(alus::gui::pageForScript("activateVoiceDialog") == "speech");
    CHECK(alus::gui::pageForScript("activateSynthesizerDialog") == "speech");
    CHECK(alus::gui::pageForScript("activateBrailleDisplayDialog") == "braille");
    CHECK(alus::gui::pageForScript("activateKeyboardSettingsDialog") == "keyboard");
    CHECK(alus::gui::pageForScript("activateObjectPresentationDialog") == "presentation");
    CHECK(alus::gui::pageForScript("activateDocumentFormattingDialog") == "formatting");
    CHECK(alus::gui::pageForScript("activateInputGesturesDialog") == "gestures");
    // `showGui` is upstream's "open the menu", and ALUS has no menu: the key
    // opens this window instead of saying there is no such panel. Its key is
    // ALUS's own (data/gestures-alus.ini: ALUS+a in the desktop layout), so
    // this is the only test that says the key leads anywhere at all.
    CHECK(alus::gui::pageForScript("showGui") == "general");

    // A script this version has no page for must map to EMPTY and not to the
    // nearest page: alus+control+m opening the keyboard settings because there
    // are no mouse settings would be worse than it saying there are none.
    CHECK(alus::gui::pageForScript("activateMouseSettingsDialog").empty());
    CHECK(alus::gui::pageForScript("activateAudioSettingsDialog").empty());

    // Nothing in the table may name a page that does not exist. This is the
    // check that catches a page being renamed and a shortcut being left
    // behind -- which would open nothing, silently.
    for (const alus::gui::ScriptPage& entry : table) {
        INFO(entry.script << " names page '" << entry.page << "'");
        CHECK_FALSE(entry.script.empty());
        if (!entry.page.empty()) {
            CHECK(alus::gui::findPage(entry.page) != nullptr);
        }
    }

    // An unknown script name gets no page rather than a wrong one.
    CHECK(alus::gui::pageForScript("thereIsNoSuchScript").empty());
}

TEST_CASE("T9.1 — a shortcut with no page reports it rather than doing nothing",
          "[gui][T9.1]") {
    // §3.8, and the four findings that made it a rule: a screen reader that
    // does nothing is indistinguishable from one whose key never arrived. So
    // the service SAYS what happened, and that is what is asserted here --
    // with no display, because openByScript reports through the hook before
    // any window is touched.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "alus-gui-shortcut-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::unique_ptr<alus::config::Manager> manager = withDefaults(dir);

    std::vector<std::string> spoken;
    alus::gui::Service::Hooks hooks;
    hooks.message = [&spoken](const std::string& text) { spoken.push_back(text); };

    alus::gui::ServiceOptions options;
    // --no-gui, so this test never tries to open a window even on a developer's
    // own desktop, where it would succeed and leave one on screen.
    options.noGui = true;
    alus::gui::Service service{*manager, nullptr, nullptr, nullptr, hooks, options};
    service.initialize();

    CHECK_FALSE(service.available());
    CHECK_FALSE(service.unavailableReason().empty());

    // A page this version does not have.
    service.openByScript("activateMouseSettingsDialog");
    REQUIRE(spoken.size() == 1);
    CHECK(spoken.front().find("no such settings") != std::string::npos);

    // A page it does have, with the GUI switched off: a DIFFERENT message,
    // because "there are no such settings" and "the window cannot open" are
    // different problems and the user has to be able to tell them apart.
    spoken.clear();
    service.openByScript("activateGeneralSettingsDialog");
    REQUIRE(spoken.size() == 1);
    CHECK(spoken.front().find("not available") != std::string::npos);

    CHECK(service.currentPage().empty());
    service.terminate();
    std::filesystem::remove_all(dir);
}

TEST_CASE("the settings window writes nothing until something changes", "[gui]") {
    // Opening the settings window must not rewrite the configuration file.
    // A screen reader that rewrites ~/.config/alus/alus.ini every time the
    // user looks at the settings turns every default into an explicit value,
    // and the user then stops inheriting improved defaults forever.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "alus-gui-clean-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        const std::unique_ptr<alus::config::Manager> manager = withDefaults(dir);
        REQUIRE(manager->load());
        // Reading every key the window shows, which is what building it does.
        for (const auto& [section, key] : alus::gui::allSettingKeys()) {
            (void)manager->getString(section, key);
        }
        std::string error;
        REQUIRE(manager->save(&error));
    }
    const std::filesystem::path file = dir / "alus.ini";
    // Either no file at all, or one that holds no [presentation] section --
    // in both cases nothing the user did not ask for was written down.
    if (std::filesystem::exists(file)) {
        std::ifstream stream{file};
        const std::string text{std::istreambuf_iterator<char>(stream),
                               std::istreambuf_iterator<char>()};
        // One line: a multi-line INFO breaks meson's TAP parser even when
        // every assertion passes (see oneLine() in test_gestures.cpp).
        std::string flat;
        for (const char ch : text) {
            flat += ch == '\n' ? std::string{"\\n"} : std::string(1, ch);
        }
        INFO(flat);
        CHECK(text.find("[presentation]") == std::string::npos);
    }
    std::filesystem::remove_all(dir);
}
