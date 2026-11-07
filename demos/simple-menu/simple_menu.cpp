#include "simple_menu.hpp"
#include "igi_sdk/IGI.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Weapon { int id; int ammo; std::string name; };
std::vector<Weapon> weapons;
std::size_t selected = 0;
using GetCount = int (__cdecl*)();
using GetWeapon = const unsigned char* (__cdecl*)(int);
using NewTask = unsigned char* (__cdecl*)(void*, std::uint16_t, void*);
using ChangeEffect = void (__cdecl*)(void*);
using DeleteTask = void (__cdecl*)(void*);
unsigned char* weatherTask = nullptr;

bool setWeather(std::string_view weather) {
    auto* game = reinterpret_cast<unsigned char*>(IGI::GAME::GET());
    if (!game || !IGI::GAME::GET_WORLD()) return false;
    const auto type = *reinterpret_cast<const std::uint16_t*>(0x0057bd84);
    const auto texture = *reinterpret_cast<unsigned char* const*>(0x0057bd80);
    if (type >= 0x180 || !texture) return false;

    bool found = false;
    auto* node = *reinterpret_cast<unsigned char**>(game + 8);
    while (node) {
        auto* next = *reinterpret_cast<unsigned char**>(node);
        if (!next) break;
        if (node == weatherTask) {
            std::uint16_t nodeType;
            std::memcpy(&nodeType, node + 0x1c, sizeof(nodeType));
            found = nodeType == type;
            break;
        }
        node = next;
    }
    if (!found) weatherTask = nullptr;
    if (weather == "Off") {
        if (weatherTask) reinterpret_cast<DeleteTask>(0x004015f0)(weatherTask);
        weatherTask = nullptr;
    } else {
        if (!weatherTask)
            weatherTask = reinterpret_cast<NewTask>(0x004012a0)(game, type, nullptr);
        if (!weatherTask) return false;
        weatherTask[0x24] = weather == "Rain" ? 1 : 0;
        reinterpret_cast<ChangeEffect>(0x00436a50)(weatherTask);
    }
    const char raining[] = {'r', static_cast<char>(weather == "Rain"), '\0'};
    IGI::GAME::SET_RAINING(raining);
    return true;
}

bool pickup() {
    if (selected >= weapons.size()) return false;
    auto* player = IGI::GAME::GET_PLAYER(0);
    if (!player) return false;
    int args[] = {weapons[selected].id, weapons[selected].ammo};
    IGI::PLAYER::PICK_UP_WEAPON(reinterpret_cast<IGI::Human*>(player), args);
    return true;
}
}

bool configureSimpleMenu(igi::MenuConfig& config) {
    weapons.clear();
    const int count = reinterpret_cast<GetCount>(0x00413bb0)();
    if (count <= 0 || count > 64) {
        igi::log(igi::LogLevel::Error, "simple menu: weapon registry unavailable");
        return false;
    }
    igi::ModMenuItemConfig weaponList{"weapon", igi::ModMenuItemKind::List, "Weapon", "", {}};
    for (int i = 0; i < count; ++i) {
        const auto* weapon = reinterpret_cast<GetWeapon>(0x00413b90)(i);
        int id, ammo;
        std::memcpy(&id, weapon, sizeof(id));
        std::memcpy(&ammo, weapon + 0x648, sizeof(ammo));
        const char* name = reinterpret_cast<const char*>(weapon + 0x10);
        const auto length = strnlen(name, 39);
        std::string label(name, length);
        if (label.empty()) label = "Weapon " + std::to_string(id);
        weapons.push_back({id, std::max(0, ammo), label});
        weaponList.values.push_back(label);
    }
    selected = 0;
    config.items = {
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "FPS", "", {"30", "60", "120"}},
        std::move(weaponList),
        {"pickup", igi::ModMenuItemKind::Action, "OK - Pick up weapon", "", {}}
    };
    return true;
}

void simpleMenuAction(igi::Runtime& runtime, std::size_t item, std::string_view value) {
    if (item == 2) {
        for (std::size_t i = 0; i < weapons.size(); ++i)
            if (weapons[i].name == value) { selected = i; break; }
        return;
    }
    if (!igi::pauseForMenu(runtime)) return;
    if (item == 1) {
        IGI::MISC::FRAMES_SET(value == "30" ? 30 : value == "60" ? 60 : 120);
    } else if (item == 3) {
        igi::log(pickup() ? igi::LogLevel::Info : igi::LogLevel::Error, "simple menu: weapon pickup requested");
    } else if (item == 0) {
        const bool ok = setWeather(value);
        igi::log(ok ? igi::LogLevel::Info : igi::LogLevel::Error,
                 ok ? "simple menu: weather effect updated" : "simple menu: weather effect unavailable");
    }
}

bool configureDemoMenu(igi::MenuConfig& config) { return configureSimpleMenu(config); }
void runDemoMenuAction(const igi::MenuConfig&, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    simpleMenuAction(runtime, item, value);
}
