#include "config/menu_config.hpp"
#include "natives/mvp_natives.hpp"
#include "runtime/runtime.hpp"
#include "ui/mod_menu.hpp"
#include "../helpers/menu_helpers.hpp"

#include <string_view>

bool configureModMenu(igi::MenuConfig &config) {
    config.title = "My Custom Mod Menu";
    config.items = {
        {"ammo", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}},
        {"health", igi::ModMenuItemKind::Action, "Restore Health & Clear Flash", "", {}},
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "FPS Limit", "", {"30", "60", "120"}},
        {"view", igi::ModMenuItemKind::List, "Camera View", "", {"First Person", "Third Person"}},
        {"greeting", igi::ModMenuItemKind::Action, "Show Greeting", "", {}},
    };
    return true;
}

void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value) {
    if (item >= config.items.size())
        return;
    const auto &id = config.items[item].id;

    if (id == "ammo") {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "Unlimited Ammo Enabled!");
    } else if (id == "health") {
        auto *player = IGI::GAME::GET_PLAYER(0);
        if (player) {
            auto *hp = reinterpret_cast<IGI::HumanPlayer *>(player);
            IGI::PLAYER::FADE_HIT_FLASH(hp);
            IGI::PLAYER::FADE_FLASH(hp);
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "Health & Flash Restored!");
        }
    } else if (id == "weather") {
        igi::setWeather(runtime, value);
    } else if (id == "fps") {
        igi::setFps(runtime, value);
    } else if (id == "view") {
        igi::setThirdPersonView(value == "Third Person");
    } else if (id == "greeting") {
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "Welcome to Project I.G.I. Modding!");
    }
}
