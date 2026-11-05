#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace igi {
struct MenuConfig {
    bool enabled = true;
    std::uint8_t hotkey = 0x3F; // DIK_F5
    std::int32_t screenId = 990;
    std::int32_t x = 95;
    std::int32_t y = 185;
    std::int32_t width = 455;
    std::int32_t height = 300;
};

// Reads the SDK-owned config file. Missing or malformed values keep the safe
// defaults above; callers can load it on startup without making the game fail.
MenuConfig loadMenuConfig(const std::filesystem::path& path);
std::uint8_t parseDirectInputKey(std::string_view name, std::uint8_t fallback = 0x3F) noexcept;
}
