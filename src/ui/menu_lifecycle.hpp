#pragma once

#include "runtime/runtime.hpp"

namespace igi {
bool pauseForMenu(Runtime &runtime);
bool resumeFromMenu(Runtime &runtime);
bool lockHumanInput();
bool unlockHumanInput();
} // namespace igi
