#include "ui/menu_values.hpp"

#include <algorithm>

namespace igi {
std::size_t MenuValues::choiceCountFor(const ModMenuItemConfig &item) const {
    if (item.kind == ModMenuItemKind::List)
        return item.values.size();
    if (item.kind == ModMenuItemKind::Input)
        return inputSamples(item).size();
    return 0;
}

std::string MenuValues::choiceLabelFor(const ModMenuItemConfig &item, std::size_t index) const {
    if (item.kind == ModMenuItemKind::List)
        return index < item.values.size() ? item.values[index] : std::string{};
    if (item.kind == ModMenuItemKind::Input) {
        const auto samples = inputSamples(item);
        return index < samples.size() ? std::to_string(samples[index]) : std::string{};
    }
    return {};
}

MenuValues::MenuValues(const MenuConfig &config)
    : items_(config.items), selected_(items_.size(), 0), toggles_(items_.size(), false),
      committedSelected_(items_.size(), 0), committedToggles_(items_.size(), false),
      inputLabels_(items_.size()) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].kind == ModMenuItemKind::Input) {
            for (const int sample : inputSamples(items_[i]))
                inputLabels_[i].push_back(std::to_string(sample));
        }
        toggles_[i] = items_[i].defaultToggle;
        committedToggles_[i] = items_[i].defaultToggle;
        if (items_[i].kind == ModMenuItemKind::List &&
            items_[i].defaultSelection < items_[i].values.size()) {
            selected_[i] = items_[i].defaultSelection;
            committedSelected_[i] = items_[i].defaultSelection;
        }
    }
}

void MenuValues::snapshot() noexcept {
    committedSelected_ = selected_;
    committedToggles_ = toggles_;
}

void MenuValues::revert() noexcept {
    selected_ = committedSelected_;
    toggles_ = committedToggles_;
}

void MenuValues::commit() noexcept {
    committedSelected_ = selected_;
    committedToggles_ = toggles_;
}

bool MenuValues::selectedChanged(std::size_t item) const noexcept {
    return item < items_.size() && items_[item].kind == ModMenuItemKind::List &&
           selected_[item] != committedSelected_[item];
}

bool MenuValues::toggleChanged(std::size_t item) const noexcept {
    return item < items_.size() && items_[item].kind == ModMenuItemKind::Toggle &&
           toggles_[item] != committedToggles_[item];
}

std::optional<bool> MenuValues::toggle(std::size_t item) const noexcept {
    if (item >= items_.size() || items_[item].kind != ModMenuItemKind::Toggle)
        return std::nullopt;
    return toggles_[item];
}

bool MenuValues::setToggle(std::size_t item, bool value) noexcept {
    if (item >= items_.size() || items_[item].kind != ModMenuItemKind::Toggle)
        return false;
    toggles_[item] = value;
    return true;
}

std::size_t MenuValues::choiceCount(std::size_t item) const noexcept {
    if (item >= items_.size())
        return 0;
    return choiceCountFor(items_[item]);
}

std::optional<std::string_view> MenuValues::choice(std::size_t item,
                                                   std::size_t index) const noexcept {
    if (index >= choiceCount(item))
        return std::nullopt;
    if (items_[item].kind == ModMenuItemKind::Input) {
        const auto &labels = inputLabels_[item];
        return index < labels.size() ? std::optional<std::string_view>(labels[index])
                                     : std::nullopt;
    }
    return items_[item].values[index];
}

std::optional<std::size_t> MenuValues::selected(std::size_t item) const noexcept {
    if (choiceCount(item) == 0)
        return std::nullopt;
    return selected_[item];
}

bool MenuValues::select(std::size_t item, std::size_t index) noexcept {
    if (index >= choiceCount(item))
        return false;
    selected_[item] = index;
    return true;
}

bool MenuValues::resync(const MenuConfig &config) {
    inputLabels_.assign(items_.size(), {});
    if (config.items.size() != items_.size()) {
        // A different control set needs a new bridge and screen, not a resync.
        return false;
    }
    bool changed = false;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto &live = config.items[i];
        if (items_[i].kind == ModMenuItemKind::List && items_[i].values != live.values) {
            items_[i].values = live.values;
            // The refresher resolved the previously selected task identity to a
            // new index; adopt it on both mirrors so Apply/Cancel stay honest.
            const std::size_t selection =
                live.defaultSelection < live.values.size() ? live.defaultSelection : 0;
            selected_[i] = selection;
            committedSelected_[i] = selection;
            changed = true;
        }
        items_[i].label = live.label;
    }
    return changed;
}

bool MenuValues::fill(std::size_t item,
                      const std::function<void(std::string_view, int)> &addItem) const {
    const auto count = choiceCount(item);
    if (count == 0 || !addItem)
        return false;
    for (std::size_t index = 0; index < count; ++index) {
        const auto label = choice(item, index);
        if (!label)
            return false;
        addItem(*label, static_cast<int>(index));
    }
    return true;
}
} // namespace igi
