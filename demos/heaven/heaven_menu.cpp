#include "config/menu_config.hpp"
#include "helpers/menu_helpers.hpp"

#include <string_view>

bool configureModMenu(igi::MenuConfig &config) {
    config.title = "Heaven's Mod";
    config.items = {
        {"ammo", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}},
        {"weapon_cycle", igi::ModMenuItemKind::Action, "Cycle Weapon", "", {}},
        {"view", igi::ModMenuItemKind::List, "Camera Mode", "", {"First Person", "Third Person"}},
        {"input", igi::ModMenuItemKind::List, "Player Input", "", {"Enabled", "Disabled"}},
        {"look", igi::ModMenuItemKind::List, "Player Look", "", {"Enabled", "Disabled"}},
        {"pause", igi::ModMenuItemKind::List, "Game Pause", "", {"Running", "Paused"}},
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "Frame Cap", "", {"30", "60", "120"}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "", {}},
        {"restart_mission", igi::ModMenuItemKind::Action, "Restart Mission", "", {}},
        {"level",
         igi::ModMenuItemKind::List,
         "Mission",
         "",
         {"Level 1", "Level 2", "Level 3", "Level 4", "Level 5", "Level 6", "Level 7", "Level 8",
          "Level 9", "Level 10", "Level 11", "Level 12", "Level 13", "Level 14"}},
    };
    config.items[0].category = "Weapons";
    config.items[1].category = "Weapons";
    config.items[2].category = "Camera";
    config.items[3].category = "Player";
    config.items[4].category = "Player";
    config.items[5].category = "World";
    config.items[6].category = "World";
    config.items[7].category = "World";
    config.items[8].category = "World";
    config.items[9].category = "World";
    config.items[10].category = "World";
    return true;
}

void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value) {
    igi::executeMenuNativeAction(config, runtime, item, value);
}
