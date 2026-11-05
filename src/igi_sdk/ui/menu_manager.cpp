#include "igi_sdk/ui/menu_manager.hpp"

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/scheduler.hpp"

#include <algorithm>
#include <utility>

namespace igi {
MenuManager::MenuManager(Scheduler& scheduler) noexcept : scheduler_(scheduler) {}

bool MenuManager::createMenu(std::string id, std::string title) {
    if (shutdown_ || findMenu(id) != nullptr) return false;
    try {
        menus_.push_back(Menu{std::move(id), std::move(title), {}, 0});
        activeMenuIndex_ = menus_.size() - 1;
        visible_ = true;
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addInfo(std::string_view menuId, std::string label, std::string value) {
    Menu* menu = findMenu(menuId);
    if (menu == nullptr) return false;
    try {
        menu->items.push_back(MenuEntry{MenuItem{MenuItemKind::Info, std::move(label), std::move(value)}});
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addSeparator(std::string_view menuId) {
    Menu* menu = findMenu(menuId);
    if (menu == nullptr) return false;
    try {
        menu->items.push_back(MenuEntry{MenuItem{MenuItemKind::Separator}});
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addAction(std::string_view menuId, std::string label, std::function<void()> callback) {
    Menu* menu = findMenu(menuId);
    if (menu == nullptr) return false;
    try {
        MenuEntry entry{MenuItem{MenuItemKind::Action, std::move(label)}};
        entry.action = std::move(callback);
        menu->items.push_back(std::move(entry));
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addToggle(std::string_view menuId, std::string label, bool initialValue, std::function<void(bool)> callback) {
    Menu* menu = findMenu(menuId);
    if (menu == nullptr) return false;
    try {
        MenuEntry entry{MenuItem{MenuItemKind::Toggle, std::move(label)}};
        entry.item.boolValue = initialValue;
        entry.toggle = std::move(callback);
        menu->items.push_back(std::move(entry));
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addInteger(std::string_view menuId, std::string label, int initialValue, int minimum, int maximum,
                             int step, std::function<void(int)> callback) {
    Menu* menu = findMenu(menuId);
    if (menu == nullptr || minimum > maximum || step <= 0) return false;
    try {
        MenuEntry entry{MenuItem{MenuItemKind::Integer, std::move(label)}};
        entry.minimum = minimum;
        entry.maximum = maximum;
        entry.step = step;
        entry.item.numberValue = std::clamp(initialValue, minimum, maximum);
        entry.integer = std::move(callback);
        menu->items.push_back(std::move(entry));
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addFloat(std::string_view menuId, std::string label, double initialValue, double minimum,
                           double maximum, double step, std::function<void(double)> callback) {
    Menu* menu = findMenu(menuId);
    if (menu == nullptr || minimum > maximum || step <= 0.0) return false;
    try {
        MenuEntry entry{MenuItem{MenuItemKind::Float, std::move(label)}};
        entry.minimum = minimum;
        entry.maximum = maximum;
        entry.step = step;
        entry.item.numberValue = std::clamp(initialValue, minimum, maximum);
        entry.floating = std::move(callback);
        menu->items.push_back(std::move(entry));
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addList(std::string_view menuId, std::string label, std::vector<std::string> values,
                          std::size_t initialIndex, std::function<void(std::size_t)> callback) {
    Menu* menu = findMenu(menuId);
    if (menu == nullptr || values.empty()) return false;
    try {
        MenuEntry entry{MenuItem{MenuItemKind::List, std::move(label)}};
        entry.listValues = std::move(values);
        entry.item.listIndex = std::min(initialIndex, entry.listValues.size() - 1);
        entry.item.value = entry.listValues[entry.item.listIndex];
        entry.list = std::move(callback);
        menu->items.push_back(std::move(entry));
        return true;
    } catch (...) { return false; }
}

bool MenuManager::addSubmenu(std::string_view menuId, std::string label, std::string_view submenuId) {
    Menu* menu = findMenu(menuId);
    const Menu* submenu = findMenu(submenuId);
    if (menu == nullptr || submenu == nullptr) return false;
    const std::size_t submenuIndex = static_cast<std::size_t>(submenu - menus_.data());
    try {
        MenuEntry entry{MenuItem{MenuItemKind::Submenu, std::move(label)}};
        entry.submenuIndex = submenuIndex;
        menu->items.push_back(std::move(entry));
        return true;
    } catch (...) { return false; }
}

bool MenuManager::handleInput(MenuInput input) {
    if (shutdown_) return false;
    if (input == MenuInput::ToggleVisibility) {
        if (menus_.empty()) return false;
        visible_ = !visible_;
        return true;
    }
    if (menus_.empty() || !visible_) return false;

    switch (input) {
    case MenuInput::Up: return moveSelection(-1);
    case MenuInput::Down: return moveSelection(1);
    case MenuInput::Left: return adjustSelectedValue(-1);
    case MenuInput::Right: return adjustSelectedValue(1);
    case MenuInput::Confirm: {
        Menu& menu = menus_[activeMenuIndex_];
        if (menu.items.empty() || menu.selectedIndex >= menu.items.size()) return false;
        MenuEntry& item = menu.items[menu.selectedIndex];
        if (!isSelectable(item.item.kind)) return false;
        if (item.item.kind == MenuItemKind::Action) {
            if (scheduler_.enqueueOnce([callback = item.action] { callback(); }) == 0) {
                log(LogLevel::Warning, "menu action callback could not be scheduled");
                return false;
            }
        }
        if (item.item.kind == MenuItemKind::Toggle) {
            item.item.boolValue = !item.item.boolValue;
            const bool value = item.item.boolValue;
            if (scheduler_.enqueueOnce([callback = item.toggle, value] { callback(value); }) == 0) {
                log(LogLevel::Warning, "menu toggle callback could not be scheduled");
                return false;
            }
        }
        if (item.item.kind == MenuItemKind::Submenu && item.submenuIndex.has_value()) {
            menuStack_.push_back(activeMenuIndex_);
            activeMenuIndex_ = *item.submenuIndex;
        }
        return true;
    }
    case MenuInput::Back:
        if (menuStack_.empty()) visible_ = false;
        else {
            activeMenuIndex_ = menuStack_.back();
            menuStack_.pop_back();
        }
        return true;
    case MenuInput::ToggleVisibility: return false;
    }
    return false;
}

void MenuManager::updateInput(MenuInput input, std::chrono::milliseconds elapsed) {
    if (shutdown_ || menus_.empty() || !visible_) return;

    if (!isRepeatable(input)) {
        repeatingInput_.reset();
        repeatElapsed_ = std::chrono::milliseconds{0};
        repeatStarted_ = false;
        handleInput(input);
        return;
    }

    if (!repeatingInput_.has_value() || *repeatingInput_ != input) {
        repeatingInput_ = input;
        repeatElapsed_ = std::chrono::milliseconds{0};
        repeatStarted_ = false;
        handleInput(input);
        return;
    }

    if (elapsed.count() <= 0) return;
    repeatElapsed_ += elapsed;
    constexpr auto initialDelay = std::chrono::milliseconds{300};
    constexpr auto repeatInterval = std::chrono::milliseconds{100};
    if (!repeatStarted_) {
        if (repeatElapsed_ < initialDelay) return;
        repeatElapsed_ -= initialDelay;
        repeatStarted_ = true;
        handleInput(input);
    }
    while (repeatElapsed_ >= repeatInterval) {
        repeatElapsed_ -= repeatInterval;
        handleInput(input);
    }
}

void MenuManager::shutdown() noexcept {
    menus_.clear();
    menuStack_.clear();
    activeMenuIndex_ = 0;
    visible_ = false;
    shutdown_ = true;
    repeatingInput_.reset();
    repeatElapsed_ = std::chrono::milliseconds{0};
    repeatStarted_ = false;
}

MenuSnapshot MenuManager::snapshot() const {
    if (menus_.empty()) return {};
    const Menu& menu = menus_[activeMenuIndex_];
    MenuSnapshot result{menu.title, {}, menu.selectedIndex, visible_};
    result.items.reserve(menu.items.size());
    for (const MenuEntry& entry : menu.items) result.items.push_back(entry.item);
    return result;
}

MenuManager::Menu* MenuManager::findMenu(std::string_view menuId) noexcept {
    const auto iterator = std::find_if(menus_.begin(), menus_.end(), [menuId](const Menu& menu) { return menu.id == menuId; });
    return iterator == menus_.end() ? nullptr : &*iterator;
}

const MenuManager::Menu* MenuManager::findMenu(std::string_view menuId) const noexcept {
    const auto iterator = std::find_if(menus_.begin(), menus_.end(), [menuId](const Menu& menu) { return menu.id == menuId; });
    return iterator == menus_.end() ? nullptr : &*iterator;
}

bool MenuManager::moveSelection(int direction) noexcept {
    Menu& menu = menus_[activeMenuIndex_];
    if (menu.items.empty()) return false;
    const std::size_t start = std::min(menu.selectedIndex, menu.items.size() - 1);
    if (direction > 0) {
        for (std::size_t index = start + 1; index < menu.items.size(); ++index) {
            if (isSelectable(menu.items[index].item.kind)) {
                menu.selectedIndex = index;
                return true;
            }
        }
    } else {
        for (std::size_t index = start; index > 0; --index) {
            if (isSelectable(menu.items[index - 1].item.kind)) {
                menu.selectedIndex = index - 1;
                return true;
            }
        }
    }
    return true;
}

bool MenuManager::adjustSelectedValue(int direction) noexcept {
    Menu& menu = menus_[activeMenuIndex_];
    if (menu.items.empty() || menu.selectedIndex >= menu.items.size()) return false;
    MenuEntry& item = menu.items[menu.selectedIndex];
    if (item.item.kind == MenuItemKind::Integer || item.item.kind == MenuItemKind::Float) {
        item.item.numberValue = std::clamp(item.item.numberValue + direction * item.step, item.minimum, item.maximum);
        if (item.item.kind == MenuItemKind::Integer) {
            const int value = static_cast<int>(item.item.numberValue);
            if (scheduler_.enqueueOnce([callback = item.integer, value] { callback(value); }) == 0) {
                log(LogLevel::Warning, "menu integer callback could not be scheduled");
                return false;
            }
        } else {
            const double value = item.item.numberValue;
            if (scheduler_.enqueueOnce([callback = item.floating, value] { callback(value); }) == 0) {
                log(LogLevel::Warning, "menu float callback could not be scheduled");
                return false;
            }
        }
        return true;
    }
    if (item.item.kind != MenuItemKind::List || item.listValues.empty()) return false;
    const std::size_t size = item.listValues.size();
    item.item.listIndex = direction > 0 ? (item.item.listIndex + 1) % size : (item.item.listIndex + size - 1) % size;
    item.item.value = item.listValues[item.item.listIndex];
    const std::size_t value = item.item.listIndex;
    if (scheduler_.enqueueOnce([callback = item.list, value] { callback(value); }) == 0) {
        log(LogLevel::Warning, "menu list callback could not be scheduled");
        return false;
    }
    return true;
}

bool MenuManager::isSelectable(MenuItemKind kind) noexcept {
    return kind != MenuItemKind::Info && kind != MenuItemKind::Separator;
}

bool MenuManager::isRepeatable(MenuInput input) noexcept {
    return input == MenuInput::Up || input == MenuInput::Down || input == MenuInput::Left || input == MenuInput::Right;
}
}
