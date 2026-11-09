// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/watchdog.hpp"

#include "alus/core/log.hpp"

namespace alus::core {
namespace {

std::int64_t nowNs() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

}  // namespace

Watchdog::Watchdog(WatchdogConfig config) : config_(config) {
    lastAliveNs_.store(nowNs(), std::memory_order_relaxed);
}

Watchdog::~Watchdog() {
    stop();
}

void Watchdog::start() {
    if (running_.exchange(true)) {
        return;
    }
    lastAliveNs_.store(nowNs(), std::memory_order_relaxed);
    watcher_ = std::thread([this] { watcherLoop(); });
}

void Watchdog::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    {
        // Taken so the watcher cannot be between its predicate check and its wait.
        const std::lock_guard lock{mutex_};
        wake_.notify_all();
    }
    if (watcher_.joinable()) {
        watcher_.join();
    }
    // A stopped watchdog must not keep the input layer in bypass mode.
    attemptingRecovery_.store(false, std::memory_order_release);
    cancelCalls_.store(false, std::memory_order_release);
}

bool Watchdog::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void Watchdog::alive() noexcept {
    lastAliveNs_.store(nowNs(), std::memory_order_relaxed);
    coreAsleep_.store(false, std::memory_order_relaxed);
    // Loads first: in the normal case this is a cheap read and no store at all.
    if (cancelCalls_.load(std::memory_order_relaxed)) {
        cancelCalls_.store(false, std::memory_order_release);
    }
}

void Watchdog::asleep() noexcept {
    alive();
    coreAsleep_.store(true, std::memory_order_relaxed);
}

bool Watchdog::coreAsleep() const noexcept {
    return coreAsleep_.load(std::memory_order_relaxed);
}

bool Watchdog::isAttemptingRecovery() const noexcept {
    return attemptingRecovery_.load(std::memory_order_acquire);
}

bool Watchdog::callCancelRequested() const noexcept {
    return cancelCalls_.load(std::memory_order_acquire);
}

std::uint64_t Watchdog::freezeCount() const noexcept {
    return freezes_.load(std::memory_order_relaxed);
}

std::chrono::steady_clock::time_point Watchdog::lastAlive() const noexcept {
    return std::chrono::steady_clock::time_point{
        std::chrono::steady_clock::duration{lastAliveNs_.load(std::memory_order_relaxed)}};
}

void Watchdog::watcherLoop() {
    using namespace std::chrono;

    Log::setThreadName("watchdog");

    std::unique_lock lock{mutex_};
    while (running_.load(std::memory_order_acquire)) {
        wake_.wait_for(lock, config_.pollInterval,
                       [this] { return !running_.load(std::memory_order_acquire); });
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }
        if (coreAsleep_.load(std::memory_order_relaxed)) {
            continue;
        }

        const auto stuckFor = steady_clock::now() - lastAlive();
        if (stuckFor < config_.minAliveTimeout) {
            continue;
        }

        // ---- the core is stuck: let keystrokes through, abandon platform calls ----
        const auto freezeStart = lastAlive();
        attemptingRecovery_.store(true, std::memory_order_release);
        cancelCalls_.store(true, std::memory_order_release);
        freezes_.fetch_add(1, std::memory_order_relaxed);
        lock.unlock();
        ALUS_INFO("core stuck for {} ms, recovery started",
                  duration_cast<milliseconds>(stuckFor).count());
        // Outside the lock, and before the recovery wait below: a handler here
        // gives the user back whatever the frozen core is holding — in phase 3,
        // the keyboard (N19).
        freezeDetected_.notify();
        lock.lock();

        bool warned = false;
        while (running_.load(std::memory_order_acquire) &&
               !coreAsleep_.load(std::memory_order_relaxed) &&
               steady_clock::now() - lastAlive() >= config_.minAliveTimeout) {
            if (!warned && steady_clock::now() - freezeStart >= config_.frozenWarningTimeout) {
                warned = true;
                lock.unlock();
                ALUS_ERROR("core frozen for {} ms",
                           duration_cast<milliseconds>(steady_clock::now() - freezeStart).count());
                lock.lock();
            }
            wake_.wait_for(lock, config_.pollInterval,
                           [this] { return !running_.load(std::memory_order_acquire); });
        }

        attemptingRecovery_.store(false, std::memory_order_release);
        // `cancelCalls_` is cleared by alive(), mirroring NVDA's ResetEvent, so a
        // call abandoned mid-flight is not retried before the core breathes again.
        if (running_.load(std::memory_order_acquire)) {
            const auto frozenFor = duration_cast<milliseconds>(steady_clock::now() - freezeStart);
            lock.unlock();
            ALUS_INFO("core recovered after {} ms", frozenFor.count());
            lock.lock();
        }
    }
}

}  // namespace alus::core
