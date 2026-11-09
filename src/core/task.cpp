// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/task.hpp"

#include <format>

#include "alus/core/log.hpp"
#include "alus/core/watchdog.hpp"

namespace alus::core {

Task Task::promise_type::get_return_object() {
    return Task{Handle::from_promise(*this)};
}

void Task::destroy() noexcept {
    if (handle_) {
        handle_.destroy();
        handle_ = {};
    }
}

void Task::resume() {
    if (!handle_ || handle_.done()) {
        return;
    }
    handle_.resume();
    if (std::exception_ptr error = handle_.promise().error) {
        handle_.promise().error = {};
        std::rethrow_exception(error);
    }
}

TaskRegistry::TaskRegistry(Watchdog* watchdog) : watchdog_(watchdog) {}

TaskRegistry::Id TaskRegistry::add(Task task, std::string name) {
    const Id id = ++nextId_;
    tasks_.emplace(id, Entry{std::make_shared<Task>(std::move(task)), std::move(name)});
    ALUS_DEBUG("task {} registered", id);
    return id;
}

bool TaskRegistry::cancel(Id id) {
    return tasks_.erase(id) > 0;
}

void TaskRegistry::cancelAll() {
    tasks_.clear();
}

bool TaskRegistry::has(Id id) const {
    return tasks_.find(id) != tasks_.end();
}

std::size_t TaskRegistry::size() const {
    return tasks_.size();
}

bool TaskRegistry::pumpAll() {
    // The map can mutate during iteration (a task may register or cancel tasks,
    // including itself), so iterate over a snapshot of the ids — NVDA:
    // `for ID in list(generators)`.
    std::vector<Id> ids;
    ids.reserve(tasks_.size());
    for (const auto& [id, entry] : tasks_) {
        ids.push_back(id);
    }

    for (const Id id : ids) {
        const auto it = tasks_.find(id);
        if (it == tasks_.end()) {
            continue;  // cancelled meanwhile; that is fine
        }
        // Hold a reference so a task that cancels itself does not have its own
        // frame destroyed underneath it — NVDA holds `gen` for the same reason.
        const std::shared_ptr<Task> task = it->second.task;
        const std::string name = it->second.name;

        if (watchdog_ != nullptr) {
            watchdog_->alive();
        }

        try {
            task->resume();
        } catch (...) {
            ++failed_;
            Log::instance().exception(
                name.empty() ? std::format("error in task {}", id)
                             : std::format("error in task {} ({})", id, name));
            tasks_.erase(id);
            continue;
        }

        if (task->done()) {
            ALUS_DEBUG("task {} finished", id);
            ++completed_;
            tasks_.erase(id);
        }
    }

    return !tasks_.empty();
}

}  // namespace alus::core
