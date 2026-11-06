#pragma once

#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/runtime/scheduler.hpp"

#include <cstdint>

namespace igi {
class HookManager final {
public:
    explicit HookManager(Scheduler& scheduler) noexcept : scheduler_(scheduler) {}
    // Per-frame tick selection:
    //   DisplayAddFrameCallback 0x4916C0  register IGI1SDK_Tick into the game per-frame list
    //   DisplayEndFrame         0x491D20  `call Scene_DrawAll` then the end-frame callbacks
    //   ScreenDraw              0x4E7400  screen-level frame (Display_NextFrame + children + Display_EndFrame)
    //   TextDraw                0x4B6E60  text draw, five bytes, no relocated branch
    //   GameRunHandler          0x416260  task slot 1, eight bytes with a relocated call
    bool installDisplayAddFrameCallbackTick();
    bool installTickHook(igi1::TickHook hook);
    bool installDisplayEndFrameHook();
    bool installTextDrawHook();
    bool installPrimaryGameRunHandler();
    bool installed() const noexcept { return installed_; }
    void uninstall() noexcept;

private:
    Scheduler& scheduler_;
    bool installed_ = false;
    void* trampoline_ = nullptr;
    const void* targetSpec_ = nullptr;
};
}
