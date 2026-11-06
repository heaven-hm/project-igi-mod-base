#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/logging/logging.hpp"

extern "C" bool IGI_MenuManager_Internal_Initialize();
extern "C" void IGI_MenuManager_Internal_Shutdown();

namespace {
// Debug builds may allocate a console for tracing.
void attachConsole() {
    if (!AllocConsole()) return;
    SetConsoleTitleA("IGI SDK - internal menu");
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
}

std::uint8_t virtualKeyFor(std::uint8_t dik) noexcept {
    switch (dik) {
    case 0x3D: return VK_F3;
    case 0x3E: return VK_F4;
    case 0x3F: return VK_F5;
    case 0x40: return VK_F6;
    case 0x41: return VK_F7;
    case 0x42: return VK_F8;
    case 0x47: return VK_HOME;
    case 0x4F: return VK_END;
    case 0x49: return VK_PRIOR;
    case 0x51: return VK_NEXT;
    case 0x52: return VK_INSERT;
    case 0x53: return VK_DELETE;
    default: return 0;
    }
}

const char* levelName(igi::LogLevel level) noexcept {
    switch (level) {
    case igi::LogLevel::Trace: return "trace";
    case igi::LogLevel::Debug: return "debug";
    case igi::LogLevel::Info: return "info";
    case igi::LogLevel::Warning: return "warning";
    case igi::LogLevel::Error: return "error";
    case igi::LogLevel::Critical: return "critical";
    }
    return "unknown";
}
}

static DWORD WINAPI igiSdkStartup(void*) {
    const auto config = igi::loadMenuConfig("config.ini");
    igi::setLogLevel(config.minimumLogLevel);
#ifndef NDEBUG
    attachConsole();
#endif

    char message[192]{};
    igi::log(igi::LogLevel::Info, "internal DLL attached");
    std::snprintf(message, sizeof(message),
                  "internal startup: enabled=%s items=%lu hook=%s log_level=%s console=%s",
                  config.enabled ? "true" : "false", static_cast<unsigned long>(config.items.size()),
                  igi::tickHookName(config.hook), levelName(config.minimumLogLevel),
                  config.console ? "true" : "false");
    igi::log(igi::LogLevel::Info, message);

    if (!config.enabled) {
        igi::log(igi::LogLevel::Info, "internal menu disabled by config.ini");
        return 0;
    }

    const std::uint8_t virtualKey = virtualKeyFor(config.hotkey);
    if (virtualKey == 0) {
        igi::log(igi::LogLevel::Error, "unsupported mod menu hotkey in config.ini");
        return 0;
    }

    std::snprintf(message, sizeof(message), "internal menu waiting for start key DIK 0x%02X", config.hotkey);
    igi::log(igi::LogLevel::Info, message);
    while ((GetAsyncKeyState(virtualKey) & 0x8000) == 0) Sleep(25);

    igi::log(igi::LogLevel::Info, "internal menu start key pressed; initializing");
    Sleep(250);
    const bool ready = IGI_MenuManager_Internal_Initialize();
    if (!ready) {
        igi::log(igi::LogLevel::Error, "internal menu initialization failed after hotkey");
        return 0;
    }
    std::snprintf(message, sizeof(message), "internal menu initialization completed; scheduler ticks on %s",
                  igi::tickHookName(config.hook));
    igi::log(igi::LogLevel::Info, message);

    // End key closes the menu and shuts the SDK down cleanly. DIK 0 disables it.
    const std::uint8_t exitVirtualKey = config.exitKey ? virtualKeyFor(config.exitKey) : 0;
    if (exitVirtualKey == 0) {
        igi::log(igi::LogLevel::Info, "internal menu end key disabled; SDK stays loaded");
        return 0;
    }
    std::snprintf(message, sizeof(message), "internal menu end key DIK 0x%02X; press it to close the menu and unload the SDK",
                  config.exitKey);
    igi::log(igi::LogLevel::Info, message);
    while ((GetAsyncKeyState(exitVirtualKey) & 0x8000) == 0) Sleep(25);
    igi::log(igi::LogLevel::Info, "internal menu end key pressed; shutting down");
    IGI_MenuManager_Internal_Shutdown();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, igiSdkStartup, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        IGI_MenuManager_Internal_Shutdown();
        FreeConsole();
    }
    return TRUE;
}
