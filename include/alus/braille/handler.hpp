// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The handler: what the display shows, and when it is written.
//
// Port of NVDA's `braille/brailleHandler.py` (`BrailleHandler`) and
// `braille/regions/focus.py` (`getFocusRegions`, `getFocusContextRegions`).
//
// 01-ARHITEKTURA.md §14 names TWO coalescing mechanisms and says neither is
// optional. Both are here, and they are the two exit criteria that measure
// this file:
//
//   `regionsPendingUpdate_`   a SET of regions, drained once per pump cycle.
//                             Fifty caret movements in one cycle are one
//                             region update and one write (T7.7).
//   the single-slot write     `DisplayWriter`. A hundred frames stored in one
//                             cycle are one write, the last (T7.6).
//
// WHY THAT IS NOT MICRO-OPTIMISATION. A braille display is the slowest output
// device a screen reader has: 10-40 ms per frame. Translating a region costs a
// liblouis call and, for a document, a `getTextWithFields` over D-Bus. A
// screen reader that did either per event would lag behind the user's own
// fingers, which is the one failure a braille user cannot work around.
//
// TETHERING is the other half of the file: the display follows either the
// FOCUS or the REVIEW cursor, and in `auto` it follows whichever moved last.
// `setTether(.., auto=true)` is how a focus event claims the display without
// overriding a user who pinned it.
//
// THREADING: everything here is main thread, except the actual cell write,
// which the writer may hand to its worker (display.hpp).
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/braille/buffer.hpp"
#include "alus/braille/display.hpp"
#include "alus/braille/louis.hpp"
#include "alus/braille/region.hpp"
#include "alus/braille/settings.hpp"
#include "alus/core/loop.hpp"
#include "alus/text/range.hpp"
#include "alus/text/unit.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace alus::a11y {
class Registry;
}

namespace alus::nav {
class BrowseMode;
class Service;
}

namespace alus::braille {

// What the handler needs from the rest of ALUS. Callables rather than
// pointers, for the reason every other subsystem does it this way: the whole
// braille layer can be driven in a test with no object model, no speech and
// no display.
struct HandlerHooks {
    std::function<a11y::Registry*()> registry;
    std::function<nav::Service*()> nav;
    // NVDA: `ui.message` for the braille-only messages the commands report.
    std::function<void(const std::string&)> message;
    // NVDA: `spellTextInfo`, for `speakOnRouting`.
    std::function<void(const text::TextRange&)> spellRange;
    // NVDA: `speakTextInfo` with reason CARET, for `speakOnNavigatingByUnit`.
    std::function<void(const text::TextRange&, text::TextUnit)> speakRange;
    std::function<void()> cancelSpeech;
};

// What the handler needs from the braille INPUT layer: the cells the user has
// typed but that have not yet become text. They are shown inside the text
// region, so the region has to be able to ask -- and the input handler has to
// be able to exist without the display, which is why this is an interface and
// not a pointer to the class.
class IBrailleInput {
public:
    virtual ~IBrailleInput() = default;

    virtual std::string untranslatedBraille() const = 0;
    virtual int untranslatedCursorPos() const = 0;
    virtual void setUntranslatedCursorPos(int position) = 0;
};

class Handler final : public RegionHost {
public:
    Handler(core::Loop& loop, Louis& louis, HandlerHooks hooks = {});
    ~Handler() override;

    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;

    // ---- lifecycle ----
    // Takes over the display and installs the pump stage. `display` may be a
    // NullDisplay, which is the normal case on a machine with no braille.
    void start(DisplayPtr display, Settings settings);
    void stop();
    bool isStarted() const noexcept { return started_; }

    // Re-reads the settings (SIGHUP). The table may have changed, so every
    // region is re-translated.
    void applySettings(Settings settings);
    const Settings& settings() const noexcept { return settings_; }

    // NVDA: `enabled`. False when there is no display to write to -- and the
    // whole braille layer then does nothing at all, which is what keeps the
    // cost of braille at zero for a user who has none.
    bool isEnabled() const;

    IDisplay* display() const noexcept { return display_.get(); }
    Buffer& mainBuffer() noexcept { return mainBuffer_; }
    Buffer& messageBuffer() noexcept { return messageBuffer_; }
    Buffer& currentBuffer() noexcept { return *buffer_; }
    const DisplayWriter& writer() const noexcept { return writer_; }

    // ---- RegionHost ----
    const Settings& brailleSettings() const override { return settings_; }
    const a11y::Settings& objectSettings() const override;
    const text::FormatReportSettings& formatSettings() const override;
    Louis& louis() const override { return *louis_; }
    std::vector<std::string> outputTables() const override;
    DisplayDimensions dimensions() const override { return dimensions_; }
    std::string untranslatedBraille() const override;
    int untranslatedCursorPos() const override;
    void setUntranslatedCursorPos(int position) override;
    void updateForUntranslatedInput() override;
    void speakCharacterAt(const text::TextRange& at) override;
    void speakUnitAt(const text::TextRange& at, text::TextUnit unit) override;
    void stopAutoScroll() override;
    bool routingShouldMoveSystemCaret() const override;

    // The braille input handler sets this so that untranslated cells appear
    // in the text region as the user types them.
    void setInput(IBrailleInput* input) noexcept { input_ = input; }

    // ---- the events ----
    // NVDA: `handleGainFocus`. `shouldAutoTether` false is what the tether
    // command passes: it has just decided the tether itself.
    void handleGainFocus(const a11y::ObjectPtr& object, bool shouldAutoTether = true);
    // NVDA: `handleCaretMove`.
    void handleCaretMove(const a11y::ObjectPtr& object, bool shouldAutoTether = true);
    // NVDA: `handleUpdate`. A property of an object changed.
    void handleUpdate(const a11y::ObjectPtr& object);
    // NVDA: `handleReviewMove`.
    void handleReviewMove(bool shouldAutoTether = true);
    // NVDA: `initialDisplay`. Shows whatever the focus or the review cursor is
    // on right now; called once after the display is connected.
    void initialDisplay();

    // ---- the pump (T7.6, T7.7) ----
    // NVDA: `braille.pumpAll` -> `_handlePendingUpdate`. Runs in
    // PumpStage::Braille.
    void pump();
    // How many regions are waiting. The T7.7 measurement reads it.
    std::size_t pendingUpdateCount() const noexcept { return regionsPendingUpdate_.size(); }

    // ---- the display content ----
    // NVDA: `update`. Recomputes the window and stores a frame.
    void update();
    void scrollForward();
    void scrollBack();
    void routeTo(int windowPos);
    // NVDA: `getTextInfoForWindowPos`.
    text::TextRangePtr rangeForWindowPos(int windowPos) const;

    // NVDA: `message`. A braille-only message with a timeout, dismissed by a
    // routing key or by the next thing written.
    void message(const std::string& text);
    void dismissMessage(bool shouldUpdate = true);
    bool isShowingMessage() const noexcept { return buffer_ == &messageBuffer_; }

    // ---- tethering ----
    TetherTo tether() const noexcept { return tether_; }
    void setTether(TetherTo tether, bool automatic = false);
    bool shouldAutoTether() const;

    // ---- automatic scroll ----
    void setAutoScroll(bool enable);
    bool isAutoScrolling() const noexcept { return autoScrollTimer_ != 0; }

    // ---- the braille mode (NVDA: BrailleMode) ----
    // In `SpeechOutput` the display shows what is spoken. Fed by the speech
    // extension point, as NVDA feeds it from `pre_speech`.
    void showSpeech(const std::string& text);
    void clearSpeechRegions();

    // The cells that would be written right now, for the tests and for
    // `alus-cli braille-info`.
    std::vector<std::uint8_t> currentCells() const { return cells_; }
    const std::string& currentRawText() const noexcept { return rawText_; }

    struct Stats {
        std::uint64_t regionUpdates = 0;
        std::uint64_t pendingUpdateBatches = 0;
        std::uint64_t coalescedRegionUpdates = 0;
        std::uint64_t framesStored = 0;
        std::uint64_t framesWritten = 0;
    };
    Stats stats() const;

    std::string describe() const;

private:
    // NVDA: `_doNewObject`.
    void showRegions(std::vector<RegionPtr> regions);
    // NVDA: `_handlePendingUpdate`.
    void handlePendingUpdate();
    // NVDA: `scrollToCursorOrSelection`.
    void scrollToCursorOrSelection(Region* region);
    // NVDA: `_displayWithCursor` and `_blink`.
    void writeWithCursor();
    void restartCursorBlink();
    void stopCursorBlink();
    void resetMessageTimer();
    void refreshDimensions();

    // NVDA: `getFocusRegions`.
    std::vector<RegionPtr> focusRegions(const a11y::ObjectPtr& object, bool review);
    // NVDA: `getFocusContextRegions`, including the ancestor cache.
    std::vector<RegionPtr> focusContextRegions(const a11y::ObjectPtr& object,
                                               std::vector<RegionPtr>& oldRegions);
    // The browse mode the focus is in, when it owns the caret.
    nav::BrowseMode* activeBrowseMode() const;
    // The object a focus event should build regions for: the document root
    // when browse mode owns the caret, the object itself otherwise.
    a11y::ObjectPtr effectiveFocus(const a11y::ObjectPtr& object) const;

    core::Loop* loop_;
    Louis* louis_;
    HandlerHooks hooks_;
    Settings settings_;
    DisplayPtr display_;
    DisplayDimensions dimensions_;
    DisplayWriter writer_;
    IBrailleInput* input_ = nullptr;

    Buffer mainBuffer_;
    Buffer messageBuffer_;
    Buffer* buffer_ = nullptr;

    // NVDA: `_regionsPendingUpdate`. A SET, and that is the mechanism: the
    // same region added fifty times is one entry.
    std::set<Region*> regionsPendingUpdate_;

    std::vector<std::uint8_t> cells_;
    std::string rawText_;
    std::optional<int> cursorPos_;
    bool cursorBlinkUp_ = true;
    core::Loop::TimerId cursorBlinkTimer_ = 0;
    core::Loop::TimerId messageTimer_ = 0;
    core::Loop::TimerId autoScrollTimer_ = 0;
    core::Loop::HandlerId pumpHandler_ = 0;

    TetherTo tether_ = TetherTo::Focus;
    // NVDA: `_cachedFocusAncestorsEnd`.
    int cachedFocusAncestorsEnd_ = 0;
    // NVDA keeps `_showSpeechInBrailleRegions`, a second list of regions
    // alongside the buffer's. ALUS keeps the TEXTS and rebuilds the regions,
    // because the buffer owns its regions and two owners of one region is the
    // kind of thing that works until a profile switch clears one of them.
    std::vector<std::string> speechTexts_;
    bool started_ = false;
    Stats stats_;
};

}  // namespace alus::braille
