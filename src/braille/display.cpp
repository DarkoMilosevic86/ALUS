// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// See include/alus/braille/display.hpp.

#include "alus/braille/display.hpp"

#include "alus/braille/louis.hpp"
#include "alus/core/log.hpp"

#ifdef ALUS_HAVE_BRLAPI
#include "alus/braille/brlapi_display.hpp"
#endif

#include <utility>

namespace alus::braille {

IDisplay::~IDisplay() = default;

// ---------------------------------------------------------------------------
// NullDisplay
// ---------------------------------------------------------------------------

NullDisplay::NullDisplay(DisplayDimensions dimensions) : dimensions_(dimensions) {}

bool NullDisplay::initialize(std::string* /*error*/) { return true; }

bool NullDisplay::display(const std::vector<std::uint8_t>& cells) {
    last_ = cells;
    countWrite();
    return true;
}

// ---------------------------------------------------------------------------
// DisplayWriter
// ---------------------------------------------------------------------------

DisplayWriter::DisplayWriter(bool worker) {
    if (worker) {
        thread_ = std::thread{[this] { workerLoop(); }};
    }
}

DisplayWriter::~DisplayWriter() {
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        stopping_ = true;
        pending_.reset();
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void DisplayWriter::store(std::vector<std::uint8_t> cells) {
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        // THE SINGLE SLOT. A newer frame replaces the one waiting; nothing
        // accumulates, so nothing can lag.
        pending_ = std::move(cells);
    }
    ++stored_;
}

bool DisplayWriter::flush(IDisplay* display) {
    std::vector<std::uint8_t> frame;
    {
        std::unique_lock<std::mutex> lock{mutex_};
        if (!pending_.has_value() || display == nullptr) {
            return false;
        }
        if (thread_.joinable() && display->isThreadSafe()) {
            // Hand it to the worker: taking it out of the slot here is what
            // makes "one flush, at most one write" true even while the worker
            // is still busy with the previous frame.
            target_ = display;
            wake_.notify_one();
            return true;
        }
        frame = std::move(*pending_);
        pending_.reset();
    }
    if (!display->display(frame)) {
        ALUS_WARNING("braille: the display refused a frame of {} cells", frame.size());
        return false;
    }
    ++written_;
    return true;
}

void DisplayWriter::clear() {
    const std::lock_guard<std::mutex> lock{mutex_};
    pending_.reset();
    target_ = nullptr;
}

void DisplayWriter::drain() {
    std::unique_lock<std::mutex> lock{mutex_};
    wake_.wait(lock, [this] { return !busy_ && !pending_.has_value(); });
}

bool DisplayWriter::hasPending() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return pending_.has_value();
}

void DisplayWriter::workerLoop() {
    core::Log::setThreadName("braille-io");
    std::unique_lock<std::mutex> lock{mutex_};
    while (!stopping_) {
        wake_.wait(lock, [this] {
            return stopping_ || (pending_.has_value() && target_ != nullptr);
        });
        if (stopping_) {
            break;
        }
        std::vector<std::uint8_t> frame = std::move(*pending_);
        pending_.reset();
        IDisplay* display = target_;
        busy_ = true;
        lock.unlock();
        const bool ok = display->display(frame);
        lock.lock();
        busy_ = false;
        if (ok) {
            ++written_;
        } else {
            ALUS_WARNING("braille: the display refused a frame of {} cells", frame.size());
        }
        // `drain()` and a flush that arrived while this write was in flight
        // are both waiting on this.
        wake_.notify_all();
    }
    busy_ = false;
    wake_.notify_all();
}

// ---------------------------------------------------------------------------
// The factory
// ---------------------------------------------------------------------------

bool hasDisplaySupport() {
#ifdef ALUS_HAVE_BRLAPI
    return true;
#else
    return false;
#endif
}

DisplayPtr makeDisplay(const std::string& name, const std::string& host, std::string* error) {
    const auto fail = [&](std::string message) -> DisplayPtr {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::make_unique<NullDisplay>();
    };

    if (name.empty() || name == "none" || name == "noBraille") {
        return std::make_unique<NullDisplay>();
    }
#ifdef ALUS_HAVE_BRLAPI
    if (name == "brlapi" || name == "brltty") {
        auto display = std::make_unique<BrlapiDisplay>(host);
        std::string openError;
        if (display->initialize(&openError)) {
            return display;
        }
        return fail(openError.empty() ? "no braille display" : openError);
    }
#else
    (void)host;
    if (name == "brlapi" || name == "brltty") {
        return fail("this build has no libbrlapi");
    }
#endif
    return fail("unknown braille display driver: " + name);
}

}  // namespace alus::braille
