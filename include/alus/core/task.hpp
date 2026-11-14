// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Cooperative tasks: one step per pump cycle.
//
// Port of NVDA's registered generator objects (`queueHandler.py:80-102`), the
// cooperative multitasking behind say-all and buffer loading. See
// architecture.md §3.2: these become C++20 coroutines, resumed exactly once
// per cycle, so no single task can monopolise the core.
//
// A task yields with `co_yield nextCycle;` (or `co_await nextCycle;`), which
// means "continue in the next pump cycle".
//
// THREADING: main thread only, like everything that touches core state (§13).
#pragma once

#include <coroutine>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace alus::core {

class Watchdog;

// The yield/await tag. `co_yield nextCycle;` reads as what it does.
struct NextCycleTag {};
inline constexpr NextCycleTag nextCycle{};

struct NextCycleAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

inline NextCycleAwaiter operator co_await(NextCycleTag) noexcept {
    return {};
}

class Task {
public:
    struct promise_type {
        Task get_return_object();

        // The body does not start until the first pump cycle — like a Python
        // generator, which runs nothing before the first next().
        std::suspend_always initial_suspend() const noexcept { return {}; }

        // Stay suspended at the end so `done()` is observable; the frame is
        // destroyed by the Task destructor.
        std::suspend_always final_suspend() const noexcept { return {}; }

        std::suspend_always yield_value(NextCycleTag) const noexcept { return {}; }

        void return_void() const noexcept {}

        void unhandled_exception() noexcept { error = std::current_exception(); }

        std::exception_ptr error;
    };

    using Handle = std::coroutine_handle<promise_type>;

    Task() = default;
    explicit Task(Handle handle) : handle_(handle) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~Task() { destroy(); }

    bool valid() const noexcept { return static_cast<bool>(handle_); }
    bool done() const noexcept { return !handle_ || handle_.done(); }

    // One step. Rethrows an exception that escaped the coroutine body, so the
    // registry can log it and drop the task.
    void resume();

private:
    void destroy() noexcept;

    Handle handle_{};
};

// The set of live tasks. `pumpAll()` gives each exactly one step per cycle.
class TaskRegistry {
public:
    using Id = std::uint64_t;

    // `watchdog` may be null (unit tests, or a core without freeze detection).
    explicit TaskRegistry(Watchdog* watchdog = nullptr);

    TaskRegistry(const TaskRegistry&) = delete;
    TaskRegistry& operator=(const TaskRegistry&) = delete;

    // Registers a task; it takes its first step in the next pump cycle.
    // `name` appears in the log if the task throws.
    Id add(Task task, std::string name = {});

    // NVDA: cancelGeneratorObject. Cancelling an unknown id is not an error.
    // A task may cancel ITSELF from inside its own step; the frame stays alive
    // until that step returns.
    bool cancel(Id id);
    void cancelAll();

    bool has(Id id) const;
    std::size_t size() const;

    // One resume() per task. Returns true if any task is still alive, which is
    // the Loop's signal to request another pump (the self-sustaining tick).
    bool pumpAll();

    std::uint64_t completed() const noexcept { return completed_; }
    std::uint64_t failed() const noexcept { return failed_; }

private:
    struct Entry {
        std::shared_ptr<Task> task;
        std::string name;
    };

    // Ordered map so the pump order is deterministic and matches registration
    // order, as NVDA's insertion-ordered dict does.
    std::map<Id, Entry> tasks_;
    Id nextId_ = 0;
    Watchdog* watchdog_;
    std::uint64_t completed_ = 0;
    std::uint64_t failed_ = 0;
};

}  // namespace alus::core
