#include "ui/menu_lifecycle.hpp"
#include "natives/mvp_natives.hpp"
#include "logging/logging.hpp"

namespace igi {
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
    lockHumanInput();
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
    unlockHumanInput();
    IGI::GAME::SET_PAUSED(0);
    return !IGI::GAME::IS_PAUSED();
}
} // namespace igi
