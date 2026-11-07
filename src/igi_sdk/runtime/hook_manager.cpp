#include "igi_sdk/runtime/hook_manager.hpp"

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/runtime/target_profile.hpp"
#include "macos_hook_backend.hpp"

#include <atomic>
#include <cstdint>
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
#include <MinHook.h>

namespace {
// The scheduler that frame ticks drain into. Cleared on detach so an in-flight
// game callback that still points at IGI1SDK_Tick becomes a safe no-op instead
// of touching an SDK that is being torn down.
std::atomic<igi::Scheduler*> activeScheduler{nullptr};

// Set while a Display_RunCallbacks detour is walking the game's callback list so
// the list-registered IGI1SDK_Tick does not run a second time in the same frame.
std::atomic<bool> frameTickHandled{false};

// Tracks whether each MinHook target currently owns a trampoline. Trampolines are
// intentionally never freed: the module stays resident, and an in-flight detour
// must never jump into released memory.
bool addHookCreated = false;
bool runHookCreated = false;
// True while the SDK owns a live tick in the game's frame callback list.
std::atomic<bool> tickRegistered{false};

// Display_AddFrameCallback's verified retail prologue:
//   mov eax,[esp+4]        ; 8B 44 24 04  (callback argument)
//   push 0x00BCAE00        ; 68 00 AE BC 00  (display frame callback list)
//   push eax               ; 50
//   call Display_Register  ; E8 81 FF FF FF -> 0x00491650
//   add esp,8 ; ret
// The registrar prepends an eight-byte {next, callback} node taken from a pool of
// 32 slots at 0x00C28A40. Display_RunCallbacks (0x00491D00) reads the head at
// 0x00BCAE00 and walks it once per frame, calling each callback with no arguments.
// Registering IGI1SDK_Tick into that same list is exactly how the game itself
// obtains a per-frame callback.
using DisplayAddFrameCallbackFn = int (__cdecl *)(void* callback);
DisplayAddFrameCallbackFn displayAddFrameCallbackOriginal = nullptr;

// Removes a callback from the same list. Used only when the SDK unloads while the
// list is still being walked, which is why the call is best-effort.
using DisplayRemoveFrameCallbackFn = int (__cdecl *)(void* callback);

// Display_RunCallbacks (0x00491D00) is invoked once per frame by ScreenDraw. Its
// detour is the guaranteed tick driver even if a list reset drops our node.
using DisplayRunCallbacksFn = int (__cdecl *)(void);
DisplayRunCallbacksFn displayRunCallbacksOriginal = nullptr;

struct FrameCallbackNode {
    FrameCallbackNode* next;
    void* callback;
};

// Defined below; declared here so the list walker can compare against its address.
extern "C" void __cdecl IGI1SDK_Tick() noexcept;

FrameCallbackNode* frameCallbackHead() noexcept {
    return *reinterpret_cast<FrameCallbackNode* const*>(
        static_cast<std::uintptr_t>(igi::igi1::displayFrameCallbackHead));
}

// True when IGI1SDK_Tick already occupies a node in the game's display frame
// callback list. The walk is bounded so a malformed list cannot spin forever.
bool tickNodePresent() noexcept {
    FrameCallbackNode* node = frameCallbackHead();
    for (int guard = 0; node && guard < 64; ++guard) {
        if (node->callback == reinterpret_cast<void*>(&IGI1SDK_Tick)) return true;
        node = node->next;
    }
    return false;
}

// Runs on the game thread once per displayed frame because the game's own
// Display_RunCallbacks invokes it from the display frame callback list.
extern "C" void __cdecl IGI1SDK_Tick() noexcept {
    // The Display_RunCallbacks detour already drained this frame's work, so skip
    // the duplicate invocation produced by the list walk.
    if (frameTickHandled.load(std::memory_order_acquire)) return;
    if (igi::Scheduler* scheduler = activeScheduler.load(std::memory_order_acquire)) {
        scheduler->runPendingFromGameHook();
    }
}

void sdkFrameTick() noexcept {
    if (igi::Scheduler* scheduler = activeScheduler.load(std::memory_order_acquire)) {
        scheduler->runPendingFromGameHook();
    }
}

// Registers IGI1SDK_Tick into the game's display frame callback list when the
// list does not already hold it. Cheap enough to call from a watchdog: the pool
// holds at most 32 nodes. Returns true when the tick is present afterwards.
bool registerFrameTick() noexcept {
    if (!displayAddFrameCallbackOriginal) return false;
    if (tickNodePresent()) {
        tickRegistered.store(true, std::memory_order_release);
        return true;
    }
    displayAddFrameCallbackOriginal(reinterpret_cast<void*>(&IGI1SDK_Tick));
    const bool present = tickNodePresent();
    tickRegistered.store(present, std::memory_order_release);
    return present;
}

// Detour for Display_AddFrameCallback itself. It fires when the game registers a
// callback (usually once during startup). The detour forwards to the registrar so
// the game's callback is installed, then makes sure our tick is still present so a
// display reset cannot silently drop it.
extern "C" int __cdecl displayAddFrameCallbackDetour(void* callback) {
    const int result = displayAddFrameCallbackOriginal ? displayAddFrameCallbackOriginal(callback) : 0;
    registerFrameTick();
    if (igi::Scheduler* scheduler = activeScheduler.load(std::memory_order_acquire)) {
        scheduler->runPendingFromGameHook();
    }
    return result;
}

// Detour for Display_RunCallbacks. This runs exactly once per displayed frame
// from ScreenDraw. It suppresses the duplicate list-walk tick, runs the original
// frame dispatch, then drains the scheduler for the completed frame.
extern "C" int __cdecl displayRunCallbacksDetour() {
    frameTickHandled.store(true, std::memory_order_release);
    const int result = displayRunCallbacksOriginal ? displayRunCallbacksOriginal() : 0;
    frameTickHandled.store(false, std::memory_order_release);
    sdkFrameTick();
    return result;
}

bool installDisplayFrameHook(igi::Scheduler& scheduler, void*& trampolineOut) {
    using igi::LogLevel;
    if (!igi::igi1::isSupportedExecutable()) {
        igi::log(LogLevel::Error, "Display_AddFrameCallback hook refused: executable hash mismatch");
        return false;
    }
    const auto initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED) return false;

    activeScheduler.store(&scheduler, std::memory_order_release);

    if (!addHookCreated) {
        const auto status = MH_CreateHook(
            reinterpret_cast<void*>(igi::igi1::displayAddFrameCallback),
            reinterpret_cast<void*>(&displayAddFrameCallbackDetour),
            reinterpret_cast<void**>(&displayAddFrameCallbackOriginal));
        if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
            activeScheduler.store(nullptr, std::memory_order_release);
            igi::log(LogLevel::Error, "Display_AddFrameCallback hook creation failed");
            return false;
        }
        addHookCreated = true;
    }
    if (MH_EnableHook(reinterpret_cast<void*>(igi::igi1::displayAddFrameCallback)) != MH_OK) {
        activeScheduler.store(nullptr, std::memory_order_release);
        igi::log(LogLevel::Error, "Display_AddFrameCallback hook enable failed");
        return false;
    }

    // Guaranteed per-frame driver: hook the display callback dispatch function.
    if (!runHookCreated) {
        const auto runStatus = MH_CreateHook(
            reinterpret_cast<void*>(igi::igi1::displayRunCallbacks),
            reinterpret_cast<void*>(&displayRunCallbacksDetour),
            reinterpret_cast<void**>(&displayRunCallbacksOriginal));
        if (runStatus == MH_OK || runStatus == MH_ERROR_ALREADY_CREATED) {
            runHookCreated = true;
        } else {
            igi::log(LogLevel::Warning,
                     "Display_RunCallbacks hook creation failed; relying on the callback list only");
        }
    }
    if (runHookCreated &&
        MH_EnableHook(reinterpret_cast<void*>(igi::igi1::displayRunCallbacks)) != MH_OK) {
        igi::log(LogLevel::Warning, "Display_RunCallbacks hook enable failed");
        runHookCreated = false;
    }

    trampolineOut = reinterpret_cast<void*>(displayAddFrameCallbackOriginal);

    const bool registered = registerFrameTick();
    igi::log(LogLevel::Info,
             "IGI 1 Display_AddFrameCallback hook installed at 0x004916C0; frame tick registered");
    igi::log(LogLevel::Info, runHookCreated
                                 ? "IGI 1 Display_RunCallbacks per-frame driver hooked at 0x00491D00"
                                 : "IGI 1 Display_RunCallbacks driver unavailable; tick depends on the callback list");
    if (!registered) {
        igi::log(LogLevel::Warning,
                 "Display_AddFrameCallback tick not present after registration; watchdog will retry");
    }
    return true;
}
} // namespace
#endif

namespace igi {
bool HookManager::installDisplayAddFrameCallbackTick() {
#if defined(__APPLE__)
    return macos::installGameTickHook(scheduler_, trampoline_);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) {
        // Re-enable an inert detach (the module stays resident so a restart can
        // reattach without reloading the DLL).
        activeScheduler.store(&scheduler_, std::memory_order_release);
        if (addHookCreated) MH_EnableHook(reinterpret_cast<void*>(igi1::displayAddFrameCallback));
        if (runHookCreated) MH_EnableHook(reinterpret_cast<void*>(igi1::displayRunCallbacks));
        registerFrameTick();
        installed_ = true;
        log(LogLevel::Info, "IGI 1 Display_AddFrameCallback hook re-enabled");
        return true;
    }
    void* trampoline = nullptr;
    if (!installDisplayFrameHook(scheduler_, trampoline)) return false;
    trampoline_ = trampoline;
    installed_ = true;
    return true;
#else
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

bool HookManager::installTickHook(igi1::TickHook hook) {
    if (hook != igi1::TickHook::DisplayAddFrameCallback) {
        log(LogLevel::Warning, "refusing scheduler hook other than Display_AddFrameCallback");
        return false;
    }
    return installDisplayAddFrameCallbackTick();
}

bool HookManager::installDisplayEndFrameHook() { return false; }
bool HookManager::installTextDrawHook() { return false; }

bool HookManager::ensureFrameTickRegistered() noexcept {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!installed_ || !displayAddFrameCallbackOriginal) return false;
    return registerFrameTick();
#else
    return false;
#endif
}

bool HookManager::uninstall() noexcept {
#if defined(__APPLE__)
    macos::uninstallGameTickHook(trampoline_);
    return true;
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!installed_) return true;

    // Stop draining first: an in-flight tick then observes a null scheduler and
    // returns without touching SDK state that is about to be destroyed.
    activeScheduler.store(nullptr, std::memory_order_release);

    // Restore the game's own code and leave the trampolines allocated. The module
    // stays resident, so an in-flight detour never jumps into freed memory. The
    // stale IGI1SDK_Tick node in the game's list is harmless: it observes a null
    // scheduler and does nothing.
    if (addHookCreated) {
        if (MH_DisableHook(reinterpret_cast<void*>(igi1::displayAddFrameCallback)) != MH_OK)
            log(LogLevel::Warning, "Display_AddFrameCallback hook disable failed");
    }
    if (runHookCreated) {
        if (MH_DisableHook(reinterpret_cast<void*>(igi1::displayRunCallbacks)) != MH_OK)
            log(LogLevel::Warning, "Display_RunCallbacks hook disable failed");
    }

    tickRegistered.store(false, std::memory_order_release);
    installed_ = false;
    trampoline_ = nullptr;
    log(LogLevel::Info,
        "IGI 1 display frame hooks disabled; module left resident so in-flight callbacks stay valid");
    return true;
#else
    return true;
#endif
}
} // namespace igi
