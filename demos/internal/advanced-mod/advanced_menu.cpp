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

    for (auto &item : config.items) {
        if (item.id == "weather" || item.id == "level") {
            item.category = "World";
        } else if (item.id == "fps" || item.id == "perf" || item.id == "display") {
            item.category = "Display";
        } else if (item.id == "weapon" || item.id == "pickup" || item.id == "ammo") {
            item.category = "Weapons";
        } else if (item.id == "view" || item.id == "player_info") {
            item.category = "Player";
        } else if (item.id == "sfx" || item.id == "music") {
            item.category = "Audio";
        } else if (item.id == "flash") {
            item.category = "Display";
        }
    }

    config.items.push_back({"ammo", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}});
    config.items.back().category = "Weapons";
    config.items.push_back(
        {"music", igi::ModMenuItemKind::List, "Music Volume", "", {"Normal", "Low", "Mute"}});
    config.items.back().category = "Audio";
    config.items.push_back({"player_info", igi::ModMenuItemKind::Action, "Player State", "", {}});
    config.items.back().category = "Player";
    config.items.push_back({"display", igi::ModMenuItemKind::Action, "Refresh Display", "", {}});
    config.items.back().category = "Display";
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
