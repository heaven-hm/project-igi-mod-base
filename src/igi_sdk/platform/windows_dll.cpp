#include "igi_sdk/platform/windows_dll.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/hotkeys.hpp"

namespace {

struct MainThreadState {
    std::atomic<bool> running{false};
    std::uint8_t startKey = 0;
    std::uint8_t endKey = 0;
    bool initialized = false;
    bool directNativeMode = true;
    bool autoStart = false;
    bool tickMissingReported = false;
    unsigned watchdogCounter = 0;
    unsigned autoStartCounter = 0;
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
    SetConsoleTitleA("IGI SDK - mod menu");
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
    switch (dik) {
        case 0x3D: return VK_F3;
        case 0x3E: return VK_F4;
        case 0x3F: return VK_F5;
        case 0x40: return VK_F6;
        case 0x41: return VK_F7;
        case 0x42: return VK_F8;
        case 0x43: return VK_F9;
        case 0x44: return VK_F10;
        case 0x57: return VK_F11;
        case 0x58: return VK_F12;
        case 0x47: return VK_HOME;
        case 0x4F: return VK_END;
        case 0x49: return VK_PRIOR;
        case 0x51: return VK_NEXT;
        case 0x52: return VK_INSERT;
        case 0x53: return VK_DELETE;
        default:
            return static_cast<std::uint8_t>(MapVirtualKeyA(dik, MAPVK_VSC_TO_VK_EX));
    }
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
    if (context == 0) return;
    auto* const byte = reinterpret_cast<std::uint8_t*>(0x00567C74);
    reinterpret_cast<StatusMessageShow>(0x00485970)(context, text, "STATUSSCREEN:note.spr", byte);
}

void mainLoopTick(const igi::DllMenuCallbacks& callbacks, igi::hotkeys::KeyTracker& keys) {
    if (g_state.initialized && !g_state.directNativeMode && ++g_state.watchdogCounter >= 100) {
        g_state.watchdogCounter = 0;
        if (callbacks.tickCount && callbacks.tickCount() == 0) {
            if (!g_state.tickMissingReported) {
                g_state.tickMissingReported = true;
                const bool registered = callbacks.ensureTick ? callbacks.ensureTick() : false;
                igi::log(registered ? igi::LogLevel::Info : igi::LogLevel::Warning,
                         registered ? "internal watchdog: display frame tick was absent; re-registered into the callback list"
                                    : "internal watchdog: display frame tick absent and registration failed; the game may not be rendering frames yet");
            }
        } else {
            g_state.tickMissingReported = false;
        }
    }

    bool startRequested = keys.isPressedOnce(g_state.startKey);
    if (!g_state.initialized && g_state.autoStart && ++g_state.autoStartCounter >= 25) {
        g_state.autoStartCounter = 0;
        startRequested = true;
    }

    if (startRequested) {
        if (!g_state.initialized) {
            if (callbacks.initialize && callbacks.initialize()) {
                g_state.initialized = true;
                showLifecycleStatus("IGI SDK Mod Menu: enabled");
                igi::log(igi::LogLevel::Info, "internal menu: initialized and opened from configured start key");
            } else {
                igi::log(igi::LogLevel::Warning, "internal menu: initialization deferred until gameplay is active");
            }
        } else {
            const bool toggled = callbacks.toggle ? callbacks.toggle() : false;
            igi::log(toggled ? igi::LogLevel::Info : igi::LogLevel::Error,
                     toggled ? "internal menu toggle: configured start key handled"
                             : "internal menu toggle: configured start key failed");
        }
    }

    if (g_state.endKey && keys.isPressedOnce(g_state.endKey)) {
        g_state.running.store(false, std::memory_order_release);
    }
}

DWORD WINAPI igiSdkMainThread(void* rawModule) {
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

    const auto& callbacks = igi::getDllMenuCallbacks();

    g_state.initialized = false;
    g_state.directNativeMode = config.directNativeMode;
    g_state.autoStart = validationAutoStartEnabled();
    g_state.tickMissingReported = false;
    g_state.watchdogCounter = 0;
    g_state.autoStartCounter = 25;
    g_state.running.store(true, std::memory_order_release);

    igi::hotkeys::KeyTracker keys;
    keys.seed(g_state.startKey);
    keys.seed(g_state.endKey);

    while (g_state.running.load(std::memory_order_acquire)) {
        mainLoopTick(callbacks, keys);
        igi::hotkeys::pollSleep();
    }

    if (!g_state.initialized) {
        detachConsole();
        return 0;
    }

    if (callbacks.requestShutdown && !callbacks.requestShutdown()) {
        igi::log(igi::LogLevel::Error, "internal unload: menu close request failed; SDK stays resident");
        return 0;
    }

    const DWORD deadline = GetTickCount() + 5000;
    while (callbacks.canUnload && !callbacks.canUnload() &&
           static_cast<LONG>(GetTickCount() - deadline) < 0) {
        igi::hotkeys::pollSleep();
    }
    if (callbacks.canUnload && !callbacks.canUnload()) {
        igi::log(igi::LogLevel::Error, "internal unload: screen did not detach; SDK stays resident");
        return 0;
    }

    showLifecycleStatus("IGI SDK Mod Menu: disabled");
    if (callbacks.shutdown) callbacks.shutdown();
    igi::log(igi::LogLevel::Info,
             "internal unload: callbacks and resources released; SDK detached and left resident");
    detachConsole();
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, igiSdkMainThread, module, 0, nullptr);
    }
    return TRUE;
}
