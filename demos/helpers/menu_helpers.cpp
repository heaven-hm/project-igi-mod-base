#include "menu_helpers.hpp"

#include "logging/logging.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/igi_hashes/hashes.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/mvp_natives.hpp"
#include "ui/menu_resource.hpp"
#include "ui/menu_script.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <vector>

namespace igi {
static bool executeMenuCommandFor(IGI::Task *manager, std::string_view commandScript);
static bool requireLiveGameThread(Runtime &runtime);
static bool refreshLiveMenu(Runtime &runtime, std::string_view effect);
static bool executeMenuCommand(std::string_view commandScript);
static void setThirdPersonView(bool active);

namespace {
constexpr int kModPrecipitationTaskId = 4095;

IGI::Game *g_precipitationGame = nullptr;
bool g_precipitationActive = false;
std::atomic<bool> g_thirdPersonActive{false};

std::uint16_t findRainEffectType() {
#if defined(_WIN32)
    auto *typePtr = reinterpret_cast<const std::uint16_t *>(igi::re::HASH::gRainEffectType);
    return typePtr ? *typePtr : static_cast<std::uint16_t>(0);
#else
    return 0;
#endif
}

bool hasLevelWeather() {
    auto *level = reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET());
    if (IGI::TASK::FIND_BY_ID(kModPrecipitationTaskId))
        return true;

    const auto rainType = findRainEffectType();
    if (rainType != 0 && level && IGI::TASK::FIND_CHILD(level, rainType))
        return true;

    return g_precipitationActive;
}

bool removePrecipitationTask() {
    auto *level = reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET());

    auto *task = IGI::TASK::FIND_BY_ID(kModPrecipitationTaskId);
    if (task)
        IGI::TASK::DELETE_TASK(task);

    const auto rainType = findRainEffectType();
    if (rainType != 0 && level) {
        while (auto *existing = IGI::TASK::FIND_CHILD(level, rainType)) {
            log(LogLevel::Info, "weather: deleting existing level RainEffect task");
            IGI::TASK::DELETE_TASK(existing);
        }
    }

    g_precipitationActive = false;
    g_precipitationGame = nullptr;
    return true;
}

bool createPrecipitationTask(std::string_view weather) {
    auto *game = IGI::GAME::GET();
    if (!game)
        return false;
    auto *level = reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET());
    if (!level) {
        log(LogLevel::Error, "weather: active Level task unavailable");
        return false;
    }

    if (!removePrecipitationTask())
        return false;
    if (IGI::TASK::FIND_BY_ID(kModPrecipitationTaskId)) {
        log(LogLevel::Error, "weather: reserved precipitation task ID is already in use");
        return false;
    }

    const bool snow = weather == "Snow" || weather == "snow";
    const std::string script = "Task_New(" + std::to_string(kModPrecipitationTaskId) +
                               ", \"RainEffect\", \"IGIModPrecipitation\", " +
                               (snow ? "FALSE" : "TRUE") + ", 50.0, 20.0, \"TRUE\\n\", 0.3125);";
    if (!executeMenuCommandFor(level, script)) {
        log(LogLevel::Error, "weather: RainEffect task creation command failed");
        return false;
    }

    auto *task = IGI::TASK::FIND_BY_ID(kModPrecipitationTaskId);
    if (!task) {
        log(LogLevel::Error, "weather: RainEffect task was not registered");
        return false;
    }

#if defined(_WIN32)
    using GetChangedEvent = std::uint8_t(__cdecl *)();
    using Broadcast = void(__cdecl *)(IGI::Task *, std::uint8_t, void *);
    const auto changedEvent =
        reinterpret_cast<GetChangedEvent>(igi::re::HASH::Param_GetChangedEvent)();
    char changed = 1;
    reinterpret_cast<Broadcast>(igi::re::HASH::Task_Broadcast)(task, changedEvent, &changed);
#endif

    g_precipitationGame = game;
    g_precipitationActive = true;
    return true;
}
} // namespace

static bool requireLiveGameThread(Runtime &runtime) {
    if (runtime.allowDirectNativeCallbackThread())
        return true;
    log(LogLevel::Error, "live menu action rejected: runtime/game thread unavailable");
    return false;
}

static bool refreshLiveMenu(Runtime &runtime, std::string_view effect) {
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
    if (!requireLiveGameThread(runtime))
        return false;
    const bool wasActive = hasLevelWeather();

    if (weather == "Off" || weather == "off" || weather == "false") {
        const char off[] = {'r', 0, '\0'};
        (void)removePrecipitationTask();
        IGI::GAME::SET_RAINING(off);
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: Weather set to Off");
        log(LogLevel::Info, wasActive ? "weather: existing level weather replaced with Off"
                                      : "weather: RainEffect removed and Game_SetRaining disabled");
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
    if (wasActive) {
        const std::string msg = std::string("IGI MOD: Weather replaced with ") + label;
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, msg.c_str());
        log(LogLevel::Info, std::string("weather: replaced existing level weather with ") + label +
                                " (single weather enforced)");
    } else {
        const std::string msg = std::string("IGI MOD: ") + label + " enabled";
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, msg.c_str());
        log(LogLevel::Info, std::string("weather: ") + label +
                                " created through the retail RainEffect task lifecycle");
    }
    (void)refreshLiveMenu(runtime, snow ? "snow" : "rain");
    return true;
}

bool setFps(Runtime &runtime, std::string_view value) {
    if (!requireLiveGameThread(runtime))
        return false;
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

bool enableUnlimitedAmmo(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return false;
    IGI::PLAYER::SET_UNLIMITED_AMMO();
    log(LogLevel::Info, "unlimited ammo enabled via IGI::PLAYER::SET_UNLIMITED_AMMO");
    (void)refreshLiveMenu(runtime, "unlimited ammo");
    return true;
}

bool pickupPlayerWeapon(Runtime &runtime, std::string_view weaponId, int ammo) {
    if (!requireLiveGameThread(runtime) || weaponId.empty())
        return false;
    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;
    int args[] = {IGI::SCRIPT::EVAL_CONST(std::string(weaponId).c_str()), ammo};
    if (args[0] == 0)
        return false;
    IGI::PLAYER::PICK_UP_WEAPON(reinterpret_cast<IGI::Human *>(player), args);
    log(LogLevel::Info, "weapon pickup requested via IGI::PLAYER::PICK_UP_WEAPON");
    (void)refreshLiveMenu(runtime, "weapon pickup");
    return true;
}

bool cyclePlayerWeapon(Runtime &runtime, int force) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;
    IGI::PLAYER::CYCLE_WEAPON(reinterpret_cast<IGI::HumanPlayer *>(player), force);
    log(LogLevel::Info, "weapon cycle requested via IGI::PLAYER::CYCLE_WEAPON");
    return true;
}

bool requestPlayerWeaponAnimation(Runtime &runtime, int animationId) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;
    IGI::PLAYER::REQUEST_WEAPON_ANIM(reinterpret_cast<IGI::HumanPlayer *>(player), animationId);
    log(LogLevel::Info, "weapon animation requested via IGI::PLAYER::REQUEST_WEAPON_ANIM id=" +
                            std::to_string(animationId));
    return true;
}

bool restorePlayerVisualFeedback(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;
    auto *humanPlayer = reinterpret_cast<IGI::HumanPlayer *>(player);
    IGI::PLAYER::FADE_HIT_FLASH(humanPlayer);
    IGI::PLAYER::FADE_FLASH(humanPlayer);
    log(LogLevel::Info, "player visual feedback reset through retail fade natives");
    return true;
}

bool setPlayerInputEnabled(Runtime &runtime, bool enabled) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;
    auto *human = reinterpret_cast<IGI::Human *>(player);
    if (enabled)
        IGI::HUMAN::UNLOCK_INPUT(human);
    else
        IGI::HUMAN::LOCK_INPUT(human);
    log(LogLevel::Info, std::string("player input ") + (enabled ? "enabled" : "disabled"));
    return true;
}

bool setPlayerLookEnabled(Runtime &runtime, bool enabled) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;
    auto *human = reinterpret_cast<IGI::Human *>(player);
    if (enabled)
        IGI::HUMAN::ENABLE_LOOK(human);
    else
        IGI::HUMAN::DISABLE_LOOK(human);
    log(LogLevel::Info, std::string("player look ") + (enabled ? "enabled" : "disabled"));
    return true;
}

bool setMusicVolume(Runtime &runtime, float volume) {
    if (!requireLiveGameThread(runtime))
        return false;
    const float clamped = std::clamp(volume, 0.0F, 1.0F);
    IGI::AUDIO::MUSIC_SET_VOLUME(clamped, clamped);
    log(LogLevel::Info, "music volume set to " + std::to_string(clamped));
    (void)refreshLiveMenu(runtime, "music volume");
    return true;
}

bool setSfxVolume(Runtime &runtime, float volume) {
    if (!requireLiveGameThread(runtime))
        return false;
    const float clamped = std::clamp(volume, 0.0F, 1.0F);
    IGI::AUDIO::SET_SFX_VOLUME(clamped);
    log(LogLevel::Info, "SFX volume set to " + std::to_string(clamped));
    (void)refreshLiveMenu(runtime, "SFX volume");
    return true;
}

bool logFlowDiagnostics(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return false;
    std::ostringstream state;
    state << "IGI flow: fps=" << IGI::OTHER::FLOW_GET_FPS()
          << " frames=" << IGI::OTHER::FLOW_GET_FRAMES()
          << " renders=" << IGI::OTHER::FLOW_GET_RENDERS();
    log(LogLevel::Info, state.str());
    return true;
}

bool logPlayerState(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    const std::string state = "Player: team " + std::to_string(IGI::HUMAN::GET_TEAM(player)) +
                              ", " + (IGI::HUMAN::IS_DEAD(player) ? "dead" : "alive");
    log(LogLevel::Info, state);
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, state.c_str());
    return true;
}

bool setPlayerThirdPersonView(Runtime &runtime, bool active) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;
    const int mode = active ? 2 : 1;
    IGI::HUMAN::VIEW_CAM(reinterpret_cast<IGI::Human *>(player), mode);
    setThirdPersonView(active);
    log(LogLevel::Info,
        std::string("camera view set to ") + (active ? "third person" : "first person"));
    (void)refreshLiveMenu(runtime, "camera view");
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
        enableUnlimitedAmmo(runtime);
    } else if (id == "music" || action.find("MusicVolume") != std::string::npos) {
        float volume = 1.0F;
        if (value == "mute" || value == "Mute")
            volume = 0.0F;
        else if (value == "low" || value == "Low")
            volume = 0.35F;
        else
            volume = 1.0F;
        setMusicVolume(runtime, volume);
    } else if (id == "sfx") {
        float vol = 1.0F;
        if (value == "Mute" || value == "mute")
            vol = 0.0F;
        else if (value == "Low" || value == "low")
            vol = 0.35F;
        else
            vol = 1.0F;
        setSfxVolume(runtime, vol);
    } else if (id == "view") {
        setPlayerThirdPersonView(runtime, value == "Third Person");
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

static bool executeMenuCommand(std::string_view commandScript) {
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
    if (!requireLiveGameThread(runtime))
        return false;
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

static void setThirdPersonView(bool active) {
    g_thirdPersonActive.store(active, std::memory_order_release);
}

bool isThirdPersonActive() {
    return g_thirdPersonActive.load(std::memory_order_acquire);
}

bool renderThirdPersonCrosshair() {
#if defined(_WIN32)
    if (!isThirdPersonActive())
        return false;

    auto *game = IGI::GAME::GET();
    if (!game)
        return false;

    if (IGI::GAME::IS_PAUSED && IGI::GAME::IS_PAUSED())
        return false;

    auto *player = IGI::GAME::GET_PLAYER(0);
    if (!player)
        return false;

    using StatusScreenGetFn = void *(__cdecl *)();
    const auto statusScreenGet =
        reinterpret_cast<StatusScreenGetFn>(igi::re::HASH::StatusScreen_Get);
    if (!statusScreenGet)
        return false;

    auto *status = statusScreenGet();
    if (!status)
        return false;

    void *cross = reinterpret_cast<char *>(status) + 0x74;

    using DisplayGetModeFn = int *(__cdecl *)();
    const auto displayGetMode = reinterpret_cast<DisplayGetModeFn>(igi::re::HASH::Display_GetMode);
    if (!displayGetMode)
        return false;

    auto *mode = displayGetMode();
    if (!mode)
        return false;

    const int width = mode[1];
    const int height = mode[2];
    if (width <= 0 || height <= 0)
        return false;

    using SpriteDimFn = int(__cdecl *)(void *);
    const auto spriteWidth = reinterpret_cast<SpriteDimFn>(igi::re::HASH::Sprite_Width);
    const auto spriteHeight = reinterpret_cast<SpriteDimFn>(igi::re::HASH::Sprite_Height);
    if (!spriteWidth || !spriteHeight)
        return false;

    const int sw = spriteWidth(cross);
    const int sh = spriteHeight(cross);
    if (sw <= 0 || sh <= 0)
        return false;

    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    const float hw = static_cast<float>(sw) * 0.5f;
    const float hh = static_cast<float>(sh) * 0.5f;

    const float x = cx - std::round(hw);
    const float y = cy - std::round(hh);
    constexpr float kGray = 128.0f;

    using DrawSpriteZFn =
        void(__cdecl *)(const void *, float, float, float, float, float, int, int, int, float);
    const auto drawSpriteZ = reinterpret_cast<DrawSpriteZFn>(igi::re::HASH::Draw_SpriteZ);
    if (!drawSpriteZ)
        return false;
    drawSpriteZ(cross, x, y, kGray, kGray, kGray, 0, 0, 0, 1.0f);
    return true;
#else
    return false;
#endif
}
} // namespace igi
