#pragma once

#include "IGI/HookManager.hpp"

#include <cstdint>

namespace igi {
class Runtime final {
public:
    bool initialize();
    void shutdown() noexcept;
    Scheduler& scheduler() noexcept { return scheduler_; }
    std::uint64_t gameHookInvocationCount() const noexcept {
        return scheduler_.gameHookInvocationCount();
    }
    bool initialized() const noexcept { return initialized_; }

private:
    Scheduler scheduler_;
    HookManager hooks_{scheduler_};
    bool initialized_ = false;
};
}
