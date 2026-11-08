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
} // namespace

bool configureModMenu(igi::MenuConfig &config) {
    config.title = "Heaven's Mod";
    config.items = {
        {"ammo", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}},
        {"weapon_pickup",
         igi::ModMenuItemKind::List,
         "Equip Weapon",
         "",
         {"Glock 17", "MP5SD", "Uzi", "M16A2", "AK-47", "Dragunov SVD", "Minimi", "SPAS-12",
          "Binoculars", "Medipack"}},
        {"weapon_cycle", igi::ModMenuItemKind::Action, "Cycle Weapon", "", {}},
        {"view", igi::ModMenuItemKind::List, "Camera Mode", "", {"First Person", "Third Person"}},
        {"player_reset_damage", igi::ModMenuItemKind::Action, "Reset Player Damage", "", {}},
        {"pause", igi::ModMenuItemKind::List, "Game Pause", "", {"Running", "Paused"}},
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "Frame Cap", "", {"30", "60", "120"}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "", {}},
        {"restart_mission", igi::ModMenuItemKind::Action, "Restart Mission", "", {}},
        {"vehicle_reset", igi::ModMenuItemKind::Action, "Reset First Car", "", {}},
        {"vehicle_explode", igi::ModMenuItemKind::Action, "Explode First Car", "", {}},
        {"npc_ensure_weapon", igi::ModMenuItemKind::Action, "Equip First NPC", "", {}},
        {"npc_reset_damage", igi::ModMenuItemKind::Action, "Reset First NPC Damage", "", {}},
        {"level",
         igi::ModMenuItemKind::List,
         "Mission",
         "",
         {"Level 1", "Level 2", "Level 3", "Level 4", "Level 5", "Level 6", "Level 7", "Level 8",
          "Level 9", "Level 10", "Level 11", "Level 12", "Level 13", "Level 14"}},
        {"player_snap_ground", igi::ModMenuItemKind::Action, "Snap Player To Ground", "", {}},
        {"player_collision",
         igi::ModMenuItemKind::List,
         "Player Collision",
         "",
         {"Enabled", "Disabled"}},
    };
    config.items[0].category = "Weapons";
    config.items[1].category = "Weapons";
    config.items[2].category = "Weapons";
    config.items[3].category = "Camera";
    config.items[4].category = "Player";
    config.items[5].category = "World";
    config.items[6].category = "World";
    config.items[7].category = "World";
    config.items[8].category = "World";
    config.items[9].category = "World";
    config.items[10].category = "Vehicles";
    config.items[11].category = "Vehicles";
    config.items[12].category = "AI / NPC";
    config.items[13].category = "AI / NPC";
    config.items[14].category = "World";
    config.items[15].category = "Player";
    config.items[16].category = "Player";
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
