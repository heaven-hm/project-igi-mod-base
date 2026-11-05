#pragma once

#include "IGI/Scheduler.hpp"

#include <cstdint>

namespace igi {
class HookManager final {
public:
    explicit HookManager(Scheduler& scheduler) noexcept : scheduler_(scheduler) {}
    bool installPrimaryGameRunHandler();
    bool installed() const noexcept { return installed_; }
    void uninstall() noexcept;

private:
    Scheduler& scheduler_;
    bool installed_ = false;
    void* trampoline_ = nullptr;
};
}
