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
    // Set when the start key was pressed while gameplay was not ready (level
    // loading). The loop then retries initialization and opens the menu on the
    // first active gameplay frame, so one press is enough.
    bool autoOpenPending = false;
    // True once requestShutdown has been accepted for the current unload. The
    // wait loop must not re-request cleanup every 10 ms; doing so queued the
    // game-thread shutdown multiple times and double-freed the menu resources.
    bool shutdownRequested = false;
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
    // A press that arrived while the game was loading keeps trying: the menu
    // opens itself on the first active gameplay frame.
    if (!g_state.initialized && !startRequested && g_state.autoOpenPending &&
        ++g_state.autoStartCounter >= 25) {
        g_state.autoStartCounter = 0;
        startRequested = true;
        igi::log(igi::LogLevel::Info,
                 "internal menu: retrying the staged start-key press while the level loads");
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
                g_state.autoOpenPending = false;
                // Initialization deliberately leaves the menu closed; this same
                // key press opens it once. Staging the attach during initialize
                // plus toggling here is what produced two opens for one F5.
                const bool opened = callbacks.toggle ? callbacks.toggle() : false;
                igi::log(opened ? igi::LogLevel::Info : igi::LogLevel::Warning,
                         opened ? "internal menu: initialized and opened from configured start key"
                                : "internal menu: initialized; menu opens on the next active "
                                  "gameplay frame");
            } else {
                g_state.autoOpenPending = true;
                g_state.autoStartCounter = 0;
                igi::log(igi::LogLevel::Info,
                         "internal menu: start key staged; initialization retries while the "
                         "level loads and the menu opens on the first active gameplay frame");
            }
        } else {
            g_state.autoOpenPending = false;
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

    if (!g_state.shutdownRequested) {
        if (callbacks.requestShutdown && !callbacks.requestShutdown()) {
            igi::log(igi::LogLevel::Error, "internal deactivate: menu close request failed");
            return false;
        }
        g_state.shutdownRequested = true;
    }

    const DWORD deadline = GetTickCount() + 5000;
    while (callbacks.canUnload && !callbacks.canUnload() &&
           static_cast<LONG>(GetTickCount() - deadline) < 0) {
        igi::hotkeys::pollSleep();
    }
    if (callbacks.canUnload && !callbacks.canUnload()) {
        // Allow a later loop to request cleanup again instead of waiting on a
        // request that never reached the game thread.
        g_state.shutdownRequested = false;
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
    (void)rawModule;
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
    g_state.shutdownRequested = false;
    g_state.autoOpenPending = false;
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
        const bool ejectRequested = g_state.endKey && keys.isPressedOnce(g_state.endKey);
        if (ejectRequested && !unloadRequested) {
            // The configured end key ejects the mod: the menu closes, the retail
            // callbacks are removed and the DLL stays resident with pass-through
            // hooks. The start key must not bring the menu back after an eject.
            g_state.unloadRequested.store(true, std::memory_order_release);
            igi::log(igi::LogLevel::Info,
                     "internal menu: eject requested by the configured end key");
        }
        if (unloadRequested) {
            if (!deactivateMenuRuntime(callbacks))
                continue;
            g_state.running.store(false, std::memory_order_release);
            // Never FreeLibrary from this worker. The eject path removed the
            // retail callback and retired the detours, but the game thread may
            // still be inside a Display_AddFrameCallback/RunCallbacks detour or
            // about to enter one; freeing the module there was the F12/End-key
            // eject crash ("safe unload: releasing DLL" immediately before the
            // page fault). A retired detour is a pass-through, so keeping the
            // module resident costs nothing and can never execute freed code.
            if (callbacks.hooksResident && callbacks.hooksResident()) {
                igi::log(igi::LogLevel::Error,
                         "eject: retail hooks are still installed; mod disabled and the DLL "
                         "stays resident until the game exits");
            } else {
                igi::log(igi::LogLevel::Info,
                         "eject: callbacks stopped and hooks retired; the DLL stays resident "
                         "until the game exits (safe unload on Windows x86)");
            }
            igi::log(igi::LogLevel::Info, "internal host stopped; DLL remains resident safely");
            detachConsole();
            igi::closeLogFile();
            return 0;
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
    // This only requests the eject. igiSdkMainThread retires the retail
    // callbacks and keeps the module resident so the game can never call into
    // freed code.
    if (!g_state.running.load(std::memory_order_acquire))
        return FALSE;
    g_state.unloadRequested.store(true, std::memory_order_release);
    return TRUE;
}
