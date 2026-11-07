#pragma once

#include "runtime/igi_hooks.hpp"
#include "runtime/scheduler.hpp"

#include <cstdint>

namespace igi {
class HookManager final {
  public:
    explicit HookManager(Scheduler &scheduler) noexcept : scheduler_(scheduler) {}
    // The accepted scheduler hook is Display_AddFrameCallback at 0x004916C0.
    // Its detour registers IGISDK_Tick into the game's frame callback list, so
    // the SDK runs once per displayed frame on the game thread. Legacy selection
    // methods remain refusal-only to avoid a source or ABI break for callers.
    bool installDisplayAddFrameCallbackTick();
    bool installTickHook(game::TickHook hook);
    bool installDisplayEndFrameHook();
    bool installTextDrawHook();
    bool installed() const noexcept {
        return installed_;
    }
    // Verifies IGISDK_Tick is present in the game's display frame callback
    // list and re-registers it when a list reset dropped it. Safe to call from
    // the SDK worker thread; used as a watchdog.
    bool ensureFrameTickRegistered() noexcept;
    // Refuses a re-entrant call from the active Display_AddFrameCallback detour.
    // Detaches the scheduler and disables the detours but deliberately leaves the
    // module resident so no in-flight game callback can jump into freed memory.
    bool uninstall() noexcept;

  private:
    Scheduler &scheduler_;
    bool installed_ = false;
    void *trampoline_ = nullptr;
};
} // namespace igi
