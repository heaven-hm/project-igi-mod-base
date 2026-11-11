#include "menu_helpers.hpp"

#include "logging/logging.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/igi_hashes/hashes.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/mvp_natives.hpp"
#include "natives/native_caller.hpp"
#include "platform/windows_dll.hpp"
#include "ui/menu_lifecycle.hpp"
#include "ui/menu_resource.hpp"
#include "ui/menu_script.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iterator>
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
static std::string liveTaskName(IGI::Task *task);
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
// Synthetic menu key for the per-level spawn position. Live task keys are
// non-negative and address-derived, so this value cannot collide in practice.
constexpr int kLevelStartTargetId = 2147483600;
void *g_levelStartGame = nullptr;
std::array<double, 3> g_levelStartPosition{};
bool g_levelStartValid = false;

// Retail Vec3d is three doubles (IGI-RE types.h). The bindings keep the type
// opaque, so helpers pass positions through this explicitly sized mirror.
struct RetailVec3d {
    double x;
    double y;
    double z;
};
static_assert(sizeof(RetailVec3d) == 24);

// Retail Matrix is float[9] followed by the multiply-depth counter (IGI-RE
// types.h); the bindings keep it opaque, so helpers use this sized mirror.
struct RetailMatrix {
    float m[9];
    int count;
};
static_assert(sizeof(RetailMatrix) == 0x28);

// Camera (IGI-RE hazetypes.h): the rendered view for this frame. The axes
// already include the retail set_view (-90 X) rotation.
#pragma pack(push, 4)
struct RetailCameraView {
    RetailMatrix axes;
    double position[3];
    float fov;
    float aspect;
    float priority;
    int frame;
    int count;
    void *owner;
};
#pragma pack(pop)
static_assert(offsetof(RetailCameraView, position) == 0x28);
static_assert(offsetof(RetailCameraView, fov) == 0x40);
static_assert(offsetof(RetailCameraView, owner) == 0x54);

// Largest node id the retail graph accepts (Graph_AddNode rejects id >= 1000).
constexpr int kMaxGraphNodes = 1000;

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
    // The retail ListBox values must stay below the 40-character QVM limit,
    // including the " [#id]" suffix. Tasks without a script id use an address
    // key up to 10 digits, which previously pushed every refreshed label over
    // the limit and made the whole menu resource invalid.
    constexpr std::size_t kMaxLabelLength = 39;
    const std::string suffix = " [#" + std::to_string(id) + "]";
    const std::size_t maxName =
        suffix.size() < kMaxLabelLength ? kMaxLabelLength - suffix.size() : 0;
    if (name.empty())
        name = "Unnamed";
    if (name.size() > maxName)
        name.resize(maxName);
    return name + suffix;
}

// QTaskList (IGI-RE qasmtypes.h): count, capacity, then the Task* array.
struct QTaskListLayout {
    int count;
    int max;
    IGI::Task *items[1];
};

// Every live task registered with the game, deduplicated across its task lists.
// The old FIND_BY_ID(0..4095) scan missed objects whose ids are outside that
// range or are not registered in the id table, which left the teleport picker
// empty on most levels. The game's own task lists are authoritative.
std::vector<IGI::Task *> liveTasks(Runtime &runtime) {
    std::vector<IGI::Task *> tasks;
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return tasks;
    const auto collect = [&tasks](void *raw) {
        if (!raw)
            return;
        const auto *list = reinterpret_cast<const QTaskListLayout *>(raw);
        if (list->count <= 0 || list->max <= 0 || list->count > list->max)
            return;
        for (int index = 0; index < list->count && tasks.size() < 4096; ++index) {
            auto *task = list->items[index];
            if (task && std::find(tasks.begin(), tasks.end(), task) == tasks.end())
                tasks.push_back(task);
        }
    };
    collect(IGI::GAME::GET_TASK_LIST0());
    collect(IGI::GAME::GET_TASK_LIST1());
    collect(IGI::GAME::GET_TASK_LIST2());
    collect(IGI::GAME::GET_TASK_LIST3());
    collect(IGI::GAME::GET_TASK_LIST4());
    if (tasks.empty()) {
        // Last resort when the game lists are not available yet: keep the old
        // id scan so the menu still works during level startup.
        for (int id = 0; id < 4096; ++id) {
            if (auto *task = IGI::TASK::FIND_BY_ID(id))
                tasks.push_back(task);
        }
    }
    return tasks;
}

// Stable menu key for a task. Script ids are the normal case; objects without
// one get a positive key derived from their address so the dropdown can still
// represent them. The key is re-resolved against the live list on every action.
int liveTaskKey(IGI::Task *task) {
    if (!task)
        return -1;
    const int id = IGI::TASK::GET_ID(task);
    if (id >= 0)
        return id;
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(task) & 0x7FFFFFFF);
}

IGI::Task *findLiveTask(Runtime &runtime, int key) {
    if (key < 0 || !requireLiveGameThread(runtime))
        return nullptr;
    for (auto *task : liveTasks(runtime)) {
        if (liveTaskKey(task) == key)
            return task;
    }
    // Some tasks are reachable by id but not in a list the mod can walk.
    return IGI::TASK::FIND_BY_ID(key);
}

template <typename Predicate>
IGI::Task *findLiveTarget(Runtime &runtime, int id, Predicate &&predicate) {
    if (!requireLiveGameThread(runtime) || id < 0)
        return nullptr;
    auto *task = findLiveTask(runtime, id);
    if (!task)
        return nullptr;
    return predicate(task) ? task : nullptr;
}

// Retail LevelFlow layout (IGI-RE levelflowtypes.h). The world clock and the
// mission timer are independent fields of the level script task.
struct LevelFlowLayout {
    std::byte prefix[0x68];
    int nTick;
    float startHour;
    std::byte unk70[4];
    float timeLimit;
};
static_assert(offsetof(LevelFlowLayout, nTick) == 0x68);
static_assert(offsetof(LevelFlowLayout, startHour) == 0x6C);
static_assert(offsetof(LevelFlowLayout, timeLimit) == 0x74);

// Retail Mission layout (IGI-RE mission.c). The name is the display label.
struct MissionLayout {
    int id;
    char name[0x20];
};
static_assert(offsetof(MissionLayout, name) == 4);

// Minimal Task/Node layout for walking a level's authored object tree.
#pragma pack(push, 1)
struct TaskTreeLayout {
    TaskTreeLayout *next;
    TaskTreeLayout *prev;
    TaskTreeLayout *head;
    TaskTreeLayout *tail;
    TaskTreeLayout *tailpred;
    TaskTreeLayout *parent;
    int refs;
    std::uint16_t type;
    char pooled;
    char unk1F;
};
#pragma pack(pop)

std::array<double, 3> taskPosition(const IGI::Task *task) {
    std::array<double, 3> position{};
    if (!task)
        return position;
    std::memcpy(position.data(), reinterpret_cast<const std::uint8_t *>(task) + 0x20,
                sizeof(double) * position.size());
    return position;
}

void collectAuthoredTasks(const TaskTreeLayout *node, std::vector<IGI::Task *> &out, int depth) {
    if (!node || depth <= 0 || out.size() >= 4096)
        return;
    for (auto *child = node->head; child && child->next; child = child->next) {
        out.push_back(reinterpret_cast<IGI::Task *>(child));
        collectAuthoredTasks(child, out, depth - 1);
    }
}

// Every task below the active level's authored object roots. Unlike the game's
// event task lists this contains the buildings, doors, props and landmarks the
// level script created, which is what the object teleport picker needs.
std::vector<IGI::Task *> authoredLevelTasks(Runtime &runtime) {
    std::vector<IGI::Task *> tasks;
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return tasks;
    const auto collect = [&tasks](IGI::Task *root) {
        if (!root)
            return;
        const auto *layout = reinterpret_cast<const TaskTreeLayout *>(root);
        collectAuthoredTasks(layout, tasks, 32);
    };
    collect(reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET_DYNAMIC()));
    collect(reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET_STATIC()));
    return tasks;
}

IGI::Task *findLevelFlowTask(Runtime &runtime) {
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return nullptr;
    const auto levelFlowType = IGI::TASK::FIND("LevelFlow");
    if (levelFlowType == 0)
        return nullptr;
    if (auto *dynamic = IGI::LEVEL::GET_DYNAMIC()) {
        if (auto *found =
                IGI::TASK::FIND_CHILD(reinterpret_cast<IGI::Task *>(dynamic), levelFlowType))
            return found;
    }
    if (auto *level = reinterpret_cast<IGI::Task *>(IGI::LEVEL::GET())) {
        if (auto *found = IGI::TASK::FIND_CHILD(level, levelFlowType))
            return found;
    }
    for (auto *task : liveTasks(runtime)) {
        const auto type = reinterpret_cast<const TaskTreeLayout *>(task)->type;
        if (type == levelFlowType || IGI::TASK::IS_A(type, levelFlowType) != 0)
            return task;
    }
    return nullptr;
}

int currentFlowFps() {
    const int fps = IGI::OTHER::FLOW_GET_FPS();
    return fps > 0 ? fps : 30;
}

// Places a human on the ground below a requested point. Human_SnapToGround
// probes from the body position, so a typed Z below the map made the upward
// fallback find the terrain underside and left the body underground. Probing
// from above the requested point always finds the floor the player should stand
// on; a point already under a low ceiling falls back to an exact probe.
bool placeHumanOnGround(Runtime &runtime, IGI::Human *human,
                        const std::array<double, 3> &position) {
    if (!human)
        return false;
    if (!isCallableNative(IGI::HASH::HUMAN_SNAP_TO_GROUND, "void Human_SnapToGround(Human *h)")) {
        auto destination = position;
        IGI::HUMAN::SET_POSITION(human, destination.data());
        return true;
    }
    constexpr double kProbeLift = 1.5 * 4096.0;
    constexpr double kHighProbe = 40.0 * 4096.0;
    // A failed snap must not leave the body parked at the probe height. Keep the
    // original pose and report failure so the menu can refuse the teleport.
    const auto original = taskPosition(reinterpret_cast<const IGI::Task *>(human));
    auto probe = position;
    probe[2] += kProbeLift;
    IGI::HUMAN::SET_POSITION(human, probe.data());
    IGI::HUMAN::SNAP_TO_GROUND(human);
    auto grounded = taskPosition(reinterpret_cast<const IGI::Task *>(human));
    const auto unchanged = [](const std::array<double, 3> &a, const std::array<double, 3> &b) {
        return std::fabs(a[0] - b[0]) < 1.0 && std::fabs(a[1] - b[1]) < 1.0 &&
               std::fabs(a[2] - b[2]) < 1.0;
    };
    // No movement means the probe found no surface and left the body at the
    // probe height, which is the airborne teleport this guard exists to prevent.
    if (!(grounded[2] <= probe[2] + 4096.0) || unchanged(grounded, probe)) {
        // The downward probe found nothing and the upward fallback hit a
        // surface above the lift point: the requested point is under terrain or
        // a ceiling. Probe down from well above it so the player lands on the
        // top surface instead of under the map.
        auto high = position;
        high[2] += kHighProbe;
        IGI::HUMAN::SET_POSITION(human, high.data());
        IGI::HUMAN::SNAP_TO_GROUND(human);
        grounded = taskPosition(reinterpret_cast<const IGI::Task *>(human));
        if (!(grounded[2] <= high[2] + 4096.0) || unchanged(grounded, high)) {
            std::array<double, 3> restore = original;
            IGI::HUMAN::SET_POSITION(human, restore.data());
            log(LogLevel::Warning,
                "teleport ground probe: no floor below the requested point; player left at "
                "the original position");
            return false;
        }
    }
    const double finite = std::fabs(grounded[0]) + std::fabs(grounded[1]) + std::fabs(grounded[2]);
    if (!std::isfinite(finite)) {
        std::array<double, 3> restore = original;
        IGI::HUMAN::SET_POSITION(human, restore.data());
        log(LogLevel::Warning, "teleport ground probe: surface result was not finite; player left "
                               "at the original position");
        return false;
    }
    log(LogLevel::Info, "teleport ground probe: requested " + std::to_string(position[0]) + ", " +
                            std::to_string(position[1]) + ", " + std::to_string(position[2]) +
                            " -> grounded " + std::to_string(grounded[0]) + ", " +
                            std::to_string(grounded[1]) + ", " + std::to_string(grounded[2]));
    return true;
}

void broadcastPlayerPosition(IGI::Human *player) {
    if (!player)
        return;
    // The retail player update sends both broadcasts: list 1 (world systems)
    // and list 2 (the player state machine). Only sending the player broadcast
    // left world-side listeners on the old position, which is why Send Player
    // Position appeared to do nothing.
    IGI::HUMAN::SEND_POSITION(player);
    IGI::PLAYER::SEND_POSITION(reinterpret_cast<IGI::HumanPlayer *>(player));
}

std::atomic<int> g_pendingLevel{0};
std::atomic<bool> g_restartRequested{false};

void appendTargetsTo(std::vector<LiveTarget> &destination, const std::vector<LiveTarget> &source) {
    for (const auto &target : source) {
        if (destination.size() >= kMaxLiveTargets)
            return;
        const bool duplicate =
            std::any_of(destination.begin(), destination.end(),
                        [&target](const LiveTarget &item) { return item.taskId == target.taskId; });
        if (!duplicate)
            destination.push_back(target);
    }
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
// Human.view (IGI-RE humanbasetypes.h). No native exposes the field getter, so
// the documented struct offset is the only way to reach the view task.
constexpr std::size_t kHumanViewOffset = 0x4EC;
// Free camera state, owned entirely by the mod: the retail view is told to use
// this pose through HumanView_SetCamera (camOverride).
std::atomic<bool> g_freecamActive{false};
// The live render Camera captured by a viewport callback. Camera_Set accepts
// the override even while the game is paused (the renderer keeps drawing), so
// the free camera works while the mod menu is open.
std::atomic<void *> g_renderCamera{nullptr};
std::atomic<bool> g_viewportCallbackRegistered{false};
// True when this module owns the human input lock for the free camera. The menu
// pause can own the same lock; disabling freecam must not unlock it then.
std::atomic<bool> g_freecamInputLocked{false};
// Reset the frame clock on enable so a re-enabled camera does not jump by the
// clamped stale delta from the previous session.
std::atomic<bool> g_freecamClockReset{true};
struct FreecamPose {
    double position[3]{};
    float pitch = 0.0F;
    float yaw = 0.0F;
    float fov = 1.0F;
};
FreecamPose g_freecamPose;

IGI::HumanView *playerView(IGI::Human *player) {
    if (!player)
        return nullptr;
    return *reinterpret_cast<IGI::HumanView **>(reinterpret_cast<std::uint8_t *>(player) +
                                                kHumanViewOffset);
}

// The retail camera axes are the world orientation turned by -90 degrees around
// X (IGI-RE HumanCamera set_view). HumanView_SetCamera takes the world matrix,
// Camera_Set takes the converted axes.
void buildRenderCameraAxes(float pitch, float yaw, RetailMatrix &axes) {
    RetailMatrix world{};
    IGI::CAMERA::SET_EULER(reinterpret_cast<IGI::Matrix *>(&world), pitch, 0.0F, yaw);
    constexpr float kCameraRotX = -1.57079637F;
    IGI::CAMERA::ROTATE_X(reinterpret_cast<IGI::Matrix *>(&axes),
                          reinterpret_cast<const IGI::Matrix *>(&world), kCameraRotX);
}
} // namespace

// Viewport_AddCallback runs before the viewport builds its TransContext, which is
// exactly where IGI-RE FreeCamera_Run calls Camera_Set. Applying the pose here
// makes the free camera visible on the same frame, including while the retail
// game is paused behind the menu (HumanCamera does not run while paused).
extern "C" void __cdecl igiCaptureViewportCamera(void *camera) noexcept {
    g_renderCamera.store(camera, std::memory_order_release);
    if (!camera || !g_freecamActive.load(std::memory_order_acquire))
        return;
    RetailMatrix axes{};
    buildRenderCameraAxes(g_freecamPose.pitch, g_freecamPose.yaw, axes);
    // Same call shape as IGI-RE FreeCamera_Run, with a priority above the retail
    // human camera (300) so a paused frame cannot replace this pose.
    IGI::CAMERA::SET(reinterpret_cast<IGI::Camera *>(camera),
                     reinterpret_cast<const IGI::Vec3d *>(g_freecamPose.position),
                     reinterpret_cast<const IGI::Matrix *>(&axes), g_freecamPose.fov, 1.0F, 1000.0F,
                     IGI::OTHER::SCREEN_GET_FRAME(), nullptr, 0, 0);
}

// The retail free camera: HumanView_SetCamera sets camOverride, and the view's
// update copies camPos/camRot verbatim, so the mod owns the camera while keeping
// the player body and its normal controls untouched. Movement keys are polled
// with GetAsyncKeyState on the display callback, like the retail debug camera.
bool freecamEnabled() {
    return g_freecamActive.load(std::memory_order_acquire);
}

bool setFreecamEnabled(Runtime &runtime, bool enabled) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    auto *view = playerView(player);
    if (!enabled) {
        // Disabling must always clear the mod state, even during a level teardown
        // where the player/view may already be gone.
        if (view)
            IGI::CAMERA::CLEAR_CAM_OVERRIDE(view);
        if (!freecamEnabled()) {
            log(LogLevel::Info, "free camera already off");
            return true;
        }
        g_freecamActive.store(false, std::memory_order_release);
        if (g_freecamInputLocked.exchange(false, std::memory_order_acq_rel))
            (void)unlockHumanInput();
        log(LogLevel::Info, "free camera disabled: the retail view owns the camera again");
        return true;
    }
    if (!player || !view) {
        log(LogLevel::Warning, "free camera rejected: the player view is unavailable");
        return false;
    }
    if (freecamEnabled()) {
        log(LogLevel::Info, "free camera already active");
        return true;
    }
    // Start from the camera the game is rendering, so enabling never snaps the
    // view. The captured render Camera is the third-person eye position; the
    // camera-info handler returns the player's eye, which is a different point.
    IGI::HumanCameraInfo info{};
    IGI::HUMAN::GET_CAMERA_INFO(player, &info);
    g_freecamPose.position[0] = info.position[0];
    g_freecamPose.position[1] = info.position[1];
    g_freecamPose.position[2] = info.position[2];
    float forwardX = info.matrix[1];
    float forwardY = info.matrix[4];
    float forwardZ = info.matrix[7];
    g_freecamPose.fov = info.fov > 0.0F ? info.fov : 1.0F;
#if defined(_WIN32)
    if (auto *camera = g_renderCamera.load(std::memory_order_acquire)) {
        const auto *rendered = reinterpret_cast<const RetailCameraView *>(camera);
        if (std::isfinite(rendered->position[0]) && std::isfinite(rendered->position[1]) &&
            std::isfinite(rendered->position[2])) {
            g_freecamPose.position[0] = rendered->position[0];
            g_freecamPose.position[1] = rendered->position[1];
            g_freecamPose.position[2] = rendered->position[2];
            // The render axes are the world matrix turned by -90 X, so column 2
            // (m[2], m[5], m[8]) is the world view forward.
            forwardX = rendered->axes.m[2];
            forwardY = rendered->axes.m[5];
            forwardZ = rendered->axes.m[8];
            if (rendered->fov > 0.0F)
                g_freecamPose.fov = rendered->fov;
        }
    }
#endif
    // Matrix_SetEuler(pitch, 0, yaw) makes forward = (-sin yaw cos pitch,
    // cos yaw cos pitch, sin pitch), so the inverse is atan2(-x, y).
    g_freecamPose.yaw = std::atan2(-forwardX, forwardY);
    g_freecamPose.pitch = std::asin(std::clamp(forwardZ, -1.0F, 1.0F));
#if defined(_WIN32)
    // Register once; the callback keeps the Camera* the renderer is using.
    if (!g_viewportCallbackRegistered.exchange(true, std::memory_order_acq_rel))
        IGI::OTHER::VIEWPORT_ADD_CALLBACK(&igiCaptureViewportCamera);
#endif
    // The view override does not stop the body controls, so lock them while the
    // camera is detached. The menu pause may already own the lock; track only a
    // lock this module created so disabling cannot unlock the retail menu.
    g_freecamInputLocked.store(igi::menuPauseActive() ? false : lockHumanInput(),
                               std::memory_order_release);
    g_freecamClockReset.store(true, std::memory_order_release);
    g_freecamActive.store(true, std::memory_order_release);
    log(LogLevel::Info, "free camera enabled: WASD moves, Space/Ctrl rise/drop, Shift is 4x, "
                        "arrow keys turn; the player body stays in place");
    return true;
}

bool updateFreecam(Runtime &runtime) {
    if (!freecamEnabled() || !requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    auto *view = playerView(player);
    if (!player || !view)
        return false;
#if defined(_WIN32)
    // Clamp the step so a menu pause or a stalled frame cannot teleport the camera.
    static ULONGLONG lastTick = GetTickCount64();
    const ULONGLONG now = GetTickCount64();
    if (g_freecamClockReset.exchange(false, std::memory_order_acq_rel))
        lastTick = now;
    const double dt = std::clamp(static_cast<double>(now - lastTick) / 1000.0, 0.0, 0.1);
    lastTick = now;
    const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    constexpr float kTurnSpeed = 1.8F;
    // forward = (-sin yaw, cos yaw); turning right moves forward toward +X, which
    // means decreasing yaw in this matrix convention.
    if (down(VK_LEFT))
        g_freecamPose.yaw += kTurnSpeed * static_cast<float>(dt);
    if (down(VK_RIGHT))
        g_freecamPose.yaw -= kTurnSpeed * static_cast<float>(dt);
    if (down(VK_UP))
        g_freecamPose.pitch += kTurnSpeed * static_cast<float>(dt);
    if (down(VK_DOWN))
        g_freecamPose.pitch -= kTurnSpeed * static_cast<float>(dt);
    g_freecamPose.pitch = std::clamp(g_freecamPose.pitch, -1.5F, 1.5F);
    const float sinYaw = std::sin(g_freecamPose.yaw);
    const float cosYaw = std::cos(g_freecamPose.yaw);
    const float sinPitch = std::sin(g_freecamPose.pitch);
    const float cosPitch = std::cos(g_freecamPose.pitch);
    double moveX = 0.0;
    double moveY = 0.0;
    double moveZ = 0.0;
    // W/S run along the full aim direction (including pitch) so looking up and
    // flying forward climbs; A/D strafe horizontally along the camera right
    // vector, and Space/Ctrl remain the direct vertical controls.
    if (down('W')) {
        moveX -= sinYaw * cosPitch;
        moveY += cosYaw * cosPitch;
        moveZ += sinPitch;
    }
    if (down('S')) {
        moveX += sinYaw * cosPitch;
        moveY -= cosYaw * cosPitch;
        moveZ -= sinPitch;
    }
    if (down('D')) {
        moveX += cosYaw;
        moveY += sinYaw;
    }
    if (down('A')) {
        moveX -= cosYaw;
        moveY -= sinYaw;
    }
    if (down(VK_SPACE))
        moveZ += 1.0;
    if (down(VK_CONTROL))
        moveZ -= 1.0;
    // Retail world units: 4096 per metre (IGI-RE kUnitsToMeters). Two metres per
    // second normally, eight with Shift, is responsive without overshooting rooms.
    const double speed = down(VK_SHIFT) ? 32768.0 : 8192.0;
    g_freecamPose.position[0] += moveX * speed * dt;
    g_freecamPose.position[1] += moveY * speed * dt;
    g_freecamPose.position[2] += moveZ * speed * dt;
    RetailMatrix rotation{};
    IGI::CAMERA::SET_EULER(reinterpret_cast<IGI::Matrix *>(&rotation), g_freecamPose.pitch, 0.0F,
                           g_freecamPose.yaw);
    // The view override makes the retail HumanCamera copy this pose on its next
    // update (mode 4), so the player camera, sound listener and crosshair all use
    // it. The viewport callback above applies the same pose to the live render
    // Camera for paused frames and for the current frame.
    IGI::CAMERA::SET_CAMERA(view, g_freecamPose.position,
                            reinterpret_cast<const IGI::Matrix *>(&rotation));
#endif
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
    auto *target = findLiveTask(runtime, targetTaskId);
    if (!target || target == reinterpret_cast<IGI::Task *>(player)) {
        log(LogLevel::Warning, "teleport rejected: target is no longer live on this level");
        return false;
    }
    const auto targetPosition = taskPosition(target);
    std::array<double, 3> playerPosition{};
    (void)readPlayerPosition(runtime, playerPosition);
    // Stand 1.5 m from the target on the side the player is already on, so the
    // body does not spawn inside the object. Z is vertical in this world, so the
    // stand-off is horizontal only and the ground probe fixes the height.
    constexpr double kTeleportStandOff = 1.5 * 4096.0;
    const double direction[2] = {playerPosition[0] - targetPosition[0],
                                 playerPosition[1] - targetPosition[1]};
    const double length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    std::array<double, 3> position{};
    if (length > 1.0) {
        position[0] = targetPosition[0] + direction[0] / length * kTeleportStandOff;
        position[1] = targetPosition[1] + direction[1] / length * kTeleportStandOff;
    } else {
        position[0] = targetPosition[0] + kTeleportStandOff;
        position[1] = targetPosition[1];
    }
    position[2] = targetPosition[2];
    if (!placeHumanOnGround(runtime, player, position))
        return false;
    broadcastPlayerPosition(player);
    log(LogLevel::Info, "player teleported to target #" + std::to_string(targetTaskId));
    return true;
}

bool readTargetPosition(Runtime &runtime, int targetTaskId, std::array<double, 3> &position) {
    if (!requireLiveGameThread(runtime) || targetTaskId < 0)
        return false;
    auto *target = findLiveTask(runtime, targetTaskId);
    if (!target)
        return false;
    position = taskPosition(target);
    return std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]);
}

bool readPlayerPosition(Runtime &runtime, std::array<double, 3> &position) {
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    // Human.pos at +0x20 (IGI-RE humanbasetypes.h) is the body position the
    // teleport natives move. The camera sits behind/above it in third person, so
    // prefilling from the camera would store the viewing offset as the player
    // position. No native exposes the field, so the documented layout is used.
    double bodyPosition[3]{};
    std::memcpy(bodyPosition, reinterpret_cast<const std::uint8_t *>(player) + 0x20,
                sizeof(bodyPosition));
    const double magnitude =
        std::fabs(bodyPosition[0]) + std::fabs(bodyPosition[1]) + std::fabs(bodyPosition[2]);
    if (std::isfinite(magnitude) && magnitude >= 1.0) {
        position = {bodyPosition[0], bodyPosition[1], bodyPosition[2]};
        return true;
    }
    // A zero body position means the player task is not placed yet (mission
    // start); the camera is still a usable starting point.
    IGI::HumanCameraInfo info{};
    IGI::HUMAN::GET_CAMERA_INFO(player, &info);
    position = {info.position[0], info.position[1], info.position[2]};
    return std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]);
}

void captureLevelStartPosition(Runtime &runtime) {
    auto *game = IGI::GAME::GET();
    if (game != g_levelStartGame) {
        g_levelStartGame = game;
        g_levelStartValid = false;
    }
    if (!game || g_levelStartValid)
        return;
    std::array<double, 3> position{};
    if (!readPlayerPosition(runtime, position))
        return;
    // The first frames of a level can report an unplaced (0,0,0) body; keep
    // sampling until the retail spawn has actually been applied.
    const double magnitude =
        std::fabs(position[0]) + std::fabs(position[1]) + std::fabs(position[2]);
    if (!std::isfinite(magnitude) || magnitude < 1.0)
        return;
    g_levelStartPosition = position;
    g_levelStartValid = true;
    log(LogLevel::Info, "level start teleport target captured at " + std::to_string(position[0]) +
                            ", " + std::to_string(position[1]) + ", " +
                            std::to_string(position[2]));
}

bool readLevelStartPosition(Runtime &runtime, std::array<double, 3> &position) {
    (void)runtime;
    if (!g_levelStartValid || IGI::GAME::GET() != g_levelStartGame)
        return false;
    position = g_levelStartPosition;
    return true;
}

bool teleportPlayerToPosition(Runtime &runtime, const std::array<double, 3> &position) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    if (!placeHumanOnGround(runtime, player, position))
        return false;
    broadcastPlayerPosition(player);
    log(LogLevel::Info, "player moved to " + std::to_string(position[0]) + ", " +
                            std::to_string(position[1]) + ", " + std::to_string(position[2]));
    return true;
}

bool sendPlayerToTarget(Runtime &runtime, int targetTaskId) {
    if (!requireLiveGameThread(runtime) || targetTaskId < 0)
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    if (findLiveTask(runtime, targetTaskId) == reinterpret_cast<IGI::Task *>(player)) {
        // Sending to the player entity means re-publishing the current position
        // to both retail broadcast lists, not moving the body.
        broadcastPlayerPosition(player);
        log(LogLevel::Info, "send player position: re-sent the live player position");
        return true;
    }
    if (!teleportPlayerToTarget(runtime, targetTaskId))
        return false;
    log(LogLevel::Info, "send player position: destination target #" +
                            std::to_string(targetTaskId) + " reached and broadcast");
    return true;
}

bool missionAvailable(Runtime &runtime, int missionId) {
    if (!requireLiveGameThread(runtime) || missionId <= 0)
        return false;
    return IGI::MISSION::FIND(missionId) != nullptr;
}

std::vector<LiveTarget> missionTargets(Runtime &runtime) {
    std::vector<LiveTarget> missions;
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return missions;
    for (int id = 1; id <= 32 && missions.size() < kMaxLiveTargets; ++id) {
        auto *mission = IGI::MISSION::FIND(id);
        if (!mission)
            continue;
        const auto *layout = reinterpret_cast<const MissionLayout *>(mission);
        std::string name = "Mission " + std::to_string(id);
        if (layout->name[0] != '\0') {
            std::size_t nameLength = 0;
            while (nameLength < sizeof(layout->name) && layout->name[nameLength] != '\0')
                ++nameLength;
            name += ": ";
            name.append(layout->name, nameLength);
        }
        missions.push_back({id, std::move(name)});
    }
    return missions;
}

bool requestModExit() {
#if defined(_WIN32)
    const bool requested = IGI_SDK_RequestSafeUnload() != 0;
    log(requested ? LogLevel::Info : LogLevel::Warning,
        requested ? "Exit Mod: safe unload requested; the host closes the menu and removes the "
                    "retail callbacks"
                  : "Exit Mod rejected: the host is not running or unload is already pending");
    return requested;
#else
    return false;
#endif
}

bool requestLevelChange(Runtime &runtime, int levelId) {
    if (!requireLiveGameThread(runtime) || levelId <= 0)
        return false;
    if (!missionAvailable(runtime, levelId)) {
        log(LogLevel::Warning, "level change rejected: mission " + std::to_string(levelId) +
                                   " is not installed in the retail mission table");
        return false;
    }
    // Do not touch the Game task here: this runs from a frame callback, before
    // the retail scene is flushed. The host performs the change from the end of
    // the frame, after Scene_DrawAll, like the retail F12 level switch.
    g_pendingLevel.store(levelId, std::memory_order_release);
    log(LogLevel::Info, "level change queued: mission " + std::to_string(levelId) +
                            "; switching after the current frame is flushed");
    return true;
}

bool processPendingLevelChange(Runtime &runtime) {
    const int levelId = g_pendingLevel.exchange(0, std::memory_order_acq_rel);
    if (levelId <= 0)
        return false;
    return changeLevel(runtime, levelId);
}

bool requestMissionRestart(Runtime &runtime) {
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    if (g_restartRequested.exchange(true, std::memory_order_acq_rel))
        return true;
    log(LogLevel::Info,
        "mission restart queued; the host detaches the injected menu before LevelRestart");
    return true;
}

bool processMissionRestart(Runtime &runtime) {
    if (!g_restartRequested.exchange(false, std::memory_order_acq_rel))
        return false;
    if (!requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    clearLiveTargetSelections("mission restart rebuilds the level task list");
    (void)setFreecamEnabled(runtime, false);
    g_thirdPersonCrosshairEnabled.store(false, std::memory_order_release);
    (void)removePrecipitationTask();
    (void)igi::resumeFromMenu(runtime);
    igi::abandonMenuPause();
    (void)igi::unlockHumanInput();
    IGI::GAME::SET_PAUSED(0);
    if (!prepareMenuForLevelChange()) {
        log(LogLevel::Error, "mission restart: injected menu cleanup failed");
        return false;
    }
    IGI::GAME::RESTART();
    log(LogLevel::Info,
        "mission restart: menu detached and resources released before LevelRestart; press F5 "
        "after the level loads to open a fresh menu");
    return true;
}

bool setWorldHour(Runtime &runtime, float hour) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto *task = findLevelFlowTask(runtime);
    if (!task) {
        log(LogLevel::Warning, "world time rejected: the LevelFlow task is unavailable");
        return false;
    }
    if (!(hour >= 0.0F && hour <= 24.0F)) {
        log(LogLevel::Warning, "world time rejected: hour must be 0..24");
        return false;
    }
    auto *layout = reinterpret_cast<LevelFlowLayout *>(task);
    // World time = startHour + elapsed/fps/3600 (IGI-RE LevelFlow_GetHour). Only
    // startHour changes here, so the mission timer stays independent.
    const double elapsedHours =
        static_cast<double>(layout->nTick) / (static_cast<double>(currentFlowFps()) * 3600.0);
    layout->startHour = static_cast<float>(static_cast<double>(hour) - elapsedHours);
    log(LogLevel::Info, "world time set to " + std::to_string(hour) + " h (start hour " +
                            std::to_string(layout->startHour) + ")");
    return true;
}

std::optional<float> worldHour(Runtime &runtime) {
    auto *task = findLevelFlowTask(runtime);
    if (!task)
        return std::nullopt;
#if defined(_WIN32)
    // The retail routine is the authority for the clock (startHour + ticks).
    const double retailHour = IGI::OTHER::LEVELFLOW_GET_HOUR();
    if (std::isfinite(retailHour))
        return static_cast<float>(retailHour);
#endif
    const auto *layout = reinterpret_cast<const LevelFlowLayout *>(task);
    const double elapsedHours =
        static_cast<double>(layout->nTick) / (static_cast<double>(currentFlowFps()) * 3600.0);
    return static_cast<float>(static_cast<double>(layout->startHour) + elapsedHours);
}

bool setMissionElapsedSeconds(Runtime &runtime, int seconds) {
    if (!requireLiveGameThread(runtime) || seconds < 0)
        return false;
    auto *task = findLevelFlowTask(runtime);
    if (!task) {
        log(LogLevel::Warning, "mission time rejected: the LevelFlow task is unavailable");
        return false;
    }
    auto *layout = reinterpret_cast<LevelFlowLayout *>(task);
    const float keepHour = worldHour(runtime).value_or(layout->startHour);
    layout->nTick = seconds * currentFlowFps();
    // Reset startHour so changing the mission clock does not move the world sun.
    const double elapsedHours =
        static_cast<double>(layout->nTick) / (static_cast<double>(currentFlowFps()) * 3600.0);
    layout->startHour = static_cast<float>(static_cast<double>(keepHour) - elapsedHours);
    log(LogLevel::Info, "mission time set to " + std::to_string(seconds) + " s (" +
                            std::to_string(layout->nTick) + " ticks)");
    return true;
}

std::optional<int> missionElapsedSeconds(Runtime &runtime) {
    auto *task = findLevelFlowTask(runtime);
    if (!task)
        return std::nullopt;
    const auto *layout = reinterpret_cast<const LevelFlowLayout *>(task);
    return layout->nTick / currentFlowFps();
}

bool setMissionTimeLimit(Runtime &runtime, float seconds) {
    if (!requireLiveGameThread(runtime) || seconds < 0.0F)
        return false;
    auto *task = findLevelFlowTask(runtime);
    if (!task) {
        log(LogLevel::Warning, "mission time limit rejected: the LevelFlow task is unavailable");
        return false;
    }
    reinterpret_cast<LevelFlowLayout *>(task)->timeLimit = seconds;
    log(LogLevel::Info, "mission time limit set to " + std::to_string(seconds) + " s");
    return true;
}

std::optional<int> missionTimeLimitSeconds(Runtime &runtime) {
    auto *task = findLevelFlowTask(runtime);
    if (!task)
        return std::nullopt;
    return static_cast<int>(reinterpret_cast<const LevelFlowLayout *>(task)->timeLimit);
}

bool saveLiveHumanPlayerParams(Runtime &runtime, const HumanPlayerQvmParams &values) {
    if (!requireLiveGameThread(runtime))
        return false;
    auto current = readHumanPlayerQvmParams();
    if (!current) {
        log(LogLevel::Warning,
            "human physics rejected: the humanplayer.qvm globals are unavailable");
        return false;
    }
    // Retail order matters: DefineHumanPlayerGeneral (run by
    // HumanPlayer_LoadParameters) writes these globals from the qvm script, so a
    // write after a load would be reverted. Load only when the qvm has not run
    // yet this level (all globals zero) to establish the authored baseline, then
    // write the user's values on top of it.
    if (current->moveScale <= 0.0F && current->jumpSpeedKmh <= 0.0F && current->jumpUpKmh <= 0.0F) {
        if (!reloadLiveHumanPlayerParams(runtime)) {
            log(LogLevel::Warning, "human physics rejected: the qvm parameters were never loaded");
            return false;
        }
        current = readHumanPlayerQvmParams();
        if (!current) {
            log(LogLevel::Warning, "human physics rejected: the baseline read failed");
            return false;
        }
    }
    // Keep the live conversion factor so the typed km/h values are written in
    // the same units the retail qvm used.
    HumanPlayerQvmParams updated = values;
    updated.kmhScale = current->kmhScale;
    updated.live = true;
    if (!writeHumanPlayerQvmParams(updated)) {
        log(LogLevel::Warning, "human physics rejected: writing the retail globals failed");
        return false;
    }
    // Read back immediately: the retail jump state machine reads these globals
    // directly on the next jump (humanstate.c), so the read-back IS the live
    // value the game will use. Verifying it proves the change took effect
    // without a level restart.
    const auto verify = readHumanPlayerQvmParams();
    if (!verify) {
        log(LogLevel::Error, "human physics: write succeeded but the live read-back failed");
        return false;
    }
    const auto close = [](float a, float b) {
        return std::fabs(a - b) <= 0.001F + 0.002F * std::max(std::fabs(a), std::fabs(b));
    };
    if (!close(verify->moveScale, updated.moveScale) ||
        !close(verify->jumpSpeedKmh, updated.jumpSpeedKmh) ||
        !close(verify->jumpUpKmh, updated.jumpUpKmh) ||
        !close(verify->airSpeedKmh, updated.airSpeedKmh) ||
        !close(verify->maxDamage, updated.maxDamage) ||
        !close(verify->fenceDamage, updated.fenceDamage)) {
        log(LogLevel::Error,
            "human physics: the read-back did not match the write (move " +
                std::to_string(verify->moveScale) + " vs " + std::to_string(updated.moveScale) +
                ", jump " + std::to_string(verify->jumpSpeedKmh) + " vs " +
                std::to_string(updated.jumpSpeedKmh) + " km/h, height " +
                std::to_string(verify->jumpUpKmh) + " vs " + std::to_string(updated.jumpUpKmh) +
                " km/h); the game may have reloaded the qvm script");
        return false;
    }
    log(LogLevel::Info, "human physics live (read back verified): move " +
                            std::to_string(verify->moveScale) + ", jump speed " +
                            std::to_string(verify->jumpSpeedKmh) + " km/h, jump height " +
                            std::to_string(verify->jumpUpKmh) + " km/h, air " +
                            std::to_string(verify->airSpeedKmh) + " km/h, damage " +
                            std::to_string(verify->maxDamage) + ", fence damage " +
                            std::to_string(verify->fenceDamage));
    return true;
}

bool reloadLiveHumanPlayerParams(Runtime &runtime) {
    if (!requireLiveGameThread(runtime))
        return false;
    if (!isCallableNative(IGI::HASH::HUMAN_PLAYER_LOAD_PARAMETERS,
                          "void HumanPlayer_LoadParameters(void)")) {
        log(LogLevel::Warning,
            "human physics reload rejected: HumanPlayer_LoadParameters is unavailable");
        return false;
    }
    if (!reloadHumanPlayerParameters()) {
        log(LogLevel::Warning, "human physics reload rejected: the qvm loader failed");
        return false;
    }
    log(LogLevel::Info, "human physics reloaded from LOCAL:humanplayer/humanplayer.qsc");
    return true;
}

bool captureHumanPlayerBaseline(Runtime &runtime, HumanPlayerQvmParams &baseline) {
    if (!requireLiveGameThread(runtime))
        return false;
    if (!isCallableNative(IGI::HASH::HUMAN_PLAYER_LOAD_PARAMETERS,
                          "void HumanPlayer_LoadParameters(void)")) {
        log(LogLevel::Warning,
            "human physics baseline rejected: HumanPlayer_LoadParameters is unavailable");
        return false;
    }
    // Run the retail loader so the globals hold exactly what humanplayer.qvm
    // authored, then read them back. This is the authoritative default set; the
    // menu boxes must show these values, not hard-coded guesses.
    if (!reloadHumanPlayerParameters()) {
        log(LogLevel::Warning, "human physics baseline rejected: the qvm loader failed");
        return false;
    }
    const auto params = readHumanPlayerQvmParams();
    if (!params || params->moveScale <= 0.0F) {
        log(LogLevel::Warning,
            "human physics baseline rejected: the qvm globals did not initialize");
        return false;
    }
    baseline = *params;
    log(LogLevel::Info, "human physics baseline from HumanPlayer_LoadParameters: move " +
                            std::to_string(params->moveScale) + ", jump speed " +
                            std::to_string(params->jumpSpeedKmh) + " km/h, jump height " +
                            std::to_string(params->jumpUpKmh) + " km/h, air " +
                            std::to_string(params->airSpeedKmh) + " km/h, damage " +
                            std::to_string(params->maxDamage) + ", fence damage " +
                            std::to_string(params->fenceDamage));
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
    // The mod crosshair exists to replace the retail HUD crosshair in third
    // person. Gating it on WeaponType.sightDisplay only ever matched the handful
    // of weapons that already ask retail for a cross sprite, so the toggle
    // appeared dead on every other weapon.
    return camera.mode == 2;
}

bool thirdPersonCrosshairScreenPosition(Runtime &runtime, int width, int height, float &x,
                                        float &y) {
    if (width <= 0 || height <= 0 || !requireLiveGameThread(runtime) || !IGI::GAME::GET())
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player)
        return false;
    // GET_CAMERA_INFO already applies the view's camOverride, so this works for
    // the normal third-person camera and the mod free camera alike.
    IGI::HumanCameraInfo info{};
    IGI::HUMAN::GET_CAMERA_INFO(player, &info);
    const double aim[3] = {info.position[0] + info.matrix[1] * 100000.0,
                           info.position[1] + info.matrix[4] * 100000.0,
                           info.position[2] + info.matrix[7] * 100000.0};
    // Project through the renderer's own active TransContext. This is the exact
    // call the retail label/HUD code uses to place a world point on screen and it
    // is evaluated after the viewport draw, when gTransContext is still valid.
    float projected[2]{};
    float depth = 0.0F;
    IGI::OTHER::TRANS_CONTEXT_PROJECT(projected, &depth, aim);
    if (!(depth > 1.0F) || !std::isfinite(projected[0]) || !std::isfinite(projected[1]))
        return false;
    // TransContext_Project already returns display pixels: its scale/centre come
    // from the viewport scene rect (gScene.x/halfW). Scaling that by the passed
    // viewport size again moved the crosshair off the aim point.
    auto *displayMode = IGI::OTHER::DISPLAY_GET_MODE();
    const float limitX = displayMode && displayMode->width > 0
                             ? static_cast<float>(displayMode->width)
                             : static_cast<float>(width);
    const float limitY = displayMode && displayMode->height > 0
                             ? static_cast<float>(displayMode->height)
                             : static_cast<float>(height);
    if (projected[0] < 0.0F || projected[1] < 0.0F || projected[0] > limitX ||
        projected[1] > limitY) {
        return false;
    }
    x = projected[0];
    y = projected[1];
    return true;
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
    float centerX = static_cast<float>(displayMode->width) * 0.5F;
    float centerY = static_cast<float>(displayMode->height) * 0.5F;
    // The third-person camera is offset behind/above the player, so the weapon's aim
    // ray does not pass through the screen centre. Draw where the ray projects.
    float aimX = 0.0F;
    float aimY = 0.0F;
    if (thirdPersonCrosshairScreenPosition(runtime, displayMode->width, displayMode->height, aimX,
                                           aimY)) {
        centerX = aimX;
        centerY = aimY;
    }
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
    // The first attach can happen before the end-frame frameUpdate has sampled
    // the spawn, so make sure the per-level Level Start target exists here too.
    captureLevelStartPosition(runtime);
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
        if (choices.size() > kMaxMenuListChoices) {
            choices.resize(kMaxMenuListChoices);
            if (selection >= choices.size())
                selection = 0;
        }
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
    // The teleport picker accepts every live world object, not only NPCs, cars and
    // helicopters: the authored level tree supplies the doors, props and
    // landmarks that the game event lists never contain.
    const auto objects = entities.worldObjectTargets();
    std::vector<LiveTarget> teleportTargets;
    teleportTargets.reserve(entities.npcTargets().size() + entities.carTargets().size() +
                            entities.heliTargets().size() + objects.size());
    const auto appendTargets = [&teleportTargets](const std::vector<LiveTarget> &source) {
        for (const auto &target : source) {
            if (teleportTargets.size() >= kMaxLiveTargets)
                return;
            const bool duplicate = std::any_of(
                teleportTargets.begin(), teleportTargets.end(),
                [&target](const LiveTarget &existing) { return existing.taskId == target.taskId; });
            if (!duplicate)
                teleportTargets.push_back(target);
        }
    };
    appendTargets(entities.npcTargets());
    appendTargets(entities.carTargets());
    appendTargets(entities.heliTargets());
    appendTargets(objects);
    const auto teleport =
        replaceChoices("player_teleport_target", teleportTargets, "No live targets", -1);
    // Send Player Position accepts the same destinations plus the player entity.
    std::vector<LiveTarget> sendTargets;
    sendTargets.push_back({liveTaskKey(IGI::GAME::GET_PLAYER(0)), "Player (self)"});
    appendTargetsTo(sendTargets, teleportTargets);
    const auto send = replaceChoices("player_send_target", sendTargets, "No live targets", -1);
    const auto landmarks =
        replaceChoices("landmark_target", entities.landmarkTargets(), "No level landmarks", -1);
    const auto levels =
        replaceChoices("level", missionTargets(runtime), "No installed missions", -1);
    if (!npc.preservedIsLive)
        g_selectedNpcTaskId = -1;
    if (!car.preservedIsLive)
        g_selectedCarTaskId = -1;
    if (!heli.preservedIsLive)
        g_selectedHeliTaskId = -1;
    if (npc.changed || car.changed || heli.changed || teleport.changed || send.changed ||
        landmarks.changed || levels.changed) {
        log(LogLevel::Info, "live targets refreshed from the active game task list");
    }
    return npc.changed || car.changed || heli.changed || teleport.changed || send.changed ||
           landmarks.changed || levels.changed;
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
    } else if (id == "player_send_position") {
        if (PlayerHelpers(runtime).sendPosition())
            log(LogLevel::Info, "player position resent to the retail player state machine");
        else
            log(LogLevel::Warning, "send player position rejected: no live player was found");
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
        // The retail debug fly mode writes the body-fly state through the player
        // state machine, which crashed the game when it was driven from the menu. This
        // control now enables the camera-override free camera instead (the same
        // safe path as the Free Camera control) and documents the retail chart.
        const bool enabled = value == "Enabled";
        log(LogLevel::Info, std::string("debug keys ") + (enabled ? "enabled" : "disabled") +
                                "; retail chart: LCtrl+LShift+ F fly, Q/E cycle camera, "
                                "A default camera, F9 unlock missions, F11 position, "
                                "F12 next mission, P all weapons, R restart, I invincible, "
                                "Numpad +/- zoom, Numpad Enter zoom reset, Numpad * third "
                                "person, Numpad 8/2 pitch, 4/6 yaw, 9/3 distance. Fly mode is "
                                "implemented through the safe camera override.");
        if (setFreecamEnabled(runtime, enabled))
            log(LogLevel::Info, "debug fly mode applied through the camera override");
        else
            log(LogLevel::Warning, "debug fly mode request rejected");
    } else if (id == "freecam") {
        if (setFreecamEnabled(runtime, value == "Enabled"))
            log(LogLevel::Info, "free camera state applied");
        else
            log(LogLevel::Warning, "free camera request rejected");
    } else if (id == "third_person_crosshair") {
        setThirdPersonCrosshairEnabled(runtime, value == "Enabled");
    } else if (id == "level") {
        // The label is "Mission N: name [#N]" (the retail mission table decides
        // which ids exist), so the bracketed id is the authoritative value.
        const auto missionId = targetId(value).value_or(-1);
        if (missionId < 0 || !requestLevelChange(runtime, missionId))
            log(LogLevel::Warning, "level change rejected: pick an installed mission");
    } else if (id == "exit_mod") {
        if (!requestModExit())
            log(LogLevel::Warning, "Exit Mod: the host did not accept the unload request");
    } else if (id == "landmark_target") {
        const auto selected = targetId(value).value_or(-1);
        if (selected < 0) {
            log(LogLevel::Warning, "teleport rejected: pick a level landmark first");
        } else if (selected == kLevelStartTargetId) {
            std::array<double, 3> start{};
            if (readLevelStartPosition(runtime, start) &&
                teleportPlayerToPosition(runtime, start)) {
                log(LogLevel::Info, "player returned to the captured level start position");
            } else {
                log(LogLevel::Warning,
                    "teleport rejected: the level start position is not available yet");
            }
        } else if (teleportPlayerToTarget(runtime, selected)) {
            log(LogLevel::Info, "player teleported to level landmark " + std::string(value));
        } else {
            log(LogLevel::Warning, "teleport rejected: the selected landmark is no longer live");
        }
    } else if (id == "player_send_target") {
        const auto selected = targetId(value).value_or(-1);
        if (selected < 0) {
            log(LogLevel::Warning, "send player position rejected: pick a destination first");
        } else if (sendPlayerToTarget(runtime, selected)) {
            log(LogLevel::Info, "send player position: destination " + std::string(value));
        } else {
            log(LogLevel::Warning,
                "send player position rejected: the destination is no longer live");
        }
    } else if (id == "world_time_preset" || id == "world_time_hour") {
        float hour = 10.0F;
        if (id == "world_time_preset") {
            if (value.find("Night") != std::string::npos)
                hour = 22.0F;
            else if (value.find("Dawn") != std::string::npos)
                hour = 6.0F;
            else if (value.find("Dusk") != std::string::npos)
                hour = 18.0F;
            else if (value.find("Noon") != std::string::npos)
                hour = 12.0F;
        } else {
            hour = std::strtof(std::string(value).c_str(), nullptr);
        }
        if (setWorldHour(runtime, hour))
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: world time updated");
        else
            log(LogLevel::Warning, "world time update rejected");
    } else if (id == "mission_time_seconds") {
        const int seconds = std::atoi(std::string(value).c_str());
        if (setMissionElapsedSeconds(runtime, seconds))
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: mission time updated");
        else
            log(LogLevel::Warning, "mission time update rejected");
    } else if (id == "mission_time_preset") {
        const int current = missionElapsedSeconds(runtime).value_or(0);
        int seconds = current;
        if (value == "Reset")
            seconds = 0;
        else if (value == "+1 minute")
            seconds = current + 60;
        else if (value == "+5 minutes")
            seconds = current + 300;
        else if (value == "+15 minutes")
            seconds = current + 900;
        else if (value == "+30 minutes")
            seconds = current + 1800;
        if (setMissionElapsedSeconds(runtime, seconds))
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: mission time updated");
        else
            log(LogLevel::Warning, "mission time update rejected");
    } else if (id == "mission_time_limit") {
        const float seconds = std::strtof(std::string(value).c_str(), nullptr);
        if (setMissionTimeLimit(runtime, seconds))
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI MOD: mission time limit updated");
        else
            log(LogLevel::Warning, "mission time limit update rejected");
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
    } else if (id == "tank_drive") {
        // Tanks are Car tasks in the retail build, so the same Car_Control path
        // drives them; the label is separate because the speed presets differ
        // from what a jeep can use.
        float targetSpeed = 0.0F;
        bool brake = false;
        if (value == "Reverse")
            targetSpeed = -4.0F;
        else if (value == "Forward")
            targetSpeed = 8.0F;
        else if (value == "Full")
            targetSpeed = 18.0F;
        else if (value == "Brake")
            brake = true;
        auto *car =
            reinterpret_cast<IGI::Car *>(findLiveTarget(runtime, g_selectedCarTaskId, isCarTask));
        if (!car) {
            reportMissingCarTarget("tank drive");
        } else if (VehicleHelpers(runtime).driveCar(car, targetSpeed, brake)) {
            log(LogLevel::Info, "tank drive: selected vehicle set to " + std::string(value));
        } else {
            log(LogLevel::Warning, "tank drive rejected: Car_Control native was unavailable");
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
    } else if (id == "heli_stop") {
        auto *heli = selectedHeli(runtime);
        if (!heli) {
            log(LogLevel::Warning, "heli stop rejected: select a live helicopter first");
        } else if (VehicleHelpers(runtime).driveHeli(heli, 0.0F, 0.0F, 0.0F, 0.0F)) {
            log(LogLevel::Info,
                "heli stop: target speed cleared; the heli no longer holds reverse");
        } else {
            log(LogLevel::Warning, "heli stop rejected: Heli_Control was unavailable");
        }
    } else if (id == "heli_control") {
        auto *heli = selectedHeli(runtime);
        if (!heli) {
            log(LogLevel::Warning, "heli control rejected: select a live helicopter first");
        } else {
            // Heli_Control axes: 0 yaw, 1 pitch, 2 roll, 3 target speed. A
            // negative speed reverses; every steering choice keeps the current
            // speed command at zero so the heli holds its position while
            // rotating, and the explicit Stop/Reverse/Forward entries set speed.
            float speed = 0.0F;
            float yaw = 0.0F;
            float pitch = 0.0F;
            float roll = 0.0F;
            if (value == "Reverse")
                speed = -6.0F;
            else if (value == "Forward")
                speed = 6.0F;
            else if (value == "Forward Fast")
                speed = 20.0F;
            else if (value == "Yaw Left")
                yaw = -0.6F;
            else if (value == "Yaw Right")
                yaw = 0.6F;
            else if (value == "Nose Down")
                pitch = -0.6F;
            else if (value == "Nose Up")
                pitch = 0.6F;
            else if (value == "Roll Left")
                roll = -0.6F;
            else if (value == "Roll Right")
                roll = 0.6F;
            else if (value != "Stop") {
                log(LogLevel::Warning,
                    "heli control rejected: unknown control '" + std::string(value) + "'");
                return;
            }
            if (VehicleHelpers(runtime).driveHeli(heli, speed, yaw, pitch, roll))
                log(LogLevel::Info, "heli control applied: " + std::string(value));
            else
                log(LogLevel::Warning, "heli control rejected: Heli_Control was unavailable");
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
    } else if (id == "human_info") {
        const auto params = readHumanPlayerQvmParams();
        if (!params) {
            log(LogLevel::Warning,
                "human params rejected: the humanplayer.qvm globals are unavailable");
        } else {
            std::ostringstream status;
            status << "Humanplayer: move " << params->moveScale << ", jump speed "
                   << params->jumpSpeedKmh << " km/h, jump height " << params->jumpUpKmh
                   << " km/h, air " << params->airSpeedKmh << " km/h, damage " << params->maxDamage
                   << ", fence damage " << params->fenceDamage;
            log(LogLevel::Info, status.str());
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, status.str().c_str());
        }
    } else if (id == "camera_info") {
        if (CameraHelpers(runtime).reportInfo())
            log(LogLevel::Info, "camera info reported from the retail camera-info native");
        else
            log(LogLevel::Warning, "camera info rejected: player unavailable");
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
    // The temporary command script only calls registered game functions, so any
    // live task can host it. This keeps mission switching working in the external
    // ImGui build, which has no retail MenuManager on the stack.
    auto *manager = IGI::TASK::FIND_CHILD(root, IGI::MENU::GET_TYPE());
    return executeMenuCommandFor(manager ? manager : root, commandScript);
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
    // A level id that is not in the retail mission table makes LevelStart call
    // Error_Show and hang the process, so the id is validated before the running
    // level is unloaded.
    if (!missionAvailable(runtime, levelId)) {
        log(LogLevel::Warning,
            "level change rejected: mission " + std::to_string(levelId) + " is not installed");
        return false;
    }
    auto *game = IGI::GAME::GET();
    if (!game)
        return false;
    clearLiveTargetSelections("level change unloads the current level task list");
    // Fly mode writes body state through the player SM; the crosshair draws
    // through the player view. Both must be off before the Game task dies.
    (void)setFreecamEnabled(runtime, false);
    g_thirdPersonCrosshairEnabled.store(false, std::memory_order_release);
    (void)removePrecipitationTask();
    // The pause ownership described the player/Game task that is about to die;
    // carrying it into the new level made the first close restore the wrong
    // pause state.
    igi::abandonMenuPause();
    if (!prepareMenuForLevelChange()) {
        log(LogLevel::Error, "level change: injected menu cleanup failed");
        return false;
    }
    // The cleanup above deleted the retail MenuManager together with the injected
    // screens, so the temporary command script runs on the game root instead.
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
    for (auto *task : liveTasks(runtime_)) {
        if (targets.size() >= kMaxLiveTargets)
            break;
        if (!isNpcTask(task))
            continue;
        targets.push_back({liveTaskKey(task), liveTaskName(task)});
    }
    return targets;
}
std::vector<LiveTarget> EntityHelpers::carTargets() const {
    std::vector<LiveTarget> targets;
    if (!requireLiveGameThread(runtime_))
        return targets;
    for (auto *task : liveTasks(runtime_)) {
        if (targets.size() >= kMaxLiveTargets)
            break;
        if (!isCarTask(task))
            continue;
        targets.push_back({liveTaskKey(task), liveTaskName(task)});
    }
    return targets;
}
IGI::Heli *EntityHelpers::firstHeli() const {
    if (!requireLiveGameThread(runtime_))
        return nullptr;
    for (auto *task : liveTasks(runtime_)) {
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
    for (auto *task : liveTasks(runtime_)) {
        if (targets.size() >= kMaxLiveTargets)
            break;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (!isHeliTaskType(type))
            continue;
        targets.push_back({liveTaskKey(task), liveTaskName(task)});
    }
    return targets;
}

std::vector<LiveTarget> EntityHelpers::worldObjectTargets() const {
    std::vector<LiveTarget> targets;
    if (!requireLiveGameThread(runtime_))
        return targets;
    auto *playerTask = IGI::GAME::GET_PLAYER(0);
    const auto append = [&](IGI::Task *task) {
        if (!task || task == playerTask || targets.size() >= kMaxLiveTargets)
            return;
        if (isNpcTask(task) || isCarTask(task) || isHeliTask(task))
            return;
        // Accept objects that actually carry a world position: the retail
        // rigid-object hierarchy plus named Area/Building tasks. This skips
        // script/AI helper tasks whose +0x20 is not a position.
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        const auto rigidType = IGI::TASK::RIGID_DYN_CUBE_GET_TYPE();
        bool worldType = IGI::TASK::IS_A(type, rigidType) != 0;
        if (!worldType) {
            if (const char *typeName = IGI::TASK::GET_NAME(type)) {
                const std::string_view view(typeName);
                worldType = view.find("Obj") != std::string_view::npos ||
                            view.find("Area") != std::string_view::npos ||
                            view.find("Building") != std::string_view::npos ||
                            view.find("Door") != std::string_view::npos;
            }
        }
        if (!worldType)
            return;
        const auto name = liveTaskName(task);
        if (name.empty())
            return;
        const auto position = taskPosition(task);
        const double magnitude =
            std::fabs(position[0]) + std::fabs(position[1]) + std::fabs(position[2]);
        if (!std::isfinite(magnitude) || magnitude < 1.0)
            return;
        const int key = liveTaskKey(task);
        if (std::any_of(targets.begin(), targets.end(),
                        [key](const LiveTarget &existing) { return existing.taskId == key; }))
            return;
        targets.push_back({key, name, position, true});
    };
    // The authored Level tree is the only complete object source: the game's
    // event lists contain actors and vehicles, not buildings, doors or props.
    for (auto *task : authoredLevelTasks(runtime_))
        append(task);
    if (targets.empty()) {
        // Early in level load the authored roots may not be attached yet; keep
        // the live-list scan as a fallback instead of showing an empty picker.
        for (auto *task : liveTasks(runtime_))
            append(task);
    }
    std::sort(targets.begin(), targets.end(), [](const LiveTarget &left, const LiveTarget &right) {
        return left.name < right.name;
    });
    return targets;
}

std::vector<LiveTarget> EntityHelpers::landmarkTargets() const {
    static constexpr std::string_view kLandmarkWords[] = {
        "airport", "airstrip",   "base",     "bunker",     "bridge",    "building", "camp",
        "castle",  "checkpoint", "church",   "compound",   "dam",       "depot",    "dock",
        "embassy", "factory",    "gate",     "hangar",     "helipad",   "hospital", "hotel",
        "hq",      "lab",        "landmark", "lighthouse", "mosque",    "office",   "outpost",
        "palace",  "port",       "power",    "prison",     "radar",     "silo",     "station",
        "temple",  "tower",      "train",    "tunnel",     "warehouse", "yard",
    };
    std::vector<LiveTarget> landmarks;
    if (!requireLiveGameThread(runtime_))
        return landmarks;
    auto *playerTask = IGI::GAME::GET_PLAYER(0);
    const auto rigidType = IGI::TASK::RIGID_DYN_CUBE_GET_TYPE();
    // Scan the whole authored tree for landmark names so a building late in the
    // level is not dropped by the 3D-object list's display cap.
    for (auto *task : authoredLevelTasks(runtime_)) {
        if (landmarks.size() >= kMaxLiveTargets)
            break;
        if (!task || task == playerTask || isNpcTask(task) || isCarTask(task) || isHeliTask(task))
            continue;
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        bool worldType = IGI::TASK::IS_A(type, rigidType) != 0;
        if (!worldType) {
            if (const char *typeName = IGI::TASK::GET_NAME(type)) {
                const std::string_view view(typeName);
                worldType = view.find("Obj") != std::string_view::npos ||
                            view.find("Area") != std::string_view::npos ||
                            view.find("Building") != std::string_view::npos ||
                            view.find("Door") != std::string_view::npos;
            }
        }
        if (!worldType)
            continue;
        const auto name = liveTaskName(task);
        if (name.empty())
            continue;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool matches = std::any_of(
            std::begin(kLandmarkWords), std::end(kLandmarkWords),
            [&lower](std::string_view word) { return lower.find(word) != std::string::npos; });
        if (!matches)
            continue;
        const auto position = taskPosition(task);
        const double magnitude =
            std::fabs(position[0]) + std::fabs(position[1]) + std::fabs(position[2]);
        if (!std::isfinite(magnitude) || magnitude < 1.0)
            continue;
        landmarks.push_back({liveTaskKey(task), name, position, true});
    }
    // Every mission must offer useful destinations even when its object names do
    // not contain a landmark keyword: fall back to the named authored objects.
    if (landmarks.size() < 8) {
        for (const auto &object : worldObjectTargets()) {
            if (landmarks.size() >= kMaxLiveTargets)
                break;
            const bool already = std::any_of(
                landmarks.begin(), landmarks.end(),
                [&object](const LiveTarget &existing) { return existing.taskId == object.taskId; });
            if (!already)
                landmarks.push_back(object);
        }
    }
    // Guarantee a valid destination on every level, including missions whose
    // authored objects are unnamed: the spawn point captured for this Game task.
    // Reserve its slot before sorting/capping so the synthetic target is never
    // dropped by the list limit.
    std::array<double, 3> levelStart{};
    if (readLevelStartPosition(runtime_, levelStart)) {
        if (landmarks.size() >= kMaxLiveTargets)
            landmarks.resize(kMaxLiveTargets - 1);
        landmarks.push_back({kLevelStartTargetId, "Level Start", levelStart, true});
    }
    std::sort(
        landmarks.begin(), landmarks.end(),
        [](const LiveTarget &left, const LiveTarget &right) { return left.name < right.name; });
    if (landmarks.size() > kMaxLiveTargets)
        landmarks.resize(kMaxLiveTargets);
    return landmarks;
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
    // Send both retail broadcasts. HumanPlayer_SendPosition only feeds task list
    // 2 (the player state machine); the world/collision listeners live in list 1
    // and used to keep the old body position, so this control did nothing.
    broadcastPlayerPosition(reinterpret_cast<IGI::Human *>(p));
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
// Every Gun task owned by a human, matched by the retail Gun.id at +0x100.
// WeaponGunPickup walks the same child list, so this reaches weapons that were
// added this session and are not in one of the three carried slots.
static IGI::Task *findOwnedGun(IGI::Human *player, int gunId) {
    if (!player || gunId == 0)
        return nullptr;
    const auto gunType = IGI::WEAPON::GUN_GET_TYPE();
    if (gunType == 0)
        return nullptr;
    const auto *layout = reinterpret_cast<const IgiTaskLayout *>(player);
    for (auto *child = layout->head; child && child->next; child = child->next) {
        auto *task = reinterpret_cast<IGI::Task *>(child);
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (type != gunType && IGI::TASK::IS_A(type, gunType) == 0)
            continue;
        const int id = *reinterpret_cast<const int *>(reinterpret_cast<const std::uint8_t *>(task) +
                                                      kGunIdOffset);
        if (id == gunId)
            return task;
    }
    return nullptr;
}

// Any gun task other than the one being dropped. This finds inventory weapons
// that are children of the human but not currently in one of the three slots.
static IGI::Task *findAnyOtherGun(IGI::Human *player, const IGI::Task *excludeTask) {
    if (!player)
        return nullptr;
    const auto gunType = IGI::WEAPON::GUN_GET_TYPE();
    if (gunType == 0)
        return nullptr;
    const auto *layout = reinterpret_cast<const IgiTaskLayout *>(player);
    for (auto *child = layout->head; child && child->next; child = child->next) {
        auto *task = reinterpret_cast<IGI::Task *>(child);
        const auto type = reinterpret_cast<const IgiTaskLayout *>(task)->type;
        if (type != gunType && IGI::TASK::IS_A(type, gunType) == 0)
            continue;
        if (task != excludeTask)
            return task;
    }
    return nullptr;
}

// Adds reserve rounds and tops the loaded clip for one gun id. If the player
// does not carry the weapon yet, the retail pickup creates the weapon task and
// the new task is then filled, so newly added weapons receive the typed ammo.
static bool refillGun(IGI::Human *player, int gunId, int ammo) {
    if (!player || gunId == 0 || ammo <= 0)
        return false;
    auto *gun = findOwnedGun(player, gunId);
    // WeaponGunPickup adds reserve rounds for the carried gun id (the same call
    // the player pickup uses), and creates the weapon when it is missing.
    int message[] = {gunId, ammo};
    IGI::PLAYER::PICK_UP_WEAPON(player, message);
    if (!gun)
        gun = findOwnedGun(player, gunId);
    if (!gun) {
        log(LogLevel::Warning,
            "weapon ammo: the retail pickup did not create gun id " + std::to_string(gunId));
        return false;
    }
    // Retail Gun.rounds at +0x144 (IGI-RE guncasingtypes.h): the loaded clip.
    int &rounds =
        *reinterpret_cast<int *>(reinterpret_cast<std::uint8_t *>(gun) + kGunRoundsOffset);
    const int before = rounds;
    rounds = ammo;
    log(LogLevel::Info, "weapon ammo: gun id " + std::to_string(gunId) + " clip " +
                            std::to_string(before) + " -> " + std::to_string(rounds) + " and " +
                            std::to_string(ammo) + " reserve rounds added");
    return true;
}

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
    return refillGun(player, gunId, ammo);
}

bool WeaponHelpers::refillWeapon(std::string_view weaponId, int ammo) const {
    if (!requireLiveGameThread(runtime_) || weaponId.empty() || ammo <= 0)
        return false;
    auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
    if (!player) {
        log(LogLevel::Warning, "weapon ammo rejected: player is unavailable");
        return false;
    }
    const int gunId = IGI::SCRIPT::EVAL_CONST(std::string(weaponId).c_str());
    if (gunId == 0) {
        log(LogLevel::Warning,
            "weapon ammo rejected: unknown retail weapon symbol " + std::string(weaponId));
        return false;
    }
    return refillGun(player, gunId, ammo);
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
    log(LogLevel::Info, "drop weapon: dropping task #" + std::to_string(weaponId) +
                            " while keeping the rest of the live inventory");
    // A live player must not follow the retail death path. die_start drops the
    // held weapon and then calls Weapon_DeleteAll because the human is dying;
    // doing that to a live player deleted every carried weapon, and the next
    // gameplay frame crashed on the unarmed state. Stop the draw animation first,
    // drop only the held task, and let the game's World remove event delete it.
    IGI::HUMAN::STOP_WEAPON_ANIM(player, 0.0F);
    (void)IGI::WEAPON::DROP(weapon);
    auto **slots = reinterpret_cast<IGI::Task **>(reinterpret_cast<std::uint8_t *>(player) +
                                                  kHumanWeaponSlotsOffset);
    for (int slot = 0; slot < kHumanWeaponSlots; ++slot) {
        if (IGI::WEAPON::SLOT_GET(slots, slot) == weapon)
            IGI::WEAPON::SLOT_SET(slots, slot, nullptr);
    }

    // When the dropped gun was the only one, the player would be left unarmed.
    // IGI's live player state machine expects a weapon task, so grant the retail
    // sidearm and put it in slot 0. When another carried weapon exists, cycle to
    // it instead.
    auto *remaining = IGI::HUMAN::GET_WEAPON(player);
    if (!remaining) {
        for (int slot = 1; slot < kHumanWeaponSlots && !remaining; ++slot)
            remaining = IGI::WEAPON::SLOT_GET(slots, slot);
    }
    if (!remaining)
        remaining = findAnyOtherGun(player, weapon);
    if (remaining) {
        // Move the replacement into slot 0 and clear its old slot so the cycle
        // list does not contain the same task twice.
        for (int slot = 1; slot < kHumanWeaponSlots; ++slot) {
            if (IGI::WEAPON::SLOT_GET(slots, slot) == remaining)
                IGI::WEAPON::SLOT_SET(slots, slot, nullptr);
        }
        IGI::WEAPON::SLOT_SET(slots, 0, remaining);
    } else {
        const int glockId = IGI::SCRIPT::EVAL_CONST("WEAPON_ID_GLOCK");
        int message[] = {glockId, 30};
        IGI::PLAYER::PICK_UP_WEAPON(player, message);
        auto *glock = findOwnedGun(player, glockId);
        if (glock)
            IGI::WEAPON::SLOT_SET(slots, 0, glock);
        log(glock ? LogLevel::Info : LogLevel::Warning,
            glock ? "drop weapon: no other weapon remained; equipped the retail Glock sidearm so "
                    "the live player stays armed"
                  : "drop weapon: no other weapon remained and the retail sidearm could not be "
                    "equipped");
    }
    log(LogLevel::Info,
        "drop weapon: task #" + std::to_string(weaponId) +
            " dropped; other carried weapons were kept and the player remains armed");
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
    // This runs inside the retail menu script. Deleting the MenuScreen task or
    // its QVM resource here would pull the code out from under the running
    // script, so only queue the request; the host frame tick performs the
    // detach and LevelRestart after the script returns.
    return requestMissionRestart(runtime_);
}
bool WorldHelpers::setWeather(std::string_view weather) const {
    return igi::setWeather(runtime_, weather);
}
bool WorldHelpers::setWorldHour(float hour) const {
    return igi::setWorldHour(runtime_, hour);
}
std::optional<float> WorldHelpers::worldHour() const {
    return igi::worldHour(runtime_);
}
bool WorldHelpers::setMissionElapsedSeconds(int seconds) const {
    return igi::setMissionElapsedSeconds(runtime_, seconds);
}
std::optional<int> WorldHelpers::missionElapsedSeconds() const {
    return igi::missionElapsedSeconds(runtime_);
}
bool WorldHelpers::setMissionTimeLimit(float seconds) const {
    return igi::setMissionTimeLimit(runtime_, seconds);
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
bool VehicleHelpers::driveHeli(IGI::Heli *heli, float targetSpeed, float yaw, float pitch,
                               float roll) const {
    if (!requireLiveGameThread(runtime_) || !heli)
        return false;
    if (!isCallableNative(IGI::HASH::HELI_CONTROL, "void Heli_Control(Heli *h, ControlArg *arg)"))
        return false;
    // Heli_Control skips the speed write when axes[3] is exactly -1.0F, so a
    // "stop" has to be 0.0F, not -1. tick 1 selects the drive path; tick 0 is
    // the full reset and would teleport the helicopter back to its start.
    float axes[5] = {yaw, pitch, roll, targetSpeed, -2.0F};
    IGI::HeliControlArg drive{1, axes};
    log(LogLevel::Info, "heli control: speed " + std::to_string(targetSpeed) + " yaw " +
                            std::to_string(yaw) + " pitch " + std::to_string(pitch) + " roll " +
                            std::to_string(roll));
    IGI::VEHICLE::HELI_CONTROL(heli, &drive);
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
