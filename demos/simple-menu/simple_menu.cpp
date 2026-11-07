#include "simple_menu.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "igi_sdk/IGI.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/ui/mod_menu.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"

namespace {

constexpr std::uintptr_t gAICurrentAddr = 0x005bda58;
constexpr std::uintptr_t gMoveScaleAddr = 0x0056e1e8;

bool g_ghostMode = false;
bool g_godMode = false;
bool g_oneHitKill = false;
float g_currentSpeed = 1.0f;
double g_defaultSightRange = 100.0;

std::uint8_t* getCurrentAi() noexcept {
    auto* const aiPtr = reinterpret_cast<std::uint8_t* const*>(gAICurrentAddr);
    return aiPtr ? *aiPtr : nullptr;
}

std::uint8_t* getPlayerAi() noexcept {
    auto* player = reinterpret_cast<std::uint8_t*>(IGI::GAME::GET_PLAYER(0));
    if (!player) return nullptr;
    auto* const aiPtr = reinterpret_cast<std::uint8_t* const*>(player + 0x800);
    return aiPtr ? *aiPtr : nullptr;
}

void applyGhostMode(bool enabled) noexcept {
    g_ghostMode = enabled;
    auto* ai = getCurrentAi();
    if (ai) {
        if (enabled) {
            const double current = *reinterpret_cast<const double*>(ai + 0xb0);
            if (current > 0.0) g_defaultSightRange = current;
            *reinterpret_cast<double*>(ai + 0xb0) = 0.0;
            *reinterpret_cast<float*>(ai + 0xb0) = 0.0f;
        } else {
            *reinterpret_cast<double*>(ai + 0xb0) = g_defaultSightRange;
            *reinterpret_cast<float*>(ai + 0xb0) = static_cast<float>(g_defaultSightRange);
        }
    }
}

void applyGodMode(bool enabled) noexcept {
    g_godMode = enabled;
    const std::uint8_t flag = enabled ? 1 : 0;
    auto* ai = getCurrentAi();
    if (ai) {
        *reinterpret_cast<std::uint8_t*>(ai + 0x3abc) = flag;
    }
    auto* playerAi = getPlayerAi();
    if (playerAi) {
        *reinterpret_cast<std::uint8_t*>(playerAi + 0x3abc) = flag;
    }
}

void applyMoveSpeed(float scale) noexcept {
    g_currentSpeed = scale;
    *reinterpret_cast<float*>(gMoveScaleAddr) = scale;
}

void applyOneHitKill(bool enabled) noexcept {
    g_oneHitKill = enabled;
    const std::uint8_t flag = enabled ? 0 : 1;
    auto* ai = getCurrentAi();
    if (ai) {
        *reinterpret_cast<std::uint8_t*>(ai + 0x3abd) = flag;
    }
    auto* playerAi = getPlayerAi();
    if (playerAi) {
        *reinterpret_cast<std::uint8_t*>(playerAi + 0x3abd) = flag;
    }
}

}  // namespace

bool configureSimpleMenu(igi::MenuConfig& config) {
    config.items = {
        {"ghost", igi::ModMenuItemKind::Toggle, "Ghost Mode", "", {"false"}, false},
        {"god", igi::ModMenuItemKind::Toggle, "God Mode", "", {"false"}, false},
        {"speed", igi::ModMenuItemKind::List, "Player Speed", "", {"1.0x (Normal)", "2.0x (Fast)", "3.0x (Super)"}},
        {"onehit", igi::ModMenuItemKind::Toggle, "One-Hit Kill", "", {"false"}, false},
    };
    return true;
}

void simpleMenuAction(igi::Runtime& runtime, std::size_t item, std::string_view value) {
    if (!igi::pauseForMenu(runtime)) return;

    if (item == 0) {
        const bool enabled = (value == "true" || value == "1" || value == "on");
        applyGhostMode(enabled);
        igi::log(igi::LogLevel::Info, enabled ? "simple menu: Ghost Mode (Blind AI) enabled"
                                              : "simple menu: Ghost Mode (Blind AI) disabled");
    } else if (item == 1) {
        const bool enabled = (value == "true" || value == "1" || value == "on");
        applyGodMode(enabled);
        igi::log(igi::LogLevel::Info, enabled ? "simple menu: God Mode (Invulnerable) enabled"
                                              : "simple menu: God Mode (Invulnerable) disabled");
    } else if (item == 2) {
        float scale = 1.0f;
        if (value.find("2.0") != std::string_view::npos || value == "2") {
            scale = 2.0f;
        } else if (value.find("3.0") != std::string_view::npos || value == "3") {
            scale = 3.0f;
        }
        applyMoveSpeed(scale);
        igi::log(igi::LogLevel::Info, "simple menu: Player Movement Speed set to " + std::string(value));
    } else if (item == 3) {
        const bool enabled = (value == "true" || value == "1" || value == "on");
        applyOneHitKill(enabled);
        igi::log(igi::LogLevel::Info, enabled ? "simple menu: One-Hit Kill enabled"
                                              : "simple menu: One-Hit Kill disabled");
    }
}

bool configureModMenu(igi::MenuConfig& config) { return configureSimpleMenu(config); }
void runModMenuAction(const igi::MenuConfig&, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    simpleMenuAction(runtime, item, value);
}
