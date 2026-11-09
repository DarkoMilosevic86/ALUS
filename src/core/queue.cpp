// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
#include "alus/core/queue.hpp"

#include "alus/core/log.hpp"
#include "alus/core/watchdog.hpp"

#include <utility>

namespace alus::core {

Queue::Queue(Watchdog* watchdog) : watchdog_(watchdog) {}

void Queue::push(Function function, std::string name) {
    if (!function) {
        return;
    }
    const std::lock_guard lock{mutex_};
    items_.push_back(Item{std::move(function), std::move(name)});
}

bool Queue::empty() const {
    const std::lock_guard lock{mutex_};
    return items_.empty();
}

std::size_t Queue::size() const {
    const std::lock_guard lock{mutex_};
    return items_.size();
}

void Queue::flush() {
    // NVDA: range(qsize() + 1). The bound is mandatory: a function that
    // re-queues itself must not keep the pump in this loop forever.
    std::size_t bound = 0;
    {
        const std::lock_guard lock{mutex_};
        bound = items_.size() + 1;
    }

    for (std::size_t i = 0; i < bound; ++i) {
        Item item;
        {
            const std::lock_guard lock{mutex_};
            if (items_.empty()) {
                return;
            }
            item = std::move(items_.front());
            items_.pop_front();
        }

        // Heartbeat per item: a long queue is progress, not a freeze.
        if (watchdog_ != nullptr) {
            watchdog_->alive();
        }

        try {
            item.function();
        } catch (...) {
            {
                const std::lock_guard lock{mutex_};
                ++failed_;
            }
            Log::instance().exception(item.name.empty() ? "error in queued function"
                                                        : "error in queued function " + item.name);
        }
        const std::lock_guard lock{mutex_};
        ++executed_;
    }
}

std::uint64_t Queue::executed() const {
    const std::lock_guard lock{mutex_};
    return executed_;
}

std::uint64_t Queue::failed() const {
    const std::lock_guard lock{mutex_};
    return failed_;
}

}  // namespace alus::core
