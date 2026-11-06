#pragma once

#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/runtime/scheduler.hpp"

#include <cstdint>

namespace igi {
class HookManager final {
public:
    explicit HookManager(Scheduler& scheduler) noexcept : scheduler_(scheduler) {}
    // The accepted scheduler hook is Display_AddFrameCallback at 0x004916C0.
    // Its detour registers IGI1SDK_Tick into the game's frame callback list, so
    // the SDK runs once per displayed frame on the game thread. Legacy selection
    // methods remain refusal-only to avoid a source or ABI break for callers.
    bool installDisplayAddFrameCallbackTick();
    bool installTickHook(igi1::TickHook hook);
    bool installDisplayEndFrameHook();
    bool installTextDrawHook();
    bool installed() const noexcept { return installed_; }
    // Refuses a re-entrant call from the active Display_AddFrameCallback detour.
    bool uninstall() noexcept;

private:
    Scheduler& scheduler_;
    bool installed_ = false;
    void* trampoline_ = nullptr;
};
}
