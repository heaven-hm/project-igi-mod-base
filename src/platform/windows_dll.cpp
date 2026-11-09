#include "platform/windows_dll.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "config/menu_config.hpp"
#include "logging/logging.hpp"
#include "runtime/hotkeys.hpp"
#include "natives/mvp_natives.hpp"

namespace {

struct MainThreadState {
    std::atomic<bool> running{false};
    std::atomic<bool> unloadRequested{false};
    std::uint8_t startKey = 0;
    std::uint8_t endKey = 0;
    bool initialized = false;
    bool directNativeMode = true;
    bool autoStart = false;
    // The end key disables the menu without tearing anything down, so the retail
    // game keeps its own menus and the start key can bring the mod back.
    bool disabled = false;
    bool tickMissingReported = false;
    unsigned watchdogCounter = 0;
    unsigned autoStartCounter = 0;
};

MainThreadState g_state;
HMODULE g_module = nullptr;

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
    if (!AllocConsole())
        return;
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);
    SetConsoleTitleA("IGI Mod - mod menu");
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
}

void detachConsole() {
    std::fflush(stdout);
    std::fflush(stderr);
    SetConsoleCtrlHandler(consoleControlHandler, FALSE);
    const HWND console = GetConsoleWindow();
    if (console)
        ShowWindow(console, SW_HIDE);
    FreeConsole();
}

std::uint8_t virtualKeyFor(std::uint8_t dik) noexcept {
    switch (dik) {
    case 0x3D:
        return VK_F3;
    case 0x3E:
        return VK_F4;
    case 0x3F:
        return VK_F5;
    case 0x40:
        return VK_F6;
    case 0x41:
        return VK_F7;
    case 0x42:
        return VK_F8;
    case 0x43:
        return VK_F9;
    case 0x44:
        return VK_F10;
    case 0x57:
        return VK_F11;
    case 0x58:
        return VK_F12;
    case 0x47:
    case 0xC7:
        return VK_HOME;
    case 0x4F:
    case 0xCF:
        return VK_END;
    case 0x49:
    case 0xC9:
        return VK_PRIOR;
    case 0x51:
    case 0xD1:
        return VK_NEXT;
    case 0x52:
    case 0xD2:
        return VK_INSERT;
    case 0x53:
    case 0xD3:
        return VK_DELETE;
    default:
        break;
    }
    return static_cast<std::uint8_t>(MapVirtualKeyA(dik, MAPVK_VSC_TO_VK_EX));
}

bool validationAutoStartEnabled() noexcept {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("IGI_SDK_AUTOSTART", value, sizeof(value));
    return length > 0 && length < sizeof(value) &&
           (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
}

void mainLoopTick(const igi::DllMenuCallbacks &callbacks, igi::hotkeys::KeyTracker &keys) {
    if (g_state.initialized && !g_state.directNativeMode && ++g_state.watchdogCounter >= 100) {
        g_state.watchdogCounter = 0;
        if (callbacks.tickCount && callbacks.tickCount() == 0) {
            if (!g_state.tickMissingReported) {
                g_state.tickMissingReported = true;
                const bool registered = callbacks.ensureTick ? callbacks.ensureTick() : false;
                igi::log(registered ? igi::LogLevel::Info : igi::LogLevel::Warning,
                         registered
                             ? "internal watchdog: display frame tick was absent; requested "
                               "game-thread callback-list registration"
                             : "internal watchdog: display frame tick absent and registration "
                               "failed; the game may not be rendering frames yet");
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
        if (g_state.disabled) {
            g_state.disabled = false;
            const bool reopened = callbacks.toggle ? callbacks.toggle() : false;
            igi::log(reopened ? igi::LogLevel::Info : igi::LogLevel::Warning,
                     reopened ? "internal menu: re-enabled by the configured start key"
                              : "internal menu: re-enable requested; the menu opens on the next "
                                "active gameplay frame");
            return;
        }
        if (!g_state.initialized) {
            if (callbacks.initialize && callbacks.initialize()) {
                g_state.initialized = true;
                igi::log(igi::LogLevel::Info,
                         "internal menu: initialized and opened from configured start key");
            } else {
                igi::log(igi::LogLevel::Warning,
                         "internal menu: initialization deferred until gameplay is active");
            }
        } else {
            const bool toggled = callbacks.toggle ? callbacks.toggle() : false;
            igi::log(toggled ? igi::LogLevel::Info : igi::LogLevel::Error,
                     toggled ? "internal menu toggle: configured start key handled"
                             : "internal menu toggle: configured start key failed");
        }
    }
}

bool deactivateMenuRuntime(const igi::DllMenuCallbacks &callbacks) {
    if (!g_state.initialized)
        return true;

    if (callbacks.requestShutdown && !callbacks.requestShutdown()) {
        igi::log(igi::LogLevel::Error, "internal deactivate: menu close request failed");
        return false;
    }

    const DWORD deadline = GetTickCount() + 5000;
    while (callbacks.canUnload && !callbacks.canUnload() &&
           static_cast<LONG>(GetTickCount() - deadline) < 0) {
        igi::hotkeys::pollSleep();
    }
    if (callbacks.canUnload && !callbacks.canUnload()) {
        igi::log(igi::LogLevel::Error, "internal deactivate: screen did not detach");
        return false;
    }

    if (callbacks.shutdown)
        callbacks.shutdown();
    g_state.initialized = false;
    g_state.watchdogCounter = 0;
    g_state.tickMissingReported = false;
    igi::log(igi::LogLevel::Info,
             "internal deactivate: menu and hooks stopped; DLL remains resident safely");
    return true;
}

DWORD WINAPI igiSdkMainThread(void *rawModule) {
    g_module = static_cast<HMODULE>(rawModule);
    const auto config = igi::loadMenuConfig("project-igi-mod-config.ini");
    igi::setLogFile(config.logFile);
    igi::setLogLevel(config.minimumLogLevel);
#ifndef NDEBUG
    attachConsole();
#endif
    igi::log(igi::LogLevel::Info, "internal DLL attached");
    if (!config.enabled)
        return 0;

    g_state.startKey = virtualKeyFor(config.hotkey);
    g_state.endKey = config.exitKey ? virtualKeyFor(config.exitKey) : 0;
    if (!g_state.startKey) {
        igi::log(igi::LogLevel::Error, "internal menu: configured start key is unsupported");
        return 0;
    }

    const auto &callbacks = igi::getDllMenuCallbacks();

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
        const bool unloadRequested = g_state.unloadRequested.load(std::memory_order_acquire);
        const bool disableRequested = g_state.endKey && keys.isPressedOnce(g_state.endKey);
        if (disableRequested && !unloadRequested) {
            // Disabling must not release or delete anything the retail menu system
            // still references: closing the screen is the only state change. The
            // game keeps its own pause/main menus and the start key re-opens this
            // menu, so no re-injection is needed.
            if (g_state.initialized) {
                const bool closed = callbacks.closeMenu ? callbacks.closeMenu() : false;
                g_state.disabled = true;
                igi::log(closed ? igi::LogLevel::Info : igi::LogLevel::Warning,
                         closed ? "internal menu: disabled by the configured end key; press the "
                                  "start key to re-enable"
                                : "internal menu: disable requested; the menu closes on the next "
                                  "frame");
            }
        }
        if (unloadRequested) {
            if (!deactivateMenuRuntime(callbacks))
                continue;
            g_state.running.store(false, std::memory_order_release);
            // Unloading is only safe once the retail hooks are restored. Otherwise
            // the game still calls into this module and freeing it freezes the game.
            if (callbacks.hooksResident && callbacks.hooksResident()) {
                igi::log(igi::LogLevel::Error,
                         "safe unload refused: retail hooks are still installed; the mod is "
                         "disabled and the DLL stays resident until the game exits");
                igi::log(igi::LogLevel::Info, "internal host stopped; DLL remains resident safely");
                detachConsole();
                igi::closeLogFile();
                return 0;
            }
            igi::log(igi::LogLevel::Info,
                     "safe unload: callbacks stopped; releasing DLL from its host thread");
            detachConsole();
            igi::closeLogFile();
            FreeLibraryAndExitThread(g_module, 0);
        }
        igi::hotkeys::pollSleep();
    }

    (void)deactivateMenuRuntime(callbacks);
    igi::log(igi::LogLevel::Info,
             "internal host stopped; DLL remains resident safely until the game exits");
    detachConsole();
    igi::closeLogFile();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, igiSdkMainThread, module, 0, nullptr);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL __cdecl IGI_SDK_RequestSafeUnload() {
    // This only requests unloading. igiSdkMainThread removes every game callback
    // before it calls FreeLibraryAndExitThread on itself.
    if (!g_state.running.load(std::memory_order_acquire))
        return FALSE;
    g_state.unloadRequested.store(true, std::memory_order_release);
    return TRUE;
}
