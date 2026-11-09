// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The main-thread work queue.
//
// Port of NVDA's `queueHandler.py:61-69`. See 01-ARHITEKTURA.md §3.2: ONE FIFO.
// Priority is not expressed by having several queues, but by the `immediate`
// flag that changes how the pump is scheduled (`Loop::queue()`).
//
// Two properties are part of the contract, not implementation details:
//   1. the flush is BOUNDED to size()+1 items, so a function that re-queues
//      itself cannot wedge the pump;
//   2. a heartbeat is sent per item, so one slow item cannot look like a freeze
//      while the queue is still making progress.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace alus::core {

class Watchdog;

class Queue {
public:
    using Function = std::function<void()>;

    // `watchdog` may be null (unit tests, or a core built without freeze detection).
    explicit Queue(Watchdog* watchdog = nullptr);

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // Callable from ANY thread (AT-SPI callbacks, the SSIP reader, braille I/O).
    // `name` appears in the log if the function throws; keep it a literal.
    void push(Function function, std::string name = {});

    bool empty() const;
    std::size_t size() const;

    // Main thread only. Runs at most size()+1 items — see the class comment.
    void flush();

    std::uint64_t executed() const;
    std::uint64_t failed() const;

private:
    struct Item {
        Function function;
        std::string name;
    };

    mutable std::mutex mutex_;
    std::deque<Item> items_;
    Watchdog* watchdog_;
    std::uint64_t executed_ = 0;
    std::uint64_t failed_ = 0;
};

}  // namespace alus::core
