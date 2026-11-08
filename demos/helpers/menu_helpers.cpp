#include "menu_helpers.hpp"

#include "logging/logging.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/igi_hashes/hashes.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/mvp_natives.hpp"
#include "natives/native_caller.hpp"
#include "ui/menu_resource.hpp"
#include "ui/menu_script.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <vector>

namespace igi {
static bool executeMenuCommandFor(IGI::Task *manager, std::string_view commandScript);
static bool requireLiveGameThread(Runtime &runtime);
static bool refreshLiveMenu(Runtime &runtime, std::string_view effect);
static bool executeMenuCommand(std::string_view commandScript);
static bool isNpcTask(IGI::Task *task);
static bool isCarTask(IGI::Task *task);

namespace {
constexpr int kModPrecipitationTaskId = 4095;
constexpr std::size_t kMaxLiveTargets = kMaxMenuListChoices;

int g_selectedNpcTaskId = -1;
int g_selectedCarTaskId = -1;

// Reports whether a rebuilt live list changed, and whether the previously
// selected task survived the rebuild.
struct RefreshOutcome {
    bool changed = false;
    bool preservedIsLive = true;
};

// Level unloads and mission restarts rebuild the game task list, so previously
// selected NPC/car task ids can never stay valid across those transitions.
void clearLiveTargetSelections(const std::string &reason) {
    const bool hadSelection = g_selectedNpcTaskId >= 0 || g_selectedCarTaskId >= 0;
    g_selectedNpcTaskId = -1;
    g_selectedCarTaskId = -1;
    if (hadSelection)
        log(LogLevel::Info, "live target selections cleared: " + reason);
}

// Guidance for a click with no target picked yet, plus self-healing for a
// selection that stopped being live so follow-up clicks stay clean.
void reportMissingCarTarget(std::string_view action) {
    if (g_selectedCarTaskId < 0) {
        log(LogLevel::Info,
            std::string(action) + ": no car selected; pick a live car target first");
        return;
    }
    g_selectedCarTaskId = -1;
    log(LogLevel::Warning,
        std::string(action) + ": selected car is no longer live; pick a live car target again");
}

void reportMissingNpcTarget(std::string_view action) {
    if (g_selectedNpcTaskId < 0) {
        log(LogLevel::Info,
            std::string(action) + ": no NPC selected; pick a live NPC target first");
        return;
    }
    g_selectedNpcTaskId = -1;
    log(LogLevel::Warning,
        std::string(action) + ": selected NPC is no longer live; pick a live NPC target again");
}

bool isCallableNative(std::uint32_t address, std::string_view signature) {
    auto *caller = igiNativeCaller();
    if (!caller)
        return false;
    std::uintptr_t resolved = 0;
    return caller->resolveByAddress(address, signature, resolved) == NativeCallStatus::Success;
}

std::optional<int> targetId(std::string_view value) {
    constexpr std::string_view kIdPrefix = " [#";
    const auto marker = value.rfind(kIdPrefix);
    if (marker == std::string_view::npos || !value.ends_with(']'))
        return std::nullopt;
    int number = 0;
    const auto suffix =
        value.substr(marker + kIdPrefix.size(), value.size() - marker - kIdPrefix.size() - 1);
    const auto [end, error] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), number);
    if (error != std::errc{} || end != suffix.data() + suffix.size() || number < 0)
        return std::nullopt;
    return number;
}

std::string targetLabel(std::string name, int id) {
    constexpr std::size_t kMaxNameLength = 30;
    if (name.empty())
        name = "Unnamed";
    if (name.size() > kMaxNameLength)
        name.resize(kMaxNameLength);
    return name + " [#" + std::to_string(id) + "]";
}

template <typename Predicate>
IGI::Task *findLiveTarget(Runtime &runtime, int id, Predicate &&predicate) {
    if (!requireLiveGameThread(runtime) || id < 0)
        return nullptr;
    auto *task = IGI::TASK::FIND_BY_ID(id);
    if (!task)
        return nullptr;
    return predicate(task) ? task : nullptr;
}

IGI::Game *g_precipitationGame = nullptr;
bool g_precipitationActive = false;

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

static IGI::HumanPlayer *localPlayer(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return nullptr;
    return reinterpret_cast<IGI::HumanPlayer *>(IGI::GAME::GET_PLAYER(0));
}

static IGI::Task *gameRoot(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return nullptr;
    auto *game = IGI::GAME::GET();
    if (!game)
        return nullptr;
    struct GameRootLayout {
        std::uint8_t prefix[0x44];
        void *root;
    };
    return static_cast<IGI::Task *>(static_cast<GameRootLayout *>(static_cast<void *>(game))->root);
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
    log(LogLevel::Info,
        std::string("camera view set to ") + (active ? "third person" : "first person"));
    (void)refreshLiveMenu(runtime, "camera view");
    return true;
}

bool refreshLiveTargetOptions(MenuConfig &config, Runtime &runtime) {
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    // Live lists are rebuilt from the task table on every refresh, so the
    // selection must follow its task identity instead of its old index.
    // Otherwise every kill, respawn or level change silently re-points the
    // menu at a different car or soldier.
    const auto replaceChoices =
        [&config](std::string_view id, const std::vector<LiveTarget> &targets,
                  std::string_view emptyLabel, int selectedTaskId) -> RefreshOutcome {
        const auto item = std::find_if(config.items.begin(), config.items.end(),
                                       [id](const auto &candidate) { return candidate.id == id; });
        if (item == config.items.end())
            return {};
        int preserved = selectedTaskId;
        if (preserved < 0 && item->defaultSelection < item->values.size())
            preserved = targetId(item->values[item->defaultSelection]).value_or(-1);
        std::vector<std::string> choices;
        choices.reserve(targets.size());
        std::size_t selection = 0;
        bool preservedIsLive = preserved < 0;
        for (const auto &target : targets) {
            if (target.taskId == preserved) {
                selection = choices.size();
                preservedIsLive = true;
            }
            choices.push_back(targetLabel(target.name, target.taskId));
        }
        if (choices.empty())
            choices.emplace_back(emptyLabel);
        const bool changed = item->values != choices;
        item->values = std::move(choices);
        item->defaultSelection = selection;
        return {changed, preservedIsLive};
    };

    EntityHelpers entities(runtime);
    const auto npc =
        replaceChoices("npc_target", entities.npcTargets(), "No live NPCs", g_selectedNpcTaskId);
    const auto car = replaceChoices("vehicle_target", entities.carTargets(), "No live cars",
                                    g_selectedCarTaskId);
    // A selection that vanished from the task table is dropped so both menu
    // frontends stop offering actions for a task that no longer exists.
    if (!npc.preservedIsLive)
        g_selectedNpcTaskId = -1;
    if (!car.preservedIsLive)
        g_selectedCarTaskId = -1;
    if (npc.changed || car.changed) {
        log(LogLevel::Info, "live targets refreshed from the active game task list");
    }
    return npc.changed || car.changed;
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

    if (id == "npc_target") {
        if (const auto selected = targetId(value)) {
            auto *task = findLiveTarget(runtime, *selected, isNpcTask);
            if (task) {
                g_selectedNpcTaskId = *selected;
                log(LogLevel::Info, "NPC target selected: " + std::string(value));
            } else {
                g_selectedNpcTaskId = -1;
                log(LogLevel::Warning,
                    "NPC target selection rejected: target is no longer live on this level");
            }
        } else {
            g_selectedNpcTaskId = -1;
            log(LogLevel::Info, "NPC target list empty: no live NPCs to select on this level");
        }
    } else if (id == "vehicle_target") {
        if (const auto selected = targetId(value)) {
            auto *task = findLiveTarget(runtime, *selected, isCarTask);
            if (task) {
                g_selectedCarTaskId = *selected;
                log(LogLevel::Info, "car target selected: " + std::string(value));
            } else {
                g_selectedCarTaskId = -1;
                log(LogLevel::Warning,
                    "car target selection rejected: target is no longer live on this level");
            }
        } else {
            g_selectedCarTaskId = -1;
            log(LogLevel::Info, "car target list empty: no live cars to select on this level");
        }
    } else if (id == "weather" || action.find("SetWeather") != std::string::npos) {
        setWeather(runtime, value);
    } else if (id == "fps" || id == "frames" || action.find("Frame") != std::string::npos) {
        setFps(runtime, value);
    } else if (id == "ammo" || action.find("UnlimitedAmmo") != std::string::npos) {
        enableUnlimitedAmmo(runtime);
    } else if (id == "weapon_cycle") {
        WeaponHelpers(runtime).cycle(1);
    } else if (id == "player_reset_damage") {
        PlayerHelpers(runtime).resetDamage();
    } else if (id == "player_snap_ground") {
        PlayerHelpers(runtime).snapToGround();
    } else if (id == "player_collision") {
        PlayerHelpers(runtime).setCollisionEnabled(value == "Enabled");
    } else if (id == "input") {
        PlayerHelpers(runtime).setInputEnabled(value == "Enabled");
    } else if (id == "look") {
        PlayerHelpers(runtime).setLookEnabled(value == "Enabled");
    } else if (id == "pause") {
        WorldHelpers(runtime).setPaused(value == "Paused");
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
        runtime.scheduler().enqueueOnce(
            [&runtime, levelNum] { WorldHelpers(runtime).changeLevel(levelNum); });
    } else if (id == "restart_mission") {
        WorldHelpers(runtime).restartMission();
    } else if (id == "vehicle_reset") {
        auto *car =
            reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
        if (!car) {
            reportMissingCarTarget("vehicle reset");
        } else if (VehicleHelpers(runtime).resetCar(car)) {
            log(LogLevel::Info, "vehicle reset: restored selected car through retail control flow");
        } else {
            log(LogLevel::Warning, "vehicle reset rejected: Car_Control native was unavailable");
        }
    } else if (id == "vehicle_explode") {
        auto *car =
            reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
        if (!car) {
            reportMissingCarTarget("vehicle explode");
        } else if (VehicleHelpers(runtime).explodeCar(car)) {
            log(LogLevel::Info, "vehicle explode: triggered explosion for selected car");
        } else {
            log(LogLevel::Warning, "vehicle explode rejected: Car_Explode native was unavailable");
        }
    } else if (id == "vehicle_drive") {
        float throttle = 0.0F;
        bool brake = false;
        if (value == "Reverse")
            throttle = -0.25F;
        else if (value == "Quarter")
            throttle = 0.25F;
        else if (value == "Half")
            throttle = 0.5F;
        else if (value == "Full")
            throttle = 1.0F;
        else if (value == "Brake")
            brake = true;
        auto *car =
            reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
        if (!car) {
            reportMissingCarTarget("vehicle drive");
        } else if (VehicleHelpers(runtime).driveCar(car, throttle, brake)) {
            log(LogLevel::Info, "vehicle drive: applied selected car control");
        } else {
            log(LogLevel::Warning, "vehicle drive rejected: Car_Control native was unavailable");
        }
    } else if (id == "npc_ensure_weapon") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("NPC equip");
        } else if (NpcHelpers(runtime).ensureWeapon(npc)) {
            log(LogLevel::Info, "NPC weapon provisioned through the soldier native");
        } else {
            log(LogLevel::Warning,
                "NPC weapon request rejected: Soldier_EnsureWeapon native was unavailable");
        }
    } else if (id == "npc_reset_damage") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("NPC damage reset");
        } else if (NpcHelpers(runtime).resetDamage(npc)) {
            log(LogLevel::Info, "NPC damage state reset through the human native");
        } else {
            log(LogLevel::Warning,
                "NPC damage reset rejected: Human_ResetDamage native was unavailable");
        }
    } else if (id == "npc_snap_ground") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("NPC ground recovery");
        } else if (NpcHelpers(runtime).snapToGround(npc)) {
            log(LogLevel::Info, "NPC snapped to ground through the human native");
        } else {
            log(LogLevel::Warning,
                "NPC ground recovery rejected: Human_SnapToGround native was unavailable");
        }
    } else if (id == "npc_collision") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("NPC collision");
        } else if (NpcHelpers(runtime).setCollisionEnabled(npc, value == "Enabled")) {
            log(LogLevel::Info, "NPC collision state updated through the human native");
        } else {
            log(LogLevel::Warning,
                "NPC collision request rejected: Human collision native was unavailable");
        }
    } else if (id == "npc_execute") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("soldier execution");
        } else if (NpcHelpers(runtime).executeSoldier(npc)) {
            log(LogLevel::Info, "selected soldier executed through the retail soldier flow");
        } else {
            log(LogLevel::Warning,
                "soldier execution rejected: selected target is not a standard Soldier");
        }
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
    clearLiveTargetSelections("level change unloads the current level task list");
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

bool PlayerHelpers::setInputEnabled(bool enabled) const {
    return setPlayerInputEnabled(runtime_, enabled);
}
static IGI::Task *entityContainer(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return nullptr;
    if (auto *dynamic = IGI::LEVEL::GET_DYNAMIC())
        return dynamic;
    if (auto *level = reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET()))
        return level;
    return gameRoot(runtime);
}

#pragma pack(push, 1)
struct IgiTaskLayout {
    IgiTaskLayout *next;
    IgiTaskLayout *prev;
    IgiTaskLayout *head;
    IgiTaskLayout *tail;
    IgiTaskLayout *tailpred;
    IgiTaskLayout *parent;
    int refs;
    std::uint16_t type;
    char pooled;
    char unk1F;
};
#pragma pack(pop)

static bool isNpcTask(IGI::Task *task) {
    if (!task)
        return false;
    const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
    const auto humanType = IGI::HUMAN::GET_TYPE();
    return (type == humanType || IGI::TASK::IS_A(type, humanType) != 0) &&
           !IGI::HUMAN::IS_PLAYER(reinterpret_cast<IGI::Human *>(task));
}

static bool isCarTask(IGI::Task *task) {
    if (!task)
        return false;
    const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
    const auto carType = IGI::VEHICLE::GET_TYPE();
    return type == carType || IGI::TASK::IS_A(type, carType) != 0;
}

template <typename Predicate>
static IGI::Task *findTaskInHierarchy(IGI::Task *root, Predicate &&pred, int depth = 10) {
    if (!root || depth <= 0)
        return nullptr;
    auto *taskNode = reinterpret_cast<const IgiTaskLayout *>(root);
    if (pred(root, taskNode->type))
        return root;
    for (auto *child = taskNode->head; child && child->next; child = child->next) {
        if (auto *found =
                findTaskInHierarchy(reinterpret_cast<IGI::Task *>(child), pred, depth - 1))
            return found;
    }
    return nullptr;
}

IGI::Human *EntityHelpers::firstHuman() const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    auto *container = entityContainer(runtime_);
    if (container) {
        if (auto *human = IGI::TASK::FIND_CHILD(container, IGI::HUMAN::GET_TYPE()))
            return reinterpret_cast<IGI::Human *>(human);
        if (auto *soldier = IGI::TASK::FIND_CHILD(container, IGI::SOLDIER::GET_TYPE()))
            return reinterpret_cast<IGI::Human *>(soldier);
    }
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = *reinterpret_cast<const std::uint16_t *>(
            reinterpret_cast<const std::uint8_t *>(task) + 0x1c);
        if (type == IGI::HUMAN::GET_TYPE() || type == IGI::SOLDIER::GET_TYPE() ||
            type == IGI::PLAYER::GET_TYPE()) {
            return reinterpret_cast<IGI::Human *>(task);
        }
    }
    return nullptr;
}
IGI::Human *EntityHelpers::nextHuman(IGI::Human *current) const {
    if (!requireLiveGameThread(runtime_) || !current)
        return nullptr;
    auto *next =
        IGI::TASK::FIND_NEXT(reinterpret_cast<IGI::Task *>(current), IGI::HUMAN::GET_TYPE());
    if (!next) {
        next =
            IGI::TASK::FIND_NEXT(reinterpret_cast<IGI::Task *>(current), IGI::SOLDIER::GET_TYPE());
    }
    if (next)
        return reinterpret_cast<IGI::Human *>(next);
    return nullptr;
}
IGI::Human *EntityHelpers::firstNpc() const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    for (auto *human = firstHuman(); human; human = nextHuman(human)) {
        if (!IGI::HUMAN::IS_PLAYER(human))
            return human;
    }
    auto *player = IGI::GAME::GET_PLAYER(0);
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task || task == player)
            continue;
        const auto type = *reinterpret_cast<const std::uint16_t *>(
            reinterpret_cast<const std::uint8_t *>(task) + 0x1c);
        if (type == IGI::HUMAN::GET_TYPE() || type == IGI::SOLDIER::GET_TYPE()) {
            auto *human = reinterpret_cast<IGI::Human *>(task);
            if (!IGI::HUMAN::IS_PLAYER(human))
                return human;
        }
    }
    return nullptr;
}
IGI::Human *EntityHelpers::npcAt(std::size_t slot) const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    const auto humanType = IGI::HUMAN::GET_TYPE();
    std::size_t found = 0;
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (type != humanType && IGI::TASK::IS_A(type, humanType) == 0)
            continue;
        auto *human = reinterpret_cast<IGI::Human *>(task);
        if (IGI::HUMAN::IS_PLAYER(human))
            continue;
        if (found++ == slot)
            return human;
    }
    return nullptr;
}
IGI::Car *EntityHelpers::firstCar() const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    const auto carType = IGI::VEHICLE::GET_TYPE();
    auto isCar = [carType](IGI::Task *t, std::uint16_t type) {
        if (!t)
            return false;
        if (type == carType)
            return true;
        return IGI::TASK::IS_A(type, carType) != 0;
    };
    if (auto *dynamic = IGI::LEVEL::GET_DYNAMIC()) {
        if (auto *found = findTaskInHierarchy(dynamic, isCar))
            return reinterpret_cast<IGI::Car *>(found);
    }
    if (auto *level = reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET())) {
        if (auto *found = findTaskInHierarchy(level, isCar))
            return reinterpret_cast<IGI::Car *>(found);
    }
    for (int knownId : {1099, 778}) {
        if (auto *task = IGI::TASK::FIND_BY_ID(knownId)) {
            const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
            if (isCar(task, type))
                return reinterpret_cast<IGI::Car *>(task);
        }
    }
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (isCar(task, type))
            return reinterpret_cast<IGI::Car *>(task);
    }
    return nullptr;
}
IGI::Car *EntityHelpers::carAt(std::size_t slot) const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    const auto carType = IGI::VEHICLE::GET_TYPE();
    std::size_t found = 0;
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (type != carType && IGI::TASK::IS_A(type, carType) == 0)
            continue;
        if (found++ == slot)
            return reinterpret_cast<IGI::Car *>(task);
    }
    return nullptr;
}
std::vector<LiveTarget> EntityHelpers::npcTargets() const {
    std::vector<LiveTarget> targets;
    if (!requireLiveGameThread(runtime_))
        return targets;
    for (int id = 0; id < 4096 && targets.size() < kMaxLiveTargets; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!isNpcTask(task))
            continue;
        char name[128]{};
        IGI::TASK::GET_TASK_NAME(name, task);
        targets.push_back({IGI::TASK::GET_ID(task), name});
    }
    return targets;
}
std::vector<LiveTarget> EntityHelpers::carTargets() const {
    std::vector<LiveTarget> targets;
    if (!requireLiveGameThread(runtime_))
        return targets;
    for (int id = 0; id < 4096 && targets.size() < kMaxLiveTargets; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!isCarTask(task))
            continue;
        char name[128]{};
        IGI::TASK::GET_TASK_NAME(name, task);
        targets.push_back({IGI::TASK::GET_ID(task), name});
    }
    return targets;
}
IGI::Car *EntityHelpers::nextCar(IGI::Car *current) const {
    if (!requireLiveGameThread(runtime_) || !current)
        return nullptr;
    return reinterpret_cast<IGI::Car *>(
        IGI::TASK::FIND_NEXT(reinterpret_cast<IGI::Task *>(current), IGI::VEHICLE::GET_TYPE()));
}
IGI::Task *EntityHelpers::firstVehicle() const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    const auto baseVehicleType = IGI::VEHICLE::GET_BASE_TYPE();
    auto isVehicle = [baseVehicleType](IGI::Task *t, std::uint16_t type) {
        if (!t)
            return false;
        if (type == baseVehicleType)
            return true;
        return IGI::TASK::IS_A(type, baseVehicleType) != 0;
    };
    if (auto *dynamic = IGI::LEVEL::GET_DYNAMIC()) {
        if (auto *found = findTaskInHierarchy(dynamic, isVehicle))
            return found;
    }
    if (auto *level = reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET())) {
        if (auto *found = findTaskInHierarchy(level, isVehicle))
            return found;
    }
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (isVehicle(task, type))
            return task;
    }
    return nullptr;
}
IGI::Task *EntityHelpers::nextVehicle(IGI::Task *current) const {
    if (!requireLiveGameThread(runtime_) || !current)
        return nullptr;
    return IGI::TASK::FIND_NEXT(current, IGI::VEHICLE::GET_BASE_TYPE());
}
bool EntityHelpers::moveHuman(IGI::Human *human, const std::array<double, 3> &position) const {
    if (!requireLiveGameThread(runtime_) || !human)
        return false;
    auto mutablePosition = position;
    IGI::HUMAN::SET_POSITION(human, mutablePosition.data());
    return true;
}
bool EntityHelpers::setHumanYaw(IGI::Human *human, float yaw) const {
    if (!requireLiveGameThread(runtime_) || !human)
        return false;
    IGI::HUMAN::SET_YAW(human, yaw);
    return true;
}
bool EntityHelpers::snapHumanToGround(IGI::Human *human) const {
    if (!requireLiveGameThread(runtime_) || !human)
        return false;
    if (!isCallableNative(IGI::HASH::HUMAN_SNAP_TO_GROUND, "void Human_SnapToGround(Human *h)"))
        return false;
    // Log the exact native invocation so a crash inside Human_SnapToGround is
    // visible in the log instead of a silent drop after the menu action line.
    log(LogLevel::Info,
        "snap to ground: invoking Human_SnapToGround for task #" +
            std::to_string(IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(human))));
    IGI::HUMAN::SNAP_TO_GROUND(human);
    return true;
}
bool EntityHelpers::leaveHumanGround(IGI::Human *human) const {
    if (!requireLiveGameThread(runtime_) || !human)
        return false;
    if (!isCallableNative(IGI::HASH::HUMAN_LEAVE_GROUND, "void Human_LeaveGround(Human *h)"))
        return false;
    IGI::HUMAN::LEAVE_GROUND(human);
    return true;
}
bool EntityHelpers::setHumanCollision(IGI::Human *human, bool enabled) const {
    if (!requireLiveGameThread(runtime_) || !human)
        return false;
    if (enabled) {
        if (!isCallableNative(IGI::HASH::HUMAN_ENABLE_COLLISION,
                              "void Human_EnableCollision(Human *h)"))
            return false;
        IGI::HUMAN::ENABLE_COLLISION(human);
    } else {
        if (!isCallableNative(IGI::HASH::HUMAN_DISABLE_COLLISION,
                              "void Human_DisableCollision(Human *h)"))
            return false;
        IGI::HUMAN::DISABLE_COLLISION(human);
    }
    return true;
}
bool PlayerHelpers::setLookEnabled(bool enabled) const {
    return setPlayerLookEnabled(runtime_, enabled);
}
bool PlayerHelpers::checkDeath() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::CHECK_DEATH(p);
    return true;
}
bool PlayerHelpers::sendPosition() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::SEND_POSITION(p);
    return true;
}
bool PlayerHelpers::resetDamage() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::HUMAN::RESET_DAMAGE(reinterpret_cast<IGI::Human *>(p));
    return true;
}
bool PlayerHelpers::snapToGround() const {
    auto *p = localPlayer(runtime_);
    return EntityHelpers(runtime_).snapHumanToGround(reinterpret_cast<IGI::Human *>(p));
}
bool PlayerHelpers::setCollisionEnabled(bool enabled) const {
    auto *p = localPlayer(runtime_);
    return EntityHelpers(runtime_).setHumanCollision(reinterpret_cast<IGI::Human *>(p), enabled);
}
bool PlayerHelpers::startAction(int action) const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::START_ACTION(p, action);
    return true;
}
bool PlayerHelpers::setFlagD44() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::SET_FLAG_D44(p);
    return true;
}
bool PlayerHelpers::clearFlagD44() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::CLEAR_FLAG_D44(p);
    return true;
}
bool PlayerHelpers::setFlagD68() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::SET_FLAG_D68(p);
    return true;
}
bool PlayerHelpers::clearFlagD68() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::CLEAR_FLAG_D68(p);
    return true;
}
bool PlayerHelpers::restoreVisualFeedback() const {
    return restorePlayerVisualFeedback(runtime_);
}
bool WeaponHelpers::pickup(std::string_view id, int ammo) const {
    return pickupPlayerWeapon(runtime_, id, ammo);
}
bool WeaponHelpers::enableUnlimitedAmmo() const {
    return igi::enableUnlimitedAmmo(runtime_);
}
bool WeaponHelpers::setAnimation(int animation) const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::SET_WEAPON_ANIM(p, animation);
    return true;
}
bool WeaponHelpers::requestAnimation(int animation) const {
    return requestPlayerWeaponAnimation(runtime_, animation);
}
bool WeaponHelpers::cycle(int force) const {
    return cyclePlayerWeapon(runtime_, force);
}
bool WeaponHelpers::selectCategory() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    IGI::PLAYER::SELECT_WEAPON_CATEGORY(p);
    return true;
}
std::optional<int> WeaponHelpers::hasAmmo(void *task) const {
    if (!requireLiveGameThread(runtime_) || !task)
        return std::nullopt;
    return IGI::WEAPON::HAS_AMMO(task);
}
IGI::WeaponType *WeaponHelpers::type(void *task) const {
    return requireLiveGameThread(runtime_) && task ? IGI::WEAPON::WEAPON_GET_TYPE(task) : nullptr;
}
bool CameraHelpers::setViewMode(int mode) const {
    if (mode != 1 && mode != 2)
        return false;
    return setPlayerThirdPersonView(runtime_, mode == 2);
}
std::optional<int> CameraHelpers::mode() const {
    auto *player = localPlayer(runtime_);
    if (!player)
        return std::nullopt;
    IGI::HumanCameraInfo info{};
    IGI::HUMAN::GET_CAMERA_INFO(reinterpret_cast<IGI::Human *>(player), &info);
    return info.mode;
}
std::optional<bool> CameraHelpers::isThirdPersonActive() const {
    const auto cameraMode = mode();
    return cameraMode ? std::optional<bool>(*cameraMode == 2) : std::nullopt;
}
bool WorldHelpers::changeLevel(int level) const {
    return igi::changeLevel(runtime_, level);
}
bool WorldHelpers::restartMission() const {
    if (!requireLiveGameThread(runtime_) || !IGI::GAME::GET())
        return false;
    clearLiveTargetSelections("mission restart rebuilds the level task list");
    IGI::GAME::RESTART();
    log(LogLevel::Info, "mission restart requested via IGI::GAME::RESTART");
    return true;
}
bool WorldHelpers::setWeather(std::string_view weather) const {
    return igi::setWeather(runtime_, weather);
}
bool WorldHelpers::setFrameCap(int fps) const {
    return igi::setFps(runtime_, std::to_string(fps));
}
bool WorldHelpers::logFlowDiagnostics() const {
    return igi::logFlowDiagnostics(runtime_);
}
bool AudioHelpers::setMusicVolume(float volume) const {
    return igi::setMusicVolume(runtime_, volume);
}
bool AudioHelpers::setSfxVolume(float volume) const {
    return igi::setSfxVolume(runtime_, volume);
}

std::optional<float> PlayerHelpers::moveScale() const {
    if (!requireLiveGameThread(runtime_))
        return std::nullopt;
    return IGI::PLAYER::GET_MOVE_SCALE();
}
std::optional<int> PlayerHelpers::state() const {
    auto *p = localPlayer(runtime_);
    return p ? std::optional<int>(IGI::HUMAN::GET_STATE(reinterpret_cast<IGI::Human *>(p)))
             : std::nullopt;
}
std::optional<int> PlayerHelpers::team() const {
    auto *p = localPlayer(runtime_);
    return p ? std::optional<int>(IGI::HUMAN::GET_TEAM(reinterpret_cast<IGI::Human *>(p)))
             : std::nullopt;
}
std::optional<int> PlayerHelpers::index() const {
    auto *p = localPlayer(runtime_);
    return p ? std::optional<int>(IGI::HUMAN::GET_INDEX(reinterpret_cast<IGI::Human *>(p)))
             : std::nullopt;
}
std::optional<bool> PlayerHelpers::isDead() const {
    auto *p = localPlayer(runtime_);
    return p ? std::optional<bool>(IGI::HUMAN::IS_DEAD(reinterpret_cast<IGI::Human *>(p)) != 0)
             : std::nullopt;
}
std::optional<bool> PlayerHelpers::isCrouching() const {
    auto *p = localPlayer(runtime_);
    return p ? std::optional<bool>(IGI::HUMAN::IS_CROUCHING(reinterpret_cast<IGI::Human *>(p)) != 0)
             : std::nullopt;
}
std::optional<bool> PlayerHelpers::isActive() const {
    auto *p = localPlayer(runtime_);
    return p ? std::optional<bool>(IGI::HUMAN::IS_ACTIVE(reinterpret_cast<IGI::Human *>(p)) != 0)
             : std::nullopt;
}
std::optional<bool> PlayerHelpers::isPlayer() const {
    auto *p = localPlayer(runtime_);
    return p ? std::optional<bool>(IGI::HUMAN::IS_PLAYER(reinterpret_cast<IGI::Human *>(p)) != 0)
             : std::nullopt;
}
std::optional<unsigned> WeaponHelpers::fireEvent() const {
    return requireLiveGameThread(runtime_) ? std::optional<unsigned>(IGI::WEAPON::GET_FIRE_EVENT())
                                           : std::nullopt;
}
std::optional<unsigned> WeaponHelpers::typeEvent() const {
    return requireLiveGameThread(runtime_) ? std::optional<unsigned>(IGI::WEAPON::GET_TYPE_EVENT())
                                           : std::nullopt;
}
std::optional<bool> WeaponHelpers::triggerEvent() const {
    return requireLiveGameThread(runtime_)
               ? std::optional<bool>(IGI::WEAPON::GET_TRIGGER_EVENT() != 0)
               : std::nullopt;
}
std::optional<bool> WeaponHelpers::stateEvent() const {
    return requireLiveGameThread(runtime_)
               ? std::optional<bool>(IGI::WEAPON::GET_STATE_EVENT() != 0)
               : std::nullopt;
}
std::optional<bool> WeaponHelpers::moveEvent() const {
    return requireLiveGameThread(runtime_) ? std::optional<bool>(IGI::WEAPON::GET_MOVE_EVENT() != 0)
                                           : std::nullopt;
}
bool WorldHelpers::setPaused(bool paused) const {
    if (!requireLiveGameThread(runtime_))
        return false;
    IGI::GAME::SET_PAUSED(paused ? 1 : 0);
    return true;
}
std::optional<bool> WorldHelpers::isPaused() const {
    return requireLiveGameThread(runtime_) ? std::optional<bool>(IGI::GAME::IS_PAUSED() != 0)
                                           : std::nullopt;
}
bool WorldHelpers::refreshDisplay() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    IGI::GAME::SET_DISPLAY_MODE();
    return true;
}
std::optional<unsigned> WorldHelpers::event(int i) const {
    if (!requireLiveGameThread(runtime_) || i < 0 || i > 3)
        return std::nullopt;
    const unsigned e[] = {IGI::GAME::GET_EVENT0(), IGI::GAME::GET_EVENT1(), IGI::GAME::GET_EVENT2(),
                          IGI::GAME::GET_EVENT3()};
    return e[i];
}
std::optional<int> WorldHelpers::flowFps() const {
    return requireLiveGameThread(runtime_) ? std::optional<int>(IGI::OTHER::FLOW_GET_FPS())
                                           : std::nullopt;
}
std::optional<int> WorldHelpers::flowFrames() const {
    return requireLiveGameThread(runtime_) ? std::optional<int>(IGI::OTHER::FLOW_GET_FRAMES())
                                           : std::nullopt;
}
std::optional<int> WorldHelpers::flowRenders() const {
    return requireLiveGameThread(runtime_) ? std::optional<int>(IGI::OTHER::FLOW_GET_RENDERS())
                                           : std::nullopt;
}
std::optional<int> WorldHelpers::taskCount() const {
    return requireLiveGameThread(runtime_) ? std::optional<int>(IGI::TASK::GET_COUNT())
                                           : std::nullopt;
}
bool CameraHelpers::setWeaponTarget(IGI::HumanView *v, float p, float y, float ps, float ys) const {
    if (!requireLiveGameThread(runtime_) || !v)
        return false;
    IGI::CAMERA::SET_WEAPON_TARGET(v, p, y, ps, ys);
    return true;
}
bool CameraHelpers::setWeaponAngles(IGI::HumanView *v, float p, float y) const {
    if (!requireLiveGameThread(runtime_) || !v)
        return false;
    IGI::CAMERA::SET_WEAPON_ANGLES(v, p, y);
    return true;
}
bool CameraHelpers::setCamera(IGI::HumanView *v, const double *position,
                              const IGI::Matrix *rotation) const {
    if (!requireLiveGameThread(runtime_) || !v || !position || !rotation)
        return false;
    IGI::CAMERA::SET_CAMERA(v, position, rotation);
    return true;
}
bool CameraHelpers::setTarget(IGI::HumanView *v, const int *target, bool enabled) const {
    if (!requireLiveGameThread(runtime_) || !v || !target)
        return false;
    IGI::CAMERA::SET_TARGET(v, target, enabled ? 1 : 0);
    return true;
}
bool CameraHelpers::setDip(IGI::HumanView *v, float d) const {
    if (!requireLiveGameThread(runtime_) || !v)
        return false;
    IGI::CAMERA::SET_DIP(v, d);
    return true;
}
bool CameraHelpers::setZoomed(IGI::HumanView *v) const {
    if (!requireLiveGameThread(runtime_) || !v)
        return false;
    IGI::CAMERA::SET_ZOOMED(v);
    return true;
}
bool CameraHelpers::eye(IGI::HumanView *v, double *out) const {
    if (!requireLiveGameThread(runtime_) || !v || !out)
        return false;
    IGI::CAMERA::GET_EYE(out, v);
    return true;
}
bool CameraHelpers::forward(IGI::HumanView *v, float *out) const {
    if (!requireLiveGameThread(runtime_) || !v || !out)
        return false;
    IGI::CAMERA::GET_FORWARD(out, v);
    return true;
}
bool CameraHelpers::matrix(IGI::HumanView *v, IGI::Matrix *out) const {
    if (!requireLiveGameThread(runtime_) || !v || !out)
        return false;
    IGI::CAMERA::GET_MATRIX(out, v);
    return true;
}
bool VehicleHelpers::carControlInfo(IGI::Car *c, int *o) const {
    if (!requireLiveGameThread(runtime_) || !c || !o)
        return false;
    IGI::VEHICLE::CAR_GET_CONTROL_INFO(c, o);
    return true;
}
bool VehicleHelpers::carFrame(IGI::Car *c, void **out) const {
    if (!requireLiveGameThread(runtime_) || !c || !out)
        return false;
    IGI::VEHICLE::GET_FRAME(c, out);
    return true;
}
bool VehicleHelpers::carLight(IGI::Car *c, void **out) const {
    if (!requireLiveGameThread(runtime_) || !c || !out)
        return false;
    IGI::VEHICLE::GET_LIGHT(c, out);
    return true;
}
IGI::Task *VehicleHelpers::carMountedGun(IGI::Car *c) const {
    return requireLiveGameThread(runtime_) && c ? IGI::VEHICLE::GET_MOUNTED_GUN(c) : nullptr;
}
bool VehicleHelpers::carGunnerPose(IGI::Car *c, IGI::Vec3d *position, IGI::Matrix *matrix) const {
    if (!requireLiveGameThread(runtime_) || !c || !position || !matrix)
        return false;
    IGI::VEHICLE::GET_GUNNER_POS(c, position, matrix);
    return true;
}
std::optional<long double> VehicleHelpers::carTrackFactor(IGI::Car *c, int j) const {
    return requireLiveGameThread(runtime_) && c
               ? std::optional<long double>(IGI::VEHICLE::GET_TRACK_FACTOR(c, j))
               : std::nullopt;
}
bool VehicleHelpers::explodeCar(IGI::Car *car) const {
    if (!requireLiveGameThread(runtime_) || !car)
        return false;
    IGI::VEHICLE::CAR_EXPLODE(car);
    return true;
}
bool VehicleHelpers::resetCar(IGI::Car *car) const {
    if (!requireLiveGameThread(runtime_) || !car)
        return false;
    if (!isCallableNative(IGI::HASH::CAR_CONTROL, "void Car_Control(Car *o, CarControlArg *arg)"))
        return false;
    // Log the exact native invocation so a crash or hang inside the Car_Control
    // reset path is visible in the log instead of a silent drop.
    log(LogLevel::Info, "vehicle reset: invoking Car_Control reset for task #" +
                            std::to_string(IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(car))));
    float input[4] = {0.0F, -1.0F, -2.0F, -1.0F};
    IGI::CarControlArg reset{0, input};
    IGI::VEHICLE::CAR_CONTROL(car, &reset);
    return true;
}
bool VehicleHelpers::driveCar(IGI::Car *car, float throttle, bool brake) const {
    if (!requireLiveGameThread(runtime_) || !car || throttle < -0.25F || throttle > 1.0F)
        return false;
    if (!isCallableNative(IGI::HASH::CAR_CONTROL, "void Car_Control(Car *o, CarControlArg *arg)"))
        return false;
    float input[4] = {0.0F, throttle, 0.0F, brake ? 1.0F : -1.0F};
    IGI::CarControlArg drive{1, input};
    IGI::VEHICLE::CAR_CONTROL(car, &drive);
    return true;
}
bool VehicleHelpers::resetControls(void *vehicle) const {
    if (!requireLiveGameThread(runtime_) || !vehicle)
        return false;
    IGI::VEHICLE::RESET_CONTROLS(vehicle);
    return true;
}
bool NpcHelpers::ensureWeapon(IGI::Human *npc) const {
    if (!requireLiveGameThread(runtime_) || !npc || IGI::HUMAN::IS_PLAYER(npc))
        return false;
    IGI::SOLDIER::ENSURE_WEAPON(npc);
    return true;
}
bool NpcHelpers::resetDamage(IGI::Human *npc) const {
    if (!requireLiveGameThread(runtime_) || !npc)
        return false;
    IGI::HUMAN::RESET_DAMAGE(npc);
    return true;
}
bool NpcHelpers::snapToGround(IGI::Human *npc) const {
    return EntityHelpers(runtime_).snapHumanToGround(npc);
}
bool NpcHelpers::setCollisionEnabled(IGI::Human *npc, bool enabled) const {
    return EntityHelpers(runtime_).setHumanCollision(npc, enabled);
}
bool NpcHelpers::executeSoldier(IGI::Human *npc) const {
    if (!requireLiveGameThread(runtime_) || !npc)
        return false;
    const auto type = reinterpret_cast<const IgiTaskLayout *>(npc)->type;
    if (type != IGI::SOLDIER::GET_TYPE())
        return false;
    if (!isCallableNative(IGI::HASH::SOLDIER_DIE_ENTER,
                          "void Soldier_DieEnter(HumanSM *sm, Human *h)"))
        return false;
    auto *state = reinterpret_cast<void *>(reinterpret_cast<std::uint8_t *>(npc) + 0x2ec);
    IGI::SOLDIER::EXECUTE(state, npc);
    return true;
}
IGI::Task *VehicleHelpers::heliDoor(IGI::Heli *h) const {
    return requireLiveGameThread(runtime_) && h ? IGI::VEHICLE::FIND_DOOR(h) : nullptr;
}
bool VehicleHelpers::heliControlInfo(IGI::Heli *h, int *o) const {
    if (!requireLiveGameThread(runtime_) || !h || !o)
        return false;
    IGI::VEHICLE::HELI_GET_CONTROL_INFO(h, o);
    return true;
}
bool VehicleHelpers::heliStateSize(IGI::Heli *h, int *out) const {
    if (!requireLiveGameThread(runtime_) || !h || !out)
        return false;
    IGI::VEHICLE::GET_STATE_SIZE(h, out);
    return true;
}
bool VehicleHelpers::heliConfigSize(IGI::Task *task, int *out) const {
    if (!requireLiveGameThread(runtime_) || !task || !out)
        return false;
    IGI::VEHICLE::GET_CONFIG_SIZE(task, out);
    return true;
}
bool VehicleHelpers::heliSoundInfo(IGI::Heli *h, void **out) const {
    if (!requireLiveGameThread(runtime_) || !h || !out)
        return false;
    IGI::VEHICLE::GET_SOUND_INFO(h, out);
    return true;
}
#define AI_HELPER(method, native)                                                                  \
    bool AiHelpers::method(const AiScriptContext &c) const {                                       \
        if (!requireLiveGameThread(runtime_) || !c.out || !c.data || !c.args)                      \
            return false;                                                                          \
        IGI::AI::native(c.out, c.data, c.args);                                                    \
        return true;                                                                               \
    }
bool AiHelpers::removeAlarmActions(const AiScriptContext &c) const {
    if (!requireLiveGameThread(runtime_) || !c.out || !c.data || !c.args)
        return false;
    IGI::AI::REMOVE_ALARM_ACTIONS(c.out, c.data, c.args);
    return true;
}
AI_HELPER(setViewLength, SET_VIEW_LENGTH)
AI_HELPER(setAlarmViewLength, SET_ALARM_VIEW_LENGTH)
AI_HELPER(setViewAlpha, SET_VIEW_ALPHA)
AI_HELPER(setViewGamma, SET_VIEW_GAMMA)
AI_HELPER(setSecondaryViewLength, SET_SECONDARY_VIEW_LENGTH)
AI_HELPER(setSecondaryAlarmViewLength, SET_SECONDARY_ALARM_VIEW_LENGTH)
AI_HELPER(setSecondaryViewAlpha, SET_SECONDARY_VIEW_ALPHA)
AI_HELPER(setSecondaryViewGamma, SET_SECONDARY_VIEW_GAMMA)
AI_HELPER(setEventPriority, SET_EVENT_PRIORITY)
AI_HELPER(setInvulnerability, SET_INVULNERABILITY)
AI_HELPER(setInstantDeath, SET_INSTANT_DEATH)
AI_HELPER(setDeathAnimation, SET_DEATH_ANIMATION)
AI_HELPER(setAlarmTriggerId, SET_ALARM_TRIGGER_ID)
AI_HELPER(setAlarmControlId, SET_ALARM_CONTROL_ID)
AI_HELPER(setAlarmAccess, SET_ALARM_ACCESS)
AI_HELPER(setGunnerId, SET_GUNNER_ID)
AI_HELPER(setAnimationInterval, SET_ANIMATION_INTERVAL)
AI_HELPER(currentEventType, GET_CURRENT_EVENT_TYPE)
AI_HELPER(randomValue, GET_RANDOM_VALUE)
#undef AI_HELPER
} // namespace igi
