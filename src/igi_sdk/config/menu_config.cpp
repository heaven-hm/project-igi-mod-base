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

std::vector<std::string> splitCsv(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(',', start);
        const auto item = trim(value.substr(start, end - start));
        if (!item.empty()) result.push_back(item);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

igi::ModMenuItemKind parseKind(const std::string& value) {
    if (value == "toggle") return igi::ModMenuItemKind::Toggle;
    if (value == "list") return igi::ModMenuItemKind::List;
    if (value == "slider") return igi::ModMenuItemKind::Slider;
    return igi::ModMenuItemKind::Action;
}

void addItem(const std::string& id, const std::string& value, std::vector<igi::ModMenuItemConfig>& items) {
    const auto fields = splitCsv(value);
    if (fields.size() < 3) return;
    igi::ModMenuItemConfig item{id, parseKind(fields[0]), fields[1], fields[2]};
    if (fields.size() > 3) item.values.assign(fields.begin() + 3, fields.end());
    item.defaultToggle = !item.values.empty() && item.values.front() == "true";
    items.push_back(std::move(item));
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
    bool inItemsSection = false;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            inMenuSection = line == "[mod_menu]";
            inItemsSection = line == "[mod_menu.items]";
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (inItemsSection) { addItem(key, value, config.items); continue; }
        if (!inMenuSection) continue;
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
