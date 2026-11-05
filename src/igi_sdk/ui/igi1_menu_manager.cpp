#include "igi_sdk/ui/igi1_menu_manager.hpp"

#include <algorithm>
#include <utility>

namespace {
using namespace igi;

Igi1MenuWidget text(std::string key, std::string label, std::string click = {}, std::string sound = {}) {
    return {Igi1WidgetKind::MenuText, -1, std::move(key), std::move(label), "font3.fnt", {}, {}, std::move(click), std::move(sound), 0, 0, 0, 0, true};
}

Igi1MenuScreen mainScreen() {
    Igi1MenuScreen screen{900, "Main Menu", "mainmenu.pic", "MenuManager_ActivatePopupScreen(934)", {}};
    screen.widgets.push_back(text("single player", "Single Player", "MenuManager_PushScreen(914)"));
    screen.widgets.push_back(text("configuration", "Configuration", "MenuManager_PushScreen(904)"));
    screen.widgets.push_back(text("Select Player", "Select Player", "MenuManager_RequestScreen(912, FALSE)"));
    screen.widgets.push_back(text("quit", "Quit", "MenuManager_ActivatePopupScreen(934)"));
    screen.widgets.push_back(text({}, "Read Me", "MenuManager_PushScreen(913)"));
    screen.widgets.push_back(text({}, "Credits", "MenuManager_PushScreen(916)"));
    return screen;
}

Igi1MenuScreen inGameScreen() {
    Igi1MenuScreen screen{900, "Main Menu", {}, "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", {}};
    screen.widgets.push_back(text("resume", "Resume Game", "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)"));
    screen.widgets.push_back(text("restart", "Restart Game", "MenuManager_ActivatePopupScreen(932)"));
    screen.widgets.push_back(text("sound", "Sound", "MenuManager_PushScreen(902)"));
    screen.widgets.push_back(text("controls", "Controls", "MenuManager_PushScreen(903)"));
    screen.widgets.push_back(text("quit", "Quit to Main Menu", "MenuManager_ActivatePopupScreen(931)"));
    screen.widgets.push_back(text("Vita", "Vita", "MenuManager_PushScreen(980)"));
    return screen;
}

Igi1MenuScreen configurationScreen() {
    Igi1MenuScreen screen{904, "Configuration", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}};
    screen.widgets.push_back(text("Graphics", "Graphics", "MenuManager_RequestScreen(905, FALSE)"));
    screen.widgets.push_back(text("Sound", "Sound", "MenuManager_RequestScreen(906, FALSE)"));
    screen.widgets.push_back(text("Controls", "Controls", "MenuManager_RequestScreen(907, FALSE)"));
    screen.widgets.push_back(text("Language", "Language", "MenuManager_RequestScreen(910, FALSE)"));
    screen.widgets.push_back(text("Content control", "Content Control", "MenuManager_PushScreen(915)"));
    screen.widgets.push_back(text("Vita", "Vita", "MenuManager_RequestScreen(980, FALSE)"));
    return screen;
}

Igi1MenuScreen modScreen() {
    Igi1MenuScreen screen{990, "IGI SDK Mod Menu", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}};
    screen.widgets.push_back(text("weather", "Weather: Rain", "IGI_SDK_SetWeather(\"rain\")"));
    screen.widgets.push_back(text("weapons", "Give All Weapons", "IGI_SDK_GiveAllWeapons()"));
    screen.widgets.push_back(text("sound", "Sound Profile", "IGI_SDK_SetSoundProfile()"));
    screen.widgets.push_back(text("camera", "Free Camera", "IGI_SDK_ToggleFreeCamera()"));
    screen.widgets.push_back(text("god", "Invulnerability", "IGI_SDK_ToggleInvulnerability()"));
    screen.widgets.push_back(text("ammo", "Unlimited Ammo", "HumanPlayer_SetUnlimitedAmmo()"));
    screen.widgets.push_back(text("visibility", "Enemy Visibility", "IGI_SDK_ToggleEnemyVisibility()"));
    screen.widgets.push_back(text("speed", "Player Speed", "IGI_SDK_SetPlayerSpeed()"));
    screen.widgets.push_back(text("teleport", "Mission Teleport", "IGI_SDK_OpenTeleport()"));
    screen.widgets.push_back(text("logs", "Runtime Logs", "IGI_SDK_ToggleRuntimeLogs()"));
    return screen;
}
}

namespace igi {
Igi1MenuManager Igi1MenuManager::create(Igi1MenuSet set) {
    Igi1MenuManager manager(set);
    manager.screens_.push_back(set == Igi1MenuSet::Main ? mainScreen() : inGameScreen());
    if (set == Igi1MenuSet::Main) manager.screens_.push_back(configurationScreen());
    if (set == Igi1MenuSet::InGame) {
        manager.screens_.push_back(Igi1MenuScreen{902, "Sound Configuration", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}});
        manager.screens_.push_back(Igi1MenuScreen{903, "Controls Configuration", "mainmenu.pic", "MenuManager_PopScreen(FALSE)", {}});
        manager.screens_.push_back(modScreen());
    }
    manager.screens_.push_back(Igi1MenuScreen{931, "Quit Main Menu", "", "MenuManager_LeaveMenus(0, MENUMANAGER_QUIT)", {}});
    manager.screens_.push_back(Igi1MenuScreen{932, "Restart Mission", "", "MenuManager_LeaveMenus(900, MENUMANAGER_RESTARTLEVEL)", {}});
    manager.screens_.push_back(Igi1MenuScreen{934, "Quit Main Menu", "", "MenuManager_LeaveMenus(0, MENUMANAGER_QUIT)", {}});
    manager.currentScreenId_ = 900;
    return manager;
}

const Igi1MenuScreen* Igi1MenuManager::findScreen(std::int32_t id) const noexcept {
    for (const auto& screen : screens_) if (screen.id == id) return &screen;
    return nullptr;
}

Igi1MenuScreen* Igi1MenuManager::findScreen(std::int32_t id) noexcept {
    for (auto& screen : screens_) if (screen.id == id) return &screen;
    return nullptr;
}

const Igi1MenuScreen* Igi1MenuManager::currentScreen() const noexcept {
    return findScreen(currentScreenId_);
}

bool Igi1MenuManager::pushScreen(std::int32_t id) {
    if (findScreen(id) == nullptr) return false;
    screenStack_.push_back(currentScreenId_);
    currentScreenId_ = id;
    pendingScreenId_ = 0;
    return true;
}

bool Igi1MenuManager::requestScreen(std::int32_t id, bool commit) {
    if (findScreen(id) == nullptr) return false;
    if (commit) return pushScreen(id);
    pendingScreenId_ = id;
    return true;
}

bool Igi1MenuManager::popScreen(bool commit) {
    if (!commit) pendingScreenId_ = 0;
    if (screenStack_.empty()) return false;
    currentScreenId_ = screenStack_.back();
    screenStack_.pop_back();
    return true;
}

bool Igi1MenuManager::activatePopup(std::int32_t id) { return pushScreen(id); }

bool Igi1MenuManager::leaveMenus(std::int32_t id, std::string_view) {
    if (findScreen(id) == nullptr && id != 0 && id != -1) return false;
    currentScreenId_ = id;
    screenStack_.clear();
    pendingScreenId_ = 0;
    return true;
}

void Igi1MenuManager::reset() noexcept {
    screenStack_.clear();
    pendingScreenId_ = 0;
    currentScreenId_ = screens_.empty() ? 0 : screens_.front().id;
}
}
