#include "../simple-mod/simple_menu.hpp"
#include "natives/mvp_natives.hpp"
#include "logging/logging.hpp"
#include "ui/mod_menu.hpp"
#include "../helpers/menu_helpers.hpp"

#include <string>

bool configureModMenu(igi::MenuConfig &config) {
    if (!configureSimpleMenu(config))
        return false;
    config.title = "Advanced Mod Menu";
    config.y = 55;
    config.height = 390;
    config.items.push_back({"ammo", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}});
    config.items.push_back(
        {"music", igi::ModMenuItemKind::List, "Music Volume", "", {"Normal", "Low", "Mute"}});
    config.items.push_back({"player_info", igi::ModMenuItemKind::Action, "Player State", "", {}});
    config.items.push_back({"display", igi::ModMenuItemKind::Action, "Refresh Display", "", {}});
    return true;
}

void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value) {
    if (item >= config.items.size())
        return;
    const auto &id = config.items[item].id;
    if (id == "player_info") {
        if (!igi::requireLiveGameThread(runtime))
            return;
        auto *player = reinterpret_cast<IGI::Human *>(IGI::GAME::GET_PLAYER(0));
        if (!player) {
            igi::log(igi::LogLevel::Warning, "advanced menu: player unavailable");
            return;
        }
        const std::string state = "Player: team " + std::to_string(IGI::HUMAN::GET_TEAM(player)) +
                                  ", " + (IGI::HUMAN::IS_DEAD(player) ? "dead" : "alive");
        igi::log(igi::LogLevel::Info, state);
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, state.c_str());
    } else if (id == "ammo" || id == "music" || id == "display") {
        igi::executeMenuNativeAction(config, runtime, item, value);
    } else {
        simpleMenuAction(config, runtime, item, value);
    }
}
