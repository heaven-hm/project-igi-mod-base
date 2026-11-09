#include "ui/menu_lifecycle.hpp"
#include "natives/mvp_natives.hpp"
#include "logging/logging.hpp"

namespace igi {
namespace {
// The mod menu can be opened from normal gameplay or from on top of the retail
// pause menu. Only the pause this module created may be undone on close, or
// closing the mod menu resumes a game the retail pause menu is still holding.
struct MenuPauseOwnership {
    bool active = false;
    bool gamePausedBefore = false;
    bool inputLocked = false;
};
MenuPauseOwnership g_pause;
} // namespace

bool menuPauseActive() {
    return g_pause.active;
}

void abandonMenuPause() {
    g_pause = {};
}

bool lockHumanInput() {
    if (!IGI::GAME::GET())
        return false;
    bool locked = false;
    for (int i = 0; i < 4; ++i) {
        if (auto *player = IGI::GAME::GET_PLAYER(i)) {
            auto *human = reinterpret_cast<IGI::Human *>(player);
            IGI::HUMAN::LOCK_INPUT(human);
            IGI::HUMAN::DISABLE_LOOK(human);
            locked = true;
        }
    }
    if (locked)
        log(LogLevel::Info, "human input and look disabled");
    return locked;
}

bool unlockHumanInput() {
    if (!IGI::GAME::GET())
        return false;
    bool unlocked = false;
    for (int i = 0; i < 4; ++i) {
        if (auto *player = IGI::GAME::GET_PLAYER(i)) {
            auto *human = reinterpret_cast<IGI::Human *>(player);
            IGI::HUMAN::UNLOCK_INPUT(human);
            IGI::HUMAN::ENABLE_LOOK(human);
            unlocked = true;
        }
    }
    if (unlocked)
        log(LogLevel::Info, "human input and look enabled");
    return unlocked;
}

bool pauseForMenu(Runtime &runtime) {
    if (!runtime.allowDirectNativeCallbackThread())
        return false;
    if (!IGI::GAME::GET())
        return false;
    if (!g_pause.active) {
        g_pause.active = true;
        g_pause.gamePausedBefore = IGI::GAME::IS_PAUSED();
        g_pause.inputLocked = lockHumanInput();
    }
    if (IGI::GAME::IS_PAUSED())
        return true;
    IGI::GAME::SET_PAUSED(1);
    return IGI::GAME::IS_PAUSED();
}

bool resumeFromMenu(Runtime &runtime) {
    if (!runtime.allowDirectNativeCallbackThread())
        return false;
    if (!IGI::GAME::GET())
        return false;
    if (!g_pause.active) {
        // No pause was requested through pauseForMenu; keep the historical
        // behaviour for callers that manage the pause themselves.
        unlockHumanInput();
        IGI::GAME::SET_PAUSED(0);
        return !IGI::GAME::IS_PAUSED();
    }
    // The retail pause menu was already holding the game paused: leave its
    // pause and input lock exactly as they are.
    const bool retailOwnsPause = g_pause.gamePausedBefore;
    const bool inputLockedByMod = g_pause.inputLocked;
    g_pause = {};
    if (!retailOwnsPause) {
        if (inputLockedByMod)
            unlockHumanInput();
        IGI::GAME::SET_PAUSED(0);
    }
    return retailOwnsPause ? IGI::GAME::IS_PAUSED() : !IGI::GAME::IS_PAUSED();
}
} // namespace igi
