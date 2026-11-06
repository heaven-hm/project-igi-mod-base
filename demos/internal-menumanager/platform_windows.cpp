#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/logging/logging.hpp"

extern "C" bool IGI_MenuManager_Internal_Initialize();
extern "C" bool IGI_MenuManager_Internal_Reopen();
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

// Debug builds may allocate a console for tracing.
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

bool validationAutoStartEnabled() noexcept {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("IGI_SDK_AUTOSTART", value, sizeof(value));
    return length > 0 && length < sizeof(value) &&
           (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
}

void showLifecycleStatus(const char* text) noexcept {
    using StatusMessageShow = void (__cdecl*)(int, const char*, const char*, std::uint8_t*);
    constexpr std::uintptr_t statusMessageShow = 0x00485970;
    constexpr std::uintptr_t statusMessageContext = 0x00A758AC;
    constexpr std::uintptr_t statusMessageByte = 0x00567C74;
    const int context = *reinterpret_cast<const int*>(statusMessageContext);
    auto* const byte = reinterpret_cast<std::uint8_t*>(statusMessageByte);
    reinterpret_cast<StatusMessageShow>(statusMessageShow)(context, text, "STATUSSCREEN:note.spr", byte);
}
}

static DWORD WINAPI igiSdkStartup(void* rawModule) {
    const auto module = static_cast<HMODULE>(rawModule);
    const auto config = igi::loadMenuConfig("config.ini");
    igi::setLogLevel(config.minimumLogLevel);
#ifndef NDEBUG
    if (config.console) attachConsole();
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

    if (!validationAutoStartEnabled()) {
        std::snprintf(message, sizeof(message), "internal menu waiting for start key DIK 0x%02X", config.hotkey);
        igi::log(igi::LogLevel::Info, message);
        while ((GetAsyncKeyState(virtualKey) & 0x8000) == 0) Sleep(25);
    } else {
        // The bounded Wine validation starts the game with this explicit opt-in.
        // It exercises the frame hook without weakening the normal hotkey-only
        // behavior for users who launch the DLL without the environment flag.
        igi::log(igi::LogLevel::Info, "internal validation auto-start enabled; bypassing start-key wait");
    }

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
    showLifecycleStatus("IGI SDK Mod Menu: enabled");
    igi::log(igi::LogLevel::Info, "internal menu lifecycle: enabled status message shown");

    const std::uint8_t exitVirtualKey = config.exitKey ? virtualKeyFor(config.exitKey) : 0;
    std::snprintf(message, sizeof(message), "internal menu toggle ready; start key DIK 0x%02X reopens a dismissed screen",
                  config.hotkey);
    igi::log(igi::LogLevel::Info, message);
    if (exitVirtualKey != 0) {
        std::snprintf(message, sizeof(message), "internal menu end key DIK 0x%02X; press it to close the menu and unload the SDK",
                      config.exitKey);
        igi::log(igi::LogLevel::Info, message);
    }
    bool startWasDown = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    for (;;) {
        const bool startDown = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
        if (startDown && !startWasDown) {
            const bool reopened = IGI_MenuManager_Internal_Reopen();
            igi::log(reopened ? igi::LogLevel::Info : igi::LogLevel::Warning,
                     reopened ? "internal menu toggle: configured start key handled" :
                                "internal menu toggle: reopen failed");
        }
        startWasDown = startDown;
        if (exitVirtualKey != 0 && (GetAsyncKeyState(exitVirtualKey) & 0x8000) != 0) break;
        Sleep(25);
    }
    igi::log(igi::LogLevel::Info, "internal menu end key pressed; shutting down");
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
    igi::log(igi::LogLevel::Info, "internal menu lifecycle: disabled status message shown");
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
