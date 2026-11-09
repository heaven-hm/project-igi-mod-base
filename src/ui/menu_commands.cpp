#include "ui/menu_commands.hpp"

#include <algorithm>
#include <array>

namespace igi {
namespace {
// Geometry shared with writeMenuScript(): each control row is 25 pixels tall
// with 4 pixels of vertical spacing, and the frame reserves one row for the
// title plus the Okay/Cancel row.
constexpr std::size_t kReservedRows = 2;

std::string categoryPrefix(const std::string &category) {
    if (category.empty())
        return {};
    const auto separator = category.rfind('/');
    return (separator == std::string::npos ? category : category.substr(separator + 1)) + ": ";
}
} // namespace

std::size_t menuRowCapacity(const MenuConfig &config) {
    const auto usable =
        static_cast<std::size_t>(std::max(config.height, 0)) / (kRowHeight + kRowSpacing);
    return usable > kReservedRows ? usable - kReservedRows : 0;
}

std::size_t menuPageCount(const MenuConfig &config) {
    const auto rows = config.items.size() + (config.items.size() > kMenuRowsPerPage ? 1 : 0);
    const std::size_t pages = (rows + kMenuRowsPerPage - 1) / kMenuRowsPerPage;
    return pages == 0 ? 1 : pages;
}

std::int32_t menuPageScreenId(const MenuConfig &config, std::size_t page) {
    return config.screenId + static_cast<std::int32_t>(page) * kPageScreenStride;
}

std::size_t menuScreenIdSpan(const MenuConfig &config) {
    return menuPageCount(config) * static_cast<std::size_t>(kPageScreenStride);
}

int menuRequiredHeight(const MenuConfig &config) {
    // The frame must fit every row it draws: the title, the controls on this
    // page, both navigation rows when the menu is paged, and the button row.
    // The old formula counted only one navigation row, so paged menus clipped
    // the row above the buttons.
    const bool paged = menuPageCount(config) > 1;
    const std::size_t rowsOnPage = std::min(config.items.size(), kMenuRowsPerPage);
    const auto rows = rowsOnPage + (config.title.empty() ? 0 : 1) + (paged ? 2 : 0) + 1;
    return static_cast<int>(rows) * static_cast<int>(kRowHeight + kRowSpacing) + 8;
}

int menuLayoutY(const MenuConfig &config) {
    // Keep the bottom edge the user configured and grow upwards instead of
    // letting the last controls fall off the screen.
    const int bottom = config.y + config.height;
    return std::max(8, bottom - menuRequiredHeight(config));
}

bool menuNeedsCommandList(const MenuConfig &config) {
    // A ListBox is a one-line selector, so the per-control layout is preferred
    // whenever the rows can be drawn on screen at all.
    constexpr std::size_t kMaxFlatRows = 20;
    const auto room =
        static_cast<std::size_t>(std::max(config.height, 0) / (kRowHeight + kRowSpacing));
    return config.items.size() > std::max(kMaxFlatRows, room);
}

std::vector<int> inputSamples(const ModMenuItemConfig &item) {
    std::vector<int> samples;
    const int low = item.inputMinimum;
    const int high = item.inputMaximum;
    if (high < low)
        return samples;
    // The retail screen offers a few presets only; the ImGui frontend has the
    // real text box, so a long sample list would just bloat the retail list.
    constexpr std::size_t kMaxInputSamples = 5;
    // A prefilled value (for example the player's current X/Y/Z) must be the
    // first entry, not a value the list can never reach from its minimum.
    if (item.inputValue >= low && item.inputValue <= high)
        samples.push_back(item.inputValue);
    const int step = item.inputStep > 0 ? item.inputStep : 1;
    for (int value = low; value <= high;) {
        if (samples.size() >= kMaxInputSamples)
            break;
        if (std::find(samples.begin(), samples.end(), value) == samples.end())
            samples.push_back(value);
        if (value > high - step)
            break;
        value += step;
    }
    if (samples.empty() || samples.back() != high)
        samples.push_back(high);
    return samples;
}

std::vector<MenuCommand> buildMenuCommands(const MenuConfig &config) {
    std::vector<MenuCommand> commands;
    for (std::size_t index = 0; index < config.items.size(); ++index) {
        const auto &item = config.items[index];
        const auto prefix = categoryPrefix(item.category);
        switch (item.kind) {
        case ModMenuItemKind::List:
            for (const auto &value : item.values)
                commands.push_back({index, prefix + item.label + ": " + value, value});
            break;
        case ModMenuItemKind::Toggle:
            // Toggles are expanded into explicit states because the command list
            // is stateless: each entry states exactly what it applies.
            commands.push_back({index, prefix + item.label + ": Enable", "true"});
            commands.push_back({index, prefix + item.label + ": Disable", "false"});
            break;
        case ModMenuItemKind::Slider: {
            const int step = item.sliderStep > 0 ? item.sliderStep : 1;
            std::array<int, 8> samples{};
            std::size_t count = 0;
            samples[count++] = item.sliderMinimum;
            const int span = item.sliderMaximum - item.sliderMinimum;
            if (span > 0) {
                for (std::size_t i = 1; i + 1 < samples.size(); ++i) {
                    const int offset =
                        static_cast<int>((static_cast<long long>(span) * i) / (samples.size() - 1));
                    const int value = item.sliderMinimum + (offset / step) * step;
                    if (value > samples[count - 1] && value < item.sliderMaximum)
                        samples[count++] = value;
                }
                samples[count++] = item.sliderMaximum;
            }
            for (std::size_t i = 0; i < count; ++i) {
                commands.push_back({index, prefix + item.label + ": " + std::to_string(samples[i]),
                                    std::to_string(samples[i])});
            }
            break;
        }
        case ModMenuItemKind::Input:
            for (const int sample : inputSamples(item)) {
                commands.push_back({index, prefix + item.label + ": " + std::to_string(sample),
                                    std::to_string(sample)});
            }
            break;
        case ModMenuItemKind::Action:
            commands.push_back({index, prefix + item.label, std::string{}});
            break;
        }
    }
    return commands;
}
} // namespace igi
