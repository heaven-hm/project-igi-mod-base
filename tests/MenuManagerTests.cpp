#include "config/menu_config.hpp"
#include "ui/menu_manager.hpp"
#include "ui/menu_script.hpp"
#include "ui/menu_commands.hpp"
#include "ui/menu_values.hpp"
#include "runtime/scheduler.hpp"

#include <cassert>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

using namespace std::chrono_literals;

void menuManagerNavigationTest();
void menuManagerSubmenuAndInvalidIdTest();
void menuManagerSchedulerBoundaryTest();
void menuManagerInputRepeatTest();
void menuManagerShutdownTest();
void externalImGuiMenuParityTest();
void verifiedMenuHelperContractTest();

void menuConfigTest() {
    assert(igi::parseDirectInputKey("F5") == 0x3F);
    assert(igi::parseDirectInputKey("f6") == 0x40);
    assert(igi::parseDirectInputKey("F12") == 0x58);
    assert(igi::parseDirectInputKey("End") == 0x4F);
    assert(igi::parseDirectInputKey("end") == 0x4F);
    assert(igi::parseDirectInputKey("Home") == 0x47);
    assert(igi::parseDirectInputKey("Delete") == 0x53);
    assert(igi::parseDirectInputKey("unknown", 0x3F) == 0x3F);
    // exit_key keeps the safe default when the value is malformed.
    igi::MenuConfig exitKeyConfig;
    assert(exitKeyConfig.exitKey == 0x4F);
    FILE* exitKeyFile = std::fopen("exit_key_test.ini", "w");
    assert(exitKeyFile);
    std::fputs("[mod_menu]\nhotkey=F5\nexit_key=Delete\n"
               "[imgui]\nx=120\ny=140\nwidth=520\nheight=440\ntheme=light\n"
               "alpha=0.85\nfont_scale=1.25\nwindow_rounding=8.0\nframe_rounding=5.0\n"
               "border_size=2.0\npause_game=false\nshow_fps=false\nshow_status=false\n"
               "movable=false\nresizable=false\ncollapsible=true\nauto_resize=true\n"
               "mouse_cursor=false\nkeyboard_nav=false\nsave_layout=true\n"
               "ini_file=custom-imgui.ini\n",
               exitKeyFile);
    std::fclose(exitKeyFile);
    const auto exitKeyLoaded = igi::loadMenuConfig("exit_key_test.ini");
    std::remove("exit_key_test.ini");
    assert(exitKeyLoaded.exitKey == 0x53);
    assert(exitKeyLoaded.hotkey == 0x3F);
    assert(exitKeyLoaded.imgui.x == 120);
    assert(exitKeyLoaded.imgui.y == 140);
    assert(exitKeyLoaded.imgui.width == 520);
    assert(exitKeyLoaded.imgui.height == 440);
    assert(exitKeyLoaded.imgui.theme == igi::ImGuiTheme::Light);
    assert(exitKeyLoaded.imgui.alpha > 0.84F && exitKeyLoaded.imgui.alpha < 0.86F);
    assert(exitKeyLoaded.imgui.fontScale > 1.24F && exitKeyLoaded.imgui.fontScale < 1.26F);
    assert(exitKeyLoaded.imgui.windowRounding == 8.0F);
    assert(exitKeyLoaded.imgui.frameRounding == 5.0F);
    assert(exitKeyLoaded.imgui.borderSize == 2.0F);
    assert(!exitKeyLoaded.imgui.pauseGame);
    assert(!exitKeyLoaded.imgui.showFps);
    assert(!exitKeyLoaded.imgui.showStatus);
    assert(!exitKeyLoaded.imgui.movable);
    assert(!exitKeyLoaded.imgui.resizable);
    assert(exitKeyLoaded.imgui.collapsible);
    assert(exitKeyLoaded.imgui.autoResize);
    assert(!exitKeyLoaded.imgui.mouseCursor);
    assert(!exitKeyLoaded.imgui.keyboardNav);
    assert(exitKeyLoaded.imgui.saveLayout);
    assert(exitKeyLoaded.imgui.iniFile == "custom-imgui.ini");
}

void rainEffectCommandCompilerTest() {
    std::vector<std::uint8_t> qvm;
    std::string error;
    const std::string command =
        "Task_New(4095, \"RainEffect\", \"IGIModPrecipitation\", FALSE, 50.0, 20.0, "
        "\"TRUE\\n\", 0.3125);";
    assert(igi::compileIgiMenuCommandScript(command, qvm, error));
    assert(!qvm.empty());
}

void menuManagerModelTest() {
    menuConfigTest();
    rainEffectCommandCompilerTest();
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
    externalImGuiMenuParityTest();
    verifiedMenuHelperContractTest();
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

namespace {
std::string readDemoSource(const std::string& relativePath) {
    std::ifstream input(std::string(IGI_SDK_SOURCE_DIR) + "/" + relativePath);
    assert(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

} // namespace

void externalImGuiMenuParityTest() {
    const auto internalBasic = readDemoSource("demos/internal/basic-mod/basic_menu.cpp");
    const auto externalBasic = readDemoSource("demos/external/basic-mod/basic_menu.cpp");
    assert(internalBasic == externalBasic);

    const auto internalSimple = readDemoSource("demos/internal/simple-mod/simple_menu.cpp");
    const auto externalSimple = readDemoSource("demos/external/simple-mod/simple_menu.cpp");
    assert(internalSimple == externalSimple);

    const auto internalAdvanced = readDemoSource("demos/internal/advanced-mod/advanced_menu.cpp");
    const auto externalAdvanced = readDemoSource("demos/external/advanced-mod/advanced_menu.cpp");
    assert(internalAdvanced == externalAdvanced);

    const auto imguiHost = readDemoSource("src/mod_base/imgui_menu_host.cpp");
    assert(imguiHost.find("ImGui_ImplDX7_Init") != std::string::npos);
    assert(imguiHost.find("ImGui_ImplDX7_RenderDrawData") != std::string::npos);
    assert(imguiHost.find("ImGui_ImplWin32_Init") != std::string::npos);
    assert(imguiHost.find("IGI_MenuManager_External_Initialize") != std::string::npos);
    assert(imguiHost.find("IGI_MenuManager_External_Toggle") != std::string::npos);
    assert(imguiHost.find("IGI_MenuManager_External_RequestShutdown") != std::string::npos);
    assert(imguiHost.find("IGI_MenuManager_External_Shutdown") != std::string::npos);

#if defined(__APPLE__)
    const char* externalMods[] = {
        "./igi-basic-mod-external.dylib",
        "./igi-simple-mod-external.dylib",
        "./igi-advanced-mod-external.dylib",
    };
    for (const char* path : externalMods) {
        void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        assert(handle != nullptr);
        using InitFn = bool (*)();
        using RunFrameFn = std::size_t (*)();
        using ShutdownFn = void (*)();
        auto* initFn = reinterpret_cast<InitFn>(dlsym(handle, "IGI_MenuManager_External_Initialize"));
        auto* runFrameFn = reinterpret_cast<RunFrameFn>(dlsym(handle, "IGI_MenuManager_External_RunFrame"));
        auto* shutdownFn = reinterpret_cast<ShutdownFn>(dlsym(handle, "IGI_MenuManager_External_Shutdown"));
        assert(initFn != nullptr && runFrameFn != nullptr && shutdownFn != nullptr);
        assert(initFn());
        assert(runFrameFn() == 0);
        shutdownFn();
        assert(dlclose(handle) == 0);
    }
#endif
}

void verifiedMenuHelperContractTest() {
    const auto helperSource = readDemoSource("demos/helpers/menu_helpers.cpp");
    assert(helperSource.find("IGI::AUDIO::MUSIC_SET_VOLUME") != std::string::npos);
    assert(helperSource.find("IGI::MISC::FRAMES_SET") != std::string::npos);
    assert(helperSource.find("IGI::HUMAN::VIEW_CAM") != std::string::npos);
    assert(helperSource.find("bool cyclePlayerWeapon(Runtime &runtime, int force)") !=
           std::string::npos);
    assert(helperSource.find("bool requestPlayerWeaponAnimation(Runtime &runtime, int animationId)") !=
           std::string::npos);
    assert(helperSource.find("bool restorePlayerVisualFeedback(Runtime &runtime)") !=
           std::string::npos);
    assert(helperSource.find("bool setPlayerInputEnabled(Runtime &runtime, bool enabled)") !=
           std::string::npos);
    assert(helperSource.find("bool setPlayerLookEnabled(Runtime &runtime, bool enabled)") !=
           std::string::npos);
    assert(helperSource.find("bool setMusicVolume(Runtime &runtime, float volume)") !=
           std::string::npos);
    assert(helperSource.find("bool setSfxVolume(Runtime &runtime, float volume)") !=
           std::string::npos);
    assert(helperSource.find("IGI::PLAYER::CYCLE_WEAPON") != std::string::npos);
    assert(helperSource.find("IGI::PLAYER::REQUEST_WEAPON_ANIM") != std::string::npos);
    assert(helperSource.find("IGI::HUMAN::LOCK_INPUT") != std::string::npos);
    assert(helperSource.find("IGI::HUMAN::ENABLE_LOOK") != std::string::npos);
    assert(helperSource.find("IGI::AUDIO::SET_SFX_VOLUME") != std::string::npos);

    const auto heavenMenu = readDemoSource("demos/heaven/heaven_menu.cpp");
    for (const char* category : {"Weapons", "Player", "Camera", "World", "Vehicles", "AI / NPC"}) {
        assert(heavenMenu.find(category) != std::string::npos);
    }
    assert(heavenMenu.find("Live Heli Target") != std::string::npos);
    assert(heavenMenu.find("weapon_pickup") != std::string::npos);
    assert(heavenMenu.find("vehicle_reset") != std::string::npos);
    assert(heavenMenu.find("vehicle_explode") != std::string::npos);
    assert(heavenMenu.find("npc_ensure_weapon") != std::string::npos);

    assert(helperSource.find("EntityHelpers::firstNpc") != std::string::npos);
    assert(helperSource.find("VehicleHelpers::explodeCar") != std::string::npos);
    assert(helperSource.find("NpcHelpers::ensureWeapon") != std::string::npos);
    assert(helperSource.find("AiHelpers::removeAlarmActions") != std::string::npos);
    // Live lists must keep the selected task instead of its old index.
    assert(helperSource.find("target.taskId == preserved") != std::string::npos);
    assert(helperSource.find("preservedIsLive") != std::string::npos);
}

// The retail (internal) menu collects its ListBox content from MenuValues, so a
// live list rebuilt from the task table must reach it through resync().
void menuValuesLiveResyncTest() {
    igi::MenuConfig config;
    igi::ModMenuItemConfig list;
    list.id = "vehicle_target";
    list.kind = igi::ModMenuItemKind::List;
    list.label = "Live Car Target";
    list.values = {"Car_351 [#351]", "Car_352 [#352]"};
    list.defaultSelection = 1;
    list.category = "Vehicles";
    config.items.push_back(list);
    igi::ModMenuItemConfig toggle;
    toggle.id = "npc_collision";
    toggle.kind = igi::ModMenuItemKind::Toggle;
    toggle.label = "Selected NPC Collision";
    toggle.defaultToggle = true;
    config.items.push_back(toggle);

    igi::MenuValues values(config);
    assert(values.choiceCount(0) == 2);
    assert(values.selected(0) == std::optional<std::size_t>(1));
    assert(values.toggle(1) == std::optional<bool>(true));

    // Unchanged config: nothing to re-collect.
    assert(!values.resync(config));

    // Car #352 was destroyed, #351 survived and a new car appeared; the
    // refresher resolved the surviving selection to index 0.
    config.items[0].values = {"Car_351 [#351]", "Car_900 [#900]"};
    config.items[0].defaultSelection = 0;
    assert(values.resync(config));
    assert(values.choiceCount(0) == 2);
    assert(values.choice(0, 0) == std::optional<std::string_view>("Car_351 [#351]"));
    assert(values.choice(0, 1) == std::optional<std::string_view>("Car_900 [#900]"));
    assert(values.selected(0) == std::optional<std::size_t>(0));
    // The pending-selection mirrors follow the refresh so Apply stays honest.
    assert(!values.selectedChanged(0));
    // Toggles are not touched by a list refresh.
    assert(values.toggle(1) == std::optional<bool>(true));

    // A user selection then an unrelated live refresh keeps the new index.
    assert(values.select(0, 1));
    assert(values.selectedChanged(0));
    config.items[0].values = {"Car_351 [#351]", "Car_900 [#900]", "Car_901 [#901]"};
    config.items[0].defaultSelection = 1;
    assert(values.resync(config));
    assert(values.selected(0) == std::optional<std::size_t>(1));

    // A different control set needs a new bridge, not a resync.
    config.items.push_back(toggle);
    assert(!values.resync(config));
    assert(values.choiceCount(0) == 3);
}

// A control set larger than the retail frame must switch to the scrollable
// command list, or the options past the frame edge become unreachable.
void menuCommandListTest() {
    igi::MenuConfig config;
    config.x = 95;
    config.y = 185;
    config.width = 455;
    config.height = 300;
    for (int index = 0; index < 3; ++index) {
        igi::ModMenuItemConfig action;
        action.id = "action_" + std::to_string(index);
        action.kind = igi::ModMenuItemKind::Action;
        action.label = "Action " + std::to_string(index);
        action.category = "World";
        config.items.push_back(action);
    }
    // A small control set keeps the direct per-control layout.
    assert(igi::menuRowCapacity(config) >= config.items.size());
    assert(!igi::menuNeedsCommandList(config));
    std::ostringstream smallScript;
    assert(igi::writeIgiMenuScreenScript(smallScript, config));
    assert(smallScript.str().find("IGI_SDK_FillList(-1)") == std::string::npos);
    assert(smallScript.str().find("IGI_SDK_Action(0)") != std::string::npos);

    while (config.items.size() <= igi::menuRowCapacity(config)) {
        igi::ModMenuItemConfig list;
        list.id = "list_" + std::to_string(config.items.size());
        list.kind = igi::ModMenuItemKind::List;
        list.label = "List " + std::to_string(config.items.size());
        list.category = "Vehicles";
        list.values = {"First", "Second"};
        config.items.push_back(list);
    }
    assert(igi::menuNeedsCommandList(config));

    const auto commands = igi::buildMenuCommands(config);
    assert(commands.size() > config.items.size());
    // List controls expand into one runnable entry per value.
    assert(commands[0].label == "World: Action 0" && commands[0].value.empty());
    assert(commands[2].label == "World: Action 2");
    assert(commands[3].label == "Vehicles: List 3: First" && commands[3].value == "First");
    assert(commands[4].label == "Vehicles: List 3: Second" && commands[4].value == "Second");
    assert(commands[3].itemIndex == commands[4].itemIndex);

    std::ostringstream script;
    assert(igi::writeIgiMenuScreenScript(script, config));
    const auto generated = script.str();
    assert(generated.find("\"commands\"") != std::string::npos);
    assert(generated.find("IGI_SDK_FillList(-1)") != std::string::npos);
    assert(generated.find("IGI_SDK_SetList(-1,this.nActiveID)") != std::string::npos);
    assert(generated.find("ListBox") != std::string::npos);
    // Every control is now inside the command list instead of a frame row.
    assert(generated.find("IGI_SDK_Action(0)") == std::string::npos);

    // The generated resource must still compile for the retail script runtime.
    std::vector<std::uint8_t> qvm;
    std::string error;
    assert(igi::compileIgiMenuScreenScript(config, qvm, error));
    assert(!qvm.empty() && error.empty());
}
