// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The core loop and the pump.
//
// Port of NVDA's `CorePump` (`core.py:990-1049`) and `requestPump`
// (`core.py:1180-1199`). See architecture.md §3.1.
//
// Like NVDA, ALUS has no loop of its own: the loop belongs to the toolkit
// (`GMainLoop` here, `wx.MainLoop()` there), and the core's work is driven by a
// pump that is nudged on request. `requestPump()` is a COALESCER: N requests
// between two pumps cause at most one pump.
//
// Three invariants that must not change (§3.1):
//   1. platform events are drained BEFORE the queue, so that within the same
//      cycle they are converted into work and that work is executed;
//   2. `invalidateAllCaches()` at the end of each cycle defines the cache
//      lifetime;
//   3. the pump is not re-entrant — re-entry goes through the queue, never
//      directly, or a flood of immediate pumps overflows the stack.
#pragma once

#include "alus/core/queue.hpp"
#include "alus/core/task.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

using GMainContext = struct _GMainContext;
using GMainLoop = struct _GMainLoop;
using GSource = struct _GSource;

namespace alus::core {

class Watchdog;

enum class PumpRequest {
    None,
    Delayed,    // slightly delayed, so queues can rate-limit and filter
    Immediate,  // as soon as possible: user input, focus events
};

// The fixed order of one cycle. The order IS the contract, not a preference —
// handlers are run stage by stage in this exact sequence.
enum class PumpStage {
    // Platform events (AT-SPI) are converted and queued first (invariant 1).
    PlatformEvents,
    // Built into the Loop: one step per coroutine task, then the queue flush.
    Core,
    Mouse,
    Braille,
    Vision,
};

// At namespace scope, not nested in Loop: a nested class's default member
// initializers are not usable in a default argument of the enclosing class.
struct LoopConfig {
    // NVDA: PUMP_MAX_DELAY. NOT arbitrary — it exists so events can be grouped.
    // Do not lower it without measuring (§3.1).
    std::chrono::milliseconds pumpMaxDelay{10};
    // Null means the default GLib context, which is what libatspi delivers its
    // signals to. Tests pass a private context.
    GMainContext* context = nullptr;
};

class Loop {
public:
    using StageHandler = std::function<void()>;
    using HandlerId = std::uint64_t;
    using TimerId = std::uint64_t;

    // Must be constructed on the main thread: the thread that constructs the
    // Loop is the thread the whole core state belongs to (§13).
    explicit Loop(Watchdog* watchdog = nullptr, LoopConfig config = {});
    ~Loop();

    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;

    Queue& queue() noexcept { return queue_; }
    TaskRegistry& tasks() noexcept { return tasks_; }

    // NVDA: queueHandler.queueFunction(). Callable from any thread.
    void queueFunction(Queue::Function function, bool immediate = false, std::string name = {});

    // NVDA: core.requestPump(). Callable from any thread.
    void requestPump(bool immediate = false);

    // Subsystems plug into a stage; within a stage, handlers run in
    // registration order. Main thread only.
    HandlerId addStageHandler(PumpStage stage, std::string name, StageHandler handler);
    bool removeStageHandler(HandlerId id);

    // NVDA: core.callLater(). The function runs on the main thread, inside a
    // pump cycle, no earlier than `delay`. Callable from any thread.
    TimerId callLater(std::chrono::milliseconds delay, Queue::Function function,
                      std::string name = {});
    bool cancelLater(TimerId id);

    // Runs the GLib loop until quit(). This is the daemon's main call.
    void run();
    // Callable from any thread, including a signal-handler-driven idle source.
    void quit();
    bool isRunning() const noexcept;

    // One GLib context iteration. Tests drive the pump with these instead of
    // run(), so no cycle depends on wall-clock luck.
    bool iterate(bool mayBlock);
    // Iterates until the context has nothing left to do, with a bound so a
    // self-sustaining tick cannot hang a test.
    std::size_t iterateUntilIdle(std::size_t maxIterations = 10000);

    PumpRequest pendingRequest() const noexcept;
    bool isPumping() const noexcept { return isPumping_; }
    std::uint64_t tickCount() const noexcept { return ticks_; }
    GMainContext* context() const noexcept { return context_; }

private:
    struct Handler {
        PumpStage stage;
        HandlerId id;
        std::string name;
        StageHandler handler;
    };

    // NVDA: CorePump.Notify — one cycle, fixed order.
    void tick();
    // NVDA: CorePump.queueRequest / processRequest.
    void queueRequest();
    void processRequest();
    void stopDelayed();
    bool onMainThread() const noexcept;
    void runStage(PumpStage stage);
    void attachIdle();
    void attachDelayed();

    static int onIdleRequest(void* self);
    static int onDelayedPump(void* self);
    static int onCallLater(void* data);

    Watchdog* watchdog_;
    LoopConfig config_;
    GMainContext* context_ = nullptr;
    GMainLoop* mainLoop_ = nullptr;
    std::thread::id mainThread_;

    Queue queue_;
    TaskRegistry tasks_;

    // `pending_` is written from any thread (AT-SPI callbacks, the SSIP reader),
    // so the coalescer is a compare-exchange rather than NVDA's GIL-protected
    // assignment.
    std::atomic<PumpRequest> pending_{PumpRequest::None};
    bool isPumping_ = false;
    std::uint64_t ticks_ = 0;

    GSource* delayedSource_ = nullptr;  // main thread only

    std::vector<Handler> handlers_;
    HandlerId nextHandlerId_ = 0;

    struct Timer;
    std::map<TimerId, Timer*> timers_;
    TimerId nextTimerId_ = 0;
};

}  // namespace alus::core
