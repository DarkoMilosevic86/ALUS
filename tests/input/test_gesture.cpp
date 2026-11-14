// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Gestures, key names and identifier normalization:
//
//   T3.1  — "alus+shift+t" == "shift+alus+t" == "T+SHIFT+ALUS", and the same
//           for an imported map's spelling of the key (H3)
//
// plus the key-name table and the gesture properties that decide whether a key
// press is a command or a character.
#include "alus/input/gesture.hpp"

#include "alus/input/key_names.hpp"
#include "alus/input/keysyms.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

using alus::input::has;
using alus::input::KeyboardGesture;
using alus::input::KeyNames;
using alus::input::Layout;
using alus::input::Modifier;
using alus::input::Modifiers;
using alus::input::normalizeGestureIdentifier;
using alus::input::SpeechEffect;
namespace keysym = alus::input::keysym;

namespace {

KeyboardGesture makeGesture(std::uint32_t keysymValue, Modifiers modifiers = 0,
                            bool isAlusKey = false, Layout layout = Layout::Desktop,
                            std::uint32_t unichar = 0) {
    KeyboardGesture::Init init;
    init.event = alus::testing::keyDown(keysymValue, 0, 100, unichar);
    init.modifiers = modifiers;
    init.isAlusKey = isAlusKey;
    init.config.layout = layout;
    return KeyboardGesture(std::move(init));
}

}  // namespace

TEST_CASE("T3.1 identifier normalization is order and case independent") {
    const std::string canonical = normalizeGestureIdentifier("kb:alus+shift+t");
    CHECK(canonical == normalizeGestureIdentifier("kb:shift+alus+t"));
    CHECK(canonical == normalizeGestureIdentifier("kb:T+SHIFT+ALUS"));
    CHECK(canonical == normalizeGestureIdentifier("KB:Shift+T+ALUS"));
    // The parts are sorted by character, so the canonical form is predictable.
    CHECK(canonical == "kb:alus+shift+t");

    // H3: an imported map spells the modifier key the upstream way. It is
    // canonicalised here, in the one place that does it, so the same key press
    // matches both spellings and only ALUS's own is ever stored.
    CHECK(canonical == normalizeGestureIdentifier("kb:nvda+shift+t"));
    CHECK(canonical == normalizeGestureIdentifier("kb:T+SHIFT+NVDA"));

    // The source prefix is NOT sorted with the keys, and a layout-specific
    // prefix stays distinct from the layout-agnostic one.
    CHECK(normalizeGestureIdentifier("kb(laptop):ALUS+A") == "kb(laptop):a+alus");
    CHECK(normalizeGestureIdentifier("kb(laptop):alus+a") !=
          normalizeGestureIdentifier("kb:alus+a"));

    // A prefixless identifier is still normalized rather than rejected: the
    // gesture dialog and the tests compare key lists in this form.
    CHECK(normalizeGestureIdentifier("T+SHIFT+ALUS") == "alus+shift+t");
}

TEST_CASE("key names keep the spelling users already know") {
    const KeyNames& names = KeyNames::shared();

    SECTION("the numeric keypad with NumLock off") {
        CHECK(names.name(keysym::kKpHome) == "numpad7");
        CHECK(names.name(keysym::kKpEnd) == "numpad1");
        CHECK(names.name(keysym::kKpBegin) == "numpad5");
        CHECK(names.name(keysym::kKpInsert) == "numpadInsert");
        CHECK(names.name(keysym::kKpEnter) == "numpadEnter");
        CHECK(names.name(keysym::kKpAdd) == "numpadPlus");
        CHECK(names.name(keysym::kKpDivide) == "numpadDivide");
        // With NumLock on the same key reports a digit, and NVDA names those
        // differently on purpose.
        CHECK(names.name(keysym::kKp0) == "numLockNumpad0");
        CHECK(names.name(keysym::kKp9) == "numLockNumpad9");
        CHECK(names.isNumpad("numpad7"));
        CHECK(names.isNumpad("numLockNumpad7"));
        CHECK_FALSE(names.isNumpad("home"));
    }

    SECTION("navigation and editing keys") {
        CHECK(names.name(keysym::kInsert) == "insert");
        CHECK(names.name(keysym::kLeft) == "leftArrow");
        CHECK(names.name(keysym::kPrior) == "pageUp");
        CHECK(names.name(keysym::kReturn) == "enter");
        CHECK(names.name(keysym::kMenu) == "applications");
        CHECK(names.name(keysym::kF1) == "f1");
        CHECK(names.name(keysym::kF24) == "f24");
    }

    SECTION("letters, digits and punctuation are named after the character") {
        CHECK(names.name('t') == "t");
        // A shifted letter arrives as the upper-case keysym; the NAME is lower
        // case, because `shift+t` is the gesture, not `shift+T`.
        CHECK(names.name('T') == "t");
        CHECK(names.name('7') == "7");
        CHECK(names.name('\'') == "'");
        // "+" is the separator in an identifier, so the key is called "plus".
        CHECK(names.name(keysym::kPlus) == "plus");
    }

    SECTION("a key the system cannot name is still bindable") {
        CHECK(names.name(0xFEFF).rfind("unknown_", 0) == 0);
    }

    SECTION("name to keysym, including Serbian letters") {
        CHECK(names.keysym("numpad7") == keysym::kKpHome);
        CHECK(names.keysym("NUMPAD7") == keysym::kKpHome);
        CHECK(names.keysym("t") == static_cast<std::uint32_t>('t'));
        CHECK(names.keysym("plus") == keysym::kPlus);
        // The reason libxkbcommon is a dependency: the LEGACY keysym, which is
        // what the compositor compares a grab against (O2 in docs/history/phase-3.md).
        CHECK(names.keysym("š") == 0x1b9);
        CHECK(names.keysym("đ") == 0x1f0);
        CHECK_FALSE(names.keysym("there-is-no-such-key").has_value());
    }

    SECTION("modifier names generalize left and right") {
        CHECK(names.modifierFor("leftControl") == Modifier::Control);
        CHECK(names.modifierFor("rightShift") == Modifier::Shift);
        CHECK(names.modifierFor("alus") == Modifier::Alus);
        // Accepted for imported maps only (H3).
        CHECK(names.modifierFor("nvda") == Modifier::Alus);
        CHECK_FALSE(names.modifierFor("t").has_value());
    }
}

TEST_CASE("the X11 modifier state mask becomes ALUS modifiers") {
    const Modifiers modifiers = KeyNames::modifiersFromState(
        keysym::kStateShift | keysym::kStateControl | keysym::kStateMod1 |
            keysym::kStateMod4 | keysym::kStateMod2 | (1u << 15),
        1u << 15);
    CHECK(has(modifiers, Modifier::Shift));
    CHECK(has(modifiers, Modifier::Control));
    CHECK(has(modifiers, Modifier::Alt));
    CHECK(has(modifiers, Modifier::Windows));
    CHECK(has(modifiers, Modifier::NumLock));
    CHECK(has(modifiers, Modifier::Alus));

    // Caps lock is a STATE, not a modifier: a gesture is never `capslock+t`.
    CHECK(KeyNames::modifiersFromState(keysym::kStateLock) == 0);
    // AltGr composes characters. If it counted as alt, every AltGr-typed
    // character would look like a command.
    CHECK(KeyNames::modifiersFromState(keysym::kStateMod5) == 0);
    // Without a mapped ALUS key, the virtual modifier bit means nothing.
    CHECK(KeyNames::modifiersFromState(1u << 15, 0) == 0);
}

TEST_CASE("a keyboard gesture reports its identifiers in priority order") {
    const KeyboardGesture gesture =
        makeGesture('t', Modifier::Alus | Modifier::Shift, false, Layout::Laptop);
    const std::vector<std::string> identifiers = gesture.identifiers();
    REQUIRE(identifiers.size() == 2);
    // Layout-specific first: that is what makes a laptop binding win (T3.2).
    CHECK(identifiers[0] == "kb(laptop):alus+shift+t");
    CHECK(identifiers[1] == "kb:alus+shift+t");
    CHECK(gesture.normalizedIdentifiers()[0] == "kb(laptop):alus+shift+t");
}

TEST_CASE("ALUS's own key is called alus") {
    const KeyboardGesture gesture = makeGesture(keysym::kInsert, 0, true);
    CHECK(gesture.mainKeyName() == "alus");
    CHECK(gesture.identifiers()[1] == "kb:alus");
    CHECK(gesture.isModifier());
    // What input help says out loud, and what the gesture dialog shows.
    CHECK(gesture.displayName() == "alus");
}

TEST_CASE("character or command") {
    SECTION("a bare letter is a character") {
        const KeyboardGesture gesture = makeGesture('t', 0, false, Layout::Desktop, 't');
        CHECK(gesture.isCharacter());
        CHECK_FALSE(gesture.shouldReportAsCommand());
    }
    SECTION("shift keeps it a character, control does not") {
        CHECK(makeGesture('T', static_cast<Modifiers>(Modifier::Shift), false, Layout::Desktop,
                          'T')
                  .isCharacter());
        CHECK_FALSE(makeGesture('t', static_cast<Modifiers>(Modifier::Control)).isCharacter());
        CHECK(makeGesture('t', static_cast<Modifiers>(Modifier::Control))
                  .shouldReportAsCommand());
    }
    SECTION("a key with a long name is a command, space excepted") {
        CHECK_FALSE(makeGesture(keysym::kKpHome).isCharacter());
        CHECK(makeGesture(keysym::kSpace, 0, false, Layout::Desktop, ' ').isCharacter());
    }
    SECTION("toggle keys are not reported as commands") {
        // #5490 in NVDA: otherwise the key is announced twice, once as a
        // command and once as the state it changed.
        CHECK_FALSE(makeGesture(keysym::kCapsLock).shouldReportAsCommand());
        CHECK_FALSE(makeGesture(keysym::kNumLock).shouldReportAsCommand());
    }
}

TEST_CASE("what a key press does to speech in flight") {
    SECTION("an ordinary key cancels speech") {
        CHECK(makeGesture('t').speechEffectWhenExecuted() == SpeechEffect::Cancel);
    }
    SECTION("shift pauses, and pauses again to resume") {
        KeyboardGesture::Init init;
        init.event = alus::testing::keyDown(keysym::kShiftL);
        init.config.speechIsPaused = false;
        CHECK(KeyboardGesture(init).speechEffectWhenExecuted() == SpeechEffect::Pause);
        init.config.speechIsPaused = true;
        CHECK(KeyboardGesture(init).speechEffectWhenExecuted() == SpeechEffect::Resume);
    }
    SECTION("enter can be exempted, and characters too") {
        KeyboardGesture::Init init;
        init.event = alus::testing::keyDown(keysym::kReturn);
        init.config.speechInterruptForEnter = false;
        CHECK(KeyboardGesture(init).speechEffectWhenExecuted() == SpeechEffect::None);

        KeyboardGesture::Init character;
        character.event = alus::testing::keyDown('t', 0, 100, 't');
        character.config.speechInterruptForCharacters = false;
        CHECK(KeyboardGesture(character).speechEffectWhenExecuted() == SpeechEffect::None);
    }
    SECTION("input help always cancels, so the explanation is heard") {
        KeyboardGesture::Init init;
        init.event = alus::testing::keyDown('t', 0, 100, 't');
        init.config.speechInterruptForCharacters = false;
        init.config.inputHelpActive = true;
        CHECK(KeyboardGesture(init).speechEffectWhenExecuted() == SpeechEffect::Cancel);
    }
}

TEST_CASE("numLock is handled normally even in input help") {
    // #4226 in NVDA: otherwise the keyboard controller and the system disagree
    // about the lock state from then on.
    CHECK(makeGesture(keysym::kNumLock).bypassInputHelp());
    CHECK_FALSE(makeGesture('t').bypassInputHelp());
}

TEST_CASE("Wayland cannot send a gesture to the system") {
    // Finding N18. The base class answers false so that callers handle it
    // instead of assuming the key was delivered.
    KeyboardGesture gesture = makeGesture('t');
    CHECK_FALSE(gesture.send());
}
