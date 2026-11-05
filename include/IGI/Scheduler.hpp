#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace igi {
using TaskId = std::uint64_t;
using Task = std::function<void()>;

class Scheduler final {
public:
    void bindGameThread() noexcept;
    bool isGameThread() const noexcept;
    TaskId enqueue(Task task);
    TaskId enqueueOnce(Task task) { return enqueue(std::move(task)); }
    TaskId enqueueEveryFrame(Task task);
    TaskId enqueueDelayed(std::chrono::milliseconds delay, Task task);
    bool cancel(TaskId taskId) noexcept;
    void clear() noexcept;
    std::size_t runPending() noexcept;

private:
    struct Entry {
        Task task;
        std::chrono::steady_clock::time_point due;
        bool everyFrame = false;
        bool cancelled = false;
    };

    TaskId add(Task task, std::chrono::steady_clock::time_point due, bool everyFrame);
    mutable std::mutex mutex_;
    std::thread::id gameThread_;
    TaskId nextId_ = 1;
    std::unordered_map<TaskId, Entry> entries_;
};
}
