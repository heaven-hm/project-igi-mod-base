#pragma once

#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/runtime/scheduler.hpp"

#include <cstdint>

namespace igi {
class HookManager final {
public:
    explicit HookManager(Scheduler& scheduler) noexcept : scheduler_(scheduler) {}
    // The only accepted gameplay scheduler hook is Game_RunHandler at
    // 0x00416260. Legacy selection methods remain refusal-only to avoid a
    // source or ABI break for existing callers.
    bool installDisplayAddFrameCallbackTick();
    bool installTickHook(igi1::TickHook hook);
    bool installDisplayEndFrameHook();
    bool installTextDrawHook();
    bool installPrimaryGameRunHandler();
    bool installed() const noexcept { return installed_; }
    // Refuses a re-entrant call from the active Game_RunHandler detour.
    bool uninstall() noexcept;

private:
    Scheduler& scheduler_;
    bool installed_ = false;
    void* trampoline_ = nullptr;
};
}
