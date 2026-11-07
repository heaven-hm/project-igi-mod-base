#pragma once

namespace igi {
class Scheduler;
}

namespace igi::macos {
bool installGameTickHook(Scheduler &scheduler, void *&trampoline) noexcept;
void uninstallGameTickHook(void *&trampoline) noexcept;
} // namespace igi::macos
