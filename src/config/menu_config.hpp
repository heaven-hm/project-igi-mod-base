#pragma once

#include "logging/logging.hpp"
#include "runtime/igi_hooks.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
enum class ModMenuItemKind { Action, Toggle, List, Slider };
enum class ImGuiTheme { Dark, Light, Classic };

struct ModMenuItemConfig {
    std::string id;
    ModMenuItemKind kind = ModMenuItemKind::Action;
    std::string label;
    std::string nativeAction;
    std::vector<std::string> values;
    bool defaultToggle = false;
    std::size_t defaultSelection = 0;
    int sliderMinimum = 0;
    int sliderMaximum = 100;
    int sliderStep = 1;
    int sliderDefault = 0;
    std::string category;
};

struct ImGuiMenuConfig {
    std::int32_t x = 95;
    std::int32_t y = 80;
    std::int32_t width = 460;
    std::int32_t height = 400;
    ImGuiTheme theme = ImGuiTheme::Dark;
    float alpha = 0.95F;
    float fontScale = 1.0F;
    float windowRounding = 6.0F;
    float frameRounding = 4.0F;
    float borderSize = 1.0F;
    bool pauseGame = true;
    bool showFps = true;
    bool showStatus = true;
    bool movable = true;
    bool resizable = true;
    bool collapsible = false;
    bool autoResize = false;
    bool mouseCursor = true;
    bool keyboardNav = true;
    bool saveLayout = false;
    std::string iniFile = "project-igi-imgui.ini";
};

struct MenuConfig {
    bool enabled = true;
    std::string title = "IGI SDK Mod Menu";
    std::string logFile = "project-igi-mod.log";
    // Start key opens/initializes the menu. Accepted in config.ini as either
    // `start_key` or the legacy `hotkey`.
    std::uint8_t hotkey = 0x3F; // DIK_F5
    // End key closes the menu and unloads the SDK. Accepted in config.ini as
    // either `end_key` or the legacy `exit_key`/`exit_hotkey`. 0 disables it so
    // the game keeps running unmodified.
    std::uint8_t exitKey = 0x4F; // DIK_END
    // The internal QVM menu always uses the game-frame scheduler. Retained for
    // backwards-compatible parsing; `direct` is ignored by the menu host.
    bool directNativeMode = false;
    std::int32_t screenId = 990;
    std::int32_t x = 95;
    std::int32_t y = 185;
    std::int32_t width = 455;
    std::int32_t height = 300;
    ImGuiMenuConfig imgui;
    // Debug builds default to a console and trace output; release builds write
    // to the configured log file at Info unless the config asks for more.
#ifndef NDEBUG
    LogLevel minimumLogLevel = LogLevel::Trace;
    bool console = true;
#else
    LogLevel minimumLogLevel = LogLevel::Info;
    bool console = false;
#endif
    // Display_AddFrameCallback at 0x004916C0 is the only scheduler hook. Its
    // detour registers the SDK tick into the game's per-frame callback list.
    game::TickHook hook = game::TickHook::DisplayAddFrameCallback;
    std::vector<ModMenuItemConfig> items;
};

// Reads the SDK-owned config file. Missing or malformed values keep the safe
// defaults above; callers can load it on startup without making the game fail.
MenuConfig loadMenuConfig(const std::filesystem::path &path);
std::uint8_t parseDirectInputKey(std::string_view name, std::uint8_t fallback = 0x3F) noexcept;
LogLevel parseLogLevel(std::string_view name, LogLevel fallback = LogLevel::Info) noexcept;
ImGuiTheme parseImGuiTheme(std::string_view name, ImGuiTheme fallback = ImGuiTheme::Dark) noexcept;
game::TickHook
parseTickHook(std::string_view name,
              game::TickHook fallback = game::TickHook::DisplayAddFrameCallback) noexcept;
const char *tickHookName(game::TickHook hook) noexcept;
} // namespace igi
