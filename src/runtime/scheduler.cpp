#include "runtime/scheduler.hpp"

#include "logging/logging.hpp"

#include <algorithm>
#include <vector>

namespace igi {
void Scheduler::bindGameThread() noexcept {
    std::lock_guard lock(mutex_);
    gameThreads_.assign(1, std::this_thread::get_id());
}

void Scheduler::allowCurrentGameThread() noexcept {
    std::lock_guard lock(mutex_);
    const auto current = std::this_thread::get_id();
    if (std::find(gameThreads_.begin(), gameThreads_.end(), current) == gameThreads_.end())
        gameThreads_.push_back(current);
}

bool Scheduler::isGameThread() const noexcept {
    std::lock_guard lock(mutex_);
    const auto current = std::this_thread::get_id();
    return std::find(gameThreads_.begin(), gameThreads_.end(), current) != gameThreads_.end();
}

bool Scheduler::isRunning() const noexcept {
    std::lock_guard lock(mutex_);
    return running_;
}

TaskId Scheduler::add(Task task, std::chrono::steady_clock::time_point due, bool everyFrame) {
    if (!task)
        return 0;
    std::lock_guard lock(mutex_);
    const TaskId id = nextId_++;
    entries_.emplace(id, Entry{std::move(task), due, everyFrame, false});
    return id;
}

TaskId Scheduler::enqueue(Task task) {
    return add(std::move(task), std::chrono::steady_clock::now(), false);
}

TaskId Scheduler::enqueueEveryFrame(Task task) {
    return add(std::move(task), std::chrono::steady_clock::now(), true);
}

TaskId Scheduler::enqueueDelayed(std::chrono::milliseconds delay, Task task) {
    return add(std::move(task), std::chrono::steady_clock::now() + delay, false);
}

bool Scheduler::cancel(TaskId taskId) noexcept {
    std::lock_guard lock(mutex_);
    auto found = entries_.find(taskId);
    if (found == entries_.end())
        return false;
    entries_.erase(found);
    return true;
}

void Scheduler::clear() noexcept {
    std::lock_guard lock(mutex_);
    entries_.clear();
}

void Scheduler::reset() noexcept {
    std::lock_guard lock(mutex_);
    entries_.clear();
    gameThreads_.clear();
    gameHookInvocations_.store(0, std::memory_order_relaxed);
}

std::size_t Scheduler::runPending() noexcept {
    if (!isGameThread())
        return 0;
    return runReadyTasks();
}

std::size_t Scheduler::runPendingFromGameHook() noexcept {
    const auto invocation = gameHookInvocations_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (invocation == 1)
        log(LogLevel::Info, "scheduler: first Display_AddFrameCallback tick entered");
#ifndef NDEBUG
    if (invocation <= 5 || (invocation % 1000) == 0)
        log(LogLevel::Debug, "scheduler: Display_AddFrameCallback tick entered");
#endif
    {
        std::lock_guard lock(mutex_);
        if (gameThreads_.empty())
            gameThreads_.push_back(std::this_thread::get_id());
    }
    if (!isGameThread()) {
        log(LogLevel::Error,
            "Display_AddFrameCallback tick was observed on a second thread; scheduler skipped");
        return 0;
    }
    const auto ran = runReadyTasks();
#ifndef NDEBUG
    if (invocation <= 5 || (invocation % 1000) == 0)
        log(LogLevel::Debug, "scheduler: Display_AddFrameCallback tick completed");
#endif
    return ran;
}

std::size_t Scheduler::runReadyTasks() noexcept {
    std::vector<std::pair<TaskId, Task>> ready;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        if (running_)
            return 0;
        running_ = true;
        for (auto &[id, entry] : entries_) {
            if (!entry.cancelled && entry.due <= now)
                ready.emplace_back(id, entry.task);
        }
    }
    std::size_t executed = 0;
    for (auto &[id, task] : ready) {
        {
            std::lock_guard lock(mutex_);
            const auto found = entries_.find(id);
            if (found == entries_.end() || found->second.cancelled)
                continue;
        }
        ++executed;
        try {
            task();
        } catch (...) {
            log(LogLevel::Error, "scheduler task threw; task contained");
        }
        std::lock_guard lock(mutex_);
        auto found = entries_.find(id);
        if (found != entries_.end()) {
            if (found->second.everyFrame && !found->second.cancelled)
                found->second.due = std::chrono::steady_clock::now();
            else
                entries_.erase(found);
        }
    }
    {
        std::lock_guard lock(mutex_);
        running_ = false;
    }
    return executed;
}
} // namespace igi
