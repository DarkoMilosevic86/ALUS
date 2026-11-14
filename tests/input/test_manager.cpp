// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The gesture manager — the phase 3 exit criteria that live in the key
// pipeline:
//
//   T3.5  — a consumed key down is matched by a consumed key up
//   T3.6  — the ALUS key pressed twice inside the timeout asks for a real key
//
// plus input help (which must not eat its own switch), typed-character echo,
// "pass next key through", and the grab set the maps imply.
#include "alus/input/manager.hpp"

#include "../support/loop_fixture.hpp"
#include "alus/input/key_event_filter.hpp"
#include "alus/input/keysyms.hpp"
#include "alus/script/dispatch.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <thread>

using alus::input::GestureManager;
using alus::input::GestureMap;
using alus::input::KeyDefinition;
using alus::input::KeyEvent;
using alus::input::Layout;
using alus::input::NullInputBackend;
using alus::input::TypingEcho;
using alus::script::DispatchContext;
using alus::script::Dispatcher;
using alus::testing::keyDown;
using alus::testing::keyUp;
using alus::testing::SpeechRecorder;
using alus::testing::TestScriptable;
namespace keysym = alus::input::keysym;

namespace {

// Everything a manager test needs, assembled the way the service assembles it.
struct Harness {
    alus::testing::LoopFixture fixture;
    SpeechRecorder speech;
    Dispatcher dispatcher{&fixture.loop()};
    GestureMap builtin{"built-in"};
    TestScriptable commands{{"globalCommands.GlobalCommands"}};
    NullInputBackend backend;
    std::unique_ptr<GestureManager> manager;

    Harness() {
        manager = std::make_unique<GestureManager>(fixture.loop(), dispatcher, speech.hooks());
        manager->setBuiltinMap(&builtin);
        dispatcher.setContextProvider(
            [this](DispatchContext& context) { context.globalCommands = {&commands}; });
        backend.start(nullptr);
        manager->setBackend(&backend);
    }

    void bind(const std::string& gesture, const std::string& script,
              alus::script::ScriptSpec spec = {}) {
        commands.add(script, std::move(spec));
        builtin.add(gesture, "globalCommands.GlobalCommands", script);
        manager->refreshMaps();
    }

    bool press(const KeyEvent& event) { return manager->onKeyEvent(event); }
};

}  // namespace

TEST_CASE("T3.5 a consumed key down is matched by a consumed key up") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");

    // The ALUS key first: it is never given to the application.
    const std::uint32_t alusKeycode = 118;
    CHECK(harness.press(keyDown(keysym::kInsert, 0, alusKeycode)));

    const std::uint32_t tKeycode = 28;
    CHECK(harness.press(keyDown('t', 0, tKeycode)));
    // The key up of a consumed key down must also be consumed, or the
    // application is left believing the key is still held (§5.2).
    CHECK(harness.press(keyUp('t', 0, tKeycode)));
    // ... and only once.
    CHECK_FALSE(harness.press(keyUp('t', 0, tKeycode)));
    CHECK(harness.press(keyUp(keysym::kInsert, 0, alusKeycode)));

    harness.fixture.drain();
    CHECK(harness.commands.ran == std::vector<std::string>{"reportSomething"});

    SECTION("an unbound key is consumed neither down nor up") {
        CHECK_FALSE(harness.press(keyDown('q', 0, 24)));
        CHECK_FALSE(harness.press(keyUp('q', 0, 24)));
    }
}

TEST_CASE("T3.6 the ALUS key pressed twice asks for a real key press") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    const std::uint32_t keycode = 118;

    // First press: ALUS keeps it, whatever it is bound to (H2).
    CHECK(harness.press(keyDown(keysym::kInsert, 0, keycode)));
    CHECK(harness.press(keyUp(keysym::kInsert, 0, keycode)));
    CHECK(harness.manager->stats().alusKeyPassThroughRequested == 0);

    // Second press inside the multi-press timeout: the user wants a real
    // Insert, so ALUS does not consume it.
    CHECK_FALSE(harness.press(keyDown(keysym::kInsert, 0, keycode)));
    CHECK(harness.manager->stats().alusKeyPassThroughRequested == 1);
    // And on Wayland it cannot be delivered: AT-SPI has no way to inject a
    // key press (finding N18). The gap between these two numbers IS the
    // measurement, which is why both are counted.
    CHECK(harness.manager->stats().alusKeyPassThroughDelivered == 0);
    CHECK_FALSE(harness.backend.canSendKeys());
    // The key up of a key ALUS did not consume is not consumed either, or the
    // application would see a press with no release (T3.5 in the other
    // direction).
    CHECK_FALSE(harness.press(keyUp(keysym::kInsert, 0, keycode)));

    SECTION("a press after the timeout is an ALUS key again") {
        alus::input::KeyboardConfig config = harness.manager->config();
        config.multiPressTimeout = std::chrono::milliseconds(20);
        harness.manager->setConfig(config);
        CHECK(harness.press(keyDown(keysym::kInsert, 0, keycode)));
        CHECK(harness.press(keyUp(keysym::kInsert, 0, keycode)));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        CHECK(harness.press(keyDown(keysym::kInsert, 0, keycode)));
    }

    SECTION("a key pressed in between breaks the double press") {
        CHECK(harness.press(keyDown(keysym::kInsert, 0, keycode)));
        CHECK_FALSE(harness.press(keyDown('q', 0, 24)));
        CHECK_FALSE(harness.press(keyUp('q', 0, 24)));
        CHECK(harness.press(keyUp(keysym::kInsert, 0, keycode)));
        // The ALUS key is still the ALUS key: no pass-through was requested
        // beyond the one above.
        CHECK(harness.manager->stats().alusKeyPassThroughRequested == 1);
    }
}

TEST_CASE("the ALUS key held with another key makes an ALUS gesture") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    // No virtual modifier bit in the state mask: the compositor does not track
    // one for us, so ALUS's own bookkeeping has to supply it.
    CHECK(harness.press(keyDown(keysym::kInsert, 0, 118)));
    CHECK(harness.press(keyDown('t', 0, 28)));
    harness.fixture.drain();
    CHECK(harness.commands.ran == std::vector<std::string>{"reportSomething"});
}

TEST_CASE("a key press cancels speech in flight") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    CHECK_FALSE(harness.press(keyDown('q', 0, 24)));
    harness.fixture.drain();
    // Even an unbound key cancels: "press any key to shut up" is the
    // behaviour users rely on.
    CHECK(harness.speech.cancels == 1);

    SECTION("shift pauses rather than cancelling") {
        harness.press(keyDown(keysym::kShiftL, 0, 50));
        // Pausing is queued WITHOUT the immediate flag, as in NVDA, so it
        // happens on the next delayed pump rather than inside the key event.
        CHECK(harness.fixture.pumpUntil([&] { return harness.speech.pauses == 1; }));
        CHECK(harness.speech.cancels == 1);
    }
}

TEST_CASE("speak command keys reports the gesture, not the character") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    alus::input::KeyboardConfig config = harness.manager->config();
    config.speakCommandKeys = true;
    harness.manager->setConfig(config);

    harness.press(keyDown(keysym::kInsert, 0, 118));
    harness.press(keyDown('t', 0, 28));
    harness.fixture.drain();
    REQUIRE_FALSE(harness.speech.spoken.empty());
    CHECK(harness.speech.spoken.front() == "alus+t");

    SECTION("a plain character is not reported as a command") {
        // The ALUS key has to be released first: while it is held, `q` is the
        // command `nvda+q` and is reported as one.
        harness.press(keyUp(keysym::kInsert, 0, 118));
        harness.speech.spoken.clear();
        harness.press(keyDown('q', 0, 24, 'q'));
        harness.fixture.drain();
        CHECK(harness.speech.spoken.empty());
    }
}

TEST_CASE("input help explains a key and does not run it") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething",
                 alus::script::ScriptSpec{.description = "Reports something useful."});
    harness.bind("kb:alus+1", "toggleInputHelp");
    harness.manager->inputHelp().setActive(true);

    harness.press(keyDown(keysym::kInsert, 0, 118));
    CHECK(harness.press(keyDown('t', 0, 28)));
    harness.fixture.drain();

    // Input help reports the modifier press on its own too ("alus"), exactly
    // as NVDA does: the captor runs before the "this is only a modifier" check.
    REQUIRE(harness.speech.spoken.size() >= 3);
    CHECK(harness.speech.spoken[0] == "alus");
    // Then the key name, then the script description; the script itself did
    // NOT run.
    CHECK(harness.speech.spoken[1] == "alus+t");
    CHECK(harness.speech.spoken[2] == "Reports something useful.");
    CHECK(harness.commands.ran.empty());

    SECTION("the input help toggle still runs, or there is no way out") {
        harness.speech.spoken.clear();
        harness.press(keyDown('1', 0, 10));
        harness.fixture.drain();
        CHECK(harness.commands.ran == std::vector<std::string>{"toggleInputHelp"});
    }

    SECTION("numLock bypasses input help entirely") {
        harness.speech.spoken.clear();
        harness.press(keyDown(keysym::kNumLock, 0, 77));
        harness.fixture.drain();
        // Reported in the log only; nothing is spoken and the key is handled
        // normally (#4226).
        CHECK(harness.speech.spoken.empty());
    }
}

TEST_CASE("typed characters are echoed, and words when asked") {
    Harness harness;
    alus::input::KeyboardConfig config = harness.manager->config();
    config.speakTypedCharacters = TypingEcho::Always;
    config.speakTypedWords = TypingEcho::Always;
    harness.manager->setConfig(config);

    const auto type = [&](char character, std::uint32_t keycode) {
        harness.press(keyDown(static_cast<std::uint32_t>(character), 0, keycode,
                              static_cast<std::uint32_t>(character)));
    };
    type('h', 1);
    type('e', 2);
    type('l', 3);
    type('l', 4);
    type('o', 5);
    CHECK(harness.speech.spelled == std::vector<std::string>{"h", "e", "l", "l", "o"});
    CHECK(harness.speech.spoken.empty());

    // The space ends the word, and the word is spoken once — not five letters.
    harness.press(keyDown(keysym::kSpace, 0, 6, ' '));
    CHECK(harness.speech.spoken == std::vector<std::string>{"hello"});

    SECTION("backspace corrects the word buffer") {
        type('a', 7);
        type('b', 8);
        harness.press(keyDown(keysym::kBackSpace, 0, 9));
        CHECK(harness.manager->typedChars().wordBuffer() == "a");
    }

    SECTION("a key that ran a command is not echoed") {
        harness.bind("kb:alus+t", "reportSomething");
        harness.speech.spelled.clear();
        harness.press(keyDown(keysym::kInsert, 0, 118));
        harness.press(keyDown('t', 0, 28, 't'));
        CHECK(harness.speech.spelled.empty());
    }
}

TEST_CASE("pass next key through stops ALUS acting on one key") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    harness.manager->passNextKeyThrough();
    CHECK(harness.manager->isPassingKeysThrough());

    // The ALUS gesture is not acted on, even though it is bound.
    CHECK_FALSE(harness.press(keyDown(keysym::kInsert, 0, 118)));
    CHECK_FALSE(harness.press(keyUp(keysym::kInsert, 0, 118)));
    harness.fixture.drain();
    CHECK(harness.commands.ran.empty());
    // And the state resets, so the key after it is ALUS's again.
    CHECK_FALSE(harness.manager->isPassingKeysThrough());
    CHECK(harness.press(keyDown(keysym::kInsert, 0, 118)));
}

TEST_CASE("while the core is wedged, keys pass straight through") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    bool recovering = false;
    GestureManager::Hooks hooks = harness.speech.hooks();
    hooks.isRecovering = [&]() { return recovering; };
    GestureManager manager(harness.fixture.loop(), harness.dispatcher, hooks);
    manager.setBuiltinMap(&harness.builtin);
    manager.setBackend(&harness.backend);

    recovering = true;
    CHECK_FALSE(manager.onKeyEvent(keyDown(keysym::kInsert, 0, 118)));
    CHECK_FALSE(manager.onKeyEvent(keyDown('t', 0, 28)));
    harness.fixture.drain();
    CHECK(harness.commands.ran.empty());

    recovering = false;
    CHECK(manager.onKeyEvent(keyDown(keysym::kInsert, 0, 118)));
}

TEST_CASE("the grab set is computed from the maps") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    harness.bind("kb:numpad7", "reviewPreviousLine");
    harness.bind("kb(laptop):alus+shift+t", "laptopOnly");

    const std::vector<KeyDefinition> grabs = harness.manager->computeGrabs();
    const auto hasGrab = [&](std::uint32_t keysymValue) {
        return std::any_of(grabs.begin(), grabs.end(), [&](const KeyDefinition& grab) {
            return grab.keysym == keysymValue;
        });
    };

    // The ALUS keys themselves are grabbed, or ALUS could not see them.
    CHECK(hasGrab(keysym::kInsert));
    CHECK(hasGrab(keysym::kKpInsert));
    CHECK(hasGrab('t'));
    // A numpad command must keep working in both layouts, which is why NVDA
    // writes it as `kb:` (§5.4).
    CHECK(hasGrab(keysym::kKpHome));

    SECTION("a laptop-only binding is grabbed only in the laptop layout") {
        const std::size_t desktopGrabs = grabs.size();
        alus::input::KeyboardConfig config = harness.manager->config();
        config.layout = Layout::Laptop;
        harness.manager->setConfig(config);
        // The laptop identifier adds nothing new here (same keysym, different
        // modifiers), so the count is what matters, not the keysym.
        CHECK(harness.manager->computeGrabs().size() >= desktopGrabs);
        CHECK(harness.backend.grabs().size() == harness.manager->computeGrabs().size());
    }

    SECTION("an ALUS gesture is grabbed once per ALUS key") {
        // Insert and numpad insert are separate virtual modifiers, so one
        // grab naming both would need both keys held — it would never fire.
        const auto& masks = harness.manager->alusModifierMasks();
        REQUIRE(masks.size() == 2);
        CHECK(harness.manager->alusModifierMask() == (masks[0] | masks[1]));
        for (const std::uint32_t mask : masks) {
            // N9: a MASK, never an index. 1 << 32768 is the bug this guards.
            CHECK(mask >= (1u << 15));
            const bool grabbed =
                std::any_of(grabs.begin(), grabs.end(), [&](const KeyDefinition& g) {
                    return g.keysym == 't' && g.modifiers == mask;
                });
            CHECK(grabbed);
        }
    }

    SECTION("the backend is told whenever the maps change") {
        const std::size_t before = harness.backend.grabs().size();
        harness.bind("kb:alus+f12", "somethingNew");
        CHECK(harness.backend.grabs().size() > before);
    }
}

TEST_CASE("an extension point can cancel a gesture") {
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    bool allow = false;
    auto handle = harness.manager->decideExecuteGesture().add(
        [&](alus::input::Gesture&) { return allow; });

    harness.press(keyDown(keysym::kInsert, 0, 118));
    // Cancelled gestures are still consumed: a handler that said "do not
    // execute" did not ask for the key to reach the application.
    CHECK(harness.press(keyDown('t', 0, 28)));
    harness.fixture.drain();
    CHECK(harness.commands.ran.empty());

    allow = true;
    harness.press(keyDown('t', 0, 28));
    harness.fixture.drain();
    CHECK(harness.commands.ran.size() == 1);
}

TEST_CASE("the emergency release drops every grab") {
    // Finding N19: a wedged screen reader must not keep the keyboard.
    Harness harness;
    harness.bind("kb:alus+t", "reportSomething");
    REQUIRE_FALSE(harness.backend.grabs().empty());
    harness.backend.grabKeyboard(true, nullptr);
    REQUIRE(harness.backend.keyboardGrabbed());

    harness.backend.emergencyRelease();
    CHECK(harness.backend.grabs().empty());
    CHECK_FALSE(harness.backend.keyboardGrabbed());

    SECTION("N128 and they come back once the core has recovered") {
        // Log 16:14:44: after a freeze while LibreOffice opened, the grab set
        // had not changed, so refreshing it sent nothing, and numpad Insert
        // reached Writer ("Confirm overwrite mode").
        harness.manager->refreshGrabs();
        CHECK(harness.backend.grabs().empty());
        harness.manager->reinstallGrabs();
        CHECK(harness.backend.grabs() == harness.manager->computeGrabs());
        const auto& grabs = harness.backend.grabs();
        CHECK(std::any_of(grabs.begin(), grabs.end(), [](const KeyDefinition& grab) {
            return grab.keysym == keysym::kKpInsert;
        }));
    }
}

// ---------------------------------------------------------------------------
// What the interactive run against the real compositor found (docs/history/phase-3.md,
// "Interaktivno merenje"). All three of these were measured on GNOME 48.7 and
// two of them were audible, which is why they are regression tests now.
// ---------------------------------------------------------------------------

TEST_CASE("N33 a key event the platform delivers twice is handled once") {
    alus::input::KeyEventFilter filter;
    const auto base = std::chrono::steady_clock::now();
    const KeyEvent down = keyDown('a', 0, 38, 'a');

    CHECK_FALSE(filter.isDuplicate(down, base));
    // Measured: every duplicate arrived within 0-2 ms of its original.
    CHECK(filter.isDuplicate(down, base + std::chrono::milliseconds(2)));
    CHECK(filter.dropped() == 1);

    SECTION("key auto-repeat is not a duplicate") {
        // The fastest X11 repeat rate is about 30 ms between presses.
        CHECK_FALSE(filter.isDuplicate(down, base + std::chrono::milliseconds(32)));
    }
    SECTION("a different key is not a duplicate") {
        CHECK_FALSE(filter.isDuplicate(keyDown('s', 0, 39, 's'),
                                       base + std::chrono::milliseconds(1)));
    }
    SECTION("the key up of the same key is not a duplicate of the key down") {
        CHECK_FALSE(filter.isDuplicate(keyUp('a', 0, 38), base + std::chrono::milliseconds(1)));
    }
    SECTION("a changed modifier state is not a duplicate") {
        KeyEvent shifted = down;
        shifted.state = keysym::kStateShift;
        CHECK_FALSE(filter.isDuplicate(shifted, base + std::chrono::milliseconds(1)));
    }
}

TEST_CASE("the ALUS key does not modify itself") {
    Harness harness;
    alus::input::KeyboardConfig config = harness.manager->config();
    config.speakCommandKeys = true;
    harness.manager->setConfig(config);
    const std::uint32_t keycode = 90;  // numpad insert, as in the measured run

    // First press: the ALUS key alone. The backend ORs the virtual modifier
    // bit into this very event (the compositor does not track it for us), so
    // the gesture must be `alus`, never `alus+alus`. A bare modifier is not
    // announced as a command — that is NVDA's rule and ALUS keeps it — so what
    // is checked here is the gesture, through input help, which does report it.
    harness.manager->inputHelp().setActive(true);
    KeyEvent first = keyDown(keysym::kKpInsert, 0, keycode);
    first.state = harness.manager->alusModifierMask();
    CHECK(harness.press(first));
    harness.fixture.drain();
    REQUIRE_FALSE(harness.speech.spoken.empty());
    CHECK(harness.speech.spoken.back() == "alus");
    harness.manager->inputHelp().setActive(false);

    CHECK(harness.press(keyUp(keysym::kKpInsert, 0, keycode)));

    SECTION("and the second press of a double press is the plain key") {
        // Measured bug: the daemon announced "alus plus numpad insert".
        harness.speech.spoken.clear();
        KeyEvent second = keyDown(keysym::kKpInsert, 0, keycode);
        second.state = harness.manager->alusModifierMask();
        CHECK_FALSE(harness.press(second));
        harness.fixture.drain();
        REQUIRE_FALSE(harness.speech.spoken.empty());
        CHECK(harness.speech.spoken.back() == "numpadInsert");
    }

    SECTION("but another ALUS key really held down still modifies") {
        // Insert held, numpad insert pressed: that really is an ALUS gesture.
        // Both are modifiers, so again input help is what reports it.
        KeyEvent other = keyDown(keysym::kInsert, 0, 118);
        other.state = harness.manager->alusModifierMask();
        CHECK(harness.press(other));
        harness.manager->inputHelp().setActive(true);
        harness.speech.spoken.clear();
        KeyEvent numpad = keyDown(keysym::kKpInsert, 0, keycode);
        numpad.state = harness.manager->alusModifierMask();
        CHECK(harness.press(numpad));
        harness.fixture.drain();
        REQUIRE_FALSE(harness.speech.spoken.empty());
        // Two ALUS keys held at once reads as `alus+alus`, which is what NVDA
        // produces too (it discards only the key's OWN modifier entry). It is
        // bound to nothing and harms nothing; what matters is that the bit was
        // not cleared when it belongs to a DIFFERENT key.
        CHECK(harness.speech.spoken.back() == "alus+alus");
    }

    SECTION("an ordinary key pressed with the ALUS key still carries it") {
        harness.bind("kb:alus+t", "reportSomething");
        KeyEvent other = keyDown(keysym::kInsert, 0, 118);
        other.state = harness.manager->alusModifierMask();
        CHECK(harness.press(other));
        CHECK(harness.press(keyDown('t', 0, 28, 't')));
        harness.fixture.drain();
        CHECK(harness.commands.ran == std::vector<std::string>{"reportSomething"});
    }
}

TEST_CASE("N34 the compositor passes a double-pressed ALUS key on itself") {
    Harness harness;
    const std::uint32_t keycode = 118;

    SECTION("a platform that does it needs no injection from ALUS") {
        harness.backend.setModifierDoublePressReachesApplication(true);
        CHECK(harness.press(keyDown(keysym::kInsert, 0, keycode)));
        CHECK(harness.press(keyUp(keysym::kInsert, 0, keycode)));
        CHECK_FALSE(harness.press(keyDown(keysym::kInsert, 0, keycode)));
        CHECK(harness.manager->stats().alusKeyPassThroughRequested == 1);
        // Counted as delivered because the compositor delivers it, and ALUS
        // cannot inject a key press on Wayland at all (N18).
        CHECK(harness.manager->stats().alusKeyPassThroughDelivered == 1);
        CHECK_FALSE(harness.backend.canSendKeys());
    }

    SECTION("a platform that does neither says so instead of pretending") {
        alus::testing::QuietLog quiet;
        harness.backend.setModifierDoublePressReachesApplication(false);
        CHECK(harness.press(keyDown(keysym::kInsert, 0, keycode)));
        CHECK(harness.press(keyUp(keysym::kInsert, 0, keycode)));
        CHECK_FALSE(harness.press(keyDown(keysym::kInsert, 0, keycode)));
        CHECK(harness.manager->stats().alusKeyPassThroughRequested == 1);
        CHECK(harness.manager->stats().alusKeyPassThroughDelivered == 0);
    }
}

namespace {
// Records the repeat count each time it runs, as `alus+f12` reads it.
class RepeatProbe final : public alus::script::ScriptableObject {
public:
    explicit RepeatProbe(const Dispatcher& dispatcher)
        : ScriptableObject({"globalCommands.GlobalCommands"}, nullptr) {
        bindScript("dateTime", [this, &dispatcher](alus::input::Gesture&) {
            counts.push_back(dispatcher.lastScriptRepeatCount());
        });
    }
    std::vector<int> counts;
};
}  // namespace

TEST_CASE("a double press with the ALUS key held counts as a repeat") {
    // Measured (phase-10.md N122): numpad Insert held, F12 twice 200 ms apart;
    // the second press said the time again instead of the date.
    Harness harness;
    RepeatProbe probe{harness.dispatcher};
    harness.builtin.add("kb:alus+f12", "globalCommands.GlobalCommands", "dateTime");
    harness.dispatcher.setContextProvider(
        [&probe](DispatchContext& context) { context.globalCommands = {&probe}; });
    harness.manager->refreshMaps();

    const std::uint32_t alusKeycode = 90;
    const std::uint32_t f12Keycode = 96;
    const std::uint32_t f12 = 0xffc9;
    harness.press(keyDown(keysym::kKpInsert, 0, alusKeycode));
    harness.press(keyDown(f12, 0x10000, f12Keycode));
    harness.press(keyUp(f12, 0x10000, f12Keycode));
    harness.fixture.drain();
    harness.press(keyDown(f12, 0x10000, f12Keycode));
    harness.press(keyUp(f12, 0x10000, f12Keycode));
    harness.fixture.drain();
    harness.press(keyUp(keysym::kKpInsert, 0, alusKeycode));
    CHECK(probe.counts == std::vector<int>{0, 1});
}
