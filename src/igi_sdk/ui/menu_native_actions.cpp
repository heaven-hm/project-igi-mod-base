#include "igi_sdk/ui/menu_native_actions.hpp"

#include "igi_sdk/IGI.hpp"
#include "igi_sdk/logging/logging.hpp"

#include <sstream>
#include <string>

namespace igi {
bool pauseForMenu(Runtime& runtime) {
    if (!runtime.allowDirectNativeCallbackThread()) return false;
    if (IGI::GAME::IS_PAUSED()) return true;
    IGI::GAME::SET_PAUSED(1);
    return IGI::GAME::IS_PAUSED();
}

bool resumeFromMenu(Runtime& runtime) {
    if (!runtime.allowDirectNativeCallbackThread()) return false;
    IGI::GAME::SET_PAUSED(0);
    return !IGI::GAME::IS_PAUSED();
}

void executeMenuNativeAction(const MenuConfig& config, Runtime& runtime, std::size_t item,
                             std::string_view value) {
    if (item >= config.items.size()) return;
    const auto& action = config.items[item].nativeAction;
    if (!pauseForMenu(runtime)) {
        log(LogLevel::Error, "menu action rejected: unable to pause game");
        return;
    }
    log(LogLevel::Info, "menu action: " + action + " value=" + std::string(value));
    if (action == "IGI_SDK_SetWeather(this.value)") {
        if (value != "on" && value != "off") {
            log(LogLevel::Warning, "weather selection must be on or off; native call skipped");
            return;
        }
        const char weather[] = {'r', static_cast<char>(value == "on"), '\0'};
        IGI::GAME::SET_RAINING(weather);
    } else if (action == "IGI_SDK_EnableUnlimitedAmmo()") {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
    } else if (action == "IGI_SDK_ToggleFrameLimit()") {
        IGI::MISC::FRAMES_SET(value == "false" ? 60 : 30);
    } else if (action == "IGI_SDK_SetMusicVolume(this.value)") {
        float volume = value == "mute" ? 0.0F : value == "low" ? 0.35F : 1.0F;
        if (value != "mute" && value != "low" && value != "normal") {
            log(LogLevel::Warning, "unknown music volume selection; native call skipped");
            return;
        }
        IGI::AUDIO::MUSIC_SET_VOLUME(volume, volume);
    } else if (action == "IGI_SDK_RefreshDisplay()") {
        IGI::GAME::SET_DISPLAY_MODE();
    } else if (action == "IGI_SDK_LogGameState()") {
        std::ostringstream state;
        state << "IGI state: events=" << static_cast<int>(IGI::GAME::GET_EVENT0()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT1()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT2()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT3()) << " world=" << IGI::GAME::GET_WORLD();
        log(LogLevel::Info, state.str());
    } else {
        log(LogLevel::Warning, "menu action has no native adapter: " + action);
    }
}
}
