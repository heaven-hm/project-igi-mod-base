#include "igi_sdk/ui/menu_native_actions.hpp"

#include "igi_sdk/IGI.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi1_native_registry.hpp"
#include "igi_sdk/natives/igi_re/hashes.hpp"
#include "igi_sdk/ui/menu_resource.hpp"
#include "igi_sdk/ui/menu_script.hpp"
#include "igi_sdk/ui/menu_script_bridge.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace igi {
namespace {
void clearExistingRainEffects() {
#if defined(_WIN32) && !defined(_WIN64)
    const auto rainType = *reinterpret_cast<const std::uint16_t*>(0x0057BD84);
    if (rainType == 0) return;
    if (auto* dynRoot = IGI::LEVEL::GET_DYNAMIC()) {
        while (auto* existing = IGI::TASK::FIND_CHILD(dynRoot, rainType)) {
            IGI::TASK::DELETE_TASK(existing);
        }
    }
    if (auto* statRoot = IGI::LEVEL::GET_STATIC()) {
        while (auto* existing = IGI::TASK::FIND_CHILD(statRoot, rainType)) {
            IGI::TASK::DELETE_TASK(existing);
        }
    }
#endif
}
}  // namespace
bool lockHumanInput() {
    bool locked = false;
    for (int i = 0; i < 4; ++i) {
        auto* player = IGI::GAME::GET_PLAYER(i);
        if (player) {
            auto* human = reinterpret_cast<IGI::Human*>(player);
            IGI::HUMAN::LOCK_INPUT(human);
            IGI::HUMAN::DISABLE_LOOK(human);
            locked = true;
        }
    }
    if (locked) {
        log(LogLevel::Info, "human input and look disabled");
    }
    return locked;
}

bool unlockHumanInput() {
    bool unlocked = false;
    for (int i = 0; i < 4; ++i) {
        auto* player = IGI::GAME::GET_PLAYER(i);
        if (player) {
            auto* human = reinterpret_cast<IGI::Human*>(player);
            IGI::HUMAN::UNLOCK_INPUT(human);
            IGI::HUMAN::ENABLE_LOOK(human);
            unlocked = true;
        }
    }
    if (unlocked) {
        log(LogLevel::Info, "human input and look enabled");
    }
    return unlocked;
}

bool pauseForMenu(Runtime& runtime) {
    if (!runtime.allowDirectNativeCallbackThread()) return false;
    lockHumanInput();
    if (IGI::GAME::IS_PAUSED()) return true;
    IGI::GAME::SET_PAUSED(1);
    return IGI::GAME::IS_PAUSED();
}

bool resumeFromMenu(Runtime& runtime) {
    if (!runtime.allowDirectNativeCallbackThread()) return false;
    unlockHumanInput();
    IGI::GAME::SET_PAUSED(0);
    return !IGI::GAME::IS_PAUSED();
}

bool setWeather(std::string_view weather) {
    clearExistingRainEffects();
    if (weather == "Off" || weather == "off" || weather == "false") {
        const char off[] = {'r', 0, '\0'};
        IGI::GAME::SET_RAINING(off);
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI SDK: Weather set to Off");
        log(LogLevel::Info, "weather set to Off via IGI::GAME::SET_RAINING");
        return true;
    }
    const bool isSnow = (weather == "Snow" || weather == "snow");
    const char code[] = {static_cast<char>(isSnow ? 's' : 'r'), 1, '\0'};
    IGI::GAME::SET_RAINING(code);

    auto* dynRoot = IGI::LEVEL::GET_DYNAMIC();
    auto* caller = igi1NativeCaller();
    if (!dynRoot || !caller) {
        log(LogLevel::Error, "setWeather: dynamic level root or native caller unavailable");
        return false;
    }
    const auto resources = igi1MenuResourceApi(*caller);
    if (!resources.isLoaded || !resources.registerResource || !resources.freeResource) {
        log(LogLevel::Error, "setWeather: resource API unavailable");
        return false;
    }

    std::string script =
        "Task_DeclareParameters(\"RainEffect\", \"Is Rain\", \"bool8\", \"Traceline start\", \"Real32\", \"Traceline end\", \"Real32\", \"Is Active\", \"VarString\", \"Rain Alpha\", \"Real32\");\n"
        "Task_New(-1, \"RainEffect\", \"\", " + std::string(isSnow ? "FALSE" : "TRUE") + ", 50.0, 20.0, \"TRUE\\n\", 0.3);";
    std::vector<std::uint8_t> qvm;
    std::string error;
    if (!compileIgi1MenuCommandScript(script, qvm, error)) {
        log(LogLevel::Error, "setWeather: compilation failed: " + error);
        return false;
    }

    MenuResource cmdRes;
    if (!cmdRes.registerData("LOCAL:igi_sdk/temp_rain.qvm", qvm, resources)) {
        log(LogLevel::Error, "setWeather: resource registration failed");
        return false;
    }

    auto* rainTask = IGI::TASK::RUN_SCRIPT(dynRoot, "LOCAL:igi_sdk/temp_rain.qvm", 0);
    if (!cmdRes.release())
        log(LogLevel::Error, "setWeather: temporary resource release failed");
#if defined(_WIN32) && !defined(_WIN64)
    if (rainTask) {
        using GetChangedEvent = std::uint8_t (__cdecl*)();
        using Broadcast = void (__cdecl*)(IGI::Task*, std::uint8_t, void*);
        const auto changedEvent = reinterpret_cast<GetChangedEvent>(igi::re::HASH::Param_GetChangedEvent)();
        char changed = 1;
        reinterpret_cast<Broadcast>(igi::re::HASH::Task_Broadcast)(rainTask, changedEvent, &changed);
    }
#endif
    const std::string statusMsg = "IGI SDK: Weather set to " + std::string(isSnow ? "Snow" : "Rain");
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, statusMsg.c_str());
    log(LogLevel::Info, "weather set to " + std::string(weather));
    return rainTask != nullptr;
}

bool setFps(std::string_view value) {
    int fps = 30;
    try {
        fps = std::stoi(std::string(value));
    } catch (...) {
        fps = (value == "false") ? 60 : 30;
    }
    if (fps <= 0) fps = 30;
    IGI::MISC::FRAMES_SET(fps);
    const std::string statusMsg = "IGI SDK: FPS set to " + std::to_string(fps);
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, statusMsg.c_str());
    log(LogLevel::Info, "FPS set to " + std::to_string(fps) + " via IGI::MISC::FRAMES_SET");
    return true;
}

void executeMenuNativeAction(const MenuConfig& config, Runtime& runtime, std::size_t item,
                             std::string_view value) {
    if (item >= config.items.size()) return;
    const auto& id = config.items[item].id;
    const auto& action = config.items[item].nativeAction;
    if (!runtime.allowDirectNativeCallbackThread()) {
        log(LogLevel::Error, "menu action rejected: runtime/game thread unavailable");
        return;
    }
    log(LogLevel::Info, "menu action: id=" + id + " action=" + action + " value=" + std::string(value));

    if (id == "weather" || action.find("SetWeather") != std::string::npos) {
        setWeather(value);
    } else if (id == "fps" || id == "frames" || action.find("Frame") != std::string::npos) {
        setFps(value);
    } else if (id == "ammo" || action.find("UnlimitedAmmo") != std::string::npos) {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI SDK: Unlimited ammo enabled");
        log(LogLevel::Info, "unlimited ammo enabled via IGI::PLAYER::SET_UNLIMITED_AMMO");
    } else if (id == "music" || action.find("MusicVolume") != std::string::npos) {
        float volume = 1.0F;
        if (value == "mute" || value == "Mute") volume = 0.0F;
        else if (value == "low" || value == "Low") volume = 0.35F;
        else volume = 1.0F;
        IGI::AUDIO::MUSIC_SET_VOLUME(volume, volume);
        log(LogLevel::Info, "music volume set to " + std::to_string(volume));
    } else if (id == "sfx") {
        float vol = 1.0F;
        if (value == "Mute" || value == "mute") vol = 0.0F;
        else if (value == "Low" || value == "low") vol = 0.35F;
        else vol = 1.0F;
        IGI::AUDIO::SET_SFX_VOLUME(vol);
        log(LogLevel::Info, "SFX volume set to " + std::to_string(vol));
    } else if (id == "view") {
        auto* player = IGI::GAME::GET_PLAYER(0);
        if (player) {
            const int mode = (value == "Third Person") ? 2 : 1;
            IGI::HUMAN::VIEW_CAM(reinterpret_cast<IGI::Human*>(player), mode);
            log(LogLevel::Info, "camera view set to " + std::string(value));
        }
    } else if (id == "level") {
        int levelNum = 1;
        if (value.starts_with("Level ")) {
            try { levelNum = std::stoi(std::string(value.substr(6))); } catch (...) { levelNum = 1; }
        }
        runtime.scheduler().enqueueOnce([&runtime, levelNum] { changeLevel(runtime, levelNum); });
    } else if (id == "display" || action.find("RefreshDisplay") != std::string::npos) {
        IGI::GAME::SET_DISPLAY_MODE();
        log(LogLevel::Info, "display mode refreshed");
    } else if (id == "diagnostics" || id == "perf" || action.find("LogGameState") != std::string::npos) {
        std::ostringstream state;
        state << "IGI state: events=" << static_cast<int>(IGI::GAME::GET_EVENT0()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT1()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT2()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT3())
              << " fps=" << IGI::OTHER::FLOW_GET_FPS()
              << " frames=" << IGI::OTHER::FLOW_GET_FRAMES()
              << " world=" << IGI::GAME::GET_WORLD();
        log(LogLevel::Info, state.str());
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, state.str().c_str());
    } else {
        log(LogLevel::Warning, "menu action has no native adapter: id=" + id + " action=" + action);
    }
}

static bool executeMenuCommandFor(IGI::Task* manager, std::string_view commandScript) {
    if (!manager) return false;
    auto* caller = igi1NativeCaller();
    if (!caller) return false;
    const auto resources = igi1MenuResourceApi(*caller);
    if (!resources.isLoaded || !resources.registerResource || !resources.freeResource) return false;

    std::vector<std::uint8_t> qvm;
    std::string error;
    if (!compileIgi1MenuCommandScript(commandScript, qvm, error)) {
        log(LogLevel::Error, "executeMenuCommand: compilation failed: " + error);
        return false;
    }

    MenuResource cmdRes;
    if (!cmdRes.registerData("LOCAL:igi_sdk/temp_cmd.qvm", qvm, resources)) {
        log(LogLevel::Error, "executeMenuCommand: resource registration failed");
        return false;
    }

    (void)IGI::TASK::RUN_SCRIPT(manager, "LOCAL:igi_sdk/temp_cmd.qvm", 0);
    if (!cmdRes.release())
        log(LogLevel::Error, "executeMenuCommand: temporary resource release failed");
    log(LogLevel::Info, std::string("executeMenuCommand: executed script: ") + std::string(commandScript));
    return true;
}

bool executeMenuCommand(std::string_view commandScript) {
    auto* game = IGI::GAME::GET();
    if (!game) return false;
    struct GameRootLayout {
        std::uint8_t prefix[0x44];
        void* root;
    };
    auto* root = static_cast<IGI::Task*>(static_cast<GameRootLayout*>(static_cast<void*>(game))->root);
    if (!root) return false;
    auto* manager = IGI::TASK::FIND_CHILD(root, IGI::MENU::GET_TYPE());
    return executeMenuCommandFor(manager, commandScript);
}

bool changeLevel(Runtime& runtime, int levelId) {
    if (levelId < 1 || levelId > 14) return false;
    if (!IGI::GAME::GET()) return false;
    auto taskId = std::make_shared<TaskId>(0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    *taskId = runtime.scheduler().enqueueEveryFrame([&runtime, taskId, deadline, levelId] {
        if (std::chrono::steady_clock::now() > deadline) {
            log(LogLevel::Error, "level change: timed out waiting for main menu");
            runtime.scheduler().cancel(*taskId);
            return;
        }
        if (IGI::GAME::GET()) return;
        auto* screen = static_cast<IGI::Task*>(IGI::OTHER::FLOW_GET_SCREEN());
        auto* manager = screen ? IGI::TASK::FIND_CHILD(screen, IGI::MENU::GET_TYPE()) : nullptr;
        if (!manager) return;
        const std::string script = "Game_SetMission(" + std::to_string(levelId) + "); MenuManager_LeaveMenus(-1, 13);";
        if (executeMenuCommandFor(manager, script)) {
            log(LogLevel::Info, "level change: main menu start requested for level " + std::to_string(levelId));
            runtime.scheduler().cancel(*taskId);
        }
    });
    if (!*taskId) return false;
    if (!executeMenuCommand("MenuManager_LeaveMenus(-1, 11);")) {
        runtime.scheduler().cancel(*taskId);
        return false;
    }
    log(LogLevel::Info, "level change: waiting for game teardown before starting level " + std::to_string(levelId));
    return true;
}
}
