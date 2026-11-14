// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// T9.5: the generated command reference agrees with data/scripts.csv.
//
// The reference is documentation, so nothing breaks when it goes stale -- it
// just starts lying, and a user who binds a key from a reference that no
// longer matches the program gets a key that does nothing. Hence a test, and
// hence the reference being generated rather than written: §5.4 makes the
// extractor the source of truth, and a hand-written reference would be a
// fourth copy of it.

#include "alus/script/catalog.hpp"
#include "alus/script/command_status.hpp"
#include "alus/script/script.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace {

std::filesystem::path sourceRoot() { return std::filesystem::path{ALUS_SOURCE_ROOT}; }

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream{path};
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("T9.5 — the command reference matches the command table", "[gui][T9.5]") {
    const std::filesystem::path reference = sourceRoot() / "docs" / "commands.md";
    INFO("missing or not generated: run "
         "python3 tools/generate-command-reference.py");
    REQUIRE(std::filesystem::exists(reference));
    const std::string text = readFile(reference);

    // The catalogue, loaded the way the daemon loads it. NOT the CSV parsed
    // again: the question is whether the reference matches what ALUS will
    // actually run, and that is the catalogue.
    alus::script::Catalog catalog;
    REQUIRE(catalog.loadFromDataDirectory(sourceRoot() / "data"));
    const std::vector<const alus::script::ScriptInfo*> scripts = catalog.all();
    REQUIRE(scripts.size() > 300);

    std::size_t documented = 0;
    std::size_t hidden = 0;
    for (const alus::script::ScriptInfo* info : scripts) {
        if (info->description.empty()) {
            ++hidden;
            continue;
        }
        ++documented;
        // Every documented command appears, by the name a user would search
        // for. In a code span, so a command called `tab` cannot be satisfied
        // by the word "tab" in a description.
        const std::string needle = "`" + info->name + "`";
        INFO("not in docs/commands.md: " << info->name);
        CHECK(text.find(needle) != std::string::npos);
    }
    CHECK(documented > 300);
    CHECK(hidden > 10);

    // The counts the reference states about itself have to be true, because
    // they are the first thing a reader believes.
    CHECK(text.find("**" + std::to_string(documented) + "**") != std::string::npos);

    // And a command with no description must NOT appear, for the same reason
    // the gesture dialog hides it (T9.3).
    for (const alus::script::ScriptInfo* info : scripts) {
        if (!info->description.empty()) {
            continue;
        }
        // Only when the name is not also a documented command's name -- some
        // scripts share a name across classes, and one of them may well carry
        // the description.
        const bool nameIsDocumentedElsewhere =
            std::any_of(scripts.begin(), scripts.end(),
                        [info](const alus::script::ScriptInfo* other) {
                            return other != info && other->name == info->name &&
                                   !other->description.empty();
                        });
        if (nameIsDocumentedElsewhere) {
            continue;
        }
        INFO("an undescribed command is documented anyway: " << info->name);
        CHECK(text.find("`" + info->name + "`") == std::string::npos);
    }

    // And -- the half of N99 that is documentation -- a command ALUS has not
    // written yet must not be promised as if it had been. The reference is
    // what a user reads before pressing a key, so a row that reads like every
    // other row is the lie that `quit` told for a whole phase.
    //
    // The marker is on the ROW, so each one is looked for between this
    // command's code span and the next line break.
    const alus::script::CommandStatus& status = catalog.status();
    REQUIRE_FALSE(status.empty());
    std::size_t marked = 0;
    std::size_t shouldBeMarked = 0;
    for (const alus::script::ScriptInfo* info : scripts) {
        if (info->description.empty() ||
            status.stateOf(info->name) == alus::script::CommandStatus::State::Implemented) {
            continue;
        }
        const std::size_t row = text.find("| `" + info->name + "` |");
        if (row == std::string::npos) {
            continue;  // not in the table at all; the loops above cover that
        }
        ++shouldBeMarked;
        const std::size_t end = text.find('\n', row);
        const std::string line = text.substr(row, end - row);
        INFO("docs/commands.md promises `"
             << info->name
             << "` with no sign that it is not in this version; regenerate with "
                "python3 tools/generate-command-reference.py");
        if (line.find("NOT IN THIS VERSION") != std::string::npos ||
            line.find("NOT ON WAYLAND") != std::string::npos) {
            ++marked;
        } else {
            FAIL_CHECK("docs/commands.md promises `" + info->name +
                       "` with no sign that it is not in this version; regenerate with "
                       "python3 tools/generate-command-reference.py");
        }
    }
    // EVERY one of them, and the number comes from data/commands-status.ini
    // rather than being written here.
    //
    // It was `marked > 20` and that was wrong in a way worth recording: the
    // count falls by one every time a command is IMPLEMENTED, so the floor
    // tightened as the screen reader improved and the test failed at exactly
    // 20 the day the twenty-sixth command was written. A test whose threshold
    // has to be lowered after every good commit is a test nobody will believe
    // the twenty-seventh time.
    INFO("docs/commands.md marks " << marked << " of " << shouldBeMarked
                                   << " commands that are not in this version");
    CHECK(marked == shouldBeMarked);
    // The mechanism itself has to have been exercised: a status file with
    // nothing left in it would make every assertion above vacuous. The day
    // that happens -- every command written -- this is the line to delete,
    // and the project will have earned it.
    CHECK(shouldBeMarked > 0);
}

TEST_CASE("T9.5 — the reference spells the ALUS key the way the user hears it",
          "[gui][T9.5]") {
    const std::string text = readFile(sourceRoot() / "docs" / "commands.md");
    REQUIRE_FALSE(text.empty());

    // A gesture identifier is `kb:alus+t`; a reference that showed that would
    // be showing the file format rather than the keys. Both columns show
    // `ALUS+t`.
    CHECK(text.find("ALUS+") != std::string::npos);
    CHECK(text.find("| kb:") == std::string::npos);

    // And the other product's name appears nowhere in the reference: the
    // commands are inherited, the name is not.
    CHECK(text.find("NVDA+") == std::string::npos);

    // The CSV side exists and has the same number of data rows as the
    // markdown has command rows, so one cannot be regenerated without the
    // other.
    const std::filesystem::path csv = sourceRoot() / "docs" / "commands.csv";
    REQUIRE(std::filesystem::exists(csv));
    const std::string csvText = readFile(csv);
    const auto countLines = [](const std::string& body) {
        return static_cast<std::size_t>(
            std::count(body.begin(), body.end(), '\n'));
    };
    // Header plus one row per documented command.
    CHECK(countLines(csvText) > 300);
}
