#pragma once

#include "igi_sdk/runtime/hook_manager.hpp"
#include "igi_sdk/runtime/igi1_hooks.hpp"

#include <cstdint>

namespace igi {
class Runtime final {
public:
    // Per-frame SDK tick selection. `DisplayAddFrameCallback` is the primary
    // choice: it registers IGI1SDK_Tick into the game's own per-frame callback
    // list (Display_AddFrameCallback at 0x4916C0), so the SDK runs once per
    // displayed frame on the game thread without patching the tick target.
    // `ScreenDraw`, `DisplayEndFrame`, `TextDraw` and `GameRunHandler` stay
    // selectable from config.ini for comparison and fallback.
    bool initialize(igi1::TickHook hook = igi1::TickHook::DisplayAddFrameCallback);
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
