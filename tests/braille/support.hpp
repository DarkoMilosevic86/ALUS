// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The phase 7 test fixture: the phase 5 one, plus a real `liblouis`, a real
// `Handler` and a display that records every frame it is given.
//
// WHY A REAL liblouis AND NOT A FAKE TRANSLATOR. Every exit criterion of this
// phase is about what liblouis actually does: that `sr-Cyrl.ctb` translates
// Cyrillic (T7.1), that the typeform array survives the call (T7.2), that the
// cells arrive with bit 0x8000 set (T7.3), that the resolver finds an add-on's
// table (T7.4). A fake would assert on ALUS's own assumptions about the
// library, which is exactly the class of bug this phase exists to avoid.
//
// WHY A RECORDING DISPLAY AND NOT brltty. The two coalescing criteria (T7.6,
// T7.7) are counts of writes, and a count is only a measurement if nothing
// else can swallow a frame. `RecordingDisplay` keeps every frame and the
// order, so "a hundred frames in one cycle are one write" is an assertion
// about a number rather than about timing.
#pragma once

#include "alus/braille/buffer.hpp"
#include "alus/braille/display.hpp"
#include "alus/braille/handler.hpp"
#include "alus/braille/input.hpp"
#include "alus/braille/louis.hpp"
#include "alus/braille/region.hpp"
#include "alus/braille/settings.hpp"
#include "nav/support.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace alus::testing {

// Every frame, in order. `isThreadSafe()` is false on purpose: with no worker
// thread the writer flushes inline, so a flush is one write and the count is
// exact rather than probable.
class RecordingDisplay final : public braille::IDisplay {
public:
    explicit RecordingDisplay(braille::DisplayDimensions dimensions = {1, 20})
        : dimensions_(dimensions) {}

    std::string_view name() const override { return "recording"; }
    bool initialize(std::string* /*error*/) override { return true; }
    void terminate() override {}
    braille::DisplayDimensions dimensions() const override { return dimensions_; }
    bool isThreadSafe() const override { return false; }

    bool display(const std::vector<std::uint8_t>& cells) override {
        frames.push_back(cells);
        countWrite();
        return true;
    }

    std::vector<braille::DisplayKey> readKeys() override {
        std::vector<braille::DisplayKey> out = pending;
        pending.clear();
        return out;
    }

    void setBrailleInputEnabled(bool enabled) override { brailleInput = enabled; }

    void resize(braille::DisplayDimensions dimensions) { dimensions_ = dimensions; }

    const std::vector<std::uint8_t>& lastFrame() const {
        static const std::vector<std::uint8_t> empty;
        return frames.empty() ? empty : frames.back();
    }

    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<braille::DisplayKey> pending;
    bool brailleInput = false;

private:
    braille::DisplayDimensions dimensions_;
};

// A `RegionHost` with nothing behind it: the buffer and the word-wrap tests
// need a host and nothing else, and building one by hand keeps those tests
// independent of the handler, the object model and the display.
class TestHost final : public braille::RegionHost {
public:
    TestHost() {
        louis_ = &braille::Louis::shared();
        louis_->initialize();
        // 8-dot computer braille: one cell per character, no contractions and
        // no capital signs, so a test's expectation about WHICH cell is a
        // space is a statement about the wrap logic and not about the table.
        settings.translationTable = "en-us-comp8.ctb";
        settings.expandAtCursor = false;
        settings.cursorBlink = false;
    }

    const braille::Settings& brailleSettings() const override { return settings; }
    const a11y::Settings& objectSettings() const override { return object; }
    const text::FormatReportSettings& formatSettings() const override { return format; }
    braille::Louis& louis() const override { return *louis_; }
    std::vector<std::string> outputTables() const override {
        return {settings.translationTable, "braille-patterns.cti"};
    }
    braille::DisplayDimensions dimensions() const override { return size; }

    braille::Settings settings;
    a11y::Settings object;
    text::FormatReportSettings format;
    braille::DisplayDimensions size{1, 20};

private:
    braille::Louis* louis_ = nullptr;
};

class BrailleFixture : public NavFixture {
public:
    BrailleFixture() {
        louis_ = &braille::Louis::shared();
        louis_->initialize();

        braille::HandlerHooks hooks;
        hooks.registry = [this] { return &registry; };
        hooks.nav = [this] { return &nav(); };
        hooks.message = [this](const std::string& text) { messages.push_back(text); };
        hooks.spellRange = [this](const text::TextRange& range) {
            routedSpelling.push_back(range.text());
        };
        hooks.speakRange = [this](const text::TextRange& range, text::TextUnit) {
            routedSpeech.push_back(range.text());
        };

        handler_ = std::make_unique<braille::Handler>(loop(), *louis_, std::move(hooks));

        auto display = std::make_unique<RecordingDisplay>(braille::DisplayDimensions{1, 20});
        display_ = display.get();

        braille::Settings settings;
        settings.translationTable = "en-us-comp8.ctb";
        settings.inputTable = "en-us-comp8.ctb";
        // A blinking cursor would add a timer and a second frame per cycle,
        // and every write count in this suite would then depend on the clock.
        settings.cursorBlink = false;
        settings.expandAtCursor = false;
        settings.tetherTo = braille::TetherTo::Focus;
        handler_->start(std::move(display), settings);
    }

    ~BrailleFixture() { handler_->stop(); }

    braille::Handler& handler() noexcept { return *handler_; }
    RecordingDisplay& display() noexcept { return *display_; }
    braille::Louis& louis() noexcept { return *louis_; }

    void applySettings(const braille::Settings& settings) {
        handler_->applySettings(settings);
    }

    // One pump cycle, braille included: the phase 4 fixture's own cycle, then
    // the braille stage the handler installed. Tests call this instead of
    // running the loop, so no write count depends on wall-clock luck.
    void pumpBraille() {
        pumpOnce();
        handler_->pump();
    }

    // The window as dot numbers, which is what a failure message should show:
    // "1 12 - 145" reads, "[1,3,0,9]" does not.
    std::string windowDots() {
        return braille::formatCellsForLog(handler_->currentCells());
    }
    std::string windowText() { return handler_->currentRawText(); }

    std::vector<std::string> routedSpelling;
    std::vector<std::string> routedSpeech;

private:
    braille::Louis* louis_ = nullptr;
    std::unique_ptr<braille::Handler> handler_;
    RecordingDisplay* display_ = nullptr;
};

}  // namespace alus::testing
