#include <windows.h>

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/logging/logging.hpp"

extern "C" bool IGI_MenuManager_Internal_Initialize();
extern "C" void IGI_MenuManager_Internal_Shutdown();

static DWORD WINAPI igiSdkStartup(void*) {
    igi::log(igi::LogLevel::Debug, "internal DLL attached; reading config.ini");
    const auto config = igi::loadMenuConfig("config.ini");
    igi::log(igi::LogLevel::Debug, "internal startup: config.ini parsed");
    if (!config.enabled) {
        igi::log(igi::LogLevel::Info, "internal menu disabled by config.ini");
        return 0;
    }

    std::uint8_t virtualKey = 0;
    switch (config.hotkey) {
    case 0x3D: virtualKey = VK_F3; break;
    case 0x3E: virtualKey = VK_F4; break;
    case 0x3F: virtualKey = VK_F5; break;
    case 0x40: virtualKey = VK_F6; break;
    case 0x41: virtualKey = VK_F7; break;
    case 0x42: virtualKey = VK_F8; break;
    default:
        igi::log(igi::LogLevel::Error, "unsupported mod menu hotkey in config.ini");
        return 0;
    }

    igi::log(igi::LogLevel::Debug, "internal menu waiting for configured hotkey");
    while ((GetAsyncKeyState(virtualKey) & 0x8000) == 0) Sleep(25);
    igi::log(igi::LogLevel::Info, "internal menu hotkey pressed; initializing");
    Sleep(250);
    if (!IGI_MenuManager_Internal_Initialize())
        igi::log(igi::LogLevel::Error, "internal menu initialization failed after hotkey");
    else
        igi::log(igi::LogLevel::Info, "internal menu initialization completed after hotkey");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, igiSdkStartup, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        IGI_MenuManager_Internal_Shutdown();
    }
    return TRUE;
}
