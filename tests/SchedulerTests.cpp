#include "IGI/Scheduler.hpp"

#include <cassert>
#include <chrono>

int main() {
    igi::Scheduler scheduler;
    scheduler.bindGameThread();
    int count = 0;
    scheduler.enqueueOnce([&] { ++count; });
    assert(scheduler.runPending() == 1 && count == 1);
    assert(scheduler.runPending() == 0 && count == 1);
    const auto repeating = scheduler.enqueueEveryFrame([&] { ++count; });
    assert(scheduler.runPending() == 1 && count == 2);
    assert(scheduler.cancel(repeating));
    assert(scheduler.runPending() == 0 && count == 2);
    scheduler.enqueueDelayed(std::chrono::milliseconds(10), [&] { ++count; });
    assert(scheduler.runPending() == 0);
    return 0;
}
