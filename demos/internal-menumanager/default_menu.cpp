#include "igi_sdk/ui/mod_menu.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"

bool configureModMenu(igi::MenuConfig& config) {
    config.items = {
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "FPS", "", {"30", "60", "120"}},
        {"ammo", igi::ModMenuItemKind::Action, "Enable Unlimited Ammo", "", {}},
        {"music", igi::ModMenuItemKind::List, "Music Volume", "", {"Normal", "Low", "Mute"}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "", {}},
        {"diagnostics", igi::ModMenuItemKind::Action, "Log Game State", "", {}},
    };
    return true;
}

void runModMenuAction(const igi::MenuConfig& config, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    igi::executeMenuNativeAction(config, runtime, item, value);
}
