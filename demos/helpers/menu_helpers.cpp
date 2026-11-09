#include "menu_helpers.hpp"

#include "logging/logging.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/igi_hashes/hashes.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/mvp_natives.hpp"
#include "natives/native_caller.hpp"
#include "ui/menu_lifecycle.hpp"
#include "ui/menu_resource.hpp"
#include "ui/menu_script.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
// Virtual-key codes are only used by the Windows-only free camera.
constexpr int VK_LEFT = 0x25;
constexpr int VK_UP = 0x26;
constexpr int VK_RIGHT = 0x27;
constexpr int VK_DOWN = 0x28;
constexpr int VK_SPACE = 0x20;
constexpr int VK_CONTROL = 0x11;
constexpr int VK_SHIFT = 0x10;
#endif

namespace igi {
static bool executeMenuCommandFor(IGI::Task *manager, std::string_view commandScript);
IGI::Human *selectedNpc(Runtime &runtime);
IGI::Car *selectedCar(Runtime &runtime);
IGI::Heli *selectedHeli(Runtime &runtime);
static bool requireLiveGameThread(Runtime &runtime);
static bool refreshLiveMenu(Runtime &runtime, std::string_view effect);
static bool executeMenuCommand(std::string_view commandScript);
static bool isNpcTask(IGI::Task *task);
static IGI::HumanAI *npcAiTask(IGI::Human *npc);
static bool isCarTask(IGI::Task *task);
static bool isHeliTask(IGI::Task *task);

namespace {
constexpr int kModPrecipitationTaskId = 4095;
constexpr std::size_t kMaxLiveTargets = kMaxMenuListChoices;

int g_selectedNpcTaskId = -1;
int g_selectedCarTaskId = -1;
int g_selectedHeliTaskId = -1;
std::atomic<bool> g_thirdPersonCrosshairEnabled{false};

// Retail Vec3d is three doubles (IGI-RE types.h). The bindings keep the type
// opaque, so helpers pass positions through this explicitly sized mirror.
struct RetailVec3d {
    double x;
    double y;
    double z;
};
static_assert(sizeof(RetailVec3d) == 24);

// Retail Matrix is float[9] (IGI-RE types.h); the bindings keep it opaque, so
// helpers pass a mirror with the recovered layout.
struct RetailMatrix {
    float m[9];
};
static_assert(sizeof(RetailMatrix) == 36);

// Largest node id the retail graph accepts (Graph_AddNode rejects id >= 1000).
constexpr int kMaxGraphNodes = 1000;

// Retail WeaponType layout (IGI-RE weapontypes.h): six 0x100 name/text blocks
// start at 0x10, so `category` is at 0x610 and `sightDisplay` at 0x614.
// Reading 0x61C instead lands inside the model-name string, which is why the
// third-person crosshair gate never matched.
struct WeaponTypeHudLayout {
    std::byte unused[0x614];
    int sightDisplay;
};

static_assert(offsetof(WeaponTypeHudLayout, sightDisplay) == 0x614);
// SIGHTDISPLAYTYPE_CROSSHAIR: the weapon wants the retail cross sprite.
constexpr int kSightDisplayCrosshair = 1;
constexpr int kSightDisplayNone = 0;

constexpr std::size_t kStatusScreenCrossSpriteOffset = 0x74;
// StatusScreen.scene at +0x20 (IGI-RE mission.h: Task then char scene[0x50]).
constexpr std::size_t kStatusScreenSceneOffset = 0x20;

struct RefreshOutcome {
    bool changed = false;
    bool preservedIsLive = true;
};

void clearLiveTargetSelections(const std::string &reason) {
    const bool hadSelection =
        g_selectedNpcTaskId >= 0 || g_selectedCarTaskId >= 0 || g_selectedHeliTaskId >= 0;
    g_selectedNpcTaskId = -1;
    g_selectedCarTaskId = -1;
    g_selectedHeliTaskId = -1;
    if (hadSelection)
        log(LogLevel::Info, "live target selections cleared: " + reason);
}

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
    // The request native only stores the id; HumanPlayer_SetWeaponAnim is what
    // plays the body + weapon animations (IGI-RE humanplayer.c: it switches on
    // anim, reads the weapon type's anim arrays and plays both slots).
    IGI::PLAYER::SET_WEAPON_ANIM(reinterpret_cast<IGI::HumanPlayer *>(player), animationId);
    log(LogLevel::Info, "weapon animation played via IGI::PLAYER::SET_WEAPON_ANIM id=" +
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

bool setThirdPersonCrosshairEnabled(Runtime &runtime, bool enabled) {
    if (!requireLiveGameThread(runtime))
        return false;
    g_thirdPersonCrosshairEnabled.store(enabled, std::memory_order_release);
    const char *status = enabled ? "IGI MOD: Third-person crosshair enabled"
                                 : "IGI MOD: Third-person crosshair disabled";
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status);
    log(LogLevel::Info,
        enabled ? "third-person crosshair enabled" : "third-person crosshair disabled");
    return true;
}

namespace {
// The crosshair runs every frame, so a problem must be reported once per cause
// instead of every frame or not at all.
void reportCrosshairState(std::string_view reason) {
    static std::string lastReason;
    if (lastReason == reason)
        return;
    lastReason = std::string(reason);
    if (reason.empty())
        return;
    log(LogLevel::Info, "third-person crosshair: " + std::string(reason));
}
} // namespace

namespace {
// IgiTaskLayout is defined further below; only its type field is needed here.
// Human.view (IGI-RE humanbasetypes.h).
constexpr std::size_t kHumanViewOffset = 0x4EC;
// HumanPlayerSM offsets (IGI-RE humanplayertypes.h): Human.sm at 0x2EC, and
// inside HumanPlayerSM: update 0x0, states 0x4, input 0x8, prev 0xC, state 0x10.
constexpr std::size_t kHumanSmOffset = 0x2EC;
// HumanPlayerSM (IGI-RE humanplayertypes.h): update 0x0, states 0x4, input 0x8,
// prev 0xC, state 0x10, run 0x14.
constexpr std::size_t kSmStatesOffset = 0x4;
constexpr std::size_t kSmInputOffset = 0x8;
constexpr std::size_t kSmStateOffset = 0x10;
constexpr std::size_t kSmRunOffset = 0x14;
// Player body states (IGI-RE humanplayer.c): BODY_LEAN 0x2C .. BODY_FLY 0x30.
constexpr int kBodyStateIdle = 0;
constexpr int kBodyStateFly = 0x30;
// Input bits (IGI-RE humanplayer.c common_input): the ten move controls start at
// INPUT_LEFT; HumanPlayer_BodyFlyRun reads 0x20 left, 0x40 right, 0x80 forward,
// 0x100 back, 0x7000 fast multiplier.
constexpr std::uint32_t kInLeft = 0x20;
constexpr std::uint32_t kInRight = 0x40;
constexpr std::uint32_t kInForward = 0x80;
constexpr std::uint32_t kInBack = 0x100;
constexpr std::uint32_t kInRun = 0x2000;

bool isHumanPlayerTask(IGI::Human *human) {
    if (!human)
        return false;
    const auto type = *reinterpret_cast<const std::uint16_t *>(
        reinterpret_cast<const std::uint8_t *>(human) + 0x1C);
    return type == IGI::PLAYER::GET_TYPE() || IGI::TASK::IS_A(type, IGI::PLAYER::GET_TYPE()) != 0;
}

std::uint32_t *smInput(IGI::Human *human) {
    if (!isHumanPlayerTask(human))
        return nullptr;
    return reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(human) +
                                             kHumanSmOffset + kSmInputOffset);
}

int *smState(IGI::Human *human) {
    if (!isHumanPlayerTask(human))
        return nullptr;
    return reinterpret_cast<int *>(reinterpret_cast<std::uint8_t *>(human) + kHumanSmOffset +
                                   kSmStateOffset);
}

// Retail sm_set (IGI-RE humanplayer.c): writing the state alone does nothing -
// the run pointer must switch to the new state's handler and the enter function
// runs immediately (BODY_FLY enter plays the fly animation and leaves ground).
void smGo(IGI::Human *human, int state) {
    auto *base = reinterpret_cast<std::uint8_t *>(human) + kHumanSmOffset;
    auto **states = *reinterpret_cast<void(***)>(base + kSmStatesOffset);
    if (!states || !states[state])
        return;
    *reinterpret_cast<int *>(base + kSmStateOffset) = state;
    *reinterpret_cast<void (**)(void)>(base + kSmRunOffset) =
        reinterpret_cast<void (*)(void)>(states[state]);
    using StateFn = void (*)(void *, void *);
    reinterpret_cast<StateFn>(states[state])(base, human);
}
} // namespace

// Retail fly mode: the player body state machine has a real BODY_FLY state
// (IGI-RE humanplayer.c, BODY_LEAN 0x2C .. BODY_FLY 0x30). Its run handler moves
// the body from the view matrix and the merged input bits, so the game's own key
// bindings (WASD by default) drive it - no camera math of our own to get wrong.
bool freecamEnabled() {
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    const auto *state = smState(player);
    return state && *state == kBodyStateFly;
}

bool setFreecamEnabled(Runtime &runtime, bool enabled) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    auto *state = smState(player);
    auto *input = smInput(player);
    if (!state || !input) {
        log(LogLevel::Warning, "fly mode rejected: the player state machine is unavailable");
        return false;
    }
    if (*state == kBodyStateFly) {
        if (enabled) {
            log(LogLevel::Info, "fly mode already active");
            return true;
        }
        // Retail exit: BODY_FLY run clears velocity and sm_set(0). Input unlocks so
        // the normal body controls resume.
        (void)unlockHumanInput();
        smGo(player, kBodyStateIdle);
        log(LogLevel::Info, "fly mode disabled: the player resumes normal body control");
        return true;
    }
    if (!enabled) {
        log(LogLevel::Info, "fly mode already disabled");
        return true;
    }
    // IMPORTANT: input stays unlocked. BODY_FLY movement comes from the same
    // input channels the normal controls use, and HumanPlayer_InputHandler
    // ignores keys while input is locked - locking made WASD dead.
    (void)unlockHumanInput();
    smGo(player, kBodyStateFly);
    log(LogLevel::Info, "fly mode enabled: the player body flies; use the game's own movement "
                        "keys (WASD by default), Shift for 4x speed, and the menu control to "
                        "disable");
    return true;
}

bool updateFreecam(Runtime &runtime) {
    // The retail BODY_FLY run handler moves the player every frame on its own; no
    // per-frame work is needed here.
    (void)runtime;
    return true;
}

int parseTargetId(std::string_view value) {
    constexpr std::string_view kMarker = " [#";
    const auto marker = value.rfind(kMarker);
    if (marker == std::string_view::npos || !value.ends_with(']'))
        return -1;
    const auto digits =
        value.substr(marker + kMarker.size(), value.size() - marker - kMarker.size() - 1);
    int id = -1;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    return parsed.ec == std::errc{} ? id : -1;
}

bool teleportPlayerToTarget(Runtime &runtime, int targetTaskId) {
    if (!requireLiveGameThread(runtime) || targetTaskId < 0)
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    // The live task is re-validated at call time, like every other target action.
    auto *target = IGI::TASK::FIND_BY_ID(targetTaskId);
    if (!target || target == reinterpret_cast<IGI::Task *>(player)) {
        log(LogLevel::Warning, "teleport rejected: target is no longer live on this level");
        return false;
    }
    IGI::HumanCameraInfo info{};
    IGI::HUMAN::GET_CAMERA_INFO(player, &info);
    double position[3] = {info.position[0], info.position[1], info.position[2]};
    // DynCubeObj objects keep their origin at +0x20 (IGI-RE task_pos()).
    std::memcpy(position, reinterpret_cast<const std::uint8_t *>(target) + 0x20, sizeof(position));
    // Place the player two metres behind the target so they do not clip into it.
    position[1] -= 200.0;
    IGI::HUMAN::SET_POSITION(player, position);
    (void)EntityHelpers(runtime).snapHumanToGround(player);
    log(LogLevel::Info, "player teleported to target #" + std::to_string(targetTaskId));
    return true;
}

bool teleportPlayerToPosition(Runtime &runtime, const std::array<double, 3> &position) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    auto destination = position;
    IGI::HUMAN::SET_POSITION(player, destination.data());
    (void)EntityHelpers(runtime).snapHumanToGround(player);
    log(LogLevel::Info, "player moved to " + std::to_string(position[0]) + ", " +
                            std::to_string(position[1]) + ", " + std::to_string(position[2]));
    return true;
}

bool thirdPersonCrosshairActive(Runtime &runtime) {
    if (!g_thirdPersonCrosshairEnabled.load(std::memory_order_acquire) ||
        !requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    IGI::HumanCameraInfo camera{};
    IGI::HUMAN::GET_CAMERA_INFO(player, &camera);
    if (camera.mode != 2)
        return false;
    auto *weapon = IGI::HUMAN::GET_WEAPON(player);
    if (!weapon)
        return false;
    auto *weaponType = IGI::WEAPON::WEAPON_GET_TYPE(weapon);
    if (!weaponType)
        return false;
    return reinterpret_cast<const WeaponTypeHudLayout *>(weaponType)->sightDisplay ==
           kSightDisplayCrosshair;
}

bool renderThirdPersonCrosshair(Runtime &runtime) {
    if (!g_thirdPersonCrosshairEnabled.load(std::memory_order_acquire) ||
        !requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player) {
        reportCrosshairState("no live player");
        return false;
    }
    IGI::HumanCameraInfo camera{};
    IGI::HUMAN::GET_CAMERA_INFO(player, &camera);
    if (camera.mode != 2) {
        reportCrosshairState("waiting for third-person camera");
        return false;
    }
    auto *weapon = IGI::HUMAN::GET_WEAPON(player);
    if (!weapon) {
        reportCrosshairState("player has no weapon task");
        return false;
    }
    auto *weaponType = IGI::WEAPON::WEAPON_GET_TYPE(weapon);
    if (!weaponType) {
        reportCrosshairState("weapon type unavailable");
        return false;
    }
    const int sightDisplay =
        reinterpret_cast<const WeaponTypeHudLayout *>(weaponType)->sightDisplay;
    if (sightDisplay == kSightDisplayNone)
        return false;
    if (sightDisplay != kSightDisplayCrosshair) {
        // Scoped weapons draw their own sight; report it once instead of silently
        // rendering nothing frame after frame.
        static int reportedSight = -1;
        if (reportedSight != sightDisplay) {
            reportedSight = sightDisplay;
            log(LogLevel::Info, "third-person crosshair skipped: weapon sight display type " +
                                    std::to_string(sightDisplay));
        }
        return false;
    }
    auto *statusScreen = IGI::OTHER::STATUS_SCREEN_GET();
    auto *displayMode = IGI::OTHER::DISPLAY_GET_MODE();
    if (!statusScreen || !displayMode || displayMode->width <= 0 || displayMode->height <= 0) {
        reportCrosshairState("status screen or display mode unavailable");
        return false;
    }
    auto *cross = reinterpret_cast<IGI::Sprite *>(reinterpret_cast<std::byte *>(statusScreen) +
                                                  kStatusScreenCrossSpriteOffset);
    const int width = IGI::OTHER::SPRITE_WIDTH(cross);
    const int height = IGI::OTHER::SPRITE_HEIGHT(cross);
    if (width <= 0 || height <= 0) {
        reportCrosshairState("cross sprite is not loaded yet");
        return false;
    }
    const float centerX = static_cast<float>(displayMode->width) * 0.5F;
    const float centerY = static_cast<float>(displayMode->height) * 0.5F;
    const float halfWidth = static_cast<float>(width) * 0.5F;
    const float halfHeight = static_cast<float>(height) * 0.5F;
    const float x = centerX - std::nearbyint(halfWidth);
    const float y = centerY - std::nearbyint(halfHeight);
    // Retail draws the HUD inside StatusScreen_Draw, which selects the status
    // scene first (Scene_Select +1) and relies on Screen_Draw's later
    // Scene_DrawAll flush. The mod's end-frame callback runs after that flush,
    // so drawing without a live scene queues into nothing: invisible and unsafe.
    // Selecting the status scene here adds it to the next flush, which is the
    // same mechanism the retail status screen uses.
    IGI::OTHER::SCENE_SELECT(reinterpret_cast<std::byte *>(statusScreen) + kStatusScreenSceneOffset,
                             1);
    // Same draw the retail HUD handler uses (IGI-RE gun.c Gun_HudHandler):
    // centre of the display, mid-grey, z 1.0.
    IGI::OTHER::DRAW_SPRITE_Z(cross, x, y, 128.0F, 128.0F, 128.0F, 0, 0, 0, 1.0F);
    reportCrosshairState("drawing in the status scene at the display centre");
    return true;
}

bool refreshLiveTargetOptions(MenuConfig &config, Runtime &runtime) {
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
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
    const auto heli = replaceChoices("heli_target", entities.heliTargets(), "No live helicopters",
                                     g_selectedHeliTaskId);
    if (!npc.preservedIsLive)
        g_selectedNpcTaskId = -1;
    if (!car.preservedIsLive)
        g_selectedCarTaskId = -1;
    if (!heli.preservedIsLive)
        g_selectedHeliTaskId = -1;
    if (npc.changed || car.changed || heli.changed) {
        log(LogLevel::Info, "live targets refreshed from the active game task list");
    }
    return npc.changed || car.changed || heli.changed;
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
    } else if (id == "player_ground_state") {
        auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
        if (!player) {
            log(LogLevel::Warning, "player ground state rejected: no live player was found");
        } else if (value == "Ground") {
            if (!PlayerHelpers(runtime).snapToGround())
                log(LogLevel::Warning,
                    "player ground state rejected: Human_SnapToGround was unavailable");
        } else if (!EntityHelpers(runtime).leaveHumanGround(player)) {
            log(LogLevel::Warning,
                "player ground state rejected: Human_LeaveGround was unavailable");
        }
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
    } else if (id == "debug_keys") {
        // The retail debug keys are LCtrl + LShift + KEY. Fly mode (F) is the same
        // free camera, so this control enables it and reports the retail chart.
        const bool enabled = value == "Enabled";
        if (setFreecamEnabled(runtime, enabled)) {
            log(LogLevel::Info, std::string("debug keys ") + (enabled ? "enabled" : "disabled") +
                                    "; retail chart: LCtrl+LShift+ F fly, Q/E cycle camera, "
                                    "A default camera, F9 unlock missions, F11 position, "
                                    "F12 next mission, P all weapons, R restart, I invincible, "
                                    "Numpad +/- zoom, Numpad Enter zoom reset, Numpad * third "
                                    "person, Numpad 8/2 pitch, 4/6 yaw, 9/3 distance");
        } else {
            log(LogLevel::Warning, "debug keys request rejected: the game camera is unavailable");
        }
    } else if (id == "freecam") {
        if (setFreecamEnabled(runtime, value == "Enabled"))
            log(LogLevel::Info, "free camera state applied");
        else
            log(LogLevel::Warning, "free camera request rejected");
    } else if (id == "third_person_crosshair") {
        setThirdPersonCrosshairEnabled(runtime, value == "Enabled");
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
        // Retail speeds: the car AI cruises at 5 and up. Walk is a crawl, Run is a
        // normal road speed and Full is flat out; Reverse is a negative speed.
        float targetSpeed = 0.0F;
        bool brake = false;
        if (value == "Reverse")
            targetSpeed = -6.0F;
        else if (value == "Walk")
            targetSpeed = 5.0F;
        else if (value == "Run")
            targetSpeed = 15.0F;
        else if (value == "Full")
            targetSpeed = 30.0F;
        else if (value == "Brake")
            brake = true;
        auto *car =
            reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
        if (!car) {
            reportMissingCarTarget("vehicle drive");
        } else if (VehicleHelpers(runtime).driveCar(car, targetSpeed, brake)) {
            log(LogLevel::Info, "vehicle drive: selected car set to " + std::string(value));
        } else {
            log(LogLevel::Warning, "vehicle drive rejected: Car_Control native was unavailable");
        }
    } else if (id == "npc_ensure_weapon") {
        // The demo adapter owns the retail weapon table, so it grants the weapon
        // itself; reaching here means the menu ran without a weapon selection.
        log(LogLevel::Warning,
            "NPC equip rejected: the demo adapter must supply the retail weapon");
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
        } else if (IGI::HUMAN::IS_DEAD(npc)) {
            log(LogLevel::Info, "soldier execution skipped: the selected soldier is already dead");
        } else if (NpcHelpers(runtime).executeSoldier(npc)) {
            log(LogLevel::Info, "selected soldier executed through the retail soldier flow");
        } else {
            log(LogLevel::Warning,
                "soldier execution rejected: selected target is not a standard Soldier");
        }
    } else if (id == "npc_movement") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("NPC movement");
        } else if (value == "Stop") {
            if (NpcHelpers(runtime).clearMovement(npc))
                log(LogLevel::Info, "NPC movement stopped through the retail AI clear-move call");
            else
                log(LogLevel::Warning, "NPC stop rejected: selected NPC has no live HumanAI task");
        } else if (NpcHelpers(runtime).moveTowardsPlayer(npc, value == "Run")) {
            log(LogLevel::Info, "NPC movement applied through the retail AI move call");
        } else {
            log(LogLevel::Warning, "NPC movement rejected: the retail AI move call failed");
        }
    } else if (id == "npc_ground_state") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("NPC ground state");
        } else if (value == "Ground") {
            if (NpcHelpers(runtime).snapToGround(npc))
                log(LogLevel::Info, "NPC ground state set to Ground through the human native");
            else
                log(LogLevel::Warning,
                    "NPC ground state rejected: Human_SnapToGround was unavailable");
        } else if (EntityHelpers(runtime).leaveHumanGround(npc)) {
            log(LogLevel::Info, "NPC ground state set to Air through the human native");
        } else {
            log(LogLevel::Warning, "NPC ground state rejected: Human_LeaveGround was unavailable");
        }
    } else if (id == "heli_target") {
        if (const auto selected = targetId(value)) {
            auto *task = findLiveTarget(runtime, *selected, isHeliTask);
            if (task) {
                g_selectedHeliTaskId = *selected;
                log(LogLevel::Info, "heli target selected: " + std::string(value));
            } else {
                g_selectedHeliTaskId = -1;
                log(LogLevel::Warning,
                    "heli target selection rejected: target is no longer live on this level");
            }
        } else {
            g_selectedHeliTaskId = -1;
            log(LogLevel::Info, "heli target list empty: no live helicopters on this level");
        }
    } else if (id == "heli_info") {
        auto *task = reinterpret_cast<IGI::Task *>(
            findLiveTarget(runtime, g_selectedHeliTaskId, isHeliTask));
        if (!task) {
            if (g_selectedHeliTaskId < 0)
                log(LogLevel::Info, "heli info: no helicopter selected; pick a live heli first");
            else {
                g_selectedHeliTaskId = -1;
                log(LogLevel::Warning, "heli info rejected: selected helicopter is no longer live");
            }
        } else if (VehicleHelpers(runtime).reportHeliInfo(reinterpret_cast<IGI::Heli *>(task))) {
            log(LogLevel::Info, "heli info reported from the retail vehicle natives");
        } else {
            log(LogLevel::Warning, "heli info rejected: vehicle native was unavailable");
        }
    } else if (id == "heli_reset") {
        auto *heli = selectedHeli(runtime);
        if (!heli && g_selectedHeliTaskId < 0) {
            // Reset is the one heli action that needs no further choice, so adopt
            // the only live helicopter instead of refusing the click.
            EntityHelpers entities(runtime);
            const auto helis = entities.heliTargets();
            if (helis.size() == 1) {
                g_selectedHeliTaskId = helis.front().taskId;
                heli = selectedHeli(runtime);
                log(LogLevel::Info, "heli reset: using the only live helicopter [" +
                                        std::to_string(g_selectedHeliTaskId) + "]");
            } else if (helis.empty()) {
                log(LogLevel::Info, "heli reset: no live helicopter on this level");
            } else {
                log(LogLevel::Info,
                    "heli reset: " + std::to_string(helis.size()) +
                        " helicopters are live; select one in Live Heli Target first");
            }
        }
        if (!heli) {
            if (g_selectedHeliTaskId >= 0) {
                g_selectedHeliTaskId = -1;
                log(LogLevel::Warning,
                    "heli reset rejected: the selected helicopter is no longer live");
            }
        } else if (VehicleHelpers(runtime).resetHeli(heli)) {
            log(LogLevel::Info, "heli reset: helicopter restored to its start position");
        } else {
            log(LogLevel::Warning, "heli reset rejected: Heli_Control was unavailable");
        }
    } else if (id == "vehicle_info") {
        auto *car =
            reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
        if (!car) {
            reportMissingCarTarget("vehicle info");
        } else if (VehicleHelpers(runtime).reportCarInfo(car)) {
            log(LogLevel::Info, "vehicle info reported from the retail control-info native");
        } else {
            log(LogLevel::Warning, "vehicle info rejected: control-info native was unavailable");
        }
    } else if (id == "vehicle_controls_reset") {
        auto *car =
            reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
        if (!car) {
            reportMissingCarTarget("vehicle controls reset");
        } else if (VehicleHelpers(runtime).resetVehicleControls(
                       reinterpret_cast<IGI::Task *>(car))) {
            log(LogLevel::Info, "vehicle controls reset through the retail vehicle native");
        } else {
            log(LogLevel::Warning, "vehicle controls reset rejected: native was unavailable");
        }
    } else if (id == "npc_status") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("NPC status");
        } else if (NpcHelpers(runtime).reportStatus(npc)) {
            log(LogLevel::Info, "NPC status reported from the retail human getters");
        } else {
            log(LogLevel::Warning, "NPC status rejected: selected NPC was unavailable");
        }
    } else if (id == "npc_stand_down") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("soldier stand down");
        } else if (IGI::HUMAN::IS_DEAD(npc)) {
            log(LogLevel::Info, "soldier stand down skipped: the selected soldier is dead");
        } else if (NpcHelpers(runtime).standDownSoldier(npc)) {
            log(LogLevel::Info, "selected soldier returned to its retail stand state");
        } else {
            log(LogLevel::Warning,
                "soldier stand down rejected: selected target is not a standard Soldier");
        }
    } else if (id == "npc_params") {
        auto *npc =
            reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
        if (!npc) {
            reportMissingNpcTarget("soldier params");
        } else if (NpcHelpers(runtime).reportSoldierParams(npc)) {
            log(LogLevel::Info, "soldier params reported from the retail soldier native");
        } else {
            log(LogLevel::Warning, "soldier params rejected: selected NPC was unavailable");
        }
    } else if (id == "player_status") {
        if (PlayerHelpers(runtime).reportStatus())
            log(LogLevel::Info, "player status reported from the retail human getters");
        else
            log(LogLevel::Warning, "player status rejected: player unavailable");
    } else if (id == "camera_info") {
        if (CameraHelpers(runtime).reportInfo())
            log(LogLevel::Info, "camera info reported from the retail camera-info native");
        else
            log(LogLevel::Warning, "camera info rejected: player unavailable");
    } else if (id == "world_level_tick") {
        if (WorldHelpers(runtime).reportLevelTick())
            log(LogLevel::Info, "level tick reported");
        else
            log(LogLevel::Warning, "level tick readout rejected");
    } else if (id == "world_flow") {
        if (WorldHelpers(runtime).reportFlow())
            log(LogLevel::Info, "flow counters reported");
        else
            log(LogLevel::Warning, "flow readout rejected");
    } else if (id == "world_events") {
        if (WorldHelpers(runtime).reportEvents())
            log(LogLevel::Info, "game events reported");
        else
            log(LogLevel::Warning, "event readout rejected");
    } else if (id == "audio_rate") {
        int rate = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), rate);
        if (parsed.ec != std::errc{} || rate <= 0) {
            log(LogLevel::Warning, "audio rate rejected: value is not a valid sample rate");
        } else if (AudioHelpers(runtime).setRate(rate)) {
            log(LogLevel::Info, "audio rate applied through the retail audio native");
        } else {
            log(LogLevel::Warning, "audio rate rejected: audio native was unavailable");
        }
    } else if (id == "audio_info") {
        if (AudioHelpers(runtime).reportInfo())
            log(LogLevel::Info, "audio info reported from the retail audio natives");
        else
            log(LogLevel::Warning, "audio info rejected");
    } else if (id == "weapon_draw_anim") {
        if (WeaponHelpers(runtime).requestAnimation(1))
            log(LogLevel::Info, "weapon draw animation requested");
        else
            log(LogLevel::Warning, "weapon animation rejected: player unavailable");
    } else if (id == "weapon_category") {
        if (WeaponHelpers(runtime).selectCategory())
            log(LogLevel::Info, "weapon category selection requested");
        else
            log(LogLevel::Warning, "weapon category selection rejected: player unavailable");
    } else if (id == "weapon_ammo") {
        int ammo = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), ammo);
        if (parsed.ec != std::errc{} || ammo <= 0) {
            log(LogLevel::Warning,
                "weapon ammo rejected: '" + std::string(value) + "' is not a positive round count");
        } else if (WeaponHelpers(runtime).refillCurrentWeapon(ammo)) {
            log(LogLevel::Info, "weapon ammo applied to the held weapon");
        } else {
            log(LogLevel::Warning, "weapon ammo rejected: the player is not holding a weapon");
        }
    } else if (id == "weapon_drop") {
        if (WeaponHelpers(runtime).dropCurrentWeapon())
            log(LogLevel::Info, "selected weapon dropped through the retail weapon lifecycle");
        else
            log(LogLevel::Warning, "drop weapon rejected: no droppable weapon task was found");
    } else if (id == "weapon_info") {
        if (WeaponHelpers(runtime).reportWeaponInfo())
            log(LogLevel::Info, "weapon info reported from the retail weapon task");
        else
            log(LogLevel::Warning, "weapon info rejected: player unavailable");
    } else if (id == "weapon_stop_anim") {
        if (WeaponHelpers(runtime).stopWeaponAnimation())
            log(LogLevel::Info, "weapon animation stopped through the retail native");
        else
            log(LogLevel::Warning, "weapon animation reset rejected: player unavailable");
    } else if (id == "weapon_events") {
        if (WeaponHelpers(runtime).reportEvents())
            log(LogLevel::Info, "weapon events reported from the retail weapon event getters");
        else
            log(LogLevel::Warning, "weapon events rejected: selected weapon path unavailable");
    } else if (id == "player_flag_d44" || id == "player_flag_d68") {
        const bool enabled = value == "Enabled";
        const bool applied = id == "player_flag_d44"
                                 ? (enabled ? PlayerHelpers(runtime).setFlagD44()
                                            : PlayerHelpers(runtime).clearFlagD44())
                                 : (enabled ? PlayerHelpers(runtime).setFlagD68()
                                            : PlayerHelpers(runtime).clearFlagD68());
        if (applied)
            log(LogLevel::Info, "player controller flag updated through the retail flag native");
        else
            log(LogLevel::Warning, "player controller flag rejected: player unavailable");
    } else if (id == "vehicle_explode_all") {
        if (VehicleHelpers(runtime).explodeAllCars())
            log(LogLevel::Info, "vehicle explode all: every live car was exploded");
        else
            log(LogLevel::Warning, "vehicle explode all rejected: no live car was available");
    } else if (id == "vehicle_reset_all") {
        if (VehicleHelpers(runtime).resetAllCars())
            log(LogLevel::Info, "vehicle reset all: every live car was restored");
        else
            log(LogLevel::Warning, "vehicle reset all rejected: no live car was available");
    } else if (id == "weapon_flash") {
        if (WeaponHelpers(runtime).reportFlashReset())
            log(LogLevel::Info, "player hit flash cleared");
        else
            log(LogLevel::Warning, "hit flash reset rejected: player unavailable");
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

IGI::Human *selectedNpc(Runtime &runtime) {
    return reinterpret_cast<IGI::Human *>(findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask));
}

IGI::Car *selectedCar(Runtime &runtime) {
    return reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
}

IGI::Heli *selectedHeli(Runtime &runtime) {
    return reinterpret_cast<IGI::Heli *>(findLiveTarget(runtime, g_selectedHeliTaskId, isHeliTask));
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
    // Fly mode writes body state through the player SM; the crosshair draws
    // through the player view. Both must be off before the Game task dies.
    (void)setFreecamEnabled(runtime, false);
    g_thirdPersonCrosshairEnabled.store(false, std::memory_order_release);
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

static IGI::HumanAI *npcAiTask(IGI::Human *npc) {
    if (!npc)
        return nullptr;
#if defined(_WIN32)
    const auto type = *reinterpret_cast<const std::uint16_t *>(igi::re::HASH::gHumanAIType);
    if (type == 0)
        return nullptr;
    return reinterpret_cast<IGI::HumanAI *>(
        IGI::TASK::FIND_CHILD(reinterpret_cast<IGI::Task *>(npc), type));
#else
    return nullptr;
#endif
}

static bool isHeliTaskType(std::uint16_t type) {
    const char *typeName = IGI::TASK::GET_NAME(type);
    return typeName && std::string_view(typeName).find("Heli") != std::string_view::npos;
}

static bool isCarTask(IGI::Task *task) {
    if (!task)
        return false;
    const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
    const auto carType = IGI::VEHICLE::GET_TYPE();
    return type == carType || IGI::TASK::IS_A(type, carType) != 0;
}

static bool isHeliTask(IGI::Task *task) {
    if (!task)
        return false;
    return isHeliTaskType(reinterpret_cast<const IgiTaskLayout *>(task)->type);
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
static std::string liveTaskName(IGI::Task *task) {
    if (!task)
        return {};
    char name[128]{};
    IGI::TASK::GET_TASK_NAME(name, task);
    if (name[0] != '\0')
        return name;
    const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
    if (const char *typeName = IGI::TASK::GET_NAME(type))
        return typeName;
    return {};
}

std::vector<LiveTarget> EntityHelpers::npcTargets() const {
    std::vector<LiveTarget> targets;
    if (!requireLiveGameThread(runtime_))
        return targets;
    for (int id = 0; id < 4096 && targets.size() < kMaxLiveTargets; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!isNpcTask(task))
            continue;
        targets.push_back({IGI::TASK::GET_ID(task), liveTaskName(task)});
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
        targets.push_back({IGI::TASK::GET_ID(task), liveTaskName(task)});
    }
    return targets;
}
IGI::Heli *EntityHelpers::firstHeli() const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (isHeliTaskType(type))
            return reinterpret_cast<IGI::Heli *>(task);
    }
    return nullptr;
}

std::vector<LiveTarget> EntityHelpers::heliTargets() const {
    std::vector<LiveTarget> targets;
    if (!requireLiveGameThread(runtime_))
        return targets;
    for (int id = 0; id < 4096 && targets.size() < kMaxLiveTargets; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (!isHeliTaskType(type))
            continue;
        targets.push_back({IGI::TASK::GET_ID(task), liveTaskName(task)});
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
bool PlayerHelpers::reportStatus() const {
    auto *p = localPlayer(runtime_);
    if (!p)
        return false;
    auto *human = reinterpret_cast<IGI::Human *>(p);
    std::ostringstream status;
    status << "Player: team " << IGI::HUMAN::GET_TEAM(human) << " state "
           << IGI::HUMAN::GET_STATE(human) << " index " << IGI::HUMAN::GET_INDEX(human) << ' '
           << (IGI::HUMAN::IS_DEAD(human) ? "dead" : "alive") << ' '
           << (IGI::HUMAN::IS_CROUCHING(human) ? "crouched" : "standing") << " move "
           << IGI::PLAYER::GET_MOVE_SCALE();
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool CameraHelpers::reportInfo() const {
    auto *player = localPlayer(runtime_);
    if (!player)
        return false;
    IGI::HumanCameraInfo info{};
    IGI::HUMAN::GET_CAMERA_INFO(reinterpret_cast<IGI::Human *>(player), &info);
    std::ostringstream status;
    status << "Camera: " << (info.mode == 2 ? "third person" : "first person") << " mode "
           << info.mode << " fov " << info.fov << " vfov " << info.verticalFov << " eye "
           << info.position[0] << ',' << info.position[1] << ',' << info.position[2];
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool WorldHelpers::reportLevelTick() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    if (!IGI::LEVEL::GET()) {
        log(LogLevel::Warning, "level readout rejected: no active level task");
        return false;
    }
    std::ostringstream status;
    status << "Level: tick " << IGI::LEVEL::GET_TICK() << " tasks " << IGI::TASK::GET_COUNT();
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool WorldHelpers::reportFlow() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    std::ostringstream status;
    status << "Flow: fps " << IGI::OTHER::FLOW_GET_FPS() << " frames "
           << IGI::OTHER::FLOW_GET_FRAMES() << " renders " << IGI::OTHER::FLOW_GET_RENDERS();
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool WorldHelpers::reportEvents() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    std::ostringstream status;
    status << "Events: " << static_cast<int>(IGI::GAME::GET_EVENT0()) << ','
           << static_cast<int>(IGI::GAME::GET_EVENT1()) << ','
           << static_cast<int>(IGI::GAME::GET_EVENT2()) << ','
           << static_cast<int>(IGI::GAME::GET_EVENT3()) << " world " << IGI::GAME::GET_WORLD();
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool VehicleHelpers::reportCarInfo(IGI::Car *car) const {
    if (!requireLiveGameThread(runtime_) || !car)
        return false;
    int control[8]{};
    IGI::VEHICLE::CAR_GET_CONTROL_INFO(car, control);
    auto *gun = IGI::VEHICLE::GET_MOUNTED_GUN(car);
    char gunName[128]{};
    if (gun)
        IGI::TASK::GET_TASK_NAME(gunName, gun);
    std::ostringstream status;
    status << targetLabel(liveTaskName(reinterpret_cast<IGI::Task *>(car)),
                          IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(car)))
           << ": control " << control[0] << ',' << control[1] << ',' << control[2] << ','
           << control[3] << " gun " << (gun ? gunName : "none");
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool VehicleHelpers::reportHeliInfo(IGI::Heli *heli) const {
    if (!requireLiveGameThread(runtime_) || !heli)
        return false;
    int control[8]{};
    int stateSize = 0;
    IGI::VEHICLE::HELI_GET_CONTROL_INFO(heli, control);
    IGI::VEHICLE::GET_STATE_SIZE(heli, &stateSize);
    auto *door = IGI::VEHICLE::FIND_DOOR(heli);
    std::ostringstream status;
    status << targetLabel(liveTaskName(reinterpret_cast<IGI::Task *>(heli)),
                          IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(heli)))
           << ": control " << control[0] << ',' << control[1] << " state " << stateSize << " door "
           << (door ? IGI::TASK::GET_ID(door) : -1);
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool VehicleHelpers::explodeAllCars() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    if (!isCallableNative(IGI::HASH::CAR_EXPLODE, "void Car_Explode(Car *c)"))
        return false;
    EntityHelpers entities(runtime_);
    std::vector<IGI::Car *> cars;
    for (auto *car = entities.firstCar(); car; car = entities.nextCar(car))
        cars.push_back(car);
    for (auto *car : cars)
        IGI::VEHICLE::CAR_EXPLODE(car);
    const std::string count = std::to_string(cars.size());
    log(LogLevel::Info, "vehicle explode all: triggered " + count +
                            (cars.size() == 1 ? " live car explosion" : " live car explosions"));
    IGI::MISC::STATUS_MESSAGE_SHOW(
        nullptr, ("IGI MOD: exploded " + count + (cars.size() == 1 ? " car" : " cars")).c_str());
    return !cars.empty();
}

bool VehicleHelpers::resetAllCars() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    if (!isCallableNative(IGI::HASH::CAR_CONTROL, "void Car_Control(Car *o, CarControlArg *arg)"))
        return false;
    EntityHelpers entities(runtime_);
    std::vector<IGI::Car *> cars;
    for (auto *car = entities.firstCar(); car; car = entities.nextCar(car))
        cars.push_back(car);
    float input[4] = {0.0F, -1.0F, -2.0F, -1.0F};
    IGI::CarControlArg reset{0, input};
    for (auto *car : cars)
        IGI::VEHICLE::CAR_CONTROL(car, &reset);
    log(LogLevel::Info, "vehicle reset all: restored " + std::to_string(cars.size()) +
                            " live cars through retail controls");
    return !cars.empty();
}

bool VehicleHelpers::resetVehicleControls(IGI::Task *vehicle) const {
    if (!requireLiveGameThread(runtime_) || !vehicle)
        return false;
    if (!isCallableNative(IGI::HASH::VEHICLE_RESET_CONTROLS,
                          "void Vehicle_ResetControls(Vehicle *v)"))
        return false;
    log(LogLevel::Info, "vehicle controls: invoking Vehicle_ResetControls for task #" +
                            std::to_string(IGI::TASK::GET_ID(vehicle)));
    IGI::VEHICLE::RESET_CONTROLS(vehicle);
    return true;
}

bool NpcHelpers::reportStatus(IGI::Human *npc) const {
    if (!requireLiveGameThread(runtime_) || !npc)
        return false;
    char name[128]{};
    IGI::TASK::GET_TASK_NAME(name, reinterpret_cast<IGI::Task *>(npc));
    std::ostringstream status;
    status << targetLabel(name, IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(npc))) << ": team "
           << IGI::HUMAN::GET_TEAM(npc) << " state " << IGI::HUMAN::GET_STATE(npc) << ' '
           << (IGI::HUMAN::IS_DEAD(npc) ? "dead" : "alive") << ' '
           << (IGI::HUMAN::IS_CROUCHING(npc) ? "crouched" : "standing") << ' '
           << (IGI::HUMAN::IS_ACTIVE(npc) ? "active" : "idle");
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

bool NpcHelpers::standDownSoldier(IGI::Human *npc) const {
    if (!requireLiveGameThread(runtime_) || !npc)
        return false;
    const auto type = reinterpret_cast<const IgiTaskLayout *>(npc)->type;
    if (type != IGI::SOLDIER::GET_TYPE())
        return false;
    if (!isCallableNative(IGI::HASH::SOLDIER_STAND_ENTER,
                          "void Soldier_StandEnter(SoldierSM *sm, Soldier *h)"))
        return false;
    auto *state = reinterpret_cast<void *>(reinterpret_cast<std::uint8_t *>(npc) + 0x2ec);
    log(LogLevel::Info, "soldier stand down: invoking Soldier_StandEnter for task #" +
                            std::to_string(IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(npc))));
    IGI::SOLDIER::STAND_ENTER(reinterpret_cast<IGI::SoldierSM *>(state),
                              reinterpret_cast<IGI::Soldier *>(npc));
    return true;
}

bool NpcHelpers::reportSoldierParams(IGI::Human *npc) const {
    if (!requireLiveGameThread(runtime_) || !npc)
        return false;
    void *params = nullptr;
    IGI::SOLDIER::GET_PARAMS(npc, &params);
    std::ostringstream status;
    status << targetLabel(liveTaskName(reinterpret_cast<IGI::Task *>(npc)),
                          IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(npc)))
           << " params " << params;
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

// The AI graph is the level's single graph task; moving needs a node id from it.
static IGI::Graph *findAiGraphTask() {
    const auto graphType = IGI::GRAPH::GET_TYPE();
    if (graphType == 0)
        return nullptr;
    for (int id = 0; id < 4096; ++id) {
        auto *task = IGI::TASK::FIND_BY_ID(id);
        if (!task)
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (type == graphType || IGI::TASK::IS_A(type, graphType) != 0)
            return reinterpret_cast<IGI::Graph *>(task);
    }
    return nullptr;
}

bool NpcHelpers::moveTowardsPlayer(IGI::Human *npc, bool run) const {
    if (!requireLiveGameThread(runtime_) || !npc)
        return false;
    auto *ai = npcAiTask(npc);
    if (!ai) {
        log(LogLevel::Warning, "NPC movement rejected: selected NPC has no live HumanAI task");
        return false;
    }
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    IGI::HumanCameraInfo camera{};
    IGI::HUMAN::GET_CAMERA_INFO(player, &camera);
    RetailVec3d target{camera.position[0], camera.position[1], camera.position[2]};

    auto *graph = findAiGraphTask();
    if (!graph) {
        log(LogLevel::Warning, "NPC movement rejected: no live AI graph task on this level");
        return false;
    }
    int nearest = -1;
    double nearestDistance = 0.0;
    for (int id = 0; id < kMaxGraphNodes; ++id) {
        auto *node = IGI::GRAPH::GET_NODE(graph, id);
        if (!node)
            continue;
        RetailVec3d nodePos{};
        IGI::GRAPH::GET_NODE_POS(reinterpret_cast<IGI::Vec3d *>(&nodePos), node);
        const double dx = nodePos.x - target.x;
        const double dy = nodePos.y - target.y;
        const double dz = nodePos.z - target.z;
        const double distance = dx * dx + dy * dy + dz * dz;
        if (nearest < 0 || distance < nearestDistance) {
            nearest = id;
            nearestDistance = distance;
        }
    }
    if (nearest < 0) {
        log(LogLevel::Warning, "NPC movement rejected: the AI graph has no usable node");
        return false;
    }
    auto *position = reinterpret_cast<const IGI::Vec3d *>(&target);
    const char finished = run ? IGI::AI::RUN_TO(ai, nearest, position, 1)
                              : IGI::AI::MOVE_TO(ai, nearest, position, 1);
    log(LogLevel::Info, std::string("NPC ") + (run ? "run" : "walk") + " requested towards node " +
                            std::to_string(nearest) + " (retail AI reported " +
                            (finished ? "arrived" : "moving") + ")");
    return true;
}

bool NpcHelpers::setMovementMode(IGI::Human *npc, bool run) const {
    if (!requireLiveGameThread(runtime_))
        return false;
    auto *ai = npcAiTask(npc);
    if (!ai)
        return false;
    if (run)
        IGI::AI::SET_RUN(ai);
    else
        IGI::AI::SET_WALK(ai);
    return true;
}

bool NpcHelpers::clearMovement(IGI::Human *npc) const {
    if (!requireLiveGameThread(runtime_))
        return false;
    auto *ai = npcAiTask(npc);
    if (!ai)
        return false;
    IGI::AI::CLEAR_MOVE(ai);
    return true;
}

bool AudioHelpers::setRate(int rate) const {
    if (!requireLiveGameThread(runtime_) || rate <= 0)
        return false;
    IGI::AUDIO::SET_RATE(rate);
    log(LogLevel::Info, "audio rate set to " + std::to_string(rate));
    return true;
}

bool AudioHelpers::reportInfo() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    std::ostringstream status;
    status << "Audio: ready " << static_cast<int>(IGI::AUDIO::IS_READY()) << " rate "
           << IGI::AUDIO::GET_RATE() << " voices " << IGI::AUDIO::GET_VOICE_COUNT();
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
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
bool WeaponHelpers::reportFlashReset() const {
    return restorePlayerVisualFeedback(runtime_);
}
// Retail Gun.id lives at +0x100 (IGI-RE guncasingtypes.h), which is what
// WeaponGunPickup matches against to add rounds to the carried weapon.
constexpr std::size_t kGunIdOffset = 0x100;
// Retail Gun.rounds at +0x144 (IGI-RE guncasingtypes.h): the loaded clip.
constexpr std::size_t kGunRoundsOffset = 0x144;
// Human.weapons[3] at +0x4C4 (IGI-RE human.c: WeaponSlots_Get((char *)h + 0x4C4, 0)).
constexpr std::size_t kHumanWeaponSlotsOffset = 0x4C4;
constexpr int kHumanWeaponSlots = 3;

// Adds rounds to the weapon currently in hand. The pickup native matches the
// weapon id against the carried tasks, so this refills the held gun rather than
// the menu's selection.
bool WeaponHelpers::refillCurrentWeapon(int ammo) const {
    if (!requireLiveGameThread(runtime_) || ammo <= 0)
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player) {
        log(LogLevel::Warning, "weapon ammo rejected: player is unavailable");
        return false;
    }
    auto *weapon = IGI::HUMAN::GET_WEAPON(player);
    if (!weapon) {
        log(LogLevel::Info, "weapon ammo: the player is not holding a weapon");
        return false;
    }
    const int gunId = *reinterpret_cast<const int *>(
        reinterpret_cast<const std::uint8_t *>(weapon) + kGunIdOffset);
    // Retail Gun.rounds at +0x144 (IGI-RE guncasingtypes.h): the loaded clip.
    int &rounds =
        *reinterpret_cast<int *>(reinterpret_cast<std::uint8_t *>(weapon) + kGunRoundsOffset);
    // WeaponGunPickup adds reserve rounds for the carried gun id (the same call
    // the player pickup uses), and the clip is then topped from the reserve the
    // way the retail reload does it.
    int message[] = {gunId, ammo};
    IGI::PLAYER::PICK_UP_WEAPON(player, message);
    const int before = rounds;
    rounds = ammo;
    log(LogLevel::Info, "weapon ammo: gun id " + std::to_string(gunId) + " clip " +
                            std::to_string(before) + " -> " + std::to_string(rounds) + " and " +
                            std::to_string(ammo) + " reserve rounds added");
    return true;
}

// Drops the player's current weapon completely:
//   1. Weapon_Drop runs the retail drop handler (spawns the pickup, marks remove)
//   2. every carried slot that referenced the task is cleared, so the weapon can
//      no longer be cycled back to
//   3. the weapon draw animation is stopped, so the hands return to idle
bool WeaponHelpers::dropCurrentWeapon() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player) {
        log(LogLevel::Warning, "drop weapon: player is unavailable");
        return false;
    }
    auto *weapon = IGI::HUMAN::GET_WEAPON(player);
    if (!weapon) {
        log(LogLevel::Info, "drop weapon: the player is not carrying a weapon task");
        return false;
    }
    const int weaponId = IGI::TASK::GET_ID(weapon);
    log(LogLevel::Info,
        "drop weapon: retail death-drop path for task #" + std::to_string(weaponId));
    // The retail drop sequence (IGI-RE soldier.c die_start):
    //   1. Weapon_Drop spawns the ground pickup
    //   2. the carried slot is cleared so the weapon leaves the cycle list
    //   3. Weapon_DeleteAll removes every leftover weapon task owned by the human
    // Doing the delete while holding the task pointer crashed the game: the
    // handler reparents tasks, so the pointer is re-read after each drop.
    (void)IGI::WEAPON::DROP(weapon);
    auto **slots = reinterpret_cast<IGI::Task **>(reinterpret_cast<std::uint8_t *>(player) +
                                                  kHumanWeaponSlotsOffset);
    for (int slot = 0; slot < kHumanWeaponSlots; ++slot) {
        if (IGI::WEAPON::SLOT_GET(slots, slot) == weapon)
            IGI::WEAPON::SLOT_SET(slots, slot, nullptr);
    }
    IGI::WEAPON::DELETE_ALL(player);
    IGI::HUMAN::STOP_WEAPON_ANIM(player, 0.0F);
    log(LogLevel::Info,
        "drop weapon: task #" + std::to_string(weaponId) +
            " dropped, carried slots cleared, weapon tasks deleted and the animation stopped");
    return true;
}

// Current weapon readout: the retail weapon task id and its remaining rounds.
bool WeaponHelpers::reportWeaponInfo() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    auto *weapon = IGI::HUMAN::GET_WEAPON(player);
    if (!weapon) {
        log(LogLevel::Info, "weapon info: no weapon task in hand");
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: no weapon in hand");
        return true;
    }
    std::ostringstream status;
    status << "Weapon #" << IGI::TASK::GET_ID(weapon) << ": rounds "
           << IGI::WEAPON::HAS_AMMO(weapon);
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
}

// Clears a stuck draw/reload animation on the player's current weapon.
bool WeaponHelpers::stopWeaponAnimation() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    IGI::HUMAN::STOP_WEAPON_ANIM(player, 0.0F);
    return true;
}

bool WeaponHelpers::reportEvents() const {
    if (!requireLiveGameThread(runtime_))
        return false;
    const auto fire = fireEvent();
    const auto type = typeEvent();
    const auto trigger = triggerEvent();
    const auto state = stateEvent();
    const auto move = moveEvent();
    if (!fire || !type || !trigger || !state || !move)
        return false;
    std::ostringstream status;
    status << "Weapon events: fire " << *fire << " type " << *type << " trigger "
           << (*trigger ? 1 : 0) << " state " << (*state ? 1 : 0) << " move " << (*move ? 1 : 0);
    log(LogLevel::Info, status.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
    return true;
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
    log(LogLevel::Info, "vehicle reset: invoking Car_Control reset for task #" +
                            std::to_string(IGI::TASK::GET_ID(reinterpret_cast<IGI::Task *>(car))));
    float input[4] = {0.0F, -1.0F, -2.0F, -1.0F};
    IGI::CarControlArg reset{0, input};
    IGI::VEHICLE::CAR_CONTROL(car, &reset);
    return true;
}
bool VehicleHelpers::resetHeli(IGI::Heli *heli) const {
    if (!requireLiveGameThread(runtime_) || !heli)
        return false;
    if (!isCallableNative(IGI::HASH::HELI_CONTROL, "void Heli_Control(Heli *h, ControlArg *arg)"))
        return false;
    float axes[5] = {0.0F, 0.0F, 0.0F, -1.0F, -2.0F};
    IGI::HeliControlArg reset{0, axes};
    IGI::VEHICLE::HELI_CONTROL(heli, &reset);
    return true;
}
// Retail Car_Control contract (IGI-RE rotor/car.c):
//   input[0] = steer, input[1] = target speed, input[3] = brake (!= -1 brakes),
//   and a non-zero arg->unk0 selects the drive path instead of the reset path.
// target speed is a speed, not a 0..1 throttle: the retail car AI cruises at 5
// and up, so small fractional values leave the car standing still. A negative
// target speed drives in reverse.
bool VehicleHelpers::driveCar(IGI::Car *car, float targetSpeed, bool brake) const {
    if (!requireLiveGameThread(runtime_) || !car)
        return false;
    if (!isCallableNative(IGI::HASH::CAR_CONTROL, "void Car_Control(Car *o, CarControlArg *arg)"))
        return false;
    float input[4] = {0.0F, targetSpeed, 0.0F, brake ? 1.0F : -1.0F};
    IGI::CarControlArg drive{1, input};
    log(LogLevel::Info, "vehicle drive: invoking Car_Control target speed " +
                            std::to_string(targetSpeed) + (brake ? " with brake" : ""));
    IGI::VEHICLE::CAR_CONTROL(car, &drive);
    return true;
}
bool VehicleHelpers::resetControls(void *vehicle) const {
    if (!requireLiveGameThread(runtime_) || !vehicle)
        return false;
    IGI::VEHICLE::RESET_CONTROLS(vehicle);
    return true;
}
bool NpcHelpers::ensureWeapon(IGI::Human *npc, std::string_view weaponId, int ammo) const {
    if (!requireLiveGameThread(runtime_) || !npc || weaponId.empty())
        return false;
    if (IGI::HUMAN::IS_PLAYER(npc)) {
        log(LogLevel::Warning, "NPC equip rejected: the player is not an NPC target");
        return false;
    }
    if (IGI::HUMAN::IS_DEAD(npc)) {
        log(LogLevel::Warning, "NPC equip rejected: the selected NPC is dead");
        return false;
    }
    const int weapon = IGI::SCRIPT::EVAL_CONST(std::string(weaponId).c_str());
    if (weapon == 0) {
        log(LogLevel::Warning, "NPC equip rejected: retail weapon symbol did not resolve");
        return false;
    }
    // WeaponGunPickup creates the weapon task on any human (Gun_New) or adds
    // rounds when it is already carried; Soldier_EnsureWeapon then owns slot 0.
    int args[] = {weapon, ammo};
    log(LogLevel::Info, "NPC equip: granting " + std::string(weaponId) + " with " +
                            std::to_string(ammo) + " rounds");
    IGI::PLAYER::PICK_UP_WEAPON(npc, args);
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
