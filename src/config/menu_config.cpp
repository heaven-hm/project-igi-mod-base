#include "config/menu_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace {
std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
                                        [](unsigned char ch) { return std::isspace(ch); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                          return std::isspace(ch);
                      }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool parseBool(const std::string &value, bool fallback) {
    if (value == "true" || value == "1" || value == "yes")
        return true;
    if (value == "false" || value == "0" || value == "no")
        return false;
    return fallback;
}

void setInt(const std::string &value, std::int32_t &target) {
    try {
        target = std::stoi(value);
    } catch (...) {
    }
}

void setClampedFloat(const std::string &value, float &target, float minValue, float maxValue) {
    try {
        const float parsed = std::stof(value);
        target = std::clamp(parsed, minValue, maxValue);
    } catch (...) {
    }
}

bool isSafeRelativeFileName(const std::string &value) {
    return !value.empty() && value != "." && value != ".." &&
           value.find_first_of("/\\:") == std::string::npos;
}

void applyImGuiKey(igi::ImGuiMenuConfig &imgui, const std::string &rawKey,
                   const std::string &value) {
    const std::string key = lower(rawKey);
    if (key == "x" || key == "window_x")
        setInt(value, imgui.x);
    else if (key == "y" || key == "window_y")
        setInt(value, imgui.y);
    else if (key == "width" || key == "window_width")
        setInt(value, imgui.width);
    else if (key == "height" || key == "window_height")
        setInt(value, imgui.height);
    else if (key == "theme" || key == "style")
        imgui.theme = igi::parseImGuiTheme(value, imgui.theme);
    else if (key == "alpha" || key == "opacity")
        setClampedFloat(value, imgui.alpha, 0.2F, 1.0F);
    else if (key == "font_scale" || key == "scale")
        setClampedFloat(value, imgui.fontScale, 0.5F, 2.5F);
    else if (key == "window_rounding")
        setClampedFloat(value, imgui.windowRounding, 0.0F, 16.0F);
    else if (key == "frame_rounding")
        setClampedFloat(value, imgui.frameRounding, 0.0F, 16.0F);
    else if (key == "border_size")
        setClampedFloat(value, imgui.borderSize, 0.0F, 4.0F);
    else if (key == "pause_game" || key == "pause_while_open")
        imgui.pauseGame = parseBool(lower(value), imgui.pauseGame);
    else if (key == "show_fps")
        imgui.showFps = parseBool(lower(value), imgui.showFps);
    else if (key == "show_status")
        imgui.showStatus = parseBool(lower(value), imgui.showStatus);
    else if (key == "movable")
        imgui.movable = parseBool(lower(value), imgui.movable);
    else if (key == "resizable")
        imgui.resizable = parseBool(lower(value), imgui.resizable);
    else if (key == "collapsible")
        imgui.collapsible = parseBool(lower(value), imgui.collapsible);
    else if (key == "auto_resize")
        imgui.autoResize = parseBool(lower(value), imgui.autoResize);
    else if (key == "mouse_cursor")
        imgui.mouseCursor = parseBool(lower(value), imgui.mouseCursor);
    else if (key == "keyboard_nav")
        imgui.keyboardNav = parseBool(lower(value), imgui.keyboardNav);
    else if (key == "save_layout")
        imgui.saveLayout = parseBool(lower(value), imgui.saveLayout);
    else if (key == "ini_file" && isSafeRelativeFileName(value))
        imgui.iniFile = value;
}

void applyMenuKey(igi::MenuConfig &config, const std::string &key, const std::string &value,
                  bool imguiGeometryExplicit) {
    if (key == "enabled")
        config.enabled = parseBool(lower(value), config.enabled);
    else if (key == "start_key" || key == "hotkey")
        config.hotkey = igi::parseDirectInputKey(value, config.hotkey);
    else if (key == "end_key" || key == "exit_key" || key == "exit_hotkey")
        config.exitKey = igi::parseDirectInputKey(value, config.exitKey);
    else if (key == "native_mode") {
        const auto mode = lower(trim(value));
        if (mode == "direct")
            config.directNativeMode = true;
        else if (mode == "hooked")
            config.directNativeMode = false;
    } else if (key == "screen_id")
        setInt(value, config.screenId);
    else if (key == "x") {
        setInt(value, config.x);
        if (!imguiGeometryExplicit)
            setInt(value, config.imgui.x);
    } else if (key == "y") {
        setInt(value, config.y);
        if (!imguiGeometryExplicit)
            setInt(value, config.imgui.y);
    } else if (key == "width") {
        setInt(value, config.width);
        if (!imguiGeometryExplicit)
            setInt(value, config.imgui.width);
    } else if (key == "height") {
        setInt(value, config.height);
        if (!imguiGeometryExplicit)
            setInt(value, config.imgui.height);
    } else if (key == "hook")
        config.hook = igi::parseTickHook(value, config.hook);
    else if (key == "console")
        config.console = parseBool(lower(value), config.console);
    else if (key == "log_level")
        config.minimumLogLevel = igi::parseLogLevel(value, config.minimumLogLevel);
    else if (key == "log_file" && isSafeRelativeFileName(value))
        config.logFile = value;
    else if (key.starts_with("imgui_"))
        applyImGuiKey(config.imgui, key.substr(6), value);
}
} // namespace

namespace igi {
bool validateMenuItemConfig(const MenuConfig &config, std::string *reason) {
    const auto reject = [reason](std::string message) {
        if (reason)
            *reason = std::move(message);
        return false;
    };

    for (const auto &item : config.items) {
        if (item.kind == ModMenuItemKind::Input) {
            if (item.inputMinimum > item.inputMaximum)
                return reject("menu input '" + item.id + "' has an inverted range");
            // The current value is a display hint, not a constraint. Clamping it
            // at use sites keeps one stale/large live value from invalidating the
            // entire menu resource (the player position is in world units).
            continue;
        }
        if (item.kind != ModMenuItemKind::List)
            continue;
        if (item.values.empty())
            return reject("menu list '" + item.id + "' must have at least one choice");
        if (item.values.size() > kMaxMenuListChoices) {
            return reject("menu list '" + item.id + "' has " + std::to_string(item.values.size()) +
                          " choices; maximum is " + std::to_string(kMaxMenuListChoices));
        }
        if (item.defaultSelection >= item.values.size())
            return reject("menu list '" + item.id + "' has an out-of-range default selection");
        for (const auto &value : item.values) {
            if (value.size() >= 40 || value.find('\0') != std::string::npos)
                return reject("menu list '" + item.id + "' has an invalid choice value");
        }
    }
    return true;
}

std::uint8_t parseDirectInputKey(std::string_view name, std::uint8_t fallback) noexcept {
    const std::string key = upper(std::string{name});
    if (key == "F3")
        return 0x3D;
    if (key == "F4")
        return 0x3E;
    if (key == "F5")
        return 0x3F;
    if (key == "F6")
        return 0x40;
    if (key == "F7")
        return 0x41;
    if (key == "F8")
        return 0x42;
    if (key == "F9")
        return 0x43;
    if (key == "F10")
        return 0x44;
    if (key == "F11")
        return 0x57;
    if (key == "F12")
        return 0x58;
    if (key == "HOME")
        return 0x47;
    if (key == "END")
        return 0x4F;
    if (key == "PAGEUP" || key == "PGUP")
        return 0x49;
    if (key == "PAGEDOWN" || key == "PGDN")
        return 0x51;
    if (key == "INSERT" || key == "INS")
        return 0x52;
    if (key == "DELETE" || key == "DEL")
        return 0x53;
    return fallback;
}

LogLevel parseLogLevel(std::string_view name, LogLevel fallback) noexcept {
    const std::string key = lower(std::string{name});
    if (key == "trace")
        return LogLevel::Trace;
    if (key == "debug")
        return LogLevel::Debug;
    if (key == "info")
        return LogLevel::Info;
    if (key == "warning" || key == "warn")
        return LogLevel::Warning;
    if (key == "error")
        return LogLevel::Error;
    if (key == "critical" || key == "crit")
        return LogLevel::Critical;
    return fallback;
}

ImGuiTheme parseImGuiTheme(std::string_view name, ImGuiTheme fallback) noexcept {
    const std::string key = lower(trim(std::string{name}));
    if (key == "dark")
        return ImGuiTheme::Dark;
    if (key == "light")
        return ImGuiTheme::Light;
    if (key == "classic")
        return ImGuiTheme::Classic;
    return fallback;
}

game::TickHook parseTickHook(std::string_view name, game::TickHook fallback) noexcept {
    // Normalise so displayAddFrameCallback, Display_AddFrameCallback and
    // display_add_frame_callback all resolve to the same hook.
    std::string key = lower(std::string{name});
    key.erase(std::remove_if(key.begin(), key.end(),
                             [](char c) { return c == '_' || c == '-' || c == ' '; }),
              key.end());
    if (key == "displayaddframecallback" || key == "addframecallback" || key == "framecallback")
        return game::TickHook::DisplayAddFrameCallback;
    if (key == "screendraw" || key == "screen")
        return game::TickHook::ScreenDraw;
    if (key == "displayendframe" || key == "display")
        return game::TickHook::DisplayEndFrame;
    if (key == "textdraw")
        return game::TickHook::TextDraw;
    return fallback;
}

const char *tickHookName(game::TickHook hook) noexcept {
    switch (hook) {
    case game::TickHook::TextDraw:
        return "Text_Draw";
    case game::TickHook::DisplayEndFrame:
        return "Display_EndFrame";
    case game::TickHook::ScreenDraw:
        return "Screen_Draw";
    case game::TickHook::DisplayAddFrameCallback:
        return "Display_AddFrameCallback";
    }
    return "Display_AddFrameCallback";
}

MenuConfig loadMenuConfig(const std::filesystem::path &path) {
    MenuConfig config;
    std::ifstream file(path);
    if (!file)
        return config;

    // `[mod_menu]` and `[imgui]` apply always; `.debug` and `.release`
    // sections only apply to a build with matching NDEBUG, so one file can ship both.
    enum class Section {
        None,
        Base,
        DebugOnly,
        ReleaseOnly,
        ImGuiBase,
        ImGuiDebugOnly,
        ImGuiReleaseOnly
    };
    Section section = Section::None;
    bool imguiGeometryExplicit = false;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#')
            continue;
        if (line.front() == '[' && line.back() == ']') {
            if (line == "[mod_menu]")
                section = Section::Base;
            else if (line == "[mod_menu.debug]")
                section = Section::DebugOnly;
            else if (line == "[mod_menu.release]")
                section = Section::ReleaseOnly;
            else if (line == "[imgui]")
                section = Section::ImGuiBase;
            else if (line == "[imgui.debug]")
                section = Section::ImGuiDebugOnly;
            else if (line == "[imgui.release]")
                section = Section::ImGuiReleaseOnly;
            else
                section = Section::None;
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        switch (section) {
        case Section::Base:
            applyMenuKey(config, key, value, imguiGeometryExplicit);
            break;
        case Section::DebugOnly:
#ifndef NDEBUG
            if (key != "log_file")
                applyMenuKey(config, key, value, imguiGeometryExplicit);
#endif
            break;
        case Section::ReleaseOnly:
#ifdef NDEBUG
            if (key != "log_file")
                applyMenuKey(config, key, value, imguiGeometryExplicit);
#endif
            break;
        case Section::ImGuiBase:
            if (key == "x" || key == "y" || key == "width" || key == "height" ||
                key == "window_x" || key == "window_y" || key == "window_width" ||
                key == "window_height") {
                imguiGeometryExplicit = true;
            }
            applyImGuiKey(config.imgui, key, value);
            break;
        case Section::ImGuiDebugOnly:
#ifndef NDEBUG
            applyImGuiKey(config.imgui, key, value);
#endif
            break;
        case Section::ImGuiReleaseOnly:
#ifdef NDEBUG
            applyImGuiKey(config.imgui, key, value);
#endif
            break;
        case Section::None:
            break;
        }
    }
    // A malformed frame size used to make every menu resource compilation fail
    // with the generic "invalid menu configuration". Keep the configured values
    // usable instead of letting width=0/height=0 disable the whole mod.
    config.width = std::clamp(config.width, 160, 4096);
    config.height = std::clamp(config.height, 120, 4096);
    config.screenId = std::clamp(config.screenId, 1, 4095);
    return config;
}
} // namespace igi
