#pragma once

#include "igi_sdk/runtime/hook_manager.hpp"
#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/natives/native_caller.hpp"

#include <cstdint>
#include <memory>

namespace igi {
class Runtime final {
public:
    // Gameplay work is dispatched only from the verified IGI 1 game handler.
    bool initialize(igi1::TickHook hook = igi1::TickHook::GameRunHandler);
    void shutdown() noexcept;
    Scheduler& scheduler() noexcept { return scheduler_; }
    std::uint64_t gameHookInvocationCount() const noexcept {
        return scheduler_.gameHookInvocationCount();
    }
    bool initialized() const noexcept { return initialized_; }

private:
    Scheduler scheduler_;
    HookManager hooks_{scheduler_};
    std::unique_ptr<NativeCatalogue> catalogue_;
    std::unique_ptr<NativeCaller> caller_;
    bool initialized_ = false;
};
}
