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

bool parseBool(const std::string& value, bool fallback) {
    if (value == "true" || value == "1" || value == "yes") return true;
    if (value == "false" || value == "0" || value == "no") return false;
    return fallback;
}

void setInt(const std::string& value, std::int32_t& target) {
    try { target = std::stoi(value); } catch (...) {}
}
}

namespace igi {
std::uint8_t parseDirectInputKey(std::string_view name, std::uint8_t fallback) noexcept {
    std::string key{name};
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (key == "F3") return 0x3D;
    if (key == "F4") return 0x3E;
    if (key == "F5") return 0x3F;
    if (key == "F6") return 0x40;
    if (key == "F7") return 0x41;
    if (key == "F8") return 0x42;
    return fallback;
}

MenuConfig loadMenuConfig(const std::filesystem::path& path) {
    MenuConfig config;
    std::ifstream file(path);
    if (!file) return config;

    std::string line;
    bool inMenuSection = false;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            inMenuSection = line == "[mod_menu]";
            continue;
        }
        if (!inMenuSection) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (key == "enabled") config.enabled = parseBool(value, config.enabled);
        else if (key == "hotkey") config.hotkey = parseDirectInputKey(value, config.hotkey);
        else if (key == "screen_id") setInt(value, config.screenId);
        else if (key == "x") setInt(value, config.x);
        else if (key == "y") setInt(value, config.y);
        else if (key == "width") setInt(value, config.width);
        else if (key == "height") setInt(value, config.height);
    }
    return config;
}
}
