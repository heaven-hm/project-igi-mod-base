#include "../simple-mod/simple_menu.hpp"
#include "config/menu_config.hpp"
#include "helpers/menu_helpers.hpp"
#include "logging/logging.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/mvp_natives.hpp"
#include "ui/mod_menu.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>

namespace {

// The DefineHumanPlayerGeneral parameters exposed by the Human section. Display
// units match the internal Heaven menu: move/damage x100, speeds x10.
enum HumanParamSlot {
    kHumanMoveScale,
    kHumanJumpSpeed,
    kHumanJumpHeight,
    kHumanAirSpeed,
    kHumanDamage,
    kHumanFenceDamage,
    kHumanParamCount
};

constexpr std::array<const char *, kHumanParamCount> kHumanParamIds = {
    "human_move_scale", "human_jump_speed", "human_jump_height",
    "human_air_speed",  "human_damage",     "human_fence_damage",
};

// Internal and external Advanced menus share this exact file, so both expose the
// same option set; only the UI (QVM rows versus ImGui widgets) differs.
int g_humanParamValue[kHumanParamCount] = {100, 65, 50, 65, 300, 100};
bool g_humanParamSet[kHumanParamCount] = {};

// Last live block returned by the qvm. Save patches these fields so the live
// km/h conversion factor is preserved.
igi::HumanPlayerQvmParams g_humanParams{};

void addHumanControls(igi::MenuConfig &config) {
    struct Def {
        const char *id;
        const char *label;
        int value;
        int minimum;
        int maximum;
    };
    constexpr Def defs[kHumanParamCount] = {
        {"human_move_scale", "Move Speed Scale (x100)", 100, 10, 1000},
        {"human_jump_speed", "Jump Speed (km/h x10)", 65, 0, 1000},
        {"human_jump_height", "Jump Height (km/h x10)", 50, 0, 1000},
        {"human_air_speed", "Air Speed (km/h x10)", 65, 0, 1000},
        {"human_damage", "Damage Scale (x100)", 300, 0, 2000},
        {"human_fence_damage", "Fence Damage (x100)", 100, 0, 2000},
    };
    for (std::size_t slot = 0; slot < kHumanParamCount; ++slot) {
        igi::ModMenuItemConfig item;
        item.id = defs[slot].id;
        item.kind = igi::ModMenuItemKind::Input;
        item.label = defs[slot].label;
        item.category = "Human";
        item.nativeAction = "HumanParam";
        item.inputValue = defs[slot].value;
        item.inputMinimum = defs[slot].minimum;
        item.inputMaximum = defs[slot].maximum;
        item.inputStep = 25;
        config.items.push_back(std::move(item));
    }
    igi::ModMenuItemConfig save;
    save.id = "human_save";
    save.kind = igi::ModMenuItemKind::Action;
    save.label = "Save Player Physics";
    save.category = "Human";
    save.nativeAction = "HumanSaveParams";
    config.items.push_back(std::move(save));
    igi::ModMenuItemConfig reload;
    reload.id = "human_reload";
    reload.kind = igi::ModMenuItemKind::Action;
    reload.label = "Reload From humanplayer.qvm";
    reload.category = "Human";
    reload.nativeAction = "HumanReloadParams";
    config.items.push_back(std::move(reload));
    igi::ModMenuItemConfig info;
    info.id = "human_info";
    info.kind = igi::ModMenuItemKind::Action;
    info.label = "Read Humanplayer Values";
    info.category = "Human";
    info.nativeAction = "HumanParamsInfo";
    config.items.push_back(std::move(info));
}

// Prefill the boxes from the live BSS block once per level, so the menu shows the
// values humanplayer.qvm actually authored.
void refreshHumanControls(igi::MenuConfig &config, igi::Runtime &runtime) {
    static void *lastGame = nullptr;
    auto *game = IGI::GAME::GET();
    if (game && game != lastGame) {
        lastGame = game;
        std::fill(std::begin(g_humanParamSet), std::end(g_humanParamSet), false);
    }
    const auto params = igi::readLiveHumanPlayerParams(runtime);
    if (!params || params->moveScale <= 0.0F)
        return;
    g_humanParams = *params;
    const int values[kHumanParamCount] = {
        static_cast<int>(std::lround(params->moveScale * 100.0F)),
        static_cast<int>(std::lround(params->jumpSpeedKmh * 10.0F)),
        static_cast<int>(std::lround(params->jumpUpKmh * 10.0F)),
        static_cast<int>(std::lround(params->airSpeedKmh * 10.0F)),
        static_cast<int>(std::lround(params->maxDamage * 100.0F)),
        static_cast<int>(std::lround(params->fenceDamage * 100.0F)),
    };
    for (std::size_t slot = 0; slot < kHumanParamCount; ++slot) {
        if (g_humanParamSet[slot])
            continue;
        g_humanParamValue[slot] = values[slot];
        for (auto &item : config.items) {
            if (item.id != kHumanParamIds[slot])
                continue;
            item.inputValue = std::clamp(values[slot], item.inputMinimum, item.inputMaximum);
        }
    }
}

bool handleHumanAction(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                       std::string_view value) {
    if (item >= config.items.size())
        return false;
    const auto &id = config.items[item].id;
    for (std::size_t slot = 0; slot < kHumanParamCount; ++slot) {
        if (id != kHumanParamIds[slot])
            continue;
        int typed = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), typed);
        if (parsed.ec != std::errc{} || typed < 0) {
            igi::log(igi::LogLevel::Warning,
                     "human physics rejected: enter a non-negative display value");
            return true;
        }
        g_humanParamValue[slot] = typed;
        g_humanParamSet[slot] = true;
        igi::log(igi::LogLevel::Info, "human physics: " + std::string(id) + " set to " +
                                          std::to_string(typed) + " (press Save)");
        return true;
    }
    if (id == "human_save") {
        g_humanParams.moveScale = static_cast<float>(g_humanParamValue[kHumanMoveScale]) / 100.0F;
        g_humanParams.jumpSpeedKmh = static_cast<float>(g_humanParamValue[kHumanJumpSpeed]) / 10.0F;
        g_humanParams.jumpUpKmh = static_cast<float>(g_humanParamValue[kHumanJumpHeight]) / 10.0F;
        g_humanParams.airSpeedKmh = static_cast<float>(g_humanParamValue[kHumanAirSpeed]) / 10.0F;
        g_humanParams.maxDamage = static_cast<float>(g_humanParamValue[kHumanDamage]) / 100.0F;
        g_humanParams.fenceDamage =
            static_cast<float>(g_humanParamValue[kHumanFenceDamage]) / 100.0F;
        const bool saved = igi::saveLiveHumanPlayerParams(runtime, g_humanParams);
        igi::log(saved ? igi::LogLevel::Info : igi::LogLevel::Warning,
                 saved ? "human physics saved to the live retail globals"
                       : "human physics rejected: the live globals were unavailable");
        return true;
    }
    if (id == "human_reload") {
        if (igi::reloadLiveHumanPlayerParams(runtime)) {
            std::fill(std::begin(g_humanParamSet), std::end(g_humanParamSet), false);
            igi::log(igi::LogLevel::Info, "human physics reloaded from humanplayer.qvm");
        } else {
            igi::log(igi::LogLevel::Warning, "human physics reload was rejected");
        }
        return true;
    }
    if (id == "human_info") {
        igi::executeMenuNativeAction(config, runtime, item, value);
        return true;
    }
    return false;
}

} // namespace

bool configureModMenu(igi::MenuConfig &config) {
    if (!configureSimpleMenu(config))
        return false;
    config.title = "Advanced Mod Menu";
    config.y = 55;
    config.height = 390;

    for (auto &item : config.items) {
        if (item.id == "weather" || item.id == "level") {
            item.category = item.id == "weather" ? "World/Environment" : "World/Levels";
        } else if (item.id == "fps" || item.id == "perf" || item.id == "display") {
            item.category = item.id == "fps" ? "Display/Performance" : "Display/Utilities";
        } else if (item.id == "weapon" || item.id == "pickup" || item.id == "ammo") {
            item.category = "Weapons/Loadout";
        } else if (item.id == "view" || item.id == "player_info") {
            item.category = "Player/Camera";
        } else if (item.id == "sfx" || item.id == "music") {
            item.category = "Audio/Volume";
        } else if (item.id == "flash") {
            item.category = "Display/Utilities";
        }
    }

    config.items.push_back({"ammo", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}});
    config.items.back().category = "Weapons/Loadout";
    config.items.push_back(
        {"music", igi::ModMenuItemKind::List, "Music Volume", "", {"Normal", "Low", "Mute"}});
    config.items.back().category = "Audio/Volume";
    config.items.push_back({"player_info", igi::ModMenuItemKind::Action, "Player State", "", {}});
    config.items.back().category = "Player/Camera";
    config.items.push_back({"display", igi::ModMenuItemKind::Action, "Refresh Display", "", {}});
    config.items.back().category = "Display/Utilities";
    // Same Human section as the internal menu, generated from this shared table.
    addHumanControls(config);
    const auto inherited = config.refreshLiveItems;
    config.refreshLiveItems = [inherited](igi::MenuConfig &live, igi::Runtime &runtime) {
        const bool changed = inherited ? inherited(live, runtime) : false;
        refreshHumanControls(live, runtime);
        return changed;
    };
    return true;
}

void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value) {
    if (item >= config.items.size())
        return;
    if (handleHumanAction(config, runtime, item, value))
        return;
    const auto &id = config.items[item].id;
    if (id == "player_info") {
        (void)igi::logPlayerState(runtime);
    } else if (id == "ammo" || id == "music" || id == "display") {
        igi::executeMenuNativeAction(config, runtime, item, value);
    } else {
        simpleMenuAction(config, runtime, item, value);
    }
}
