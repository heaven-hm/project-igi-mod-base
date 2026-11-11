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

// The DefineHumanPlayerGeneral parameters, shown in the Player section like the
// Heaven menu. Internal values are integers; inputDisplayDivisor renders the
// authored unit ("1.75", "17.5 km/h"). Display units: move/damage x100,
// speeds x10. Slot order must match kHumanParamIds.
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

int g_humanParamValue[kHumanParamCount] = {};
bool g_humanParamSet[kHumanParamCount] = {};

// The values HumanPlayer_LoadParameters produced for the current level; the
// input boxes prefill from this baseline, never from hard-coded guesses.
igi::HumanPlayerQvmParams g_humanBaseline{};
bool g_humanBaselineCaptured = false;
void *g_humanBaselineGame = nullptr;
// Last live block; Apply patches these fields so the live km/h conversion
// factor is preserved.
igi::HumanPlayerQvmParams g_humanParams{};

void addHumanControls(igi::MenuConfig &config) {
    struct Def {
        const char *id;
        const char *label;
        int divisor;
    };
    constexpr Def defs[kHumanParamCount] = {
        {"human_move_scale", "Move Speed Scale", 100},
        {"human_jump_speed", "Jump Speed (km/h)", 10},
        {"human_jump_height", "Jump Height (km/h)", 10},
        {"human_air_speed", "Air Speed (km/h)", 10},
        {"human_damage", "Damage Scale", 100},
        {"human_fence_damage", "Fence Damage", 100},
    };
    for (std::size_t slot = 0; slot < kHumanParamCount; ++slot) {
        igi::ModMenuItemConfig item;
        item.id = defs[slot].id;
        item.kind = igi::ModMenuItemKind::Input;
        item.label = defs[slot].label;
        item.category = "Player";
        item.nativeAction = "HumanParam";
        item.inputDisplayDivisor = defs[slot].divisor;
        item.inputValue = 0;
        item.inputMinimum = 0;
        item.inputMaximum = 100000;
        item.inputStep = 1;
        config.items.push_back(std::move(item));
    }
    igi::ModMenuItemConfig save;
    save.id = "human_save";
    save.kind = igi::ModMenuItemKind::Action;
    save.label = "Save Player Physics";
    save.category = "Player";
    save.nativeAction = "HumanSaveParams";
    config.items.push_back(std::move(save));
    igi::ModMenuItemConfig reload;
    reload.id = "human_reload";
    reload.kind = igi::ModMenuItemKind::Action;
    reload.label = "Reload From humanplayer.qvm";
    reload.category = "Player";
    reload.nativeAction = "HumanReloadParams";
    config.items.push_back(std::move(reload));
    igi::ModMenuItemConfig info;
    info.id = "human_info";
    info.kind = igi::ModMenuItemKind::Action;
    info.label = "Read Humanplayer Values";
    info.category = "Player";
    info.nativeAction = "HumanParamsInfo";
    config.items.push_back(std::move(info));
}

// Prefill from the baseline HumanPlayer_LoadParameters produces for the current
// level, and offer samples as meaningful fractions/multiples of it.
void prefillHumanControls(igi::MenuConfig &config, igi::Runtime &runtime) {
    auto *game = IGI::GAME::GET();
    if (game && game != g_humanBaselineGame) {
        g_humanBaselineGame = game;
        g_humanBaselineCaptured = false;
        std::fill(std::begin(g_humanParamSet), std::end(g_humanParamSet), false);
    }
    if (!g_humanBaselineCaptured) {
        if (!igi::captureHumanPlayerBaseline(runtime, g_humanBaseline))
            return; // retries on the next refresh
        g_humanBaselineCaptured = true;
    }
    g_humanParams = g_humanBaseline;
    const int values[kHumanParamCount] = {
        static_cast<int>(std::lround(g_humanBaseline.moveScale * 100.0F)),
        static_cast<int>(std::lround(g_humanBaseline.jumpSpeedKmh * 10.0F)),
        static_cast<int>(std::lround(g_humanBaseline.jumpUpKmh * 10.0F)),
        static_cast<int>(std::lround(g_humanBaseline.airSpeedKmh * 10.0F)),
        static_cast<int>(std::lround(g_humanBaseline.maxDamage * 100.0F)),
        static_cast<int>(std::lround(g_humanBaseline.fenceDamage * 100.0F)),
    };
    for (std::size_t slot = 0; slot < kHumanParamCount; ++slot) {
        for (auto &item : config.items) {
            if (item.id != kHumanParamIds[slot])
                continue;
            const int base = values[slot];
            item.inputSampleValues = {0, std::max(1, base / 2), base, base * 2, base * 4};
            if (!g_humanParamSet[slot]) {
                g_humanParamValue[slot] = base;
                item.inputValue = std::clamp(base, item.inputMinimum, item.inputMaximum);
            }
        }
    }
}

// Applies the displayed values to the retail globals immediately, then verifies
// with a live read-back (saveLiveHumanPlayerParams does both).
void applyHumanPhysics(igi::Runtime &runtime) {
    g_humanParams.moveScale = static_cast<float>(g_humanParamValue[kHumanMoveScale]) / 100.0F;
    g_humanParams.jumpSpeedKmh = static_cast<float>(g_humanParamValue[kHumanJumpSpeed]) / 10.0F;
    g_humanParams.jumpUpKmh = static_cast<float>(g_humanParamValue[kHumanJumpHeight]) / 10.0F;
    g_humanParams.airSpeedKmh = static_cast<float>(g_humanParamValue[kHumanAirSpeed]) / 10.0F;
    g_humanParams.maxDamage = static_cast<float>(g_humanParamValue[kHumanDamage]) / 100.0F;
    g_humanParams.fenceDamage = static_cast<float>(g_humanParamValue[kHumanFenceDamage]) / 100.0F;
    (void)igi::saveLiveHumanPlayerParams(runtime, g_humanParams);
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
            igi::log(igi::LogLevel::Warning, "human physics rejected: enter a non-negative value");
            return true;
        }
        g_humanParamValue[slot] = typed;
        g_humanParamSet[slot] = true;
        applyHumanPhysics(runtime);
        return true;
    }
    if (id == "human_save") {
        applyHumanPhysics(runtime);
        return true;
    }
    if (id == "human_reload") {
        if (igi::reloadLiveHumanPlayerParams(runtime)) {
            std::fill(std::begin(g_humanParamSet), std::end(g_humanParamSet), false);
            g_humanBaselineCaptured = false;
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
    // Same Player/Human controls as the internal menu, generated from this
    // shared table; only the frontend UI differs.
    addHumanControls(config);
    const auto inherited = config.refreshLiveItems;
    config.refreshLiveItems = [inherited](igi::MenuConfig &live, igi::Runtime &runtime) {
        const bool changed = inherited ? inherited(live, runtime) : false;
        prefillHumanControls(live, runtime);
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
