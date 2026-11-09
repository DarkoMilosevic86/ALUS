// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The braille display itself, and the one write that reaches it.
//
// Port of NVDA's `braille/display/driver.py` (the driver interface) and of the
// SINGLE-SLOT WRITE of `BrailleHandler._writeCells` /
// `_bgThreadExecutor` -- 01-ARHITEKTURA.md §13 names it as one of the two
// coalescing mechanisms that are "not optional".
//
// WHY A SINGLE SLOT AND NOT A QUEUE. A braille display takes 10-40 ms to
// accept a frame over USB or Bluetooth. If fifty caret movements each queued a
// frame, the user would feel the display lag a second behind their fingers and
// would read forty-nine frames nobody wants. INTERMEDIATE FRAMES ARE
// WORTHLESS: only the newest state is true. So there is exactly one slot, a
// newer frame REPLACES the one waiting, and the writer takes whatever is there
// when it is ready.
//
// WHEN THE SLOT IS DRAINED, and this is a deliberate deviation from NVDA.
// NVDA drains it from an I/O thread as soon as the previous write finishes, so
// the number of writes depends on how fast the hardware is. ALUS drains it
// ONCE PER PUMP CYCLE, in `PumpStage::Braille` -- which §3.1 already defines
// as the moment the cycle's state is final. The consequence is a measurement
// instead of a race: N frames stored in one cycle are exactly one write, and
// T7.6 asserts it (claude/FAZA7.md O7).
//
// THE WORKER THREAD IS STILL THERE, because a `brlapi_writeDots` to a
// disconnected Bluetooth display can block for seconds and that must not stop
// the pump. With no worker (the tests, and the null display) the flush writes
// inline, which is the same semantics without the thread.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace alus::braille {

// NVDA: `braille.display.DisplayDimensions`. A single-line display has
// numRows == 1; a multi-line one (a tactile graphics display) has more, and
// the window is then laid out row by row.
struct DisplayDimensions {
    int numRows = 0;
    int numCols = 0;

    int size() const noexcept { return numRows > 0 && numCols > 0 ? numRows * numCols : 0; }
    bool operator==(const DisplayDimensions& other) const noexcept {
        return numRows == other.numRows && numCols == other.numCols;
    }
    bool operator!=(const DisplayDimensions& other) const noexcept {
        return !(*this == other);
    }
};

// One key press from the display's own keys, as the driver reports it. The
// gesture identifiers are built from this by `braille::DisplayGesture`.
struct DisplayKey {
    // The brltty command name, lower case: "fwinlt", "lnup", "route".
    std::string command;
    // The cells the key addresses, 0 based. A routing key over cell 7 gives
    // {7}; a chord of two routing keys gives both.
    std::vector<int> cellIndexes;
    // Braille keyboard input: the dots pressed, and whether space was held.
    std::uint8_t dots = 0;
    bool space = false;
    bool isDots = false;
};

class IDisplay {
public:
    virtual ~IDisplay();

    IDisplay(const IDisplay&) = delete;
    IDisplay& operator=(const IDisplay&) = delete;

    // The driver name, which is also the gesture source: `br(brlapi):fwinlt`.
    virtual std::string_view name() const = 0;
    // False with `error` filled when there is no display to talk to. NOT an
    // ALUS error: most machines have no braille display, and §3.8 says an
    // expected failure is reported, not logged as a fault.
    virtual bool initialize(std::string* error) = 0;
    virtual void terminate() = 0;

    virtual DisplayDimensions dimensions() const = 0;
    // Writes one frame. Called from the writer (a worker thread when there is
    // one), never from the pump.
    virtual bool display(const std::vector<std::uint8_t>& cells) = 0;
    // Whether `display()` may be called from the worker thread.
    virtual bool isThreadSafe() const { return false; }

    // Drains the display's own key presses. Called once per pump cycle, on
    // the main thread.
    virtual std::vector<DisplayKey> readKeys() { return {}; }

    // Whether ALUS wants the braille KEYBOARD's dots for itself. With it off
    // (the default) the driver asks brltty to type them, which is the only
    // way a braille keyboard types anything at all on Wayland (input.hpp).
    virtual void setBrailleInputEnabled(bool /*enabled*/) {}

    // The hardware's own model name, for the model-specific gesture
    // identifiers (`br(brlapi.brltty):...`). Empty when unknown.
    virtual std::string model() const { return {}; }

    virtual std::uint64_t writeCount() const noexcept { return writes_; }

protected:
    IDisplay() = default;
    void countWrite() noexcept { ++writes_; }

private:
    std::uint64_t writes_ = 0;
};

using DisplayPtr = std::unique_ptr<IDisplay>;

// NVDA: `noBraille`. Everything runs, nothing is written. What `--no-braille`
// and every test use, and what ALUS falls back to when there is no BrlAPI.
class NullDisplay final : public IDisplay {
public:
    explicit NullDisplay(DisplayDimensions dimensions = {});

    std::string_view name() const override { return "none"; }
    bool initialize(std::string* error) override;
    void terminate() override {}
    DisplayDimensions dimensions() const override { return dimensions_; }
    bool display(const std::vector<std::uint8_t>& cells) override;

    // What was last written, for the tests and for `alus-cli braille-info`.
    const std::vector<std::uint8_t>& lastFrame() const noexcept { return last_; }

private:
    DisplayDimensions dimensions_;
    std::vector<std::uint8_t> last_;
};

// The single slot of §13.
class DisplayWriter {
public:
    // `worker` starts a background thread; without one, `flush()` writes
    // inline on the calling thread.
    explicit DisplayWriter(bool worker = false);
    ~DisplayWriter();

    DisplayWriter(const DisplayWriter&) = delete;
    DisplayWriter& operator=(const DisplayWriter&) = delete;

    // Replaces whatever is waiting. Cheap, and safe to call as often as the
    // state changes.
    void store(std::vector<std::uint8_t> cells);
    // Hands the waiting frame to the display, if there is one. Called once per
    // pump cycle. Returns true when a frame was handed over.
    bool flush(IDisplay* display);
    // Drops the waiting frame: a display being taken down must not be written
    // to afterwards.
    void clear();
    // Blocks until the worker has finished the frame it is writing. Used by
    // terminate() and by the tests.
    void drain();

    // How many frames were stored, and how many reached the display. The
    // difference IS the coalescing, and `alus-cli braille-info` prints both.
    std::uint64_t storedCount() const noexcept { return stored_; }
    std::uint64_t writtenCount() const noexcept { return written_; }
    bool hasPending() const;

private:
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<std::vector<std::uint8_t>> pending_;
    IDisplay* target_ = nullptr;
    bool busy_ = false;
    bool stopping_ = false;
    std::thread thread_;
    std::atomic<std::uint64_t> stored_{0};
    std::atomic<std::uint64_t> written_{0};
};

// Builds the configured display. Never fails: an unknown or unavailable driver
// gives a `NullDisplay` and says why in `error`, because a screen reader whose
// braille display is missing must still speak.
DisplayPtr makeDisplay(const std::string& name, const std::string& host, std::string* error);
// Whether this build can talk to a physical display at all (libbrlapi was
// found). `alus-cli braille-info` prints it, and the manual test plan needs it.
bool hasDisplaySupport();

}  // namespace alus::braille
