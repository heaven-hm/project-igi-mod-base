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
    // Requests that the verified Display_RunCallbacks thread check and restore
    // IGISDK_Tick after a display reset. The request is safe from the SDK
    // worker thread; callback-list access itself is not performed there.
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
