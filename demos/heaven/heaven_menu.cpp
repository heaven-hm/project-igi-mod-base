#include "config/menu_config.hpp"
#include "helpers/menu_helpers.hpp"
#include "logging/logging.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>

namespace {
struct WeaponEntry {
    std::string_view label;
    std::string_view id;
    int ammo;
};

constexpr std::array kWeapons = {
    WeaponEntry{"Glock 17", "WEAPON_ID_GLOCK", 30},
    WeaponEntry{"MP5SD", "WEAPON_ID_MP5SD", 90},
    WeaponEntry{"Uzi", "WEAPON_ID_UZI", 120},
    WeaponEntry{"M16A2", "WEAPON_ID_M16A2", 120},
    WeaponEntry{"AK-47", "WEAPON_ID_AK47", 120},
    WeaponEntry{"Dragunov SVD", "WEAPON_ID_DRAGUNOV", 30},
    WeaponEntry{"Minimi", "WEAPON_ID_MINIMI", 200},
    WeaponEntry{"SPAS-12", "WEAPON_ID_SPAS12", 16},
    WeaponEntry{"Binoculars", "WEAPON_ID_BINOCULARS", 0},
    WeaponEntry{"Medipack", "WEAPON_ID_MEDIPACK", 50},
};

constexpr std::array kWeaponLabels = {"Glock 17",   "MP5SD",        "Uzi",    "M16A2",
                                      "AK-47",      "Dragunov SVD", "Minimi", "SPAS-12",
                                      "Binoculars", "Medipack"};

// Selected weapon index and the ammo amount the user typed into the text box.
std::size_t g_selectedWeapon = 0;
int g_refillAmmo = 120;
// The separate DefineHumanPlayerGeneral parameters exposed by the Human
// section. Values are display units: move/damage x100, speeds x10. Slot order
// must match kHumanParamIds.
enum HumanParamSlot {
    kHumanMoveScale,
    kHumanJumpSpeed,
    kHumanJumpHeight,
    kHumanAirSpeed,
    kHumanDamage,
    kHumanFenceDamage,
    kHumanParamCount
};
constexpr std::array<const char *, kHumanParamCount> kHumanParamIds = {
    "human_move_scale", "human_jump_speed", "human_jump_height",
    "human_air_speed",  "human_damage",     "human_fence_damage"};
int g_humanParamValue[kHumanParamCount] = {100, 65, 50, 65, 300, 100};
bool g_humanParamSet[kHumanParamCount] = {};
// Typed teleport destination in the game's WORLD UNITS (the internal truth; the
// boxes display it in the selected unit). One unit for all three axes, so a
// metres Z can never mix with a world-units X.
std::array<double, 3> g_teleportPos = {0.0, 0.0, 0.0};
bool g_teleportPosSet = false;
bool g_teleportUnitsMetres = true;
// Last live block returned by the qvm; Save patches these fields so the live
// km/h conversion factor is preserved.
igi::HumanPlayerQvmParams g_humanParams{};
// The values HumanPlayer_LoadParameters produced for the current level. The
// input boxes prefill from this baseline, never from hard-coded guesses.
igi::HumanPlayerQvmParams g_humanBaseline{};
bool g_humanBaselineCaptured = false;
void *g_humanBaselineGame = nullptr;

// Converts the display units back to the qvm's units (slot order: move, jump
// speed, jump height, air speed, damage, fence damage) and writes the retail
// globals. Called on every human input change and by the Save button.
void applyHumanPhysics(igi::Runtime &runtime) {
    g_humanParams.moveScale = static_cast<float>(g_humanParamValue[kHumanMoveScale]) / 100.0F;
    g_humanParams.jumpSpeedKmh = static_cast<float>(g_humanParamValue[kHumanJumpSpeed]) / 10.0F;
    g_humanParams.jumpUpKmh = static_cast<float>(g_humanParamValue[kHumanJumpHeight]) / 10.0F;
    g_humanParams.airSpeedKmh = static_cast<float>(g_humanParamValue[kHumanAirSpeed]) / 10.0F;
    g_humanParams.maxDamage = static_cast<float>(g_humanParamValue[kHumanDamage]) / 100.0F;
    g_humanParams.fenceDamage = static_cast<float>(g_humanParamValue[kHumanFenceDamage]) / 100.0F;
    const bool saved = igi::saveLiveHumanPlayerParams(runtime, g_humanParams);
    igi::log(saved ? igi::LogLevel::Info : igi::LogLevel::Warning,
             saved ? "human physics saved to the live retail globals"
                   : "human physics rejected: the live globals were unavailable");
}

// Replaced at runtime from the retail mission table, so the list only offers
// ids that actually exist (selecting a missing id used to hang the game).
constexpr std::array kLevelLabels = {"Loading mission list"};

// One table drives both the menu definition and the category assignment, so a
// control can never be added without a category or an action route.
struct MenuItemDef {
    std::string_view id;
    igi::ModMenuItemKind kind;
    std::string_view label;
    std::string_view category;
    std::string_view action;
    std::vector<std::string> values;
};

std::vector<igi::ModMenuItemConfig> buildItems() {
    const auto list = [](auto first, auto last) {
        std::vector<std::string> values;
        for (auto it = first; it != last; ++it)
            values.emplace_back(*it);
        return values;
    };
    const auto levelValues = list(kLevelLabels.begin(), kLevelLabels.end());
    const auto weaponValues = list(kWeaponLabels.begin(), kWeaponLabels.end());

    const MenuItemDef definitions[] = {
        // Weapons
        {"ammo", igi::ModMenuItemKind::Action, "Unlimited Ammo", "Weapons", "UnlimitedAmmo", {}},
        {"weapon_pickup", igi::ModMenuItemKind::List, "Selected Weapon", "Weapons", "SelectWeapon",
         weaponValues},
        {"weapon_ammo",
         igi::ModMenuItemKind::Input,
         "Ammo Amount",
         "Weapons",
         "RefillWithAmmo",
         {}},
        {"weapon_info",
         igi::ModMenuItemKind::Action,
         "Selected Weapon Info",
         "Weapons",
         "WeaponInfo",
         {}},
        {"weapon_stop_anim",
         igi::ModMenuItemKind::Action,
         "Reset Weapon Animation",
         "Weapons",
         "StopWeaponAnim",
         {}},
        {"weapon_drop",
         igi::ModMenuItemKind::Action,
         "Drop Selected Weapon",
         "Weapons",
         "DropWeapon",
         {}},
        {"weapon_cycle",
         igi::ModMenuItemKind::Action,
         "Cycle Weapon",
         "Weapons",
         "CycleWeapon",
         {}},
        {"weapon_draw_anim",
         igi::ModMenuItemKind::Action,
         "Draw Weapon Animation",
         "Weapons",
         "RequestWeaponAnim",
         {}},
        {"weapon_category",
         igi::ModMenuItemKind::Action,
         "Select Weapon Category",
         "Weapons",
         "SelectWeaponCategory",
         {}},
        {"weapon_all",
         igi::ModMenuItemKind::Action,
         "Equip All Weapons",
         "Weapons",
         "PickUpAllWeapons",
         {}},
        {"weapon_events",
         igi::ModMenuItemKind::Action,
         "Weapon Events",
         "Weapons",
         "WeaponEvents",
         {}},

        // Player
        {"player_reset_damage",
         igi::ModMenuItemKind::Action,
         "Reset Player Damage",
         "Player",
         "HumanResetDamage",
         {}},
        {"player_snap_ground",
         igi::ModMenuItemKind::Action,
         "Snap Player To Ground",
         "Player",
         "HumanSnapToGround",
         {}},
        {"player_status",
         igi::ModMenuItemKind::Action,
         "Player Status",
         "Player",
         "LogGameState",
         {}},
        {"player_collision",
         igi::ModMenuItemKind::List,
         "Player Collision",
         "Player",
         "HumanCollision",
         {"Enabled", "Disabled"}},
        // Teleport option 1: any live object, AI, vehicle or level landmark.
        // The three former pickers (Teleport To Target, Landmark, Send To
        // Target) were the same move with different lists, so they are one.
        {"teleport_target",
         igi::ModMenuItemKind::List,
         "Teleport To Any Object / AI",
         "Player",
         "TeleportTarget",
         {"No live target list"}},
        // Teleport option 2: typed XYZ position. The boxes prefill from the
        // live player position in the selected unit. Positions use world units
        // (4096 per metre) or metres; Euler is for rotations, not positions.
        {"teleport_x", igi::ModMenuItemKind::Input, "Teleport X", "Player", "TeleportX", {}},
        {"teleport_y", igi::ModMenuItemKind::Input, "Teleport Y", "Player", "TeleportY", {}},
        {"teleport_z", igi::ModMenuItemKind::Input, "Teleport Z", "Player", "TeleportZ", {}},
        {"teleport_units",
         igi::ModMenuItemKind::List,
         "Position Units",
         "Player",
         "TeleportUnits",
         {"Metres", "World Units (native)"}},
        {"teleport_apply",
         igi::ModMenuItemKind::Action,
         "Teleport To Position",
         "Player",
         "TeleportApply",
         {}},

        // Player/Human: the DefineHumanPlayerGeneral params authored by
        // LOCAL:humanplayer/humanplayer.qsc, up to the damage/health pair. Move
        // scale (arg 0), jump speed (arg 1), jump height (arg 2), air speed
        // (arg 3), damage scale (arg 7) and fence damage (arg 8) are separate
        // qvm values. The peek params (args 4..6) are intentionally not exposed.
        // Integer boxes keep the QVM frontend valid; x10/x100 is noted
        // in the label so 6.5 km/h is still typable.
        {"human_move_scale",
         igi::ModMenuItemKind::Input,
         "Move Speed Scale (x100)",
         "Player",
         "HumanParam",
         {}},
        {"human_jump_speed",
         igi::ModMenuItemKind::Input,
         "Jump Speed (km/h x10)",
         "Player",
         "HumanParam",
         {}},
        {"human_jump_height",
         igi::ModMenuItemKind::Input,
         "Jump Height (km/h x10)",
         "Player",
         "HumanParam",
         {}},
        {"human_air_speed",
         igi::ModMenuItemKind::Input,
         "Air Speed (km/h x10)",
         "Player",
         "HumanParam",
         {}},
        {"human_damage",
         igi::ModMenuItemKind::Input,
         "Damage Scale (x100)",
         "Player",
         "HumanParam",
         {}},
        {"human_fence_damage",
         igi::ModMenuItemKind::Input,
         "Fence Damage (x100)",
         "Player",
         "HumanParam",
         {}},
        {"human_save",
         igi::ModMenuItemKind::Action,
         "Save Player Physics",
         "Player",
         "HumanSaveParams",
         {}},
        {"human_reload",
         igi::ModMenuItemKind::Action,
         "Read Humanplayer (Load qvm Values)",
         "Player",
         "HumanReloadParams",
         {}},

        // Camera
        {"view",
         igi::ModMenuItemKind::List,
         "Camera Mode",
         "Camera",
         "ViewCam",
         {"First Person", "Third Person"}},
        {"camera_info", igi::ModMenuItemKind::Action, "Camera Info", "Camera", "CameraInfo", {}},
        {"third_person_crosshair",
         igi::ModMenuItemKind::List,
         "Third-Person Crosshair",
         "Camera",
         "ThirdPersonCrosshair",
         {"Disabled", "Enabled"}},
        {"debug_keys",
         igi::ModMenuItemKind::List,
         "Debug Keys / Fly Mode",
         "Camera",
         "DebugKeys",
         {"Disabled", "Enabled"}},
        {"freecam",
         igi::ModMenuItemKind::List,
         "Free Camera (WASD)",
         "Camera",
         "FreeCamera",
         {"Disabled", "Enabled"}},
        {"look",
         igi::ModMenuItemKind::List,
         "Look Control",
         "Camera",
         "HumanLook",
         {"Enabled", "Disabled"}},

        // World
        {"level", igi::ModMenuItemKind::List, "Mission", "World", "SetMission", levelValues},
        {"world_time_preset",
         igi::ModMenuItemKind::List,
         "World Time",
         "World",
         "SetWorldTime",
         {"Daytime (10:00)", "Nighttime (22:00)", "Dawn (06:00)", "Dusk (18:00)"}},
        {"world_time_hour",
         igi::ModMenuItemKind::Input,
         "World Hour (0-24)",
         "World",
         "SetWorldHour",
         {}},
        {"mission_time_seconds",
         igi::ModMenuItemKind::Input,
         "Mission Time (seconds)",
         "World",
         "SetMissionTime",
         {}},
        {"mission_time_preset",
         igi::ModMenuItemKind::List,
         "Mission Time Action",
         "World",
         "SetMissionTime",
         {"Reset", "+1 minute", "+5 minutes", "+15 minutes", "+30 minutes"}},
        {"mission_time_limit",
         igi::ModMenuItemKind::Input,
         "Mission Time Limit (seconds)",
         "World",
         "SetMissionTimeLimit",
         {}},
        {"restart_mission",
         igi::ModMenuItemKind::Action,
         "Restart Mission",
         "World",
         "GameRestart",
         {}},
        {"weather",
         igi::ModMenuItemKind::List,
         "Weather",
         "World",
         "SetWeather",
         {"Off", "Rain", "Snow"}},
        {"pause",
         igi::ModMenuItemKind::List,
         "Game Pause",
         "World",
         "SetPaused",
         {"Running", "Paused"}},
        {"fps",
         igi::ModMenuItemKind::List,
         "Frame Cap",
         "World",
         "FramesSet",
         {"30", "60", "120", "144", "165", "240"}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "World", "RefreshDisplay", {}},

        // Vehicles
        {"vehicle_target",
         igi::ModMenuItemKind::List,
         "Live Car / Tank Target",
         "Vehicles",
         "SelectCar",
         {"Waiting for live car list"}},
        {"vehicle_info",
         igi::ModMenuItemKind::Action,
         "Selected Car Info",
         "Vehicles",
         "CarInfo",
         {}},
        {"vehicle_reset",
         igi::ModMenuItemKind::Action,
         "Reset Selected Car",
         "Vehicles",
         "CarReset",
         {}},
        {"vehicle_controls_reset",
         igi::ModMenuItemKind::Action,
         "Reset Car Controls",
         "Vehicles",
         "CarControlsReset",
         {}},
        {"vehicle_explode",
         igi::ModMenuItemKind::Action,
         "Explode Selected Car",
         "Vehicles",
         "CarExplode",
         {}},
        {"vehicle_drive",
         igi::ModMenuItemKind::List,
         "Selected Car Drive",
         "Vehicles",
         "CarDrive",
         {"Stop", "Reverse", "Walk", "Run", "Full", "Brake"}},
        {"heli_target",
         igi::ModMenuItemKind::List,
         "Live Heli Target",
         "Vehicles",
         "SelectHeli",
         {"Waiting for live heli list"}},
        {"heli_info",
         igi::ModMenuItemKind::Action,
         "Selected Heli Info",
         "Vehicles",
         "HeliInfo",
         {}},
        {"heli_stop",
         igi::ModMenuItemKind::Action,
         "Stop Selected Heli (Stop Reverse)",
         "Vehicles",
         "HeliStop",
         {}},
        {"heli_control",
         igi::ModMenuItemKind::List,
         "Selected Heli Control",
         "Vehicles",
         "HeliDrive",
         {"Stop", "Reverse", "Forward", "Forward Fast", "Yaw Left", "Yaw Right", "Nose Down",
          "Nose Up", "Roll Left", "Roll Right"}},
        {"heli_reset",
         igi::ModMenuItemKind::Action,
         "Reset Selected Heli",
         "Vehicles",
         "HeliControlReset",
         {}},
        {"tank_drive",
         igi::ModMenuItemKind::List,
         "Selected Tank Drive",
         "Vehicles",
         "TankDrive",
         {"Stop", "Reverse", "Forward", "Full", "Brake"}},
        {"vehicle_explode_all",
         igi::ModMenuItemKind::Action,
         "Explode All Cars",
         "Vehicles",
         "CarExplodeAll",
         {}},
        {"vehicle_reset_all",
         igi::ModMenuItemKind::Action,
         "Reset All Cars",
         "Vehicles",
         "CarResetAll",
         {}},

        // AI / NPC
        {"npc_target",
         igi::ModMenuItemKind::List,
         "Live NPC Target",
         "AI / NPC",
         "SelectNpc",
         {"Waiting for live NPC list"}},
        {"npc_status",
         igi::ModMenuItemKind::Action,
         "Selected NPC Status",
         "AI / NPC",
         "NpcStatus",
         {}},
        {"npc_ensure_weapon",
         igi::ModMenuItemKind::Action,
         "Equip Selected NPC",
         "AI / NPC",
         "SoldierEnsureWeapon",
         {}},
        {"npc_reset_damage",
         igi::ModMenuItemKind::Action,
         "Reset Selected NPC Damage",
         "AI / NPC",
         "HumanResetDamage",
         {}},
        {"npc_snap_ground",
         igi::ModMenuItemKind::Action,
         "Snap Selected NPC To Ground",
         "AI / NPC",
         "HumanSnapToGround",
         {}},
        {"npc_collision",
         igi::ModMenuItemKind::List,
         "Selected NPC Collision",
         "AI / NPC",
         "HumanCollision",
         {"Enabled", "Disabled"}},
        {"npc_params",
         igi::ModMenuItemKind::Action,
         "Selected Soldier Params",
         "AI / NPC",
         "SoldierParams",
         {}},
        {"npc_stand_down",
         igi::ModMenuItemKind::Action,
         "Stand Selected Soldier Down",
         "AI / NPC",
         "SoldierStandEnter",
         {}},
        {"npc_execute",
         igi::ModMenuItemKind::Action,
         "Execute Selected Soldier",
         "AI / NPC",
         "SoldierExecute",
         {}},

        // System
        {"exit_mod", igi::ModMenuItemKind::Action, "Exit Mod", "System", "RequestExit", {}},

    };

    std::vector<igi::ModMenuItemConfig> items;
    items.reserve(std::size(definitions));
    for (const auto &definition : definitions) {
        igi::ModMenuItemConfig item;
        item.id = std::string(definition.id);
        item.kind = definition.kind;
        item.label = std::string(definition.label);
        item.category = std::string(definition.category);
        item.nativeAction = std::string(definition.action);
        item.values = definition.values;
        if (item.kind == igi::ModMenuItemKind::Input) {
            // Ammo text box: typed exactly by the user, applied on demand. The
            // teleport boxes accept signed world coordinates.
            if (item.id == "weapon_ammo") {
                item.inputValue = 120;
                item.inputMinimum = 1;
                item.inputMaximum = 9999;
                item.inputStep = 30;
            } else if (item.id.starts_with("human_") && item.id != "human_save" &&
                       item.id != "human_reload") {
                // Internal values are integers; inputDisplayDivisor makes every
                // frontend render the authored unit ("1.75", "17.5", ...). The
                // numbers here are placeholders only until prefillHumanControls
                // replaces them with what HumanPlayer_LoadParameters produced.
                item.inputDisplayDivisor =
                    (item.id == "human_move_scale" || item.id == "human_damage" ||
                     item.id == "human_fence_damage")
                        ? 100
                        : 10;
                item.inputValue = 0;
                item.inputMinimum = 0;
                item.inputMaximum = 100000;
                item.inputStep = 1;
            } else if (item.id == "world_time_hour") {
                item.inputValue = 10;
                item.inputMinimum = 0;
                item.inputMaximum = 24;
                item.inputStep = 1;
            } else if (item.id == "mission_time_seconds" || item.id == "mission_time_limit") {
                item.inputValue = 0;
                item.inputMinimum = 0;
                item.inputMaximum = 86400;
                item.inputStep = 30;
            } else {
                // IGI world units are 4096 per metre and map coordinates reach
                // tens of millions, so the old +/-1,000,000 validation range
                // rejected the live player position during the first refresh.
                item.inputValue = 0;
                item.inputMinimum = -1000000000;
                item.inputMaximum = 1000000000;
                item.inputStep = 1000;
            }
        }
        items.push_back(std::move(item));
    }
#if defined(IGI_HEAVEN_ADVANCED)
    igi::ModMenuItemConfig playerGroundState;
    playerGroundState.id = "player_ground_state";
    playerGroundState.kind = igi::ModMenuItemKind::List;
    playerGroundState.label = "Player Ground State";
    playerGroundState.category = "Player";
    playerGroundState.nativeAction = "HumanGroundState";
    playerGroundState.values = {"Ground", "Air"};
    items.push_back(std::move(playerGroundState));
    igi::ModMenuItemConfig movement;
    movement.id = "npc_movement";
    movement.kind = igi::ModMenuItemKind::List;
    movement.label = "Selected NPC Movement";
    movement.category = "AI / NPC";
    movement.nativeAction = "HumanAIMovement";
    movement.values = {"Walk", "Run", "Stop"};
    items.push_back(std::move(movement));
    igi::ModMenuItemConfig groundState;
    groundState.id = "npc_ground_state";
    groundState.kind = igi::ModMenuItemKind::List;
    groundState.label = "Selected NPC Ground State";
    groundState.category = "AI / NPC";
    groundState.nativeAction = "HumanGroundState";
    groundState.values = {"Ground", "Air"};
    items.push_back(std::move(groundState));
#endif
    return items;
}
} // namespace

namespace {
// Fill the typed teleport boxes from the live player position, converted to the
// selected unit (metres by default, or the native world units). Positions are
// world units or metres; Euler is for rotations, not positions.
bool prefillTeleportCoordinates(igi::MenuConfig &config, igi::Runtime &runtime) {
    // Seed the world-unit destination from the live player once per level; after
    // a user edit the typed values are kept and only re-displayed in the
    // selected unit.
    if (!g_teleportPosSet) {
        std::array<double, 3> position{};
        if (!igi::readPlayerPosition(runtime, position))
            return false;
        g_teleportPos = position;
    }
    const double unitScale = g_teleportUnitsMetres ? 1.0 / 4096.0 : 1.0;
    bool changed = false;
    const char *const kIds[3] = {"teleport_x", "teleport_y", "teleport_z"};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double display = g_teleportPos[axis] * unitScale;
        for (auto &item : config.items) {
            if (item.id != kIds[axis])
                continue;
            const int rounded = static_cast<int>(
                std::llround(std::clamp(display, static_cast<double>(item.inputMinimum),
                                        static_cast<double>(item.inputMaximum))));
            if (item.inputValue != rounded) {
                item.inputValue = rounded;
                changed = true;
            }
        }
    }
    return changed;
}

// Prefills the Human input boxes from the baseline HumanPlayer_LoadParameters
// produces for the current level, so the boxes show the values humanplayer.qvm
// actually authored. Runs once per level, independently of the player-position
// prefill, and falls back to the live globals when the baseline capture is
// unavailable (for example in a harness without the retail executable).
bool prefillHumanControls(igi::MenuConfig &config, igi::Runtime &runtime) {
    auto *game = IGI::GAME::GET();
    if (game && game != g_humanBaselineGame) {
        g_humanBaselineGame = game;
        g_humanBaselineCaptured = false;
        std::fill(std::begin(g_humanParamSet), std::end(g_humanParamSet), false);
    }
    if (!g_humanBaselineCaptured) {
        if (!igi::captureHumanPlayerBaseline(runtime, g_humanBaseline)) {
            // Keep the configured defaults visible instead of showing garbage;
            // the capture retries on the next refresh.
            return false;
        }
        g_humanBaselineCaptured = true;
    }
    g_humanParams = g_humanBaseline;
    const int values[kHumanParamCount] = {
        static_cast<int>(std::lround(g_humanBaseline.moveScale * 100.0F)),
        static_cast<int>(std::lround(g_humanBaseline.jumpSpeedKmh * 10.0F)),
        static_cast<int>(std::lround(g_humanBaseline.jumpUpKmh * 10.0F)),
        static_cast<int>(std::lround(g_humanBaseline.airSpeedKmh * 10.0F)),
        static_cast<int>(std::lround(g_humanBaseline.maxDamage * 100.0F)),
        static_cast<int>(std::lround(g_humanBaseline.fenceDamage * 100.0F))};
    bool changed = false;
    for (std::size_t slot = 0; slot < kHumanParamCount; ++slot) {
        for (auto &item : config.items) {
            if (item.id != kHumanParamIds[slot])
                continue;
            // Sample entries are meaningful fractions/multiples of the authored
            // value - not arbitrary steps - so the jump scales predictably:
            // "half" softens it, "x2"/"x4" push it higher/faster.
            const int base = values[slot];
            item.inputSampleValues = {0, std::max(1, base / 2), base, base * 2, base * 4};
            if (!g_humanParamSet[slot]) {
                g_humanParamValue[slot] = base;
                const int clamped = std::clamp(base, item.inputMinimum, item.inputMaximum);
                if (item.inputValue != clamped) {
                    item.inputValue = clamped;
                    changed = true;
                }
            }
        }
    }
    return changed;
}
} // namespace

bool configureModMenu(igi::MenuConfig &config) {
#if defined(IGI_HEAVEN_ADVANCED)
    config.title = "Heaven's Mod Advanced";
#else
    config.title = "Heaven's Mod";
#endif
    config.items = buildItems();
    config.renderEndFrame = [](igi::Runtime &runtime) {
        (void)igi::renderThirdPersonCrosshair(runtime);
    };
    config.frameUpdate = [](igi::Runtime &runtime) {
        igi::captureLevelStartPosition(runtime);
        (void)igi::updateFreecam(runtime);
        // End-of-frame is the safe point to unload the level: the retail scene
        // has already been flushed, so deleting the Game task cannot pull tasks
        // out from under the draw pass (the "next level" crash).
        (void)igi::processPendingLevelChange(runtime);
        (void)igi::processMissionRestart(runtime);
    };
    config.crosshairActive = [](igi::Runtime &runtime) {
        return igi::thirdPersonCrosshairActive(runtime);
    };
    config.crosshairPosition = [](igi::Runtime &runtime, int width, int height, float &x,
                                  float &y) {
        return igi::thirdPersonCrosshairScreenPosition(runtime, width, height, x, y);
    };
    config.refreshLiveItems = [](igi::MenuConfig &liveConfig, igi::Runtime &runtime) {
        const bool prefilled = prefillTeleportCoordinates(liveConfig, runtime);
        const bool humanPrefilled = prefillHumanControls(liveConfig, runtime);
        return igi::refreshLiveTargetOptions(liveConfig, runtime) || prefilled || humanPrefilled;
    };
    return true;
}

void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value) {
    const auto inRange = item < config.items.size();
    // The menu adapter owns the retail weapon table, so the selected weapon and
    // the typed ammo amount live here instead of in a helper cache.
    if (inRange && config.items[item].id == "weapon_pickup") {
        for (std::size_t index = 0; index < kWeapons.size(); ++index) {
            if (kWeapons[index].label != value)
                continue;
            g_selectedWeapon = index;
            const int ammo = g_refillAmmo > 0 ? g_refillAmmo : kWeapons[index].ammo;
            (void)igi::WeaponHelpers(runtime).pickup(kWeapons[index].id, ammo);
            igi::log(igi::LogLevel::Info, "weapon selected: " + std::string(kWeapons[index].label) +
                                              " ammo " + std::to_string(ammo));
            return;
        }
        igi::log(igi::LogLevel::Warning, "weapon selection rejected: unknown weapon label");
        return;
    }
    if (inRange && config.items[item].id == "weapon_ammo") {
        int ammo = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), ammo);
        if (parsed.ec != std::errc{} || ammo <= 0) {
            igi::log(igi::LogLevel::Warning,
                     "weapon ammo rejected: the typed amount is not a positive round count");
            return;
        }
        g_refillAmmo = ammo;
        // Apply to the weapon chosen in the weapon list, including a weapon that
        // was added this session and is not the one in hand.
        const auto &weapon = kWeapons[g_selectedWeapon];
        if (igi::WeaponHelpers(runtime).refillWeapon(weapon.id, ammo))
            igi::log(igi::LogLevel::Info, "weapon ammo applied to " + std::string(weapon.label) +
                                              " (" + std::to_string(ammo) + " rounds)");
        else
            igi::log(igi::LogLevel::Warning,
                     "weapon ammo rejected: the retail weapon task could not be filled");
        return;
    }
    if (inRange && config.items[item].id == "teleport_target") {
        // Option 1: teleport to any live object, AI, vehicle or landmark.
        const auto selected = igi::parseTargetId(value);
        const bool placed = selected >= 0 && igi::teleportPlayerToTarget(runtime, selected);
        igi::log(placed ? igi::LogLevel::Info : igi::LogLevel::Warning,
                 placed ? "player teleported to the selected target"
                        : "teleport rejected: pick a live target first");
        return;
    }
    if (inRange && config.items[item].id == "teleport_units") {
        // The stored position stays in world units; only the display converts.
        g_teleportUnitsMetres = value == "Metres";
        (void)prefillTeleportCoordinates(const_cast<igi::MenuConfig &>(config), runtime);
        igi::log(igi::LogLevel::Info,
                 std::string("teleport position units: ") +
                     (g_teleportUnitsMetres ? "metres (4096 units each)"
                                            : "world units (the game's native unit)"));
        return;
    }
    if (inRange &&
        (config.items[item].id == "teleport_x" || config.items[item].id == "teleport_y" ||
         config.items[item].id == "teleport_z")) {
        const std::string text(value);
        char *end = nullptr;
        const double coordinate = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') {
            igi::log(igi::LogLevel::Warning,
                     "teleport coordinate rejected: '" + text + "' is not a number");
            return;
        }
        // The box is in the selected unit; store world units so the axes can
        // never mix units.
        const double worldUnits = g_teleportUnitsMetres ? coordinate * 4096.0 : coordinate;
        if (config.items[item].id == "teleport_x")
            g_teleportPos[0] = worldUnits;
        else if (config.items[item].id == "teleport_y")
            g_teleportPos[1] = worldUnits;
        else
            g_teleportPos[2] = worldUnits;
        g_teleportPosSet = true;
        igi::log(igi::LogLevel::Info,
                 "teleport " +
                     std::string(config.items[item].id == "teleport_x"   ? "X"
                                 : config.items[item].id == "teleport_y" ? "Y"
                                                                         : "Z") +
                     " set to " + std::to_string(coordinate) + " " +
                     (g_teleportUnitsMetres ? "metres" : "world units"));
        return;
    }
    if (inRange && config.items[item].id == "teleport_apply") {
        // Option 2: typed XYZ. Metres convert to the game's world units before
        // the move; the ground probe keeps the body above the terrain.
        const bool placed = igi::teleportPlayerToPosition(runtime, g_teleportPos);
        igi::log(placed ? igi::LogLevel::Info : igi::LogLevel::Warning,
                 placed ? "player moved to the typed position"
                        : "teleport rejected: the live player was unavailable");
        return;
    }
    if (inRange) {
        for (std::size_t slot = 0; slot < kHumanParamCount; ++slot) {
            if (config.items[item].id != kHumanParamIds[slot])
                continue;
            // The QVM ListBox sends the DISPLAYED label ("17.5"), not the raw
            // internal integer. Convert it back by the item's display divisor,
            // or picking "35" wrote 3.5 km/h instead of 35.
            double display = 0.0;
            const std::string text(value);
            {
                char *end = nullptr;
                display = std::strtod(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0') {
                    igi::log(igi::LogLevel::Warning,
                             "human physics rejected: '" + text + "' is not a number");
                    return;
                }
            }
            const auto self = std::find_if(
                config.items.begin(), config.items.end(),
                [slot](const auto &candidate) { return candidate.id == kHumanParamIds[slot]; });
            const int divisor = self != config.items.end() && self->inputDisplayDivisor > 0
                                    ? self->inputDisplayDivisor
                                    : 1;
            const int typed = static_cast<int>(std::llround(display * divisor));
            if (typed < 0) {
                igi::log(igi::LogLevel::Warning,
                         "human physics rejected: enter a non-negative value");
                return;
            }
            g_humanParamValue[slot] = typed;
            g_humanParamSet[slot] = true;
            // Apply immediately: the retail globals take effect on this value, so
            // selecting a jump speed/height needs no second Save click.
            applyHumanPhysics(runtime);
            return;
        }
    }
    if (inRange && config.items[item].id == "human_save") {
        // Explicit re-apply of the displayed values (also repairs a partial or
        // failed earlier write).
        applyHumanPhysics(runtime);
        return;
    }
    if (inRange && config.items[item].id == "human_reload") {
        if (igi::reloadLiveHumanPlayerParams(runtime)) {
            // Read = load the qvm and push its authored values straight into the
            // input boxes on both frontends: reset the per-level capture and
            // prefill now instead of waiting for the next frame refresh.
            std::fill(std::begin(g_humanParamSet), std::end(g_humanParamSet), false);
            g_humanBaselineCaptured = false;
            g_humanBaselineGame = nullptr;
            if (prefillHumanControls(const_cast<igi::MenuConfig &>(config), runtime))
                igi::log(igi::LogLevel::Info,
                         "human physics: the humanplayer.qvm values are back in the inputs");
        } else {
            igi::log(igi::LogLevel::Warning, "human physics read was rejected");
        }
        return;
    }
    if (inRange && config.items[item].id == "npc_ensure_weapon") {
        auto *npc = igi::selectedNpc(runtime);
        if (!npc) {
            igi::log(igi::LogLevel::Warning, "NPC equip rejected: select a live NPC target first");
            return;
        }
        const auto &weapon = kWeapons[g_selectedWeapon];
        const int ammo = g_refillAmmo > 0 ? g_refillAmmo : weapon.ammo;
        if (igi::NpcHelpers(runtime).ensureWeapon(npc, weapon.id, ammo))
            igi::log(igi::LogLevel::Info, "NPC equipped with " + std::string(weapon.label) +
                                              " and " + std::to_string(ammo) + " rounds");
        else
            igi::log(igi::LogLevel::Warning, "NPC equip rejected by the retail soldier path");
        return;
    }
    if (inRange && config.items[item].id == "weapon_refill") {
        const auto &weapon = kWeapons[g_selectedWeapon];
        const int ammo = g_refillAmmo > 0 ? g_refillAmmo : weapon.ammo;
        const bool granted = igi::WeaponHelpers(runtime).pickup(weapon.id, ammo);
        igi::log(granted ? igi::LogLevel::Info : igi::LogLevel::Warning,
                 "weapon refill: " + std::string(weapon.label) + " with " + std::to_string(ammo) +
                     " rounds " + (granted ? "applied" : "rejected"));
        return;
    }
    if (item < config.items.size() && config.items[item].id == "weapon_all") {
        auto weapons = igi::WeaponHelpers(runtime);
        std::size_t granted = 0;
        const int ammo = g_refillAmmo > 0 ? g_refillAmmo : 120;
        for (const auto &weapon : kWeapons) {
            if (weapons.pickup(weapon.id, ammo) && weapon.ammo > 0 &&
                !weapons.refillWeapon(weapon.id, ammo)) {
                continue;
            }
            ++granted;
        }
        igi::log(granted > 0 ? igi::LogLevel::Info : igi::LogLevel::Warning,
                 "equip all weapons: granted " + std::to_string(granted) + " of " +
                     std::to_string(kWeapons.size()) + " retail weapons");
        return;
    }
    if (item < config.items.size() && config.items[item].id == "weapon_pickup") {
        for (const auto &weapon : kWeapons) {
            if (weapon.label == value) {
                (void)igi::WeaponHelpers(runtime).pickup(weapon.id, weapon.ammo);
                return;
            }
        }
        return;
    }
    igi::executeMenuNativeAction(config, runtime, item, value);
}
