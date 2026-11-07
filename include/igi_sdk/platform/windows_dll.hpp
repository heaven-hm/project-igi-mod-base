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
};

// Implemented by the menu hosting translation unit (e.g. internal_demo.cpp).
const DllMenuCallbacks& getDllMenuCallbacks() noexcept;

}  // namespace igi

