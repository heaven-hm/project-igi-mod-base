#include "IGI/Scheduler.hpp"

#include "IGI/Logging.hpp"

#include <vector>

namespace igi {
void Scheduler::bindGameThread() noexcept {
    std::lock_guard lock(mutex_);
    gameThread_ = std::this_thread::get_id();
}

bool Scheduler::isGameThread() const noexcept {
    std::lock_guard lock(mutex_);
    return gameThread_ != std::thread::id{} && gameThread_ == std::this_thread::get_id();
}

TaskId Scheduler::add(Task task, std::chrono::steady_clock::time_point due, bool everyFrame) {
    if (!task) return 0;
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
    if (found == entries_.end()) return false;
    found->second.cancelled = true;
    return true;
}

void Scheduler::clear() noexcept {
    std::lock_guard lock(mutex_);
    entries_.clear();
}

std::size_t Scheduler::runPending() noexcept {
    if (!isGameThread()) return 0;
    return runReadyTasks();
}

std::size_t Scheduler::runPendingFromGameHook() noexcept {
    gameHookInvocations_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(mutex_);
        if (gameThread_ == std::thread::id{}) {
            gameThread_ = std::this_thread::get_id();
        }
    }
    if (!isGameThread()) {
        log(LogLevel::Error, "Game_RunHandler was observed on a second thread; scheduler skipped");
        return 0;
    }
    return runReadyTasks();
}

std::size_t Scheduler::runReadyTasks() noexcept {
    std::vector<std::pair<TaskId, Task>> ready;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        for (auto& [id, entry] : entries_) {
            if (!entry.cancelled && entry.due <= now) ready.emplace_back(id, entry.task);
        }
    }
    for (auto& [id, task] : ready) {
        try { task(); }
        catch (...) { log(LogLevel::Error, "scheduler task threw; task contained"); }
        std::lock_guard lock(mutex_);
        auto found = entries_.find(id);
        if (found != entries_.end()) {
            if (found->second.everyFrame && !found->second.cancelled)
                found->second.due = std::chrono::steady_clock::now();
            else
                entries_.erase(found);
        }
    }
    return ready.size();
}
}
