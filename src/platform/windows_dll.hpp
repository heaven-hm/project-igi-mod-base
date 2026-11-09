#pragma once

#include <cstdint>

namespace igi {

struct DllMenuCallbacks {
    bool (*initialize)() = nullptr;
    bool (*toggle)() = nullptr;
    // Closes the menu and never opens it. The end key uses this so a repeated key
    // edge cannot re-open the menu instead of ejecting.
    bool (*closeMenu)() = nullptr;
    bool (*requestShutdown)() = nullptr;
    bool (*canUnload)() = nullptr;
    void (*shutdown)() = nullptr;
    bool (*ensureTick)() = nullptr;
    std::uint64_t (*tickCount)() = nullptr;
    // True when the retail hooks could not be restored, so the module must stay
    // resident instead of being unloaded.
    bool (*hooksResident)() = nullptr;
};

// Implemented by the shared menu host.
const DllMenuCallbacks &getDllMenuCallbacks() noexcept;

} // namespace igi
