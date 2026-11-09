// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Braille INPUT: the display's own keyboard.
//
// 02-FAZE.md leaves this side of the phase to the MANUAL test -- `bk:dot7`,
// `bk:dot8`, `bk:dot7+dot8` pressed on a real display. A manual test is not a
// measurement that survives a refactor, so everything that does not need
// hardware is asserted here instead, and what the manual test is left with is
// only the hardware: that brltty reports the keys at all.
//
// THE TWO THINGS THAT MUST NOT BREAK QUIETLY:
//
//  1. THE IDENTIFIERS. `bk:dot7` is a binding in a user's `gestures.ini`. An
//     identifier that changes spelling breaks every such file silently, so
//     each form is pinned here -- including which combinations get their OWN
//     identifier and which only the generic `bk:dots`, because that choice is
//     what decides whether a dot combination is TEXT or a COMMAND.
//  2. THAT TYPING CANNOT SILENTLY DO NOTHING. On Wayland there is no
//     keystroke injection (phase 0, N18). The honest behaviour is to say so,
//     once, and the dishonest one -- swallowing the text -- is the failure
//     that phase 0 exists to have found. It is asserted as a behaviour, not
//     left to a log line.
#include "alus/braille/input.hpp"

#include "alus/braille/louis.hpp"
#include "alus/input/gesture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using alus::braille::BrailleInput;
using alus::braille::DisplayGesture;
using alus::braille::InputGesture;
using alus::braille::kDot7;
using alus::braille::kDot8;

namespace {

// Dot numbers to a cell, so a test reads as dots and not as hexadecimal.
constexpr std::uint8_t dots(std::initializer_list<int> numbers) {
    std::uint8_t cell = 0;
    for (const int dot : numbers) {
        cell = static_cast<std::uint8_t>(cell | (1U << (dot - 1)));
    }
    return cell;
}

// The six-dot patterns the tests type. In a computer-braille table these are
// also the letters themselves, which is what makes `en-us-comp8.ctb` the
// right table for asserting that one cell becomes one character.
constexpr std::uint8_t kA = dots({1});
constexpr std::uint8_t kB = dots({1, 2});
constexpr std::uint8_t kC = dots({1, 4});
constexpr std::uint8_t kSpace = 0;

class InputFixture {
public:
    explicit InputFixture(const std::string& table = "en-us-comp8.ctb") {
        louis_ = &alus::braille::Louis::shared();
        louis_->initialize();

        BrailleInput::Hooks hooks;
        hooks.typeText = [this](const std::string& text) {
            typed.push_back(text);
            return canType;
        };
        hooks.emulateKey = [this](const std::string& gesture) {
            keys.push_back(gesture);
            return canEmulate;
        };
        hooks.message = [this](const std::string& text) { messages.push_back(text); };
        hooks.speakDots = [this](const std::string& text) { spokenDots.push_back(text); };
        hooks.isTypingProtected = [this] { return protectedField; };
        hooks.isFocusTextObject = [this] { return focusIsText; };
        hooks.updateDisplay = [this] { ++displayUpdates; };

        input = std::make_unique<BrailleInput>(*louis_, std::move(hooks));
        alus::braille::Settings settings;
        settings.inputTable = table;
        settings.language = "en";
        input->applySettings(settings);
    }

    void type(std::initializer_list<std::uint8_t> cells) {
        for (const std::uint8_t cell : cells) {
            input->input(cell);
        }
    }

    std::string typedJoined() const {
        std::string out;
        for (const std::string& piece : typed) {
            out += piece;
        }
        return out;
    }

    std::unique_ptr<BrailleInput> input;
    std::vector<std::string> typed;
    std::vector<std::string> keys;
    std::vector<std::string> messages;
    std::vector<std::string> spokenDots;
    int displayUpdates = 0;
    bool canType = true;
    bool canEmulate = true;
    bool protectedField = false;
    bool focusIsText = true;

private:
    alus::braille::Louis* louis_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// The identifiers -- a user's gestures.ini depends on every one of these
// ---------------------------------------------------------------------------

TEST_CASE("the three reserved dot combinations are bindable on their own",
          "[braille][input]") {
    // These three ARE the manual test of 02-FAZE.md, as identifiers: dot 7
    // erases, dot 8 is enter, dot 7+8 translates. Each has to reach the
    // script dispatcher by its own name, or binding it is impossible.
    const std::vector<std::string> erase = InputGesture{kDot7, false}.identifiers();
    REQUIRE_FALSE(erase.empty());
    CHECK(erase.front() == "bk:dot7");

    const std::vector<std::string> enter = InputGesture{kDot8, false}.identifiers();
    REQUIRE_FALSE(enter.empty());
    CHECK(enter.front() == "bk:dot8");

    const std::vector<std::string> translate =
        InputGesture{static_cast<std::uint8_t>(kDot7 | kDot8), false}.identifiers();
    REQUIRE_FALSE(translate.empty());
    CHECK(translate.front() == "bk:dot7+dot8");

    SECTION("and each also answers to the generic one, which is bound last") {
        CHECK(erase.back() == InputGesture::genericDotsIdentifier());
        CHECK(translate.back() == InputGesture::genericDotsIdentifier());
    }
}

TEST_CASE("a dot combination that is TEXT has no identifier of its own",
          "[braille][input]") {
    // `bk:dot1+dot2` must NOT be bindable: it is the letter "b". If it had its
    // own identifier, a user who bound the generic dots identifier to the
    // typing script would find that one letter did something else.
    const std::vector<std::string> letter = InputGesture{kB, false}.identifiers();
    REQUIRE(letter.size() == 1);
    CHECK(letter.front() == InputGesture::genericDotsIdentifier());

    SECTION("space with dots is its own generic, because it is a chord") {
        const std::vector<std::string> chord = InputGesture{kA, true}.identifiers();
        REQUIRE(chord.size() == 2);
        CHECK(chord.front() == "bk:dot1+space");
        CHECK(chord.back() == InputGesture::genericSpaceDotsIdentifier());
    }

    SECTION("space alone is neither: brltty sends it as a command key") {
        CHECK(InputGesture{0, false}.identifiers().empty());
    }
}

TEST_CASE("a braille key says what it is, in dot numbers", "[braille][input]") {
    CHECK(InputGesture{dots({1, 2, 5}), false}.displayName() == "dot 1 2 5");
    CHECK(InputGesture{0, true}.displayName() == "space");
    CHECK(InputGesture{kA, true}.displayName() == "space with dot 1");
    // Input help describes an identifier the user did NOT just press, which
    // is how the braille keys appear in the command list at all.
    const auto [source, key] = InputGesture::describeIdentifier("bk:dot1+dot2+dot5");
    CHECK(source == "braille keyboard");
    CHECK(key == "dot 1 2 5");
    CHECK(InputGesture::describeIdentifier("bk:dots").second == "any dots");
    CHECK(InputGesture::describeIdentifier("bk:space+dots").second == "space with any dots");
    // A braille key is not reported as a command: the dots are what the user
    // typed, and announcing "dot 1" before every letter would be unusable.
    CHECK_FALSE(InputGesture{kA, false}.shouldReportAsCommand());
}

TEST_CASE("a display's own key is addressable per driver, per model and per cell",
          "[braille][input]") {
    const DisplayGesture thumb{"brlapi", "", "fwinrt"};
    REQUIRE(thumb.identifiers() == std::vector<std::string>{"br(brlapi):fwinrt"});

    SECTION("a model-specific binding is offered first, so it wins the lookup") {
        const DisplayGesture known{"brlapi", "focus40", "fwinrt"};
        CHECK(known.identifiers() ==
              std::vector<std::string>{"br(brlapi.focus40):fwinrt", "br(brlapi):fwinrt"});
    }

    SECTION("a routing key carries the cell it was pressed over, 1 based") {
        const DisplayGesture route{"brlapi", "", "route", {6}};
        CHECK(route.identifiers() ==
              std::vector<std::string>{"br(brlapi):route7", "br(brlapi):route"});
        CHECK(route.cellIndexes() == std::vector<int>{6});
        CHECK(route.displayName() == "brlapi route 7");
    }

    SECTION("two routing keys at once are a different command: multiRouting") {
        CHECK(DisplayGesture::idForCellCount(1, "routing") == "routing");
        CHECK(DisplayGesture::idForCellCount(2, "routing") == "multiRouting");
    }
}

// ---------------------------------------------------------------------------
// Typing
// ---------------------------------------------------------------------------

TEST_CASE("uncontracted braille sends one character per cell", "[braille][input]") {
    InputFixture fixture;
    REQUIRE_FALSE(fixture.input->isContracted());

    fixture.type({kA, kB, kC});
    CHECK(fixture.typedJoined() == "abc");
    // Nothing is left waiting: every cell has already become text, so there
    // is nothing to show between the indicator cells.
    CHECK(fixture.input->untranslatedBraille().empty());

    SECTION("a blank cell is a space, and it ends the word") {
        fixture.type({kSpace});
        CHECK(fixture.typedJoined() == "abc ");
        CHECK(fixture.input->bufferCells().empty());
    }
}

TEST_CASE("dot 7 erases, dot 8 enters, dot 7+8 translates", "[braille][input]") {
    InputFixture fixture;

    SECTION("dot 7 with nothing typed is an ordinary backspace") {
        fixture.input->eraseLastCell();
        REQUIRE(fixture.keys.size() == 1);
        CHECK(fixture.keys.front() == "backspace");
    }

    SECTION("dot 7 after a character backspaces that character too") {
        fixture.type({kA});
        REQUIRE(fixture.typedJoined() == "a");
        fixture.input->eraseLastCell();
        REQUIRE(fixture.keys.size() == 1);
        CHECK(fixture.keys.front() == "backspace");
    }

    SECTION("dot 8 is enter") {
        fixture.input->enter();
        REQUIRE(fixture.keys.size() == 1);
        CHECK(fixture.keys.front() == "enter");
    }

    SECTION("dot 7+8 translates what is pending and presses nothing") {
        fixture.input->translate();
        CHECK(fixture.keys.empty());
    }
}

TEST_CASE("contracted braille waits for the end of the word and shows the cells",
          "[braille][input]") {
    // This is the whole reason braille input is buffered at all: in grade 2
    // "tg" is two letters and "tgr" is the word "together", so a cell can
    // change what the cells in front of it mean. Until the word ends the
    // cells are shown on the display rather than typed.
    InputFixture fixture{"en-us-g2.ctb"};
    REQUIRE(fixture.input->isContracted());
    REQUIRE(fixture.input->useContractedForCurrentFocus());

    fixture.type({dots({2, 3, 4, 5}), dots({1, 2, 4, 5})});  // "t", "g"
    CHECK(fixture.typed.empty());
    // Two cells typed, two cells shown.
    CHECK(fixture.input->untranslatedBraille() == "⠞⠛");
    CHECK(fixture.displayUpdates > 0);

    SECTION("the space ends the word and the whole word is sent at once") {
        fixture.type({kSpace});
        CHECK(fixture.typedJoined() == "tg ");
        CHECK(fixture.input->untranslatedBraille().empty());
    }

    SECTION("a password field masks every cell, as speech masks the character") {
        fixture.protectedField = true;
        fixture.type({dots({1, 3, 5})});
        // All eight dots, once per cell: three cells typed, three masked.
        CHECK(fixture.input->untranslatedBraille() == "⣿⣿⣿");
        // And the dots are not said out loud either.
        CHECK(fixture.spokenDots.empty());
    }

    SECTION("a focus change drops a half-typed word") {
        fixture.input->handleGainFocus();
        CHECK(fixture.input->untranslatedBraille().empty());
        CHECK(fixture.input->bufferCells().empty());
    }

    SECTION("the user moving the caret away drops it too") {
        fixture.input->handleCaretMove();
        CHECK(fixture.input->bufferCells().empty());
    }
}

TEST_CASE("a modifier turns the next cells into a command", "[braille][input]") {
    InputFixture fixture;
    fixture.input->toggleModifiers({"control"});
    REQUIRE(fixture.input->currentModifiers() == std::set<std::string>{"control"});
    REQUIRE_FALSE(fixture.messages.empty());
    CHECK(fixture.messages.back() == "control");

    fixture.type({kC});
    REQUIRE_FALSE(fixture.keys.empty());
    CHECK(fixture.keys.back() == "control+c");
    // The modifier is one-shot: it applies to the chord and is then released,
    // as a sticky modifier on a braille keyboard must be.
    CHECK(fixture.input->currentModifiers().empty());
    // And the text was NOT typed into the document.
    CHECK(fixture.typed.empty());

    SECTION("pressing the same toggle again releases it") {
        fixture.input->toggleModifiers({"alt"});
        CHECK(fixture.input->currentModifiers() == std::set<std::string>{"alt"});
        fixture.input->toggleModifiers({"alt"});
        CHECK(fixture.input->currentModifiers().empty());
        CHECK(fixture.messages.back() == "alt released");
    }
}

TEST_CASE("a session that cannot type says so, once (N18)", "[braille][input]") {
    // Wayland: `atspi_generate_keyboard_event` is a no-op and there is no
    // portal for keystroke injection. The text must not vanish quietly.
    InputFixture fixture;
    fixture.canType = false;
    fixture.canEmulate = false;

    fixture.type({kA, kB, kC});
    REQUIRE_FALSE(fixture.messages.empty());
    CHECK(fixture.messages.front() == "Braille typing is not available in this session");
    // Once per session, not once per cell: three characters, one message.
    std::size_t said = 0;
    for (const std::string& message : fixture.messages) {
        if (message == "Braille typing is not available in this session") {
            ++said;
        }
    }
    CHECK(said == 1);
}
