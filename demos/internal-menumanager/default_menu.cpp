#include "igi_sdk/ui/mod_menu.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"

bool configureModMenu(igi::MenuConfig& config) {
    config.items = {
        {"weather", igi::ModMenuItemKind::List, "Rain", "IGI_SDK_SetWeather(this.value)", {"off", "on"}},
        {"ammo", igi::ModMenuItemKind::Action, "Enable Unlimited Ammo", "IGI_SDK_EnableUnlimitedAmmo()", {}},
        {"frames", igi::ModMenuItemKind::Toggle, "Frame Limit", "IGI_SDK_ToggleFrameLimit()", {"true"}, true},
        {"music", igi::ModMenuItemKind::List, "Music Volume", "IGI_SDK_SetMusicVolume(this.value)", {"mute", "low", "normal"}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "IGI_SDK_RefreshDisplay()", {}},
        {"diagnostics", igi::ModMenuItemKind::Action, "Log Game State", "IGI_SDK_LogGameState()", {}},
    };
    return true;
}

void runModMenuAction(const igi::MenuConfig& config, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    igi::executeMenuNativeAction(config, runtime, item, value);
}
