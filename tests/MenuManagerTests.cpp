#include "config/menu_config.hpp"
#include "ui/menu_manager.hpp"
#include "ui/menu_script.hpp"
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
        "\"TRUE\\n\", 0.06);";
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

std::string normalizeMenuHelperInclude(std::string source) {
    const std::string externalInclude = "../../internal/helpers/menu_helpers.hpp";
    const std::string internalInclude = "../helpers/menu_helpers.hpp";
    const auto pos = source.find(externalInclude);
    if (pos != std::string::npos) {
        source.replace(pos, externalInclude.size(), internalInclude);
    }
    return source;
}
} // namespace

void externalImGuiMenuParityTest() {
    const auto internalBasic = readDemoSource("demos/internal/basic-mod/basic_menu.cpp");
    const auto externalBasic =
        normalizeMenuHelperInclude(readDemoSource("demos/external/basic-mod/basic_menu.cpp"));
    assert(internalBasic == externalBasic);

    const auto internalSimple = readDemoSource("demos/internal/simple-mod/simple_menu.cpp");
    const auto externalSimple =
        normalizeMenuHelperInclude(readDemoSource("demos/external/simple-mod/simple_menu.cpp"));
    assert(internalSimple == externalSimple);

    const auto internalAdvanced = readDemoSource("demos/internal/advanced-mod/advanced_menu.cpp");
    const auto externalAdvanced =
        normalizeMenuHelperInclude(readDemoSource("demos/external/advanced-mod/advanced_menu.cpp"));
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
