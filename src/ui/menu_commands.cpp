#include "ui/menu_commands.hpp"

#include <algorithm>
#include <array>

namespace igi {
namespace {
// Geometry shared with writeMenuScript(): each control row is 25 pixels tall
// with 4 pixels of vertical spacing, and the frame reserves one row for the
// title plus the Okay/Cancel row.
constexpr std::size_t kRowHeight = 25;
constexpr std::size_t kRowSpacing = 4;
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

int menuRequiredHeight(const MenuConfig &config) {
    const auto rows = static_cast<int>(config.items.size()) + 2;
    return rows * static_cast<int>(kRowHeight + kRowSpacing) + 8;
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
    const int step = item.inputStep > 0 ? item.inputStep : 1;
    for (int value = low; value <= high;) {
        samples.push_back(value);
        if (samples.size() >= kMaxInputSamples)
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
