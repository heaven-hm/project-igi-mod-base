#pragma once

#include "config/menu_config.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace igi {
// One runnable menu entry. A configured list control expands into one entry per
// value, so a control set with more rows than the retail frame can draw stays
// fully reachable through a single scrollable command list.
struct MenuCommand {
    std::size_t itemIndex = 0;
    std::string label;
    std::string value;
    bool operator==(const MenuCommand &) const = default;
};

// Rows a retail MenuFrame can draw for the configured frame height. The
// generator grows the frame to fit, so this is informational.
std::size_t menuRowCapacity(const MenuConfig &config);
// One retail page shows this many control rows; larger menus are split across
// several MenuScreens inside the same resource, so every control keeps the rich
// row layout (label plus ListBox/ToggleBox/button) instead of a one-line list.
inline constexpr std::size_t kMenuRowsPerPage = 14;
// Number of pages a configured control set needs.
std::size_t menuPageCount(const MenuConfig &config);
// Pixel height needed to draw one row per control plus the title and button row.
int menuRequiredHeight(const MenuConfig &config);
// Top coordinate that keeps the configured bottom edge when the frame grows.
int menuLayoutY(const MenuConfig &config);
// True only when one row per control cannot fit the retail screen at all. A
// retail ListBox draws a single item with prev/next arrows, so this fallback is
// a single-line command selector and is used only for very large menus.
bool menuNeedsCommandList(const MenuConfig &config);
// Flattens every control into command entries, grouped by category.
std::vector<MenuCommand> buildMenuCommands(const MenuConfig &config);
// The retail QVM menu cannot host a text box, so a numeric input is offered as
// discrete values instead. The ImGui frontend renders the real text box.
std::vector<int> inputSamples(const ModMenuItemConfig &item);
} // namespace igi
