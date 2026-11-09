#pragma once

#include <cstdint>

namespace igi {

struct DllMenuCallbacks {
    bool (*initialize)() = nullptr;
    bool (*toggle)() = nullptr;
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
