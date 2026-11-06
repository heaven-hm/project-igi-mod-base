#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cstdio>

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/logging/logging.hpp"

extern "C" bool IGI_MenuManager_Internal_Initialize();
extern "C" bool IGI_MenuManager_Internal_Toggle();
extern "C" bool IGI_MenuManager_Internal_RequestShutdown();
extern "C" bool IGI_MenuManager_Internal_CanUnload();
extern "C" void IGI_MenuManager_Internal_Shutdown();

namespace {
BOOL WINAPI consoleControlHandler(DWORD event) {
    if (event != CTRL_CLOSE_EVENT) return FALSE;
    const HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);
    FreeConsole();
    return TRUE;
}

void attachConsole() {
    if (!AllocConsole()) return;
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);
    SetConsoleTitleA("IGI SDK - internal menu");
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
}

std::uint8_t virtualKeyFor(std::uint8_t dik) noexcept {
    return static_cast<std::uint8_t>(MapVirtualKeyA(dik, MAPVK_VSC_TO_VK_EX));
}

bool validationAutoStartEnabled() noexcept {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("IGI_SDK_AUTOSTART", value, sizeof(value));
    return length > 0 && length < sizeof(value) &&
           (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
}

void showLifecycleStatus(const char* text) noexcept {
    using StatusMessageShow = void (__cdecl*)(int, const char*, const char*, std::uint8_t*);
    const int context = *reinterpret_cast<const int*>(0x00A758AC);
    auto* const byte = reinterpret_cast<std::uint8_t*>(0x00567C74);
    reinterpret_cast<StatusMessageShow>(0x00485970)(context, text, "STATUSSCREEN:note.spr", byte);
}
}

static DWORD WINAPI igiSdkStartup(void* rawModule) {
    const auto module = static_cast<HMODULE>(rawModule);
    const auto config = igi::loadMenuConfig("config.ini");
    igi::setLogLevel(config.minimumLogLevel);
#ifndef NDEBUG
    attachConsole();
#endif
    igi::log(igi::LogLevel::Info, "internal DLL attached");
    if (!config.enabled) return 0;
    const std::uint8_t startKey = virtualKeyFor(config.hotkey);
    const std::uint8_t endKey = config.exitKey ? virtualKeyFor(config.exitKey) : 0;
    if (!startKey) {
        igi::log(igi::LogLevel::Error, "internal menu: configured start key is unsupported");
        return 0;
    }
    if (!validationAutoStartEnabled()) {
        while ((GetAsyncKeyState(startKey) & 0x8000) == 0) Sleep(25);
    }
    if (!IGI_MenuManager_Internal_Initialize()) {
        igi::log(igi::LogLevel::Error, "internal menu initialization failed after configured start key");
        return 0;
    }
    showLifecycleStatus("IGI SDK Mod Menu: enabled");
    bool startWasDown = (GetAsyncKeyState(startKey) & 0x8000) != 0;
    for (;;) {
        const bool startDown = (GetAsyncKeyState(startKey) & 0x8000) != 0;
        if (startDown && !startWasDown) {
            const bool toggled = IGI_MenuManager_Internal_Toggle();
            igi::log(toggled ? igi::LogLevel::Info : igi::LogLevel::Error,
                     toggled ? "internal menu toggle: configured start key handled" :
                               "internal menu toggle: configured start key failed");
        }
        startWasDown = startDown;
        if (endKey && (GetAsyncKeyState(endKey) & 0x8000) != 0) break;
        Sleep(25);
    }
    if (!IGI_MenuManager_Internal_RequestShutdown()) {
        igi::log(igi::LogLevel::Error, "internal unload: menu close request failed; DLL remains loaded");
        return 0;
    }
    const DWORD deadline = GetTickCount() + 5000;
    while (!IGI_MenuManager_Internal_CanUnload() && static_cast<LONG>(GetTickCount() - deadline) < 0) Sleep(25);
    if (!IGI_MenuManager_Internal_CanUnload()) {
        igi::log(igi::LogLevel::Error, "internal unload: screen did not detach; DLL remains loaded");
        return 0;
    }
    showLifecycleStatus("IGI SDK Mod Menu: disabled");
    IGI_MenuManager_Internal_Shutdown();
    SetConsoleCtrlHandler(consoleControlHandler, FALSE);
    FreeConsole();
    igi::log(igi::LogLevel::Info, "internal unload: callbacks and resources released; ejecting DLL");
    FreeLibraryAndExitThread(module, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, igiSdkStartup, module, 0, nullptr);
    }
    return TRUE;
}
