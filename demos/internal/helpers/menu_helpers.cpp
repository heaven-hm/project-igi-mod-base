#include "menu_helpers.hpp"

#include "logging/logging.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/mvp_natives.hpp"
#include "ui/menu_resource.hpp"
#include "ui/menu_script.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace igi {
namespace {
constexpr int kModPrecipitationTaskId = 4095;

IGI::Game *g_precipitationGame = nullptr;
bool g_precipitationActive = false;

bool removePrecipitationTask() {
    auto *game = IGI::GAME::GET();
    if (!g_precipitationActive || !game || game != g_precipitationGame) {
        g_precipitationActive = false;
        g_precipitationGame = nullptr;
        return true;
    }

    auto *task = IGI::TASK::FIND_BY_ID(kModPrecipitationTaskId);
    if (task)
        IGI::TASK::DELETE_TASK(task);
    g_precipitationActive = false;
    g_precipitationGame = nullptr;
    return true;
}

bool createPrecipitationTask(std::string_view weather) {
    auto *game = IGI::GAME::GET();
    if (!game)
        return false;

    if (!removePrecipitationTask())
        return false;
    if (IGI::TASK::FIND_BY_ID(kModPrecipitationTaskId)) {
        log(LogLevel::Error, "weather: reserved precipitation task ID is already in use");
        return false;
    }

    const bool snow = weather == "Snow" || weather == "snow";
    const std::string script = "Task_New(" + std::to_string(kModPrecipitationTaskId) +
                               ", \"RainEffect\", \"IGIModPrecipitation\", " +
                               (snow ? "FALSE" : "TRUE") + ", 50.0, 20.0, \"TRUE\\n\", 0.06);";
    if (!executeMenuCommand(script)) {
        log(LogLevel::Error, "weather: RainEffect task creation command failed");
        return false;
    }

    if (!IGI::TASK::FIND_BY_ID(kModPrecipitationTaskId)) {
        log(LogLevel::Error, "weather: RainEffect task was not registered");
        return false;
    }
    g_precipitationGame = game;
    g_precipitationActive = true;
    return true;
}
} // namespace

bool requireLiveGameThread(Runtime &runtime) {
    if (runtime.allowDirectNativeCallbackThread())
        return true;
    log(LogLevel::Error, "live menu action rejected: runtime/game thread unavailable");
    return false;
}

bool refreshLiveMenu(Runtime &runtime, std::string_view effect) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *game = IGI::GAME::GET();
    if (!game)
        return false;
    struct GameRootLayout {
        std::uint8_t prefix[0x44];
        void *root;
    };
    auto *root =
        static_cast<IGI::Task *>(static_cast<GameRootLayout *>(static_cast<void *>(game))->root);
    auto *manager = root ? IGI::TASK::FIND_CHILD(root, IGI::MENU::GET_TYPE()) : nullptr;
    if (!manager) {
        log(LogLevel::Warning,
            "live effect applied without menu refresh: MenuManager unavailable for " +
                std::string(effect));
        return false;
    }
    IGI::MENU::REFRESH(reinterpret_cast<IGI::MenuManager *>(manager), 0);
    log(LogLevel::Info, "live effect applied: " + std::string(effect) + " without a level restart");
    return true;
}

bool setWeather(Runtime &runtime, std::string_view weather) {
    if (weather == "Off" || weather == "off" || weather == "false") {
        const char off[] = {'r', 0, '\0'};
        (void)removePrecipitationTask();
        IGI::GAME::SET_RAINING(off);
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: Weather set to Off");
        log(LogLevel::Info, "weather: RainEffect removed and Game_SetRaining disabled");
        (void)refreshLiveMenu(runtime, "weather off");
        return true;
    }

    const bool snow = weather == "Snow" || weather == "snow";
    if (!createPrecipitationTask(weather)) {
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: Weather effect failed");
        return false;
    }
    const char code[] = {'r', 1, '\0'};
    IGI::GAME::SET_RAINING(code);
    const char *label = snow ? "Snow" : "Rain";
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr,
                                   (std::string("IGI MOD: ") + label + " enabled").c_str());
    log(LogLevel::Info,
        std::string("weather: ") + label + " created through the retail RainEffect task lifecycle");
    (void)refreshLiveMenu(runtime, snow ? "snow" : "rain");
    return true;
}

bool setFps(Runtime &runtime, std::string_view value) {
    int fps = 30;
    try {
        fps = std::stoi(std::string(value));
    } catch (...) {
        fps = (value == "false") ? 60 : 30;
    }
    if (fps <= 0)
        fps = 30;
    IGI::MISC::FRAMES_SET(fps);
    const std::string statusMsg = "IGI MOD: FPS set to " + std::to_string(fps);
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, statusMsg.c_str());
    log(LogLevel::Info, "FPS set to " + std::to_string(fps) + " via IGI::MISC::FRAMES_SET");
    (void)refreshLiveMenu(runtime, "frame rate");
    return true;
}

void executeMenuNativeAction(const MenuConfig &config, Runtime &runtime, std::size_t item,
                             std::string_view value) {
    if (item >= config.items.size())
        return;
    const auto &id = config.items[item].id;
    const auto &action = config.items[item].nativeAction;
    if (!requireLiveGameThread(runtime))
        return;
    log(LogLevel::Info,
        "menu action: id=" + id + " action=" + action + " value=" + std::string(value));

    if (id == "weather" || action.find("SetWeather") != std::string::npos) {
        setWeather(runtime, value);
    } else if (id == "fps" || id == "frames" || action.find("Frame") != std::string::npos) {
        setFps(runtime, value);
    } else if (id == "ammo" || action.find("UnlimitedAmmo") != std::string::npos) {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: Unlimited ammo enabled");
        log(LogLevel::Info, "unlimited ammo enabled via IGI::PLAYER::SET_UNLIMITED_AMMO");
        (void)refreshLiveMenu(runtime, "unlimited ammo");
    } else if (id == "music" || action.find("MusicVolume") != std::string::npos) {
        float volume = 1.0F;
        if (value == "mute" || value == "Mute")
            volume = 0.0F;
        else if (value == "low" || value == "Low")
            volume = 0.35F;
        else
            volume = 1.0F;
        IGI::AUDIO::MUSIC_SET_VOLUME(volume, volume);
        log(LogLevel::Info, "music volume set to " + std::to_string(volume));
        (void)refreshLiveMenu(runtime, "music volume");
    } else if (id == "sfx") {
        float vol = 1.0F;
        if (value == "Mute" || value == "mute")
            vol = 0.0F;
        else if (value == "Low" || value == "low")
            vol = 0.35F;
        else
            vol = 1.0F;
        IGI::AUDIO::SET_SFX_VOLUME(vol);
        log(LogLevel::Info, "SFX volume set to " + std::to_string(vol));
        (void)refreshLiveMenu(runtime, "SFX volume");
    } else if (id == "view") {
        auto *player = IGI::GAME::GET_PLAYER(0);
        if (player) {
            const int mode = (value == "Third Person") ? 2 : 1;
            IGI::HUMAN::VIEW_CAM(reinterpret_cast<IGI::Human *>(player), mode);
            log(LogLevel::Info, "camera view set to " + std::string(value));
            (void)refreshLiveMenu(runtime, "camera view");
        }
    } else if (id == "level") {
        int levelNum = 1;
        if (value.starts_with("Level ")) {
            try {
                levelNum = std::stoi(std::string(value.substr(6)));
            } catch (...) {
                levelNum = 1;
            }
        }
        runtime.scheduler().enqueueOnce([&runtime, levelNum] { changeLevel(runtime, levelNum); });
    } else if (id == "display" || action.find("RefreshDisplay") != std::string::npos) {
        IGI::GAME::SET_DISPLAY_MODE();
        log(LogLevel::Info, "display mode refreshed");
        (void)refreshLiveMenu(runtime, "display mode");
    } else if (id == "diagnostics" || id == "perf" ||
               action.find("LogGameState") != std::string::npos) {
        std::ostringstream state;
        state << "IGI state: events=" << static_cast<int>(IGI::GAME::GET_EVENT0()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT1()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT2()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT3()) << " fps=" << IGI::OTHER::FLOW_GET_FPS()
              << " frames=" << IGI::OTHER::FLOW_GET_FRAMES() << " world=" << IGI::GAME::GET_WORLD();
        log(LogLevel::Info, state.str());
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, state.str().c_str());
    } else {
        log(LogLevel::Warning, "menu action has no native adapter: id=" + id + " action=" + action);
    }
}

static bool executeMenuCommandFor(IGI::Task *manager, std::string_view commandScript) {
    if (!manager)
        return false;
    auto *caller = igiNativeCaller();
    if (!caller)
        return false;
    const auto resources = igiMenuResourceApi(*caller);
    if (!resources.isLoaded || !resources.registerResource || !resources.freeResource)
        return false;

    std::vector<std::uint8_t> qvm;
    std::string error;
    if (!compileIgiMenuCommandScript(commandScript, qvm, error)) {
        log(LogLevel::Error, "executeMenuCommand: compilation failed: " + error);
        return false;
    }

    MenuResource cmdRes;
    if (!cmdRes.registerData("LOCAL:mod_base/temp_cmd.qvm", qvm, resources)) {
        log(LogLevel::Error, "executeMenuCommand: resource registration failed");
        return false;
    }

    (void)IGI::TASK::RUN_SCRIPT(manager, "LOCAL:mod_base/temp_cmd.qvm", 0);
    if (!cmdRes.release())
        log(LogLevel::Error, "executeMenuCommand: temporary resource release failed");
    log(LogLevel::Info,
        std::string("executeMenuCommand: executed script: ") + std::string(commandScript));
    return true;
}

bool executeMenuCommand(std::string_view commandScript) {
    auto *game = IGI::GAME::GET();
    if (!game)
        return false;
    struct GameRootLayout {
        std::uint8_t prefix[0x44];
        void *root;
    };
    auto *root =
        static_cast<IGI::Task *>(static_cast<GameRootLayout *>(static_cast<void *>(game))->root);
    if (!root)
        return false;
    auto *manager = IGI::TASK::FIND_CHILD(root, IGI::MENU::GET_TYPE());
    return executeMenuCommandFor(manager, commandScript);
}

bool changeLevel(Runtime &runtime, int levelId) {
    if (levelId < 1 || levelId > 14)
        return false;
    auto *game = IGI::GAME::GET();
    if (!game)
        return false;
    (void)removePrecipitationTask();
    if (!prepareMenuForLevelChange()) {
        log(LogLevel::Error, "level change: injected menu cleanup failed");
        return false;
    }
    if (!executeMenuCommand("Game_SetMission(" + std::to_string(levelId) + ");")) {
        log(LogLevel::Error, "level change: mission selection command failed");
        return false;
    }
    IGI::OTHER::FLOW_SET_REQUEST(4);
    IGI::TASK::DELETE_TASK(reinterpret_cast<IGI::Task *>(game));
    log(LogLevel::Info,
        "level change: game task unloaded; Flow will load level " + std::to_string(levelId));
    return true;
}
} // namespace igi
