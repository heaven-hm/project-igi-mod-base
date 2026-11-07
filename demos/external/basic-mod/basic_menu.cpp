#include "basic_menu.hpp"
#include "ui/mod_menu.hpp"
#include "natives/mvp_natives.hpp"
#include "../../internal/helpers/menu_helpers.hpp"

bool configureBasicMenu(igi::MenuConfig &config) {
    config.title = "Basic Mod Menu";
    config.items = {
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain"}},
        {"fps", igi::ModMenuItemKind::List, "FPS", "", {"30", "60", "120"}},
        {"music", igi::ModMenuItemKind::List, "Music volume", "", {"Normal", "Low", "Mute"}},
        {"ammo", igi::ModMenuItemKind::Action, "Unlimited ammo", "", {}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "", {}},
        {"diagnostics", igi::ModMenuItemKind::Action, "Log Game State", "", {}}};
    return true;
}

void basicMenuAction(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                     std::string_view value) {
    igi::executeMenuNativeAction(config, runtime, item, value);
}

bool configureModMenu(igi::MenuConfig &config) {
    return configureBasicMenu(config);
}
void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value) {
    basicMenuAction(config, runtime, item, value);
}
