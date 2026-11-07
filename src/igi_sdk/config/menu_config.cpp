#include "igi_sdk/config/menu_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace {
std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
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

bool parseBool(const std::string& value, bool fallback) {
    if (value == "true" || value == "1" || value == "yes") return true;
    if (value == "false" || value == "0" || value == "no") return false;
    return fallback;
}

void setInt(const std::string& value, std::int32_t& target) {
    try { target = std::stoi(value); } catch (...) {}
}

void applyMenuKey(igi::MenuConfig& config, const std::string& key, const std::string& value) {
    if (key == "enabled") config.enabled = parseBool(value, config.enabled);
    else if (key == "start_key" || key == "hotkey")
        config.hotkey = igi::parseDirectInputKey(value, config.hotkey);
    else if (key == "end_key" || key == "exit_key" || key == "exit_hotkey")
        config.exitKey = igi::parseDirectInputKey(value, config.exitKey);
    else if (key == "native_mode") {
        const auto mode = lower(trim(value));
        if (mode == "direct") config.directNativeMode = true;
        else if (mode == "hooked") config.directNativeMode = false;
    }
    else if (key == "screen_id") setInt(value, config.screenId);
    else if (key == "x") setInt(value, config.x);
    else if (key == "y") setInt(value, config.y);
    else if (key == "width") setInt(value, config.width);
    else if (key == "height") setInt(value, config.height);
    else if (key == "hook") config.hook = igi::parseTickHook(value, config.hook);
    else if (key == "console") config.console = parseBool(value, config.console);
    else if (key == "log_level") config.minimumLogLevel = igi::parseLogLevel(value, config.minimumLogLevel);
}
}

namespace igi {
std::uint8_t parseDirectInputKey(std::string_view name, std::uint8_t fallback) noexcept {
    const std::string key = upper(std::string{name});
    if (key == "F3") return 0x3D;
    if (key == "F4") return 0x3E;
    if (key == "F5") return 0x3F;
    if (key == "F6") return 0x40;
    if (key == "F7") return 0x41;
    if (key == "F8") return 0x42;
    if (key == "F9") return 0x43;
    if (key == "F10") return 0x44;
    if (key == "F11") return 0x57;
    if (key == "F12") return 0x58;
    if (key == "HOME") return 0x47;
    if (key == "END") return 0x4F;
    if (key == "PAGEUP" || key == "PGUP") return 0x49;
    if (key == "PAGEDOWN" || key == "PGDN") return 0x51;
    if (key == "INSERT" || key == "INS") return 0x52;
    if (key == "DELETE" || key == "DEL") return 0x53;
    return fallback;
}

LogLevel parseLogLevel(std::string_view name, LogLevel fallback) noexcept {
    const std::string key = lower(std::string{name});
    if (key == "trace") return LogLevel::Trace;
    if (key == "debug") return LogLevel::Debug;
    if (key == "info") return LogLevel::Info;
    if (key == "warning" || key == "warn") return LogLevel::Warning;
    if (key == "error") return LogLevel::Error;
    if (key == "critical" || key == "crit") return LogLevel::Critical;
    return fallback;
}

igi1::TickHook parseTickHook(std::string_view name, igi1::TickHook fallback) noexcept {
    // Normalise so displayAddFrameCallback, Display_AddFrameCallback and
    // display_add_frame_callback all resolve to the same hook.
    std::string key = lower(std::string{name});
    key.erase(std::remove_if(key.begin(), key.end(),
                             [](char c) { return c == '_' || c == '-' || c == ' '; }),
              key.end());
    if (key == "displayaddframecallback" || key == "addframecallback" || key == "framecallback")
        return igi1::TickHook::DisplayAddFrameCallback;
    if (key == "screendraw" || key == "screen") return igi1::TickHook::ScreenDraw;
    if (key == "displayendframe" || key == "display") return igi1::TickHook::DisplayEndFrame;
    if (key == "textdraw") return igi1::TickHook::TextDraw;
    return fallback;
}

const char* tickHookName(igi1::TickHook hook) noexcept {
    switch (hook) {
    case igi1::TickHook::TextDraw: return "Text_Draw";
    case igi1::TickHook::DisplayEndFrame: return "Display_EndFrame";
    case igi1::TickHook::ScreenDraw: return "Screen_Draw";
    case igi1::TickHook::DisplayAddFrameCallback: return "Display_AddFrameCallback";
    }
    return "Display_AddFrameCallback";
}

MenuConfig loadMenuConfig(const std::filesystem::path& path) {
    MenuConfig config;
    std::ifstream file(path);
    if (!file && path == "project-igi-sdk-config.ini") file.open("config.ini");
    else if (!file && path == "config.ini") file.open("project-igi-sdk-config.ini");
    if (!file) return config;

    // `[mod_menu]` applies always; `[mod_menu.debug]` and `[mod_menu.release]`
    // only apply to a build with matching NDEBUG, so one file can ship both.
    enum class Section { None, Base, DebugOnly, ReleaseOnly };
    Section section = Section::None;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            if (line == "[mod_menu]") section = Section::Base;
            else if (line == "[mod_menu.debug]") section = Section::DebugOnly;
            else if (line == "[mod_menu.release]") section = Section::ReleaseOnly;
            else section = Section::None;
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        switch (section) {
        case Section::Base:
            applyMenuKey(config, key, value);
            break;
        case Section::DebugOnly:
#ifndef NDEBUG
            applyMenuKey(config, key, value);
#endif
            break;
        case Section::ReleaseOnly:
#ifdef NDEBUG
            applyMenuKey(config, key, value);
#endif
            break;
        case Section::None:
            break;
        }
    }
    return config;
}
}
