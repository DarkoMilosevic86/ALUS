// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The commands that change speech and save the configuration, run through the
// same dispatcher the daemon uses.
//
// WHY THESE ASSERTIONS AND NOT OTHERS. tests/coverage asks whether every
// shipped key FINDS a command; it never runs one. This file runs them, and
// what it is checking is the part a blind user experiences directly:
//
//   * every one of them SAYS something, every time, including when it cannot
//     do what was asked (claude/FAZA6.md O10: an unfinished feature a user
//     can hear is a different thing from a broken one),
//   * the value they announce is the value they actually set, and
//   * the change reaches the configuration -- but NOT the disk, until
//     `alus+control+c` is pressed, which is the whole reason
//     `alus+control+r` can undo it.
#include "alus/config/commands.hpp"
#include "alus/speech/global_commands.hpp"

#include "alus/config/config.hpp"
#include "alus/input/gesture.hpp"
#include "alus/input/gesture_map.hpp"
#include "alus/script/dispatch.hpp"
#include "alus/speech/dictionaries.hpp"
#include "alus/speech/manager.hpp"
#include "alus/speech/presenter.hpp"
#include "alus/speech/settings_ring.hpp"
#include "alus/speech/synth.hpp"
#include "alus/text/symbols.hpp"

#include "input/support.hpp"
#include "support/loop_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace alus;
using namespace std::chrono_literals;

namespace {

// The whole speech layer below the commands, with a NullSynth at the bottom.
// Built the way the daemon builds it so that what the commands change is the
// same object the rest of ALUS would read.
class SpeechStack : public testing::LoopFixture {
public:
    SpeechStack()
        : symbols_(std::filesystem::path{ALUS_TEST_DATA_DIR} / "symbols"),
          manager_(loop()),
          presenter_(manager_, symbols_, dictionaries_),
          ring_(&synth_, &config_) {
        manager_.setSynth(&synth_);
        manager_.setLostIndexGrace(0ms);
        manager_.setStallTimeout(0ms);
        speech::Presenter::Settings settings;
        settings.language = "en";
        settings.symbolLevel = text::SymbolLevel::Some;
        presenter_.setSettings(settings);
    }

    speech::Presenter& presenter() noexcept { return presenter_; }
    speech::SettingsRing& ring() noexcept { return ring_; }
    speech::NullSynth& synth() noexcept { return synth_; }
    config::Manager& config() noexcept { return config_; }

private:
    testing::QuietLog quiet_;
    config::Manager config_{config::Paths::forDirectory(
        std::filesystem::temp_directory_path() / "alus-speech-commands-test")};
    text::SymbolRegistry symbols_;
    speech::Dictionaries dictionaries_;
    speech::NullSynth synth_{true};
    speech::SpeechManager manager_;
    speech::Presenter presenter_;
    speech::SettingsRing ring_;
};

// Runs a command by NAME through the real dispatcher, so repeat counting is
// the dispatcher's and not the test's.
class Runner {
public:
    // The object is attached AFTER construction on purpose: a command that
    // counts repeat presses needs the dispatcher, and the dispatcher lives
    // here -- so one of the two has to be built first.
    explicit Runner(core::Loop* loop) : dispatcher_(loop) {
        dispatcher_.setMultiPressTimeout(2s);
        dispatcher_.setBuiltinMap(&map_);
        dispatcher_.setContextProvider([this](script::DispatchContext& context) {
            if (object_ != nullptr) {
                context.globalCommands = {object_};
            }
        });
    }

    Runner(core::Loop* loop, script::ScriptableObject* object) : Runner(loop) {
        use(object);
    }

    void use(script::ScriptableObject* object) { object_ = object; }

    // Each command gets a key of its own, as the built-in map gives it one.
    void bind(const std::string& script, const std::string& key) {
        map_.add(key, "globalCommands.GlobalCommands", script);
        keys_[script] = key;
    }

    // One press. Returns what was said, which every command must have done.
    void press(const std::string& script) {
        const auto it = keys_.find(script);
        REQUIRE(it != keys_.end());
        const std::shared_ptr<input::KeyboardGesture> gesture = gestureFor(it->second);
        REQUIRE(gesture != nullptr);
        const std::optional<script::Script> found = dispatcher_.findScript(*gesture);
        INFO("`" << script << "` was not found by the lookup at all");
        REQUIRE(found.has_value());
        dispatcher_.executeScript(*found, *gesture);
    }

    void press(const std::string& script, int times) {
        for (int i = 0; i < times; ++i) {
            press(script);
        }
    }

    script::Dispatcher& dispatcher() noexcept { return dispatcher_; }

private:
    // `kb:alus+<letter>`, built the way tests/input builds one.
    static std::shared_ptr<input::KeyboardGesture> gestureFor(const std::string& key) {
        const char letter = key.back();
        input::KeyboardGesture::Init init;
        init.event = testing::keyDown(static_cast<std::uint32_t>(letter));
        init.modifiers = static_cast<input::Modifiers>(input::Modifier::Alus);
        init.config.layout = input::Layout::Desktop;
        return std::make_shared<input::KeyboardGesture>(std::move(init));
    }

    script::Dispatcher dispatcher_;
    script::ScriptableObject* object_ = nullptr;
    input::GestureMap map_{"built-in"};
    std::map<std::string, std::string> keys_;
};

// Every command announces through the context's `message`, which is what the
// daemon points at ui::message. Collecting it here is the same seam.
struct Said {
    std::vector<std::string> lines;

    std::function<void(const std::string&)> sink() {
        return [this](const std::string& text) { lines.push_back(text); };
    }

    const std::string& last() const {
        REQUIRE_FALSE(lines.empty());
        return lines.back();
    }
};

}  // namespace

TEST_CASE("alus+s cycles the speech mode and says which one", "[commands][speech]") {
    SpeechStack stack;
    Said said;
    speech::GlobalCommandsContext context;
    context.presenter = [&stack] { return &stack.presenter(); };
    context.ring = [&stack] { return &stack.ring(); };
    context.config = &stack.config();
    context.message = said.sink();
    speech::GlobalCommands commands{std::move(context), nullptr};

    Runner runner{&stack.loop(), &commands};
    runner.bind("speechMode", "kb:alus+s");

    REQUIRE(stack.presenter().mode() == speech::SpeechMode::Talk);

    runner.press("speechMode");
    CHECK(stack.presenter().mode() == speech::SpeechMode::OnDemand);
    CHECK(said.last() == "Speech mode on-demand");
    CHECK(stack.config().getString("speech", "speechMode") == "onDemand");

    runner.press("speechMode");
    CHECK(stack.presenter().mode() == speech::SpeechMode::Off);
    CHECK(said.last() == "Speech mode off");

    runner.press("speechMode");
    CHECK(stack.presenter().mode() == speech::SpeechMode::Beeps);
    CHECK(said.last() == "Speech mode beeps");

    runner.press("speechMode");
    CHECK(stack.presenter().mode() == speech::SpeechMode::Talk);
    CHECK(said.last() == "Speech mode talk");

    SECTION("the way out of `off` is audible, which is the point of the order") {
        // Leaving `off` has to be heard, so the announcement is made with
        // speech switched back on and BEFORE the new mode is applied. Four
        // presses got back to Talk above; from Off the next press must both
        // announce and arrive at Beeps.
        runner.press("speechMode", 2);  // talk -> onDemand -> off
        REQUIRE(stack.presenter().mode() == speech::SpeechMode::Off);
        const std::size_t before = said.lines.size();
        runner.press("speechMode");
        CHECK(said.lines.size() == before + 1);
        CHECK(said.last() == "Speech mode beeps");
    }
}

TEST_CASE("alus+p cycles the punctuation level", "[commands][speech]") {
    SpeechStack stack;
    Said said;
    speech::GlobalCommandsContext context;
    context.presenter = [&stack] { return &stack.presenter(); };
    context.config = &stack.config();
    context.message = said.sink();
    speech::GlobalCommands commands{std::move(context), nullptr};

    Runner runner{&stack.loop(), &commands};
    runner.bind("cycleSpeechSymbolLevel", "kb:alus+p");

    runner.press("cycleSpeechSymbolLevel");
    CHECK(stack.presenter().settings().symbolLevel == text::SymbolLevel::Most);
    CHECK(said.last() == "Symbol level most");
    CHECK(stack.config().getString("speech", "symbolLevel") == "most");

    runner.press("cycleSpeechSymbolLevel");
    CHECK(stack.presenter().settings().symbolLevel == text::SymbolLevel::All);
    CHECK(said.last() == "Symbol level all");

    // ...and round to none, never to `char`: see the comment on
    // symbolLevelLabel in src/speech/global_commands.cpp.
    runner.press("cycleSpeechSymbolLevel");
    CHECK(stack.presenter().settings().symbolLevel == text::SymbolLevel::None);
    CHECK(said.last() == "Symbol level none");

    runner.press("cycleSpeechSymbolLevel");
    CHECK(stack.presenter().settings().symbolLevel == text::SymbolLevel::Some);
}

TEST_CASE("the settings ring commands announce the setting and its value",
          "[commands][speech][ring]") {
    SpeechStack stack;
    Said said;
    speech::GlobalCommandsContext context;
    context.presenter = [&stack] { return &stack.presenter(); };
    context.ring = [&stack] { return &stack.ring(); };
    context.config = &stack.config();
    context.message = said.sink();
    speech::GlobalCommands commands{std::move(context), nullptr};

    Runner runner{&stack.loop(), &commands};
    runner.bind("nextSynthSetting", "kb:alus+n");
    runner.bind("previousSynthSetting", "kb:alus+b");
    runner.bind("increaseSynthSetting", "kb:alus+i");
    runner.bind("decreaseSynthSetting", "kb:alus+d");
    runner.bind("increaseLargeSynthSetting", "kb:alus+k");
    runner.bind("decreaseLargeSynthSetting", "kb:alus+j");

    stack.synth().setRate(50);
    REQUIRE(stack.ring().current() == speech::SettingsRing::Setting::Rate);

    SECTION("one step up is five, and the announcement is name then value") {
        runner.press("increaseSynthSetting");
        CHECK(said.last() == "Rate 55");
        CHECK(stack.synth().rate() == 55);
        runner.press("decreaseSynthSetting");
        CHECK(said.last() == "Rate 50");
    }

    SECTION("a large step is twenty") {
        runner.press("increaseLargeSynthSetting");
        CHECK(said.last() == "Rate 70");
        runner.press("decreaseLargeSynthSetting");
        CHECK(said.last() == "Rate 50");
    }

    SECTION("moving around the ring announces where it landed, with the value") {
        runner.press("nextSynthSetting");
        CHECK(said.last() == "Pitch 50");
        runner.press("previousSynthSetting");
        CHECK(said.last() == "Rate 50");
    }

    SECTION("every one of the six says something, however it goes") {
        for (const char* const command :
             {"nextSynthSetting", "previousSynthSetting", "increaseSynthSetting",
              "decreaseSynthSetting", "increaseLargeSynthSetting", "decreaseLargeSynthSetting"}) {
            const std::size_t before = said.lines.size();
            runner.press(command);
            INFO("`" << command << "` said nothing at all");
            CHECK(said.lines.size() == before + 1);
            CHECK_FALSE(said.last().empty());
        }
    }
}

TEST_CASE("a command that cannot do its job says so instead of nothing",
          "[commands][speech]") {
    SpeechStack stack;
    Said said;
    // No presenter and no ring: `--no-speech`, alus-cli, and the state the
    // command-coverage fixture runs in.
    speech::GlobalCommandsContext context;
    context.message = said.sink();
    speech::GlobalCommands commands{std::move(context), nullptr};

    Runner runner{&stack.loop(), &commands};
    runner.bind("speechMode", "kb:alus+s");
    runner.bind("cycleSpeechSymbolLevel", "kb:alus+p");
    runner.bind("nextSynthSetting", "kb:alus+n");
    runner.bind("increaseSynthSetting", "kb:alus+i");

    runner.press("speechMode");
    CHECK(said.last() == "speech is not available");
    runner.press("cycleSpeechSymbolLevel");
    CHECK(said.last() == "speech is not available");
    runner.press("nextSynthSetting");
    CHECK(said.last() == "No settings");
    runner.press("increaseSynthSetting");
    CHECK(said.last() == "No settings");
}

TEST_CASE("the ring says `No settings` rather than moving nothing",
          "[commands][speech][ring]") {
    SpeechStack stack;
    Said said;
    // A ring with no driver at all: every one of the six must be audible.
    speech::SettingsRing empty;
    speech::GlobalCommandsContext context;
    context.presenter = [&stack] { return &stack.presenter(); };
    context.ring = [&empty] { return &empty; };
    context.message = said.sink();
    speech::GlobalCommands commands{std::move(context), nullptr};

    Runner runner{&stack.loop(), &commands};
    runner.bind("nextSynthSetting", "kb:alus+n");
    runner.bind("increaseLargeSynthSetting", "kb:alus+k");
    runner.press("nextSynthSetting");
    CHECK(said.last() == "No settings");
    runner.press("increaseLargeSynthSetting");
    CHECK(said.last() == "No settings");
}

// ---------------------------------------------------------------------------
// The configuration commands
// ---------------------------------------------------------------------------

TEST_CASE("alus+control+c writes the configuration to disk", "[commands][config]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "alus-save-config-test";
    std::filesystem::remove_all(dir);
    testing::QuietLog quiet;
    testing::LoopFixture fixture;
    config::Manager manager{config::Paths::forDirectory(dir)};
    Said said;

    config::CommandsContext context;
    context.manager = &manager;
    context.message = said.sink();
    config::Commands commands{std::move(context), nullptr};

    Runner runner{&fixture.loop(), &commands};
    runner.bind("saveConfiguration", "kb:alus+c");

    manager.setInt("speech", "rate", 73);
    // Nothing on disk yet: that is the promise every other setting command
    // depends on.
    CHECK_FALSE(std::filesystem::exists(dir / "alus.ini"));

    runner.press("saveConfiguration");
    CHECK(said.last() == "Configuration saved");
    REQUIRE(std::filesystem::exists(dir / "alus.ini"));

    // And it is really there: a second Manager over the same directory reads
    // the value back.
    config::Manager reread{config::Paths::forDirectory(dir)};
    REQUIRE(reread.load());
    CHECK(reread.getInt("speech", "rate", -1) == 73);
    std::filesystem::remove_all(dir);
}

TEST_CASE("alus+control+r reverts once, and resets on the third press",
          "[commands][config]") {
    testing::QuietLog quiet;
    testing::LoopFixture fixture;
    config::Manager manager{config::Paths::forDirectory(
        std::filesystem::temp_directory_path() / "alus-revert-config-test")};
    Said said;
    std::vector<bool> resets;

    // The dispatcher first, because the command has to be able to ask it how
    // many times its key has been pressed -- which is what `alus+control+r`
    // three times means.
    Runner runner{&fixture.loop()};
    config::CommandsContext context;
    context.manager = &manager;
    context.message = said.sink();
    context.dispatcher = &runner.dispatcher();
    context.reset = [&resets](bool factoryDefaults) { resets.push_back(factoryDefaults); };
    config::Commands commands{std::move(context), nullptr};
    runner.use(&commands);
    runner.bind("revertConfiguration", "kb:alus+r");

    SECTION("one press goes back to what is on disk") {
        runner.press("revertConfiguration");
        REQUIRE(resets.size() == 1);
        CHECK(resets.front() == false);
        CHECK(said.last() == "Configuration applied");
    }

    SECTION("three presses go back to the factory defaults") {
        runner.press("revertConfiguration", 3);
        REQUIRE(resets.size() == 2);
        CHECK(resets.front() == false);
        CHECK(resets.back() == true);
        CHECK(said.last() == "Configuration restored to factory defaults");
    }

    SECTION("two presses do nothing, which is the gap that protects the third") {
        runner.press("revertConfiguration", 2);
        REQUIRE(resets.size() == 1);
        CHECK(resets.front() == false);
    }
}

TEST_CASE("a configuration command run with nothing to act on says so",
          "[commands][config]") {
    testing::QuietLog quiet;
    testing::LoopFixture fixture;
    Said said;
    config::CommandsContext context;
    context.message = said.sink();
    config::Commands commands{std::move(context), nullptr};

    Runner runner{&fixture.loop(), &commands};
    runner.bind("saveConfiguration", "kb:alus+c");
    runner.bind("revertConfiguration", "kb:alus+r");

    runner.press("saveConfiguration");
    CHECK(said.last() == "cannot save the configuration from here");
    runner.press("revertConfiguration");
    CHECK(said.last() == "cannot revert the configuration from here");
}
