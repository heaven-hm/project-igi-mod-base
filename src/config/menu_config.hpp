#pragma once

#include "logging/logging.hpp"
#include "runtime/igi_hooks.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
class Runtime;
struct MenuConfig;

enum class ModMenuItemKind { Action, Toggle, List, Slider, Input };
enum class ImGuiTheme { Dark, Light, Classic };

inline constexpr std::size_t kMaxMenuListChoices = 64;
using MenuCurrentValueReader = std::function<std::optional<std::string>(Runtime &)>;
using MenuLiveItemRefresher = std::function<bool(MenuConfig &, Runtime &)>;
// Asked once per frame by the ImGui host. Returning true draws the mod crosshair
// over the game view (the retail HUD hides it in third person).
using MenuCrosshairQuery = std::function<bool(Runtime &)>;
// Optional: screen position of the demo's crosshair in a width x height view.
// Falling back to the view centre is the host's job when this returns false.
using MenuCrosshairPosition = std::function<bool(Runtime &, int, int, float &, float &)>;
// Reports a page navigation request from the retail menu (delta -1 or +1).
using MenuPageHandler = std::function<void(int)>;
// Runs every displayed frame, regardless of whether the menu is open. Used for
// continuous features such as the free camera.
using MenuFrameUpdate = std::function<void(Runtime &)>;
using MenuEndFrameRenderer = std::function<void(Runtime &)>;

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
    // Numeric text box: the user types an exact value and applies it. The
    // retail QVM menu has no text widget, so its generated screen offers the
    // sample values from inputSamples() instead.
    int inputValue = 0;
    int inputMinimum = 0;
    int inputMaximum = 9999;
    int inputStep = 1;
    // Internal values are integers; the user-facing display divides by this.
    // 10 renders 175 as "17.5", 100 renders 300 as "3". Applies to the sample
    // list entries, the command list labels and the ImGui input widget.
    int inputDisplayDivisor = 1;
    // Explicit sample entries for the QVM ListBox. When non-empty it replaces
    // the generated min/step/max walk, so a control can offer meaningful
    // multiples of a live (baseline) value instead of arbitrary steps.
    std::vector<int> inputSampleValues;
    std::string category;
    // Optional game-thread reader used by renderers to synchronize display state
    // with a value changed outside the menu or rejected by game-side logic.
    MenuCurrentValueReader currentValue;
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
    // The ImGui frontend sizes itself to the configured control set by default.
    bool autoResize = true;
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
    // Page shown by the current screen resource. Only one page exists as a task
    // at a time: page switches re-attach with the next page (a retail ListBox
    // cannot scroll, and leftover page screens crash the retail manager).
    std::size_t pageIndex = 0;
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
    // Optional game-thread callback for a demo to replace runtime-backed list
    // choices (for example, live task targets) before a menu is displayed.
    MenuLiveItemRefresher refreshLiveItems;
    // Optional: the demo reports whether its crosshair should be drawn now, and
    // where its aim ray projects on screen.
    MenuCrosshairQuery crosshairActive;
    MenuCrosshairPosition crosshairPosition;
    // Optional per-frame mod logic (free camera, timers, overrides).
    MenuFrameUpdate frameUpdate;
    MenuEndFrameRenderer renderEndFrame;
};

// Reads the SDK-owned config file. Missing or malformed values keep the safe
// defaults above; callers can load it on startup without making the game fail.
MenuConfig loadMenuConfig(const std::filesystem::path &path);
// Validates list constraints shared by the QVM and ImGui menu frontends.
// `reason`, when supplied, receives a user-facing description of the first error.
bool validateMenuItemConfig(const MenuConfig &config, std::string *reason = nullptr);
std::uint8_t parseDirectInputKey(std::string_view name, std::uint8_t fallback = 0x3F) noexcept;
LogLevel parseLogLevel(std::string_view name, LogLevel fallback = LogLevel::Info) noexcept;
ImGuiTheme parseImGuiTheme(std::string_view name, ImGuiTheme fallback = ImGuiTheme::Dark) noexcept;
game::TickHook
parseTickHook(std::string_view name,
              game::TickHook fallback = game::TickHook::DisplayAddFrameCallback) noexcept;
const char *tickHookName(game::TickHook hook) noexcept;
} // namespace igi
