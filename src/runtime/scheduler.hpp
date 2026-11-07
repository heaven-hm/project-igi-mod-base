#pragma once

#include <chrono>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace igi {
using TaskId = std::uint64_t;
using Task = std::function<void()>;

class Scheduler final {
  public:
    void bindGameThread() noexcept;
    void allowCurrentGameThread() noexcept;
    bool isGameThread() const noexcept;
    bool isRunning() const noexcept;
    TaskId enqueue(Task task);
    TaskId enqueueOnce(Task task) {
        return enqueue(std::move(task));
    }
    TaskId enqueueEveryFrame(Task task);
    TaskId enqueueDelayed(std::chrono::milliseconds delay, Task task);
    bool cancel(TaskId taskId) noexcept;
    void clear() noexcept;
    void reset() noexcept;
    std::size_t runPending() noexcept;
    // Called exclusively by the verified game update detour. The first call
    // establishes the thread on which all game-affecting tasks may execute.
    std::size_t runPendingFromGameHook() noexcept;
    std::uint64_t gameHookInvocationCount() const noexcept {
        return gameHookInvocations_.load(std::memory_order_relaxed);
    }

  private:
    struct Entry {
        Task task;
        std::chrono::steady_clock::time_point due;
        bool everyFrame = false;
        bool cancelled = false;
    };

    TaskId add(Task task, std::chrono::steady_clock::time_point due, bool everyFrame);
    std::size_t runReadyTasks() noexcept;
    mutable std::mutex mutex_;
    std::vector<std::thread::id> gameThreads_;
    TaskId nextId_ = 1;
    std::atomic<std::uint64_t> gameHookInvocations_{0};
    bool running_ = false;
    std::unordered_map<TaskId, Entry> entries_;
};
} // namespace igi
