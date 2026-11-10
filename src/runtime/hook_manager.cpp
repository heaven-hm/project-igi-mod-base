#include "runtime/hook_manager.hpp"

#include "logging/logging.hpp"
#include "runtime/igi_hooks.hpp"
#include "runtime/target_profile.hpp"
#include "macos_hook_backend.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
#include <MinHook.h>

namespace {
// The scheduler that frame ticks drain into. Cleared on detach so an in-flight
// game callback that still points at IGISDK_Tick becomes a safe no-op instead
// of touching an SDK that is being torn down.
std::atomic<igi::Scheduler *> activeScheduler{nullptr};

// Set while a Display_RunCallbacks detour is walking the game's callback list so
// the list-registered IGISDK_Tick does not run a second time in the same frame.
std::atomic<bool> frameTickHandled{false};
// True while the mod owns game behaviour. Retiring the mod clears it so every
// installed detour becomes a pure pass-through: the retail code stays untouched
// but the game returns to its own behaviour (menu strings, frame dispatch).
std::atomic<bool> hookBehaviorActive{true};

bool addHookCreated = false;
bool runHookCreated = false;
bool getStringHookCreated = false;
// True while the SDK owns a live tick in the game's frame callback list.
std::atomic<bool> tickRegistered{false};
// Callback-list mutations must happen from Display_RunCallbacks, the verified
// render-thread frame boundary. The injected worker and the AddFrameCallback
// detour only request that work; they never inspect or mutate the list.
std::atomic<bool> tickRegistrationRequested{false};
std::atomic<bool> tickRemovalRequested{false};
std::atomic<bool> tickRemovalCompleted{true};
std::atomic<bool> runDispatchActive{false};
std::atomic<DWORD> frameDispatchThreadId{0};

// Display_AddFrameCallback's verified retail prologue:
//   mov eax,[esp+4]        ; 8B 44 24 04  (callback argument)
//   push 0x00BCAE00        ; 68 00 AE BC 00  (display frame callback list)
//   push eax               ; 50
//   call Display_Register  ; E8 81 FF FF FF -> 0x00491650
//   add esp,8 ; ret
// The registrar prepends an eight-byte {next, callback} node taken from a pool of
// 32 slots at 0x00C28A40. Display_RunCallbacks (0x00491D00) reads the head at
// 0x00BCAE00 and walks it once per frame, calling each callback with no arguments.
// Registering IGISDK_Tick into that same list is exactly how the game itself
// obtains a per-frame callback.
using DisplayAddFrameCallbackFn = int(__cdecl *)(void *callback);
DisplayAddFrameCallbackFn displayAddFrameCallbackOriginal = nullptr;

// Removes a callback from the same list. Used only when the SDK unloads while the
// list is still being walked, which is why the call is best-effort.
using DisplayRemoveFrameCallbackFn = int(__cdecl *)(void *callback);

// Display_RunCallbacks (0x00491D00) is invoked once per frame by ScreenDraw. Its
// detour is the guaranteed tick driver even if a list reset drops our node.
using DisplayRunCallbacksFn = int(__cdecl *)(void);
DisplayRunCallbacksFn displayRunCallbacksOriginal = nullptr;

// MenuManager_GetString (0x00418EA0) formats "RM: %s" into s->text when a menu
// label is not present in the .RES language files. Stripping the "RM: " prefix
// in-place in s->text lets custom SDK menu strings render cleanly.
using MenuManagerGetStringFn = const char *(__cdecl *)(void *mm, const char *name);
MenuManagerGetStringFn menuManagerGetStringOriginal = nullptr;

struct FrameCallbackNode {
    FrameCallbackNode *next;
    void *callback;
};

// Defined below; declared here so the list walker can compare against its address.
extern "C" void __cdecl IGISDK_Tick() noexcept;

FrameCallbackNode *frameCallbackHead() noexcept {
    return *reinterpret_cast<FrameCallbackNode *const *>(
        static_cast<std::uintptr_t>(igi::game::displayFrameCallbackHead));
}

// True when IGISDK_Tick already occupies a node in the game's display frame
// callback list. The walk is bounded so a malformed list cannot spin forever.
bool tickNodePresent() noexcept {
    FrameCallbackNode *node = frameCallbackHead();
    for (int guard = 0; node && guard < 64; ++guard) {
        if (node->callback == reinterpret_cast<void *>(&IGISDK_Tick))
            return true;
        node = node->next;
    }
    return false;
}

void unregisterFrameTick() noexcept {
    auto **headPtr = reinterpret_cast<FrameCallbackNode **>(
        static_cast<std::uintptr_t>(igi::game::displayFrameCallbackHead));
    FrameCallbackNode *prev = nullptr;
    FrameCallbackNode *node = *headPtr;
    for (int guard = 0; node && guard < 64; ++guard) {
        if (node->callback == reinterpret_cast<void *>(&IGISDK_Tick)) {
            if (prev)
                prev->next = node->next;
            else
                *headPtr = node->next;
            node->next = nullptr;
            node->callback = nullptr;
            break;
        }
        prev = node;
        node = node->next;
    }
    tickRegistered.store(false, std::memory_order_release);
}

// Runs on the game thread once per displayed frame because the game's own
// Display_RunCallbacks invokes it from the display frame callback list.
extern "C" void __cdecl IGISDK_Tick() noexcept {
    // The Display_RunCallbacks detour already drained this frame's work, so skip
    // the duplicate invocation produced by the list walk.
    if (frameTickHandled.load(std::memory_order_acquire))
        return;
    if (igi::Scheduler *scheduler = activeScheduler.load(std::memory_order_acquire)) {
        scheduler->runPendingFromGameHook();
    }
}

void sdkFrameTick() noexcept {
    if (igi::Scheduler *scheduler = activeScheduler.load(std::memory_order_acquire)) {
        scheduler->runPendingFromGameHook();
    }
}

// Registers IGISDK_Tick into the game's display frame callback list when the
// list does not already hold it. Cheap enough to call from a watchdog: the pool
// holds at most 32 nodes. Returns true when the tick is present afterwards.
bool registerFrameTickOnFrameThread() noexcept {
    if (!displayAddFrameCallbackOriginal)
        return false;
    if (tickNodePresent()) {
        tickRegistered.store(true, std::memory_order_release);
        return true;
    }
    displayAddFrameCallbackOriginal(reinterpret_cast<void *>(&IGISDK_Tick));
    const bool present = tickNodePresent();
    tickRegistered.store(present, std::memory_order_release);
    return present;
}

void processFrameTickRequests() noexcept {
    const auto threadId = GetCurrentThreadId();
    if (tickRemovalRequested.exchange(false, std::memory_order_acq_rel)) {
        unregisterFrameTick();
        tickRemovalCompleted.store(true, std::memory_order_release);
        igi::log(igi::LogLevel::Info,
                 "frame tick: callback removed on Display_RunCallbacks thread id=" +
                     std::to_string(threadId));
        return;
    }
    if (tickRegistrationRequested.exchange(false, std::memory_order_acq_rel)) {
        const bool registered = registerFrameTickOnFrameThread();
        igi::log(registered ? igi::LogLevel::Info : igi::LogLevel::Warning,
                 registered ? "frame tick: callback registered on Display_RunCallbacks thread id=" +
                                  std::to_string(threadId)
                            : "frame tick: callback registration failed on Display_RunCallbacks "
                              "thread id=" +
                                  std::to_string(threadId));
    }
}

void recordFrameDispatchThread() noexcept {
    const DWORD current = GetCurrentThreadId();
    DWORD expected = 0;
    if (frameDispatchThreadId.compare_exchange_strong(expected, current,
                                                      std::memory_order_acq_rel)) {
        igi::log(igi::LogLevel::Info,
                 "frame tick: Display_RunCallbacks dispatch thread id=" + std::to_string(current));
    } else if (expected != current) {
        igi::log(igi::LogLevel::Error, "frame tick: Display_RunCallbacks moved from thread id=" +
                                           std::to_string(expected) +
                                           " to thread id=" + std::to_string(current));
    }
}

// Detour for Display_AddFrameCallback itself. It fires when the game registers a
// callback (usually once during startup). The detour forwards to the registrar so
// the game's callback is installed, then makes sure our tick is still present so a
// display reset cannot silently drop it.
extern "C" int __cdecl displayAddFrameCallbackDetour(void *callback) {
    if (!hookBehaviorActive.load(std::memory_order_acquire))
        return displayAddFrameCallbackOriginal ? displayAddFrameCallbackOriginal(callback) : 0;
    const int result =
        displayAddFrameCallbackOriginal ? displayAddFrameCallbackOriginal(callback) : 0;
    tickRegistrationRequested.store(true, std::memory_order_release);
    return result;
}

// Detour for Display_RunCallbacks. This runs exactly once per displayed frame
// from ScreenDraw. It suppresses the duplicate list-walk tick, runs the original
// frame dispatch, then drains the scheduler for the completed frame.
extern "C" int __cdecl displayRunCallbacksDetour() {
    if (!hookBehaviorActive.load(std::memory_order_acquire))
        return displayRunCallbacksOriginal ? displayRunCallbacksOriginal() : 0;
    runDispatchActive.store(true, std::memory_order_release);
    recordFrameDispatchThread();
    processFrameTickRequests();
    frameTickHandled.store(true, std::memory_order_release);
    const int result = displayRunCallbacksOriginal ? displayRunCallbacksOriginal() : 0;
    frameTickHandled.store(false, std::memory_order_release);
    sdkFrameTick();
    runDispatchActive.store(false, std::memory_order_release);
    return result;
}

extern "C" const char *__cdecl menuManagerGetStringDetour(void *mm, const char *name) {
    const char *res =
        menuManagerGetStringOriginal ? menuManagerGetStringOriginal(mm, name) : nullptr;
    // This hook edits the returned menu string. Once the mod is retired the game
    // must see its own strings again, otherwise its own menus break.
    if (!hookBehaviorActive.load(std::memory_order_acquire))
        return res;
    if (res && std::strncmp(res, "RM: ", 4) == 0) {
        char *mutableRes = const_cast<char *>(res);
        std::memmove(mutableRes, res + 4, std::strlen(res + 4) + 1);
    }
    return res;
}

bool installCoexistenceHook(igi::Scheduler &scheduler, void *&trampolineOut,
                            bool &usedExistingHooks) {
    using igi::LogLevel;
    const auto initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    activeScheduler.store(&scheduler, std::memory_order_release);

    // Another IGI menu DLL already owns the display entries. Chain only the
    // per-frame dispatcher so this menu gets a game-thread tick without touching
    // the other mod's Display_AddFrameCallback detour or the retail prologues.
    if (!runHookCreated) {
        const auto status = MH_CreateHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks),
                                          reinterpret_cast<void *>(&displayRunCallbacksDetour),
                                          reinterpret_cast<void **>(&displayRunCallbacksOriginal));
        if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
            activeScheduler.store(nullptr, std::memory_order_release);
            igi::log(LogLevel::Error, "coexistence: Display_RunCallbacks chain creation failed");
            return false;
        }
        runHookCreated = true;
    }
    if (MH_EnableHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks)) != MH_OK) {
        MH_DisableHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks));
        MH_RemoveHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks));
        runHookCreated = false;
        displayRunCallbacksOriginal = nullptr;
        activeScheduler.store(nullptr, std::memory_order_release);
        igi::log(LogLevel::Error, "coexistence: Display_RunCallbacks chain enable failed");
        return false;
    }

    // The game's AddFrameCallback is already detoured by the other mod. Calling
    // that entry from our run-dispatch detour (game thread) forwards to the real
    // registrar and adds this menu's tick to the same list.
    displayAddFrameCallbackOriginal =
        reinterpret_cast<DisplayAddFrameCallbackFn>(igi::game::displayAddFrameCallback);

    if (!getStringHookCreated) {
        const auto strStatus =
            MH_CreateHook(reinterpret_cast<void *>(igi::game::menuManagerGetString),
                          reinterpret_cast<void *>(&menuManagerGetStringDetour),
                          reinterpret_cast<void **>(&menuManagerGetStringOriginal));
        if (strStatus == MH_OK || strStatus == MH_ERROR_ALREADY_CREATED)
            getStringHookCreated = true;
    }
    if (getStringHookCreated &&
        MH_EnableHook(reinterpret_cast<void *>(igi::game::menuManagerGetString)) != MH_OK) {
        getStringHookCreated = false;
    }

    hookBehaviorActive.store(true, std::memory_order_release);
    tickRegistrationRequested.store(true, std::memory_order_release);
    tickRemovalRequested.store(false, std::memory_order_release);
    tickRemovalCompleted.store(true, std::memory_order_release);
    frameDispatchThreadId.store(0, std::memory_order_release);
    usedExistingHooks = true;
    trampolineOut = nullptr;
    igi::log(LogLevel::Info,
             "coexistence: another IGI mod owns the display hooks; this menu chained "
             "Display_RunCallbacks and will register its tick through the existing registrar");
    return true;
}

bool installDisplayFrameHook(igi::Scheduler &scheduler, void *&trampolineOut,
                             bool &usedExistingHooks) {
    using igi::LogLevel;
    if (!igi::game::isSupportedExecutable()) {
        if (igi::game::hasIgiModHook()) {
            igi::log(LogLevel::Info,
                     "Display hooks are already owned by another IGI mod; enabling coexistence "
                     "mode instead of refusing the internal menu");
            return installCoexistenceHook(scheduler, trampolineOut, usedExistingHooks);
        }
        igi::log(
            LogLevel::Error,
            "Display_AddFrameCallback hook refused: supported retail hook signatures mismatch");
        return false;
    }
    const auto initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        return false;

    activeScheduler.store(&scheduler, std::memory_order_release);

    if (!addHookCreated) {
        const auto status =
            MH_CreateHook(reinterpret_cast<void *>(igi::game::displayAddFrameCallback),
                          reinterpret_cast<void *>(&displayAddFrameCallbackDetour),
                          reinterpret_cast<void **>(&displayAddFrameCallbackOriginal));
        if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
            activeScheduler.store(nullptr, std::memory_order_release);
            igi::log(LogLevel::Error, "Display_AddFrameCallback hook creation failed");
            return false;
        }
        addHookCreated = true;
    }
    if (MH_EnableHook(reinterpret_cast<void *>(igi::game::displayAddFrameCallback)) != MH_OK) {
        activeScheduler.store(nullptr, std::memory_order_release);
        igi::log(LogLevel::Error, "Display_AddFrameCallback hook enable failed");
        return false;
    }

    // Guaranteed per-frame driver: hook the display callback dispatch function.
    if (!runHookCreated) {
        const auto runStatus =
            MH_CreateHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks),
                          reinterpret_cast<void *>(&displayRunCallbacksDetour),
                          reinterpret_cast<void **>(&displayRunCallbacksOriginal));
        if (runStatus == MH_OK || runStatus == MH_ERROR_ALREADY_CREATED) {
            runHookCreated = true;
        } else {
            igi::log(
                LogLevel::Warning,
                "Display_RunCallbacks hook creation failed; relying on the callback list only");
        }
    }
    if (!runHookCreated ||
        MH_EnableHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks)) != MH_OK) {
        MH_DisableHook(reinterpret_cast<void *>(igi::game::displayAddFrameCallback));
        MH_RemoveHook(reinterpret_cast<void *>(igi::game::displayAddFrameCallback));
        addHookCreated = false;
        displayAddFrameCallbackOriginal = nullptr;
        if (runHookCreated) {
            MH_DisableHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks));
            MH_RemoveHook(reinterpret_cast<void *>(igi::game::displayRunCallbacks));
            runHookCreated = false;
            displayRunCallbacksOriginal = nullptr;
        }
        activeScheduler.store(nullptr, std::memory_order_release);
        igi::log(LogLevel::Error,
                 "Display_RunCallbacks hook is required to serialize callback-list mutations");
        return false;
    }

    if (!getStringHookCreated) {
        const auto strStatus =
            MH_CreateHook(reinterpret_cast<void *>(igi::game::menuManagerGetString),
                          reinterpret_cast<void *>(&menuManagerGetStringDetour),
                          reinterpret_cast<void **>(&menuManagerGetStringOriginal));
        if (strStatus == MH_OK || strStatus == MH_ERROR_ALREADY_CREATED) {
            getStringHookCreated = true;
        }
    }
    if (getStringHookCreated &&
        MH_EnableHook(reinterpret_cast<void *>(igi::game::menuManagerGetString)) != MH_OK) {
        getStringHookCreated = false;
    }

    hookBehaviorActive.store(true, std::memory_order_release);
    trampolineOut = reinterpret_cast<void *>(displayAddFrameCallbackOriginal);

    tickRegistrationRequested.store(true, std::memory_order_release);
    tickRemovalRequested.store(false, std::memory_order_release);
    tickRemovalCompleted.store(true, std::memory_order_release);
    frameDispatchThreadId.store(0, std::memory_order_release);
    igi::log(LogLevel::Info,
             "IGI 1 Display_AddFrameCallback hook installed at 0x004916C0; frame tick registered");
    igi::log(LogLevel::Info, "IGI 1 Display_RunCallbacks per-frame driver hooked at 0x00491D00");
    igi::log(LogLevel::Info,
             "Display_AddFrameCallback tick registration requested for the frame thread");
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
        activeScheduler.store(&scheduler_, std::memory_order_release);
        if (addHookCreated)
            MH_EnableHook(reinterpret_cast<void *>(game::displayAddFrameCallback));
        if (runHookCreated)
            MH_EnableHook(reinterpret_cast<void *>(game::displayRunCallbacks));
        if (getStringHookCreated)
            MH_EnableHook(reinterpret_cast<void *>(game::menuManagerGetString));
        // A previous retire made every detour a pass-through. Re-enabling the
        // menu must turn the behaviour back on or the tick request is ignored.
        hookBehaviorActive.store(true, std::memory_order_release);
        retired_ = false;
        tickRegistrationRequested.store(true, std::memory_order_release);
        installed_ = true;
        log(LogLevel::Info, "IGI 1 Display_AddFrameCallback hook re-enabled");
        return true;
    }
    void *trampoline = nullptr;
    if (!installDisplayFrameHook(scheduler_, trampoline, usedExistingHooks_))
        return false;
    trampoline_ = trampoline;
    installed_ = true;
    return true;
#else
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

bool HookManager::installTickHook(game::TickHook hook) {
    if (hook != game::TickHook::DisplayAddFrameCallback) {
        log(LogLevel::Warning, "refusing scheduler hook other than Display_AddFrameCallback");
        return false;
    }
    return installDisplayAddFrameCallbackTick();
}

bool HookManager::installDisplayEndFrameHook() {
    return false;
}
bool HookManager::installTextDrawHook() {
    return false;
}

bool HookManager::ensureFrameTickRegistered() noexcept {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!installed_ || !displayAddFrameCallbackOriginal)
        return false;
    tickRegistrationRequested.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

bool HookManager::uninstall(bool restoreHooks) noexcept {
#if defined(__APPLE__)
    macos::uninstallGameTickHook(trampoline_);
    return true;
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!installed_)
        return true;

    // Stop draining first: an in-flight tick then observes a null scheduler and
    // returns without touching SDK state that is about to be destroyed.
    activeScheduler.store(nullptr, std::memory_order_release);

    if (usedExistingHooks_) {
        // We only chained the other mod's Display_RunCallbacks detour. Remove our
        // tick through that detour (game thread request), then remove our chain.
        // Disabling our chain restores the other mod's jump, never retail code,
        // so this path cannot freeze the game the way restoring the prologues did.
        if (scheduler_.isGameThread()) {
            unregisterFrameTick();
            tickRemovalCompleted.store(true, std::memory_order_release);
        } else {
            tickRemovalCompleted.store(false, std::memory_order_release);
            tickRemovalRequested.store(true, std::memory_order_release);
            constexpr DWORD kCoexistRemovalTimeoutMs = 5000;
            const DWORD deadline = GetTickCount() + kCoexistRemovalTimeoutMs;
            while (!tickRemovalCompleted.load(std::memory_order_acquire) &&
                   static_cast<LONG>(GetTickCount() - deadline) < 0) {
                Sleep(1);
            }
        }
        if (!tickRemovalCompleted.load(std::memory_order_acquire)) {
            // Never unload while the game's callback list still points at this
            // module's tick. Keep the chain resident; the next displayed frame
            // will process the removal request and Exit Mod can be retried.
            log(LogLevel::Error,
                "coexistence: callback removal timed out; keeping the module resident to avoid a "
                "callback into freed memory");
            return false;
        }
        if (runHookCreated) {
            MH_DisableHook(reinterpret_cast<void *>(game::displayRunCallbacks));
            MH_RemoveHook(reinterpret_cast<void *>(game::displayRunCallbacks));
            runHookCreated = false;
            displayRunCallbacksOriginal = nullptr;
        }
        if (getStringHookCreated) {
            MH_DisableHook(reinterpret_cast<void *>(game::menuManagerGetString));
            MH_RemoveHook(reinterpret_cast<void *>(game::menuManagerGetString));
            getStringHookCreated = false;
            menuManagerGetStringOriginal = nullptr;
        }
        displayAddFrameCallbackOriginal = nullptr;
        hookBehaviorActive.store(false, std::memory_order_release);
        installed_ = false;
        retired_ = false;
        usedExistingHooks_ = false;
        tickRemovalRequested.store(false, std::memory_order_release);
        log(LogLevel::Info,
            "coexistence: this menu's chained hooks and callback were removed; the other mod's "
            "hooks are intact");
        return true;
    }

    if (scheduler_.isGameThread()) {
        unregisterFrameTick();
        tickRemovalCompleted.store(true, std::memory_order_release);
        log(LogLevel::Info, "frame tick: callback removed directly on verified frame thread");
    } else {
        // The worker must not walk or modify the retail callback list. Wait for
        // Display_RunCallbacks to remove our node after its current list walk.
        tickRemovalCompleted.store(false, std::memory_order_release);
        tickRemovalRequested.store(true, std::memory_order_release);
        constexpr DWORD kFrameRemovalTimeoutMs = 5000;
        const DWORD deadline = GetTickCount() + kFrameRemovalTimeoutMs;
        while (!tickRemovalCompleted.load(std::memory_order_acquire) &&
               static_cast<LONG>(GetTickCount() - deadline) < 0) {
            Sleep(1);
        }
    }
    if (!tickRemovalCompleted.load(std::memory_order_acquire)) {
        log(LogLevel::Error,
            "frame tick removal timed out; preserving hooks and SDK state to avoid a cross-thread "
            "callback-list mutation");
        return false;
    }

    if (!restoreHooks) {
        // Safe retire: the callback leaves the retail list, the scheduler is
        // detached and every detour becomes a pass-through. The game therefore
        // returns to its own behaviour (including its own menu strings) while its
        // code stays untouched and this module stays resident.
        hookBehaviorActive.store(false, std::memory_order_release);
        retired_ = true;
        log(LogLevel::Info,
            "IGI 1 display frame hooks retired in place: detours are pass-through, retail code "
            "left untouched and the module stays resident");
        return true;
    }

    if (!scheduler_.isGameThread()) {
        constexpr DWORD kDispatchExitTimeoutMs = 5000;
        const DWORD deadline = GetTickCount() + kDispatchExitTimeoutMs;
        while (runDispatchActive.load(std::memory_order_acquire) &&
               static_cast<LONG>(GetTickCount() - deadline) < 0) {
            Sleep(1);
        }
        if (runDispatchActive.load(std::memory_order_acquire)) {
            log(LogLevel::Error,
                "frame dispatch did not leave the detour; preserving hooks to avoid restoring code "
                "under an active game frame");
            return false;
        }
    }

    bool restored = true;
    if (addHookCreated) {
        const auto status = MH_DisableHook(reinterpret_cast<void *>(game::displayAddFrameCallback));
        if (status != MH_OK && status != MH_ERROR_DISABLED) {
            log(LogLevel::Error,
                "Display_AddFrameCallback restore failed; teardown remains pending");
            restored = false;
        } else {
            MH_RemoveHook(reinterpret_cast<void *>(game::displayAddFrameCallback));
            addHookCreated = false;
            displayAddFrameCallbackOriginal = nullptr;
        }
    }
    if (runHookCreated) {
        const auto status = MH_DisableHook(reinterpret_cast<void *>(game::displayRunCallbacks));
        if (status != MH_OK && status != MH_ERROR_DISABLED) {
            log(LogLevel::Error, "Display_RunCallbacks restore failed; teardown remains pending");
            restored = false;
        } else {
            MH_RemoveHook(reinterpret_cast<void *>(game::displayRunCallbacks));
            runHookCreated = false;
            displayRunCallbacksOriginal = nullptr;
        }
    }
    if (getStringHookCreated) {
        const auto status = MH_DisableHook(reinterpret_cast<void *>(game::menuManagerGetString));
        if (status == MH_OK || status == MH_ERROR_DISABLED) {
            MH_RemoveHook(reinterpret_cast<void *>(game::menuManagerGetString));
            getStringHookCreated = false;
            menuManagerGetStringOriginal = nullptr;
        }
    }
    if (!restored || !game::isSupportedExecutable()) {
        log(LogLevel::Error,
            "hook teardown incomplete: original retail hook bytes are not restored");
        return false;
    }

    installed_ = false;
    trampoline_ = nullptr;
    log(LogLevel::Info, "IGI 1 display frame hooks disabled and removed cleanly");
    return true;
#else
    return true;
#endif
}
} // namespace igi
