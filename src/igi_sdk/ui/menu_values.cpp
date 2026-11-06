#include "igi_sdk/ui/menu_values.hpp"

#include <algorithm>

namespace igi {
MenuValues::MenuValues(const MenuConfig& config) : items_(config.items), selected_(items_.size(), 0) {}

std::size_t MenuValues::choiceCount(std::size_t item) const noexcept {
    if (item >= items_.size() || items_[item].kind != ModMenuItemKind::List) return 0;
    return std::min<std::size_t>(items_[item].values.size(), 64);
}

std::optional<std::string_view> MenuValues::choice(std::size_t item, std::size_t index) const noexcept {
    if (index >= choiceCount(item)) return std::nullopt;
    return items_[item].values[index];
}

std::optional<std::size_t> MenuValues::selected(std::size_t item) const noexcept {
    if (choiceCount(item) == 0) return std::nullopt;
    return selected_[item];
}

bool MenuValues::select(std::size_t item, std::size_t index) noexcept {
    if (index >= choiceCount(item)) return false;
    selected_[item] = index;
    return true;
}

bool MenuValues::fill(std::size_t item, const std::function<void(std::string_view, int)>& addItem) const {
    const auto count = choiceCount(item);
    if (count == 0 || !addItem) return false;
    for (std::size_t index = 0; index < count; ++index)
        addItem(items_[item].values[index], static_cast<int>(index));
    return true;
}
}
