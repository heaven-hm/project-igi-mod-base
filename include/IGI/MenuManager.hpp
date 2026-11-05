#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
class Scheduler;

enum class MenuItemKind {
    Action,
    Toggle,
    Integer,
    Float,
    List,
    Info,
    Information = Info,
    Separator,
    Submenu,
};

enum class MenuInput {
    ToggleVisibility,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
};

struct MenuItem {
    MenuItemKind kind = MenuItemKind::Info;
    std::string label;
    std::string value;
};

struct MenuSnapshot {
    std::string title;
    std::vector<MenuItem> items;
    std::size_t selectedIndex = 0;
    bool visible = false;
};

class MenuManager final {
public:
    explicit MenuManager(Scheduler& scheduler) noexcept;

    bool createMenu(std::string id, std::string title);
    bool addInfo(std::string_view menuId, std::string label, std::string value);
    MenuSnapshot snapshot() const;

private:
    struct Menu {
        std::string id;
        std::string title;
        std::vector<MenuItem> items;
    };

    Scheduler& scheduler_;
    std::vector<Menu> menus_;
    std::size_t activeMenuIndex_ = 0;
};
}
