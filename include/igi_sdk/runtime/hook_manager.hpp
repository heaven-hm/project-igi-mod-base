#pragma once

#include "igi_sdk/runtime/scheduler.hpp"

#include <cstdint>

namespace igi {
class HookManager final {
public:
    explicit HookManager(Scheduler& scheduler) noexcept : scheduler_(scheduler) {}
    // Text_Draw (0x4B6E60) runs on the per-frame draw path and its displaced
    // prologue is five bytes with no branch inside it.
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
