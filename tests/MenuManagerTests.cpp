#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/ui/menu_manager.hpp"
#include "igi_sdk/runtime/scheduler.hpp"

#include <cassert>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

void menuManagerNavigationTest();
void menuManagerSubmenuAndInvalidIdTest();
void menuManagerSchedulerBoundaryTest();
void menuManagerInputRepeatTest();
void menuManagerShutdownTest();

void menuConfigTest() {
    assert(igi::parseDirectInputKey("F5") == 0x3F);
    assert(igi::parseDirectInputKey("f6") == 0x40);
    assert(igi::parseDirectInputKey("unknown", 0x3F) == 0x3F);
}

void menuManagerModelTest() {
    menuConfigTest();
    igi::Scheduler scheduler;
    igi::MenuManager menuManager(scheduler);
    menuManager.createMenu("main", "Main");
    menuManager.addInfo("main", "Status", "Ready");
    assert(!menuManager.addInfo("unknown", "Ignored", "Ignored"));
    const auto snapshot = menuManager.snapshot();
    assert(snapshot.title == "Main");
    assert(snapshot.items.size() == 1);
    assert(snapshot.items[0].kind == igi::MenuItemKind::Info);
    menuManagerNavigationTest();
    menuManagerSubmenuAndInvalidIdTest();
    menuManagerSchedulerBoundaryTest();
    menuManagerInputRepeatTest();
    menuManagerShutdownTest();
}

void menuManagerInputRepeatTest() {
    igi::Scheduler scheduler;
    igi::MenuManager menuManager(scheduler);

    assert(menuManager.createMenu("main", "Main"));
    assert(menuManager.addInteger("main", "Distance", 0, 0, 10, 1, [](int) {}));

    menuManager.updateInput(igi::MenuInput::Right, 0ms);
    assert(menuManager.snapshot().items[0].numberValue == 1.0);
    menuManager.updateInput(igi::MenuInput::Right, 299ms);
    assert(menuManager.snapshot().items[0].numberValue == 1.0);
    menuManager.updateInput(igi::MenuInput::Right, 1ms);
    assert(menuManager.snapshot().items[0].numberValue == 2.0);
    menuManager.updateInput(igi::MenuInput::Right, 99ms);
    assert(menuManager.snapshot().items[0].numberValue == 2.0);
    menuManager.updateInput(igi::MenuInput::Right, 1ms);
    assert(menuManager.snapshot().items[0].numberValue == 3.0);
}

void menuManagerShutdownTest() {
    igi::Scheduler scheduler;
    igi::MenuManager menuManager(scheduler);

    assert(menuManager.createMenu("main", "Main"));
    assert(menuManager.addInfo("main", "Status", "Ready"));
    menuManager.shutdown();

    const auto snapshot = menuManager.snapshot();
    assert(snapshot.items.empty());
    assert(!snapshot.visible);
    assert(!menuManager.handleInput(igi::MenuInput::Confirm));
    assert(!menuManager.handleInput(igi::MenuInput::ToggleVisibility));
}

void menuManagerSchedulerBoundaryTest() {
    igi::Scheduler scheduler;
    igi::MenuManager menuManager(scheduler);
    int calls = 0;

    assert(menuManager.createMenu("main", "Main"));
    assert(menuManager.addAction("main", "Apply", [&] { ++calls; }));
    assert(menuManager.handleInput(igi::MenuInput::Confirm));
    assert(calls == 0);
    scheduler.runPendingFromGameHook();
    assert(calls == 1);
}

void menuManagerNavigationTest() {
    igi::Scheduler scheduler;
    igi::MenuManager menuManager(scheduler);
    int callbackCount = 0;

    assert(menuManager.createMenu("main", "Main"));
    assert(menuManager.addInfo("main", "Status", "Ready"));
    assert(menuManager.addToggle("main", "God mode", false, [&](bool) { ++callbackCount; }));
    assert(menuManager.addInteger("main", "Distance", 10, 1, 20, 1, [&](int) { ++callbackCount; }));
    assert(menuManager.addFloat("main", "Speed", 1.0, 0.5, 1.5, 0.25, [&](double) { ++callbackCount; }));
    assert(menuManager.addList("main", "Weapon", {"Pistol", "Rifle", "Launcher"}, 0,
                               [&](std::size_t) { ++callbackCount; }));
    assert(menuManager.addSeparator("main"));
    assert(menuManager.addAction("main", "Apply", [&] { ++callbackCount; }));

    assert(menuManager.handleInput(igi::MenuInput::Down));
    assert(menuManager.snapshot().selectedIndex == 1);
    assert(menuManager.handleInput(igi::MenuInput::Confirm));
    assert(menuManager.snapshot().items[1].boolValue);

    assert(menuManager.handleInput(igi::MenuInput::Down));
    assert(menuManager.snapshot().selectedIndex == 2);
    assert(menuManager.handleInput(igi::MenuInput::Right));
    assert(menuManager.snapshot().items[2].numberValue == 11.0);
    for (int index = 0; index < 20; ++index) assert(menuManager.handleInput(igi::MenuInput::Right));
    assert(menuManager.snapshot().items[2].numberValue == 20.0);
    for (int index = 0; index < 20; ++index) assert(menuManager.handleInput(igi::MenuInput::Left));
    assert(menuManager.snapshot().items[2].numberValue == 1.0);

    assert(menuManager.handleInput(igi::MenuInput::Down));
    assert(menuManager.handleInput(igi::MenuInput::Left));
    assert(menuManager.snapshot().items[3].numberValue == 0.75);
    for (int index = 0; index < 20; ++index) assert(menuManager.handleInput(igi::MenuInput::Left));
    assert(menuManager.snapshot().items[3].numberValue == 0.5);

    assert(menuManager.handleInput(igi::MenuInput::Down));
    assert(menuManager.handleInput(igi::MenuInput::Left));
    assert(menuManager.snapshot().items[4].listIndex == 2);
    assert(menuManager.snapshot().items[4].value == "Launcher");
    assert(menuManager.handleInput(igi::MenuInput::Right));
    assert(menuManager.snapshot().items[4].listIndex == 0);
    assert(menuManager.snapshot().items[4].value == "Pistol");

    assert(menuManager.handleInput(igi::MenuInput::Down));
    assert(menuManager.snapshot().selectedIndex == 6);
    assert(menuManager.handleInput(igi::MenuInput::Confirm));
    assert(callbackCount == 0);
}

void menuManagerSubmenuAndInvalidIdTest() {
    igi::Scheduler scheduler;
    igi::MenuManager menuManager(scheduler);

    assert(menuManager.createMenu("settings", "Settings"));
    assert(menuManager.createMenu("main", "Main"));
    assert(menuManager.addInfo("main", "Status", "Ready"));
    assert(menuManager.addSubmenu("main", "Settings", "settings"));
    assert(menuManager.addToggle("settings", "Music", true, [](bool) {}));
    assert(!menuManager.addAction("unknown", "Ignored", [] {}));
    assert(!menuManager.addSubmenu("main", "Missing", "unknown"));

    assert(menuManager.handleInput(igi::MenuInput::Down));
    assert(menuManager.handleInput(igi::MenuInput::Confirm));
    assert(menuManager.snapshot().title == "Settings");
    assert(menuManager.snapshot().selectedIndex == 0);
    assert(menuManager.handleInput(igi::MenuInput::Back));
    assert(menuManager.snapshot().title == "Main");
    assert(menuManager.snapshot().selectedIndex == 1);
    assert(menuManager.handleInput(igi::MenuInput::Back));
    assert(!menuManager.snapshot().visible);
    assert(menuManager.handleInput(igi::MenuInput::ToggleVisibility));
    assert(menuManager.snapshot().visible);
}
