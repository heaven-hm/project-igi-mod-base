#pragma once

#include "runtime/runtime.hpp"

namespace igi {
// Pauses the game for a mod menu. Remembers whether the game was already paused
// (the retail pause menu) so resumeFromMenu can restore that state instead of
// forcing it open.
bool pauseForMenu(Runtime &runtime);
bool resumeFromMenu(Runtime &runtime);
// Drops the ownership bookkeeping without touching the game: used when the
// retail menu stack took over the pause state from the mod.
void abandonMenuPause();
// True while this module owns the pause it requested through pauseForMenu.
bool menuPauseActive();
bool lockHumanInput();
bool unlockHumanInput();
} // namespace igi
