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
// generator uses the same geometry, so this decides which layout is used.
std::size_t menuRowCapacity(const MenuConfig &config);
// True when the configured controls no longer fit the retail frame and the
// command-list layout is required.
bool menuNeedsCommandList(const MenuConfig &config);
// Flattens every control into command entries, grouped by category.
std::vector<MenuCommand> buildMenuCommands(const MenuConfig &config);
// The retail QVM menu cannot host a text box, so a numeric input is offered as
// discrete values instead. The ImGui frontend renders the real text box.
std::vector<int> inputSamples(const ModMenuItemConfig &item);
} // namespace igi
