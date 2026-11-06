#pragma once

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/igi1_hooks.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
enum class ModMenuItemKind { Action, Toggle, List, Slider };

struct ModMenuItemConfig {
    std::string id;
    ModMenuItemKind kind = ModMenuItemKind::Action;
    std::string label;
    std::string nativeAction;
    std::vector<std::string> values;
    bool defaultToggle = false;
};

struct MenuConfig {
    bool enabled = true;
    std::uint8_t hotkey = 0x3F; // DIK_F5
    std::int32_t screenId = 990;
    std::int32_t x = 95;
    std::int32_t y = 185;
    std::int32_t width = 455;
    std::int32_t height = 300;
    // Debug builds default to a console and trace output; release builds write
    // to igi_sdk.log at Info unless config.ini asks for more.
#ifndef NDEBUG
    LogLevel minimumLogLevel = LogLevel::Trace;
    bool console = true;
#else
    LogLevel minimumLogLevel = LogLevel::Info;
    bool console = false;
#endif
    igi1::TickHook hook = igi1::TickHook::DisplayAddFrameCallback;
    std::vector<ModMenuItemConfig> items;
};

// Reads the SDK-owned config file. Missing or malformed values keep the safe
// defaults above; callers can load it on startup without making the game fail.
MenuConfig loadMenuConfig(const std::filesystem::path& path);
std::uint8_t parseDirectInputKey(std::string_view name, std::uint8_t fallback = 0x3F) noexcept;
LogLevel parseLogLevel(std::string_view name, LogLevel fallback = LogLevel::Info) noexcept;
igi1::TickHook parseTickHook(std::string_view name, igi1::TickHook fallback = igi1::TickHook::TextDraw) noexcept;
const char* tickHookName(igi1::TickHook hook) noexcept;
}
