#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
class Scheduler;

enum class MenuItemKind { Action, Toggle, Integer, Float, List, Info, Information = Info, Separator, Submenu };
enum class MenuInput { ToggleVisibility, Up, Down, Left, Right, Confirm, Back };

struct MenuItem {
    MenuItemKind kind = MenuItemKind::Info;
    std::string label;
    std::string value;
    bool boolValue = false;
    double numberValue = 0.0;
    std::size_t listIndex = 0;
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
    bool addSeparator(std::string_view menuId);
    bool addAction(std::string_view menuId, std::string label, std::function<void()> callback);
    bool addToggle(std::string_view menuId, std::string label, bool initialValue, std::function<void(bool)> callback);
    bool addInteger(std::string_view menuId, std::string label, int initialValue, int minimum, int maximum, int step,
                    std::function<void(int)> callback);
    bool addFloat(std::string_view menuId, std::string label, double initialValue, double minimum, double maximum,
                  double step, std::function<void(double)> callback);
    bool addList(std::string_view menuId, std::string label, std::vector<std::string> values,
                 std::size_t initialIndex, std::function<void(std::size_t)> callback);
    bool addSubmenu(std::string_view menuId, std::string label, std::string_view submenuId);
    bool handleInput(MenuInput input);
    MenuSnapshot snapshot() const;

private:
    struct MenuEntry {
        MenuItem item;
        double minimum = 0.0;
        double maximum = 0.0;
        double step = 0.0;
        std::vector<std::string> listValues;
        std::optional<std::size_t> submenuIndex;
        std::function<void()> action;
        std::function<void(bool)> toggle;
        std::function<void(int)> integer;
        std::function<void(double)> floating;
        std::function<void(std::size_t)> list;
    };

    struct Menu {
        std::string id;
        std::string title;
        std::vector<MenuEntry> items;
        std::size_t selectedIndex = 0;
    };

    Menu* findMenu(std::string_view menuId) noexcept;
    const Menu* findMenu(std::string_view menuId) const noexcept;
    bool moveSelection(int direction) noexcept;
    bool adjustSelectedValue(int direction) noexcept;
    static bool isSelectable(MenuItemKind kind) noexcept;

    Scheduler& scheduler_;
    std::vector<Menu> menus_;
    std::vector<std::size_t> menuStack_;
    std::size_t activeMenuIndex_ = 0;
    bool visible_ = true;
};
}
