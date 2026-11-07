#include "macos_hook_backend.hpp"

#include "logging/logging.hpp"

// IGI 1's verified profile is a 32-bit PE executable. A macOS dylib cannot
// be installed into that process, and an unrelated native `igi.app` symbol is
// not an acceptable substitute for Display_AddFrameCallback at 0x004916C0.
namespace igi::macos {
bool installGameTickHook(Scheduler &, void *&) noexcept {
    log(LogLevel::Error, "IGI 1 Display_AddFrameCallback hook requires the verified Windows x86 PE "
                         "target; macOS dylib hook refused");
    return false;
}

void uninstallGameTickHook(void *&trampoline) noexcept {
    trampoline = nullptr;
}
} // namespace igi::macos
