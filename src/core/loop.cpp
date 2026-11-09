// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/loop.hpp"

#include "alus/core/cached.hpp"
#include "alus/core/log.hpp"
#include "alus/core/watchdog.hpp"

#include <glib.h>

#include <format>

#include <mutex>
#include <utility>

namespace alus::core {
namespace {

constexpr PumpStage kStageOrder[] = {
    PumpStage::PlatformEvents, PumpStage::Core,  PumpStage::Mouse,
    PumpStage::Braille,        PumpStage::Vision,
};

const char* stageName(PumpStage stage) {
    switch (stage) {
        case PumpStage::PlatformEvents:
            return "platform-events";
        case PumpStage::Core:
            return "core";
        case PumpStage::Mouse:
            return "mouse";
        case PumpStage::Braille:
            return "braille";
        case PumpStage::Vision:
            return "vision";
    }
    return "?";
}

std::mutex& timerMutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

// A callLater timer. Owned by the Loop until it fires or is cancelled.
struct Loop::Timer {
    Loop* loop = nullptr;
    TimerId id = 0;
    GSource* source = nullptr;
    Queue::Function function;
    std::string name;
};

Loop::Loop(Watchdog* watchdog, LoopConfig config)
    : watchdog_(watchdog),
      config_(config),
      context_(config.context != nullptr ? g_main_context_ref(config.context)
                                         : g_main_context_ref(g_main_context_default())),
      mainThread_(std::this_thread::get_id()),
      queue_(watchdog),
      tasks_(watchdog) {
    mainLoop_ = g_main_loop_new(context_, FALSE);
}

Loop::~Loop() {
    stopDelayed();
    {
        const std::lock_guard lock{timerMutex()};
        for (auto& [id, timer] : timers_) {
            g_source_destroy(timer->source);
            g_source_unref(timer->source);
            delete timer;
        }
        timers_.clear();
    }
    if (mainLoop_ != nullptr) {
        g_main_loop_unref(mainLoop_);
        mainLoop_ = nullptr;
    }
    if (context_ != nullptr) {
        g_main_context_unref(context_);
        context_ = nullptr;
    }
}

bool Loop::onMainThread() const noexcept {
    return std::this_thread::get_id() == mainThread_;
}

PumpRequest Loop::pendingRequest() const noexcept {
    return pending_.load(std::memory_order_acquire);
}

void Loop::queueFunction(Queue::Function function, bool immediate, std::string name) {
    queue_.push(std::move(function), std::move(name));
    requestPump(immediate);
}

// NVDA: core.requestPump (core.py:1180-1199). The coalescer: something only
// needs to happen if no pump is pending, or if a delayed one is pending and an
// immediate pump was just asked for.
void Loop::requestPump(bool immediate) {
    const PumpRequest desired = immediate ? PumpRequest::Immediate : PumpRequest::Delayed;
    PumpRequest current = pending_.load(std::memory_order_acquire);
    for (;;) {
        const bool worthDoing = current == PumpRequest::None ||
                                (immediate && current == PumpRequest::Delayed);
        if (!worthDoing) {
            return;
        }
        if (pending_.compare_exchange_weak(current, desired, std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
            break;
        }
    }
    queueRequest();
}

// NVDA: CorePump.queueRequest.
void Loop::queueRequest() {
    if (pending_.load(std::memory_order_acquire) == PumpRequest::Delayed && onMainThread()) {
        // Only a timer needs starting and we are already on the main thread,
        // so there is nothing to marshal.
        processRequest();
        return;
    }
    attachIdle();  // wx.CallAfter(self.processRequest)
}

// NVDA: CorePump.processRequest.
void Loop::processRequest() {
    if (isPumping_) {
        return;  // invariant 3: no re-entry
    }
    const PumpRequest pending = pending_.load(std::memory_order_acquire);
    if (pending == PumpRequest::Immediate) {
        stopDelayed();  // a delayed pump may be scheduled; cancel it
        tick();
    } else if (pending == PumpRequest::Delayed) {
        attachDelayed();
    }
}

void Loop::attachIdle() {
    GSource* source = g_idle_source_new();
    g_source_set_callback(source, GSourceFunc(&Loop::onIdleRequest), this, nullptr);
    g_source_attach(source, context_);
    g_source_unref(source);  // the context owns it now
}

void Loop::attachDelayed() {
    stopDelayed();  // wx.Timer.Start restarts a running timer
    delayedSource_ = g_timeout_source_new(guint(config_.pumpMaxDelay.count()));
    g_source_set_callback(delayedSource_, GSourceFunc(&Loop::onDelayedPump), this, nullptr);
    g_source_attach(delayedSource_, context_);
}

void Loop::stopDelayed() {
    if (delayedSource_ == nullptr) {
        return;
    }
    g_source_destroy(delayedSource_);
    g_source_unref(delayedSource_);
    delayedSource_ = nullptr;
}

int Loop::onIdleRequest(void* self) {
    static_cast<Loop*>(self)->processRequest();
    return G_SOURCE_REMOVE;
}

int Loop::onDelayedPump(void* self) {
    Loop* loop = static_cast<Loop*>(self);
    // One-shot: GLib detaches it when we return G_SOURCE_REMOVE, but our own
    // reference has to go too.
    if (loop->delayedSource_ != nullptr) {
        g_source_unref(loop->delayedSource_);
        loop->delayedSource_ = nullptr;
    }
    if (!loop->isPumping_) {
        loop->tick();
    }
    return G_SOURCE_REMOVE;
}

Loop::HandlerId Loop::addStageHandler(PumpStage stage, std::string name,
                                      StageHandler handler) {
    const HandlerId id = ++nextHandlerId_;
    handlers_.push_back(Handler{stage, id, std::move(name), std::move(handler)});
    return id;
}

bool Loop::removeStageHandler(HandlerId id) {
    for (auto it = handlers_.begin(); it != handlers_.end(); ++it) {
        if (it->id == id) {
            handlers_.erase(it);
            return true;
        }
    }
    return false;
}

void Loop::runStage(PumpStage stage) {
    // Iterate over a snapshot: a handler may register or remove handlers.
    const std::vector<Handler> snapshot = handlers_;
    for (const Handler& entry : snapshot) {
        if (entry.stage != stage) {
            continue;
        }
        // DELIBERATE DIVERGENCE FROM NVDA: NVDA wraps the whole pump body in one
        // try/except, so a throwing subsystem skips every later one in that
        // cycle — including the queue flush, which is where speech lives. Here
        // each handler is isolated, so a broken mouse handler cannot silence
        // speech for the cycle.
        try {
            entry.handler();
        } catch (...) {
            Log::instance().exception(
                std::format("error in {} stage handler {}", stageName(stage), entry.name));
        }
    }
}

// NVDA: CorePump.Notify (core.py:1015-1048). The order of operations is part of
// the contract — see the invariants in the header.
void Loop::tick() {
    if (isPumping_) {
        ALUS_ERROR("tick() while already pumping — the pump is not re-entrant");
        return;
    }
    isPumping_ = true;
    if (pending_.exchange(PumpRequest::None, std::memory_order_acq_rel) == PumpRequest::None) {
        ALUS_DEBUG_WARNING("pumping but no pump was pending");
    }
    ++ticks_;
    if (watchdog_ != nullptr) {
        watchdog_->alive();
    }

    // 1. Platform events first: converted and queued now, executed below in the
    //    same cycle.
    runStage(PumpStage::PlatformEvents);

    // 2. Core: one step per coroutine task, then the bounded queue flush.
    bool tasksAlive = false;
    try {
        tasksAlive = tasks_.pumpAll();
    } catch (...) {
        Log::instance().exception("error in task registry");
    }
    if (tasksAlive) {
        // NVDA: queueHandler.pumpAll ends with core.requestPump() while
        // generators remain — the self-sustaining tick. Requested from inside
        // the pump, so the tail below schedules it.
        requestPump(false);
    }
    try {
        queue_.flush();
    } catch (...) {
        Log::instance().exception("error in queue flush");
    }
    runStage(PumpStage::Core);

    runStage(PumpStage::Mouse);
    runStage(PumpStage::Braille);
    runStage(PumpStage::Vision);

    // 3. The cache lives exactly one cycle.
    try {
        invalidateAllCaches();
    } catch (...) {
        Log::instance().exception("invalidateAllCaches failed");
    }

    if (watchdog_ != nullptr) {
        watchdog_->asleep();
    }
    isPumping_ = false;

    // Another pump requested during this one: the pump is not re-entrant, so
    // re-entry is deferred. An immediate pump goes through the queue rather
    // than recursing, because a flood of immediate pumps would overflow the
    // stack (NVDA #3803).
    const PumpRequest pending = pending_.load(std::memory_order_acquire);
    if (pending == PumpRequest::Immediate) {
        queueRequest();
    } else if (pending == PumpRequest::Delayed) {
        processRequest();
    }
}

Loop::TimerId Loop::callLater(std::chrono::milliseconds delay, Queue::Function function,
                              std::string name) {
    if (!function) {
        return 0;
    }
    auto* timer = new Timer{};
    timer->loop = this;
    timer->function = std::move(function);
    timer->name = std::move(name);
    timer->source = g_timeout_source_new(guint(delay.count() < 0 ? 0 : delay.count()));
    g_source_set_callback(timer->source, GSourceFunc(&Loop::onCallLater), timer, nullptr);

    {
        const std::lock_guard lock{timerMutex()};
        timer->id = ++nextTimerId_;
        timers_.emplace(timer->id, timer);
    }
    const TimerId id = timer->id;
    g_source_attach(timer->source, context_);
    return id;
}

bool Loop::cancelLater(TimerId id) {
    Timer* timer = nullptr;
    {
        const std::lock_guard lock{timerMutex()};
        const auto it = timers_.find(id);
        if (it == timers_.end()) {
            return false;
        }
        timer = it->second;
        timers_.erase(it);
    }
    g_source_destroy(timer->source);
    g_source_unref(timer->source);
    delete timer;
    return true;
}

int Loop::onCallLater(void* data) {
    auto* timer = static_cast<Timer*>(data);
    Loop* loop = timer->loop;

    // Whoever removes the entry from the map owns the Timer; this closes the
    // race against cancelLater() from another thread.
    {
        const std::lock_guard lock{timerMutex()};
        if (loop->timers_.erase(timer->id) == 0) {
            return G_SOURCE_REMOVE;
        }
    }
    Queue::Function function = std::move(timer->function);
    std::string name = std::move(timer->name);
    g_source_unref(timer->source);
    delete timer;

    // The function runs on the main thread, inside a pump cycle.
    loop->queueFunction(std::move(function), false, std::move(name));
    return G_SOURCE_REMOVE;
}

void Loop::run() {
    Log::setThreadName("main");
    g_main_loop_run(mainLoop_);
}

void Loop::quit() {
    g_main_loop_quit(mainLoop_);
}

bool Loop::isRunning() const noexcept {
    return g_main_loop_is_running(mainLoop_) != FALSE;
}

bool Loop::iterate(bool mayBlock) {
    return g_main_context_iteration(context_, mayBlock ? TRUE : FALSE) != FALSE;
}

std::size_t Loop::iterateUntilIdle(std::size_t maxIterations) {
    std::size_t iterations = 0;
    while (iterations < maxIterations && g_main_context_pending(context_) != FALSE) {
        iterate(false);
        ++iterations;
    }
    return iterations;
}

}  // namespace alus::core
