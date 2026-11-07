#include "basic_menu.hpp"
#include "igi_sdk/ui/mod_menu.hpp"
#include "igi_sdk/IGI.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"

bool configureBasicMenu(igi::MenuConfig& config) {
    config.items = {
        {"fps", igi::ModMenuItemKind::List, "FPS", "", {"30", "60", "120"}},
        {"music", igi::ModMenuItemKind::List, "Music volume", "", {"Mute", "Low", "Normal"}},
        {"ammo", igi::ModMenuItemKind::Action, "Unlimited ammo", "", {}}
    };
    return true;
}

void basicMenuAction(igi::Runtime& runtime, std::size_t item, std::string_view value) {
    if (!igi::pauseForMenu(runtime)) return;
    if (item == 0) IGI::MISC::FRAMES_SET(value == "30" ? 30 : value == "60" ? 60 : 120);
    if (item == 1) {
        const float volume = value == "Mute" ? 0.0f : value == "Low" ? 0.35f : 1.0f;
        IGI::AUDIO::MUSIC_SET_VOLUME(volume, volume);
    }
    if (item == 2) IGI::PLAYER::SET_UNLIMITED_AMMO();
}

bool configureModMenu(igi::MenuConfig& config) { return configureBasicMenu(config); }
void runModMenuAction(const igi::MenuConfig&, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    basicMenuAction(runtime, item, value);
}
