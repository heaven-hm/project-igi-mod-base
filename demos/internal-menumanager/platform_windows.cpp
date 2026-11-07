#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hotkeys.hpp"
#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/logging/logging.hpp"

extern "C" bool IGI_MenuManager_Internal_Initialize();
extern "C" bool IGI_MenuManager_Internal_Toggle();
extern "C" bool IGI_MenuManager_Internal_RequestShutdown();
extern "C" bool IGI_MenuManager_Internal_CanUnload();
extern "C" void IGI_MenuManager_Internal_Shutdown();
extern "C" bool IGI_MenuManager_Internal_EnsureTick();
extern "C" std::uint64_t IGI_MenuManager_Internal_TickCount();

namespace {

struct MainThreadState {
    std::atomic<bool> running{false};
    std::uint8_t startKey = 0;
    std::uint8_t endKey = 0;
    bool tickMissingReported = false;
    unsigned watchdogCounter = 0;
};

MainThreadState g_state;

BOOL WINAPI consoleControlHandler(DWORD event) {
    switch (event) {
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            g_state.running.store(false, std::memory_order_release);
            return TRUE;
        default:
            return FALSE;
    }
}

void attachConsole() {
    if (!AllocConsole()) return;
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);
    SetConsoleTitleA("IGI SDK - internal menu");
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
}

void detachConsole() {
    std::fflush(stdout);
    std::fflush(stderr);
    SetConsoleCtrlHandler(consoleControlHandler, FALSE);
    const HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);
    FreeConsole();
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

void mainLoopTick(hotkeys::KeyTracker& keys) {
    // Watchdog: confirm the display frame callback actually fires (~1s at 100 Hz).
    if (++g_state.watchdogCounter >= 100) {
        g_state.watchdogCounter = 0;
        if (IGI_MenuManager_Internal_TickCount() == 0) {
            if (!g_state.tickMissingReported) {
                g_state.tickMissingReported = true;
                const bool registered = IGI_MenuManager_Internal_EnsureTick();
                igi::log(registered ? igi::LogLevel::Info : igi::LogLevel::Warning,
                         registered ? "internal watchdog: display frame tick was absent; re-registered into the callback list"
                                    : "internal watchdog: display frame tick absent and registration failed; the game may not be rendering frames yet");
            }
        } else {
            g_state.tickMissingReported = false;
        }
    }

    if (keys.isPressedOnce(g_state.startKey)) {
        const bool toggled = IGI_MenuManager_Internal_Toggle();
        igi::log(toggled ? igi::LogLevel::Info : igi::LogLevel::Error,
                 toggled ? "internal menu toggle: configured start key handled"
                         : "internal menu toggle: configured start key failed");
    }

    if (g_state.endKey && keys.isPressedOnce(g_state.endKey)) {
        g_state.running.store(false, std::memory_order_release);
    }
}

}  // namespace

static DWORD WINAPI igiSdkMainThread(void* rawModule) {
    (void)rawModule;
    const auto config = igi::loadMenuConfig("project-igi-sdk-config.ini");
    igi::setLogLevel(config.minimumLogLevel);
#ifndef NDEBUG
    attachConsole();
#endif
    igi::log(igi::LogLevel::Info, "internal DLL attached");
    if (!config.enabled) return 0;

    g_state.startKey = virtualKeyFor(config.hotkey);
    g_state.endKey = config.exitKey ? virtualKeyFor(config.exitKey) : 0;
    if (!g_state.startKey) {
        igi::log(igi::LogLevel::Error, "internal menu: configured start key is unsupported");
        return 0;
    }

    g_state.running.store(true, std::memory_order_release);

    if (!validationAutoStartEnabled()) {
        while (g_state.running.load(std::memory_order_acquire) &&
               !hotkeys::isKeyDown(g_state.startKey)) {
            hotkeys::pollSleep();
        }
    }
    if (!g_state.running.load(std::memory_order_acquire)) {
        detachConsole();
        return 0;
    }

    if (!IGI_MenuManager_Internal_Initialize()) {
        igi::log(igi::LogLevel::Error, "internal menu initialization failed after configured start key");
        g_state.running.store(false, std::memory_order_release);
        return 0;
    }

    showLifecycleStatus("IGI SDK Mod Menu: enabled");

    hotkeys::KeyTracker keys;
    keys.seed(g_state.startKey);
    keys.seed(g_state.endKey);
    g_state.tickMissingReported = false;
    g_state.watchdogCounter = 0;

    while (g_state.running.load(std::memory_order_acquire)) {
        mainLoopTick(keys);
        hotkeys::pollSleep();
    }

    if (!IGI_MenuManager_Internal_RequestShutdown()) {
        igi::log(igi::LogLevel::Error, "internal unload: menu close request failed; SDK stays resident");
        return 0;
    }

    const DWORD deadline = GetTickCount() + 5000;
    while (!IGI_MenuManager_Internal_CanUnload() &&
           static_cast<LONG>(GetTickCount() - deadline) < 0) {
        hotkeys::pollSleep();
    }
    if (!IGI_MenuManager_Internal_CanUnload()) {
        igi::log(igi::LogLevel::Error, "internal unload: screen did not detach; SDK stays resident");
        return 0;
    }

    showLifecycleStatus("IGI SDK Mod Menu: disabled");
    IGI_MenuManager_Internal_Shutdown();
    igi::log(igi::LogLevel::Info,
             "internal unload: callbacks and resources released; SDK detached and left resident");
    detachConsole();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, igiSdkMainThread, module, 0, nullptr);
    }
    return TRUE;
}
