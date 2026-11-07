#include "ui/igi_menu_manager.hpp"
#include "config/menu_config.hpp"

#include <algorithm>
#include <utility>

namespace {
using namespace igi;

IgiMenuWidget text(std::string key, std::string label, std::string click = {},
                   std::string sound = {}) {
    return {IgiWidgetKind::MenuText,
            -1,
            std::move(key),
            std::move(label),
            "font3.fnt",
            {},
            {},
            std::move(click),
            std::move(sound),
            0,
            0,
            0,
            0,
            true};
}

IgiMenuScreen mainScreen() {
    IgiMenuScreen screen{
        900, "Main Menu", "mainmenu.pic", "MenuManager_ActivatePopupScreen(934)", {}};
    screen.widgets.push_back(text("single player", "Single Player", "MenuManager_PushScreen(914)"));
    screen.widgets.push_back(text("configuration", "Configuration", "MenuManager_PushScreen(904)"));
    screen.widgets.push_back(
        text("Select Player", "Select Player", "MenuManager_RequestScreen(912, FALSE)"));
    screen.widgets.push_back(text("quit", "Quit", "MenuManager_ActivatePopupScreen(934)"));
    screen.widgets.push_back(text({}, "Read Me", "MenuManager_PushScreen(913)"));
    screen.widgets.push_back(text({}, "Credits", "MenuManager_PushScreen(916)"));
    return screen;
}

IgiMenuScreen inGameScreen() {
    IgiMenuScreen screen{900, "Main Menu", {}, "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", {}};
    screen.widgets.push_back(
        text("resume", "Resume Game", "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)"));
    screen.widgets.push_back(
        text("restart", "Restart Game", "MenuManager_ActivatePopupScreen(932)"));
    screen.widgets.push_back(text("sound", "Sound", "MenuManager_PushScreen(902)"));
    screen.widgets.push_back(text("controls", "Controls", "MenuManager_PushScreen(903)"));
    screen.widgets.push_back(
        text("quit", "Quit to Main Menu", "MenuManager_ActivatePopupScreen(931)"));
    return screen;
}

IgiMenuScreen configurationScreen() {
    IgiMenuScreen screen{904, "Configuration", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}};
    screen.widgets.push_back(text("Graphics", "Graphics", "MenuManager_RequestScreen(905, FALSE)"));
    screen.widgets.push_back(text("Sound", "Sound", "MenuManager_RequestScreen(906, FALSE)"));
    screen.widgets.push_back(text("Controls", "Controls", "MenuManager_RequestScreen(907, FALSE)"));
    screen.widgets.push_back(text("Language", "Language", "MenuManager_RequestScreen(910, FALSE)"));
    screen.widgets.push_back(
        text("Content control", "Content Control", "MenuManager_PushScreen(915)"));
    return screen;
}

IgiMenuScreen modScreen(const MenuConfig &config) {
    IgiMenuScreen screen{
        config.screenId, "IGI SDK Mod Menu", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}};
    for (const auto &item : config.items) {
        IgiMenuWidget widget{IgiWidgetKind::MenuText, -1, item.id, item.label, {}, {}, {},
                             item.nativeAction,       {}, 0,       0,          0,  0,  true};
        if (item.kind == ModMenuItemKind::Toggle) {
            widget.kind = IgiWidgetKind::ToggleBox;
            widget.readScript = item.defaultToggle ? "TRUE" : "FALSE";
            widget.writeScript = item.nativeAction;
            widget.clickScript.clear();
        } else if (item.kind == ModMenuItemKind::List) {
            widget.kind = IgiWidgetKind::ListBox;
            widget.resource = item.values.empty() ? "" : item.values.front();
            widget.writeScript = item.nativeAction;
            widget.clickScript.clear();
        } else if (item.kind == ModMenuItemKind::Slider) {
            widget.kind = IgiWidgetKind::SlideBar;
            widget.writeScript = item.nativeAction;
            widget.clickScript.clear();
        }
        screen.widgets.push_back(std::move(widget));
    }
    return screen;
}
} // namespace

namespace igi {
IgiMenuManager IgiMenuManager::create(IgiMenuSet set) {
    return create(set, MenuConfig{});
}

IgiMenuManager IgiMenuManager::create(IgiMenuSet set, const MenuConfig &config) {
    IgiMenuManager manager(set);
    manager.screens_.push_back(set == IgiMenuSet::Main ? mainScreen() : inGameScreen());
    if (set == IgiMenuSet::Main)
        manager.screens_.push_back(configurationScreen());
    if (set == IgiMenuSet::InGame) {
        manager.screens_.push_back(IgiMenuScreen{
            902, "Sound Configuration", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}});
        manager.screens_.push_back(IgiMenuScreen{
            903, "Controls Configuration", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}});
        if (config.screenId != 900 && config.screenId != 902 && config.screenId != 903 &&
            config.screenId != 931 && config.screenId != 932 && config.screenId != 934)
            manager.screens_.push_back(modScreen(config));
    }
    manager.screens_.push_back(IgiMenuScreen{
        931, "Quit Main Menu", "", "MenuManager_LeaveMenus(0, MENUMANAGER_QUIT)", {}});
    manager.screens_.push_back(IgiMenuScreen{
        932, "Restart Mission", "", "MenuManager_LeaveMenus(900, MENUMANAGER_RESTARTLEVEL)", {}});
    manager.screens_.push_back(IgiMenuScreen{
        934, "Quit Main Menu", "", "MenuManager_LeaveMenus(0, MENUMANAGER_QUIT)", {}});
    manager.currentScreenId_ = 900;
    return manager;
}

const IgiMenuScreen *IgiMenuManager::findScreen(std::int32_t id) const noexcept {
    for (const auto &screen : screens_)
        if (screen.id == id)
            return &screen;
    return nullptr;
}

IgiMenuScreen *IgiMenuManager::findScreen(std::int32_t id) noexcept {
    for (auto &screen : screens_)
        if (screen.id == id)
            return &screen;
    return nullptr;
}

const IgiMenuScreen *IgiMenuManager::currentScreen() const noexcept {
    return findScreen(currentScreenId_);
}

bool IgiMenuManager::pushScreen(std::int32_t id) {
    if (findScreen(id) == nullptr)
        return false;
    screenStack_.push_back(currentScreenId_);
    currentScreenId_ = id;
    pendingScreenId_ = 0;
    return true;
}

bool IgiMenuManager::requestScreen(std::int32_t id, bool commit) {
    if (findScreen(id) == nullptr)
        return false;
    if (commit)
        return pushScreen(id);
    pendingScreenId_ = id;
    return true;
}

bool IgiMenuManager::popScreen(bool commit) {
    if (!commit)
        pendingScreenId_ = 0;
    if (screenStack_.empty())
        return false;
    currentScreenId_ = screenStack_.back();
    screenStack_.pop_back();
    return true;
}

bool IgiMenuManager::activatePopup(std::int32_t id) {
    return pushScreen(id);
}

bool IgiMenuManager::leaveMenus(std::int32_t id, std::string_view) {
    if (findScreen(id) == nullptr && id != 0 && id != -1)
        return false;
    currentScreenId_ = id;
    screenStack_.clear();
    pendingScreenId_ = 0;
    return true;
}

void IgiMenuManager::reset() noexcept {
    screenStack_.clear();
    pendingScreenId_ = 0;
    currentScreenId_ = screens_.empty() ? 0 : screens_.front().id;
}
} // namespace igi
