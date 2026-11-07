#include "simple_menu.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "igi_sdk/IGI.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/ui/mod_menu.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"

namespace {

struct WeaponEntry {
    int id = 0;
    int ammo = 0;
    std::string name;
};

std::vector<WeaponEntry> g_weapons;
std::size_t g_selectedWeapon = 0;

bool pickupWeapon() {
    if (g_selectedWeapon >= g_weapons.size()) return false;
    auto* player = IGI::GAME::GET_PLAYER(0);
    if (!player) return false;
    int args[] = {g_weapons[g_selectedWeapon].id, g_weapons[g_selectedWeapon].ammo};
    IGI::PLAYER::PICK_UP_WEAPON(reinterpret_cast<IGI::Human*>(player), args);
    return true;
}

void clearFlashes() {
    auto* player = IGI::GAME::GET_PLAYER(0);
    if (!player) return;
    auto* hp = reinterpret_cast<IGI::HumanPlayer*>(player);
    IGI::PLAYER::FADE_HIT_FLASH(hp);
    IGI::PLAYER::FADE_FLASH(hp);
}

void setSfxVolume(std::string_view value) {
    const float vol = (value == "Mute") ? 0.0f : (value == "Low") ? 0.35f : 1.0f;
    IGI::AUDIO::SET_SFX_VOLUME(vol);
}

void logFlowDiagnostics() {
    const int fps = IGI::OTHER::FLOW_GET_FPS();
    const int frames = IGI::OTHER::FLOW_GET_FRAMES();
    const int renders = IGI::OTHER::FLOW_GET_RENDERS();
    std::ostringstream ss;
    ss << "simple menu diagnostics: Flow FPS=" << fps
       << " TotalFrames=" << frames
       << " TotalRenders=" << renders;
    igi::log(igi::LogLevel::Info, ss.str());
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, ss.str().c_str());
}

}  // namespace

bool configureSimpleMenu(igi::MenuConfig& config) {
    g_weapons = {
        {2, 30, "Glock 17"},
        {3, 14, "Desert Eagle"},
        {4, 60, "Colt Anaconda"},
        {5, 90, "MP5SD"},
        {6, 120, "UZI"},
        {7, 120, "M16A2"},
        {8, 120, "AK-47"},
        {9, 30, "Dragunov SVD"},
        {10, 200, "M249 SAW"},
        {11, 24, "Jackhammer"},
        {12, 16, "Spas 12"},
        {14, 4, "LAW 80"},
    };
    igi::ModMenuItemConfig weaponList{"weapon", igi::ModMenuItemKind::List, "Weapon", "", {}};
    for (const auto& w : g_weapons) {
        weaponList.values.push_back(w.name);
    }

    g_selectedWeapon = 0;

    config.items = {
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "FPS", "", {"30", "60", "120"}},
        std::move(weaponList),
        {"pickup", igi::ModMenuItemKind::Action, "Pick Up Selected Weapon", "", {}},
        {"view", igi::ModMenuItemKind::List, "Camera View", "", {"First Person", "Third Person"}},
        {"level", igi::ModMenuItemKind::List, "Level", "", {
            "Level 1", "Level 2", "Level 3", "Level 4", "Level 5", "Level 6", "Level 7",
            "Level 8", "Level 9", "Level 10", "Level 11", "Level 12", "Level 13", "Level 14"
        }},
        {"sfx", igi::ModMenuItemKind::List, "SFX Volume", "", {"Normal", "Low", "Mute"}},
        {"flash", igi::ModMenuItemKind::Action, "Clear Screen Flash", "", {}},
        {"perf", igi::ModMenuItemKind::Action, "Log Engine Performance", "", {}},
    };
    return true;
}

void simpleMenuAction(const igi::MenuConfig& config, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    if (item >= config.items.size()) return;
    if (!runtime.allowDirectNativeCallbackThread()) return;
    const auto& id = config.items[item].id;

    if (id == "weapon") { // Weapon selection list
        for (std::size_t i = 0; i < g_weapons.size(); ++i) {
            if (g_weapons[i].name == value) {
                g_selectedWeapon = i;
                break;
            }
        }
        const bool ok = pickupWeapon();
        igi::log(ok ? igi::LogLevel::Info : igi::LogLevel::Error,
                 ok ? "simple menu: weapon selected and picked up: " + std::string(value)
                    : "simple menu: weapon pickup failed");
        if (ok) {
            const std::string msg = "IGI SDK: Equipped " + std::string(value);
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, msg.c_str());
        }
        return;
    }

    if (id == "weather") { // Weather: Off, Rain, Snow
        igi::setWeather(value);
    } else if (id == "fps") { // FPS: 30, 60, 120
        igi::setFps(value);
    } else if (id == "pickup") { // Pick up weapon
        const bool ok = pickupWeapon();
        igi::log(ok ? igi::LogLevel::Info : igi::LogLevel::Error,
                 ok ? "simple menu: weapon pickup requested via IGI::PLAYER::PICK_UP_WEAPON"
                    : "simple menu: weapon pickup failed");
        if (ok && g_selectedWeapon < g_weapons.size()) {
            const std::string msg = "IGI SDK: Equipped " + g_weapons[g_selectedWeapon].name;
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, msg.c_str());
        }
    } else if (id == "view") { // Camera View: First Person, Third Person
        auto* player = IGI::GAME::GET_PLAYER(0);
        if (player) {
            const int mode = (value == "Third Person") ? 2 : 1;
            IGI::HUMAN::VIEW_CAM(reinterpret_cast<IGI::Human*>(player), mode);
            igi::log(igi::LogLevel::Info, "simple menu: camera view set to " + std::string(value));
            const std::string msg = "IGI SDK: Camera view set to " + std::string(value);
            IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, msg.c_str());
        }
    } else if (id == "level") { // Level selection 1..14
        int levelNum = 1;
        if (value.starts_with("Level ")) {
            try {
                levelNum = std::stoi(std::string(value.substr(6)));
            } catch (...) {
                levelNum = 1;
            }
        }
        runtime.scheduler().enqueueOnce([levelNum] {
            const bool ok = igi::changeLevel(levelNum);
            igi::log(ok ? igi::LogLevel::Info : igi::LogLevel::Error,
                     ok ? "simple menu: level changed to Level " + std::to_string(levelNum)
                        : "simple menu: level change failed");
        });
    } else if (id == "sfx") { // SFX volume
        setSfxVolume(value);
        igi::log(igi::LogLevel::Info, "simple menu: SFX volume set via IGI::AUDIO::SET_SFX_VOLUME");
    } else if (id == "flash") { // Clear screen flash
        clearFlashes();
        igi::log(igi::LogLevel::Info, "simple menu: screen flashes cleared via IGI::PLAYER::FADE_HIT_FLASH / FADE_FLASH");
    } else if (id == "perf") { // Engine performance
        logFlowDiagnostics();
    }
}

bool configureModMenu(igi::MenuConfig& config) { return configureSimpleMenu(config); }
void runModMenuAction(const igi::MenuConfig& config, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    simpleMenuAction(config, runtime, item, value);
}
