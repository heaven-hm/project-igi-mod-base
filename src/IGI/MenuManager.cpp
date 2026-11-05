#include "IGI/MenuManager.hpp"

#include "IGI/Scheduler.hpp"

#include <algorithm>

namespace igi {
MenuManager::MenuManager(Scheduler& scheduler) noexcept : scheduler_(scheduler) {}

bool MenuManager::createMenu(std::string id, std::string title) {
    const auto existing = std::find_if(menus_.begin(), menus_.end(), [&id](const Menu& menu) {
        return menu.id == id;
    });
    if (existing != menus_.end()) return false;

    menus_.push_back(Menu{std::move(id), std::move(title), {}});
    activeMenuIndex_ = menus_.size() - 1;
    return true;
}

bool MenuManager::addInfo(std::string_view menuId, std::string label, std::string value) {
    const auto menu = std::find_if(menus_.begin(), menus_.end(), [menuId](const Menu& candidate) {
        return candidate.id == menuId;
    });
    if (menu == menus_.end()) return false;

    menu->items.push_back(MenuItem{MenuItemKind::Info, std::move(label), std::move(value)});
    return true;
}

MenuSnapshot MenuManager::snapshot() const {
    if (menus_.empty()) return {};

    const Menu& menu = menus_[activeMenuIndex_];
    return MenuSnapshot{menu.title, menu.items, 0, true};
}
}
