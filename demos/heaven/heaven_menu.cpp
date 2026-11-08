#include "config/menu_config.hpp"
#include "helpers/menu_helpers.hpp"

#include <array>
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

constexpr std::array kLevelLabels = {"Level 1",  "Level 2",  "Level 3",  "Level 4", "Level 5",
                                     "Level 6",  "Level 7",  "Level 8",  "Level 9", "Level 10",
                                     "Level 11", "Level 12", "Level 13", "Level 14"};

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
        {"weapon_pickup", igi::ModMenuItemKind::List, "Equip Weapon", "Weapons", "PickUpWeapon",
         weaponValues},
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
        {"weapon_flash",
         igi::ModMenuItemKind::Action,
         "Clear Hit Flash",
         "Weapons",
         "RestoreVisualFeedback",
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
        {"player_collision",
         igi::ModMenuItemKind::List,
         "Player Collision",
         "Player",
         "HumanCollision",
         {"Enabled", "Disabled"}},
        {"player_status",
         igi::ModMenuItemKind::Action,
         "Player Status",
         "Player",
         "LogGameState",
         {}},

        // Camera
        {"view",
         igi::ModMenuItemKind::List,
         "Camera Mode",
         "Camera",
         "ViewCam",
         {"First Person", "Third Person"}},
        {"camera_info", igi::ModMenuItemKind::Action, "Camera Info", "Camera", "CameraInfo", {}},
        {"look",
         igi::ModMenuItemKind::List,
         "Look Control",
         "Camera",
         "HumanLook",
         {"Enabled", "Disabled"}},

        // World
        {"level", igi::ModMenuItemKind::List, "Mission", "World", "SetMission", levelValues},
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
        {"fps", igi::ModMenuItemKind::List, "Frame Cap", "World", "FramesSet", {"30", "60", "120"}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "World", "RefreshDisplay", {}},
        {"world_level_tick", igi::ModMenuItemKind::Action, "Level Tick", "World", "LevelTick", {}},
        {"world_flow", igi::ModMenuItemKind::Action, "Flow Counters", "World", "FlowCounters", {}},
        {"world_events", igi::ModMenuItemKind::Action, "Game Events", "World", "GameEvents", {}},

        // Vehicles
        {"vehicle_target",
         igi::ModMenuItemKind::List,
         "Live Car Target",
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
         {"Stop", "Reverse", "Quarter", "Half", "Full", "Brake"}},
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

        // Audio
        {"music",
         igi::ModMenuItemKind::List,
         "Music Volume",
         "Audio",
         "MusicVolume",
         {"Mute", "Low", "Full"}},
        {"sfx",
         igi::ModMenuItemKind::List,
         "SFX Volume",
         "Audio",
         "SfxVolume",
         {"Mute", "Low", "Full"}},
        {"audio_info", igi::ModMenuItemKind::Action, "Audio Info", "Audio", "AudioInfo", {}},
        {"audio_rate",
         igi::ModMenuItemKind::List,
         "Audio Rate",
         "Audio",
         "AudioSetRate",
         {"22050", "44100"}},
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
        items.push_back(std::move(item));
    }
    return items;
}
} // namespace

bool configureModMenu(igi::MenuConfig &config) {
    config.title = "Heaven's Mod";
    config.items = buildItems();
    config.refreshLiveItems = [](igi::MenuConfig &liveConfig, igi::Runtime &runtime) {
        return igi::refreshLiveTargetOptions(liveConfig, runtime);
    };
    return true;
}

void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value) {
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
