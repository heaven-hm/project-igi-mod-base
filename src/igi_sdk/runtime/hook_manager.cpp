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
std::atomic<igi::Scheduler*> activeScheduler{nullptr};
bool hookInstalled = false;
using GameMainLoop = int (__cdecl *)(HINSTANCE, std::uint32_t, std::uint32_t);
GameMainLoop gameMainLoopOriginal = nullptr;

extern "C" void __cdecl igiDisplayFrameScheduler() noexcept {
    if (igi::Scheduler* scheduler = activeScheduler.load(std::memory_order_acquire)) {
        scheduler->runPendingFromGameHook();
    }
}

extern "C" int __cdecl igiGameMainLoopDetour(HINSTANCE instance, std::uint32_t arg1, std::uint32_t arg2) {
    igiDisplayFrameScheduler();
    return gameMainLoopOriginal ? gameMainLoopOriginal(instance, arg1, arg2) : 0;
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
    const auto status = MH_CreateHook(reinterpret_cast<void*>(igi::igi1::gameMainLoop),
                                      reinterpret_cast<void*>(&igiGameMainLoopDetour),
                                      reinterpret_cast<void**>(&gameMainLoopOriginal));
    if (status != MH_OK || MH_EnableHook(reinterpret_cast<void*>(igi::igi1::gameMainLoop)) != MH_OK) {
        if (status == MH_OK) MH_RemoveHook(reinterpret_cast<void*>(igi::igi1::gameMainLoop));
        activeScheduler.store(nullptr, std::memory_order_release);
        return false;
    }
    hookInstalled = true;
    trampolineOut = reinterpret_cast<void*>(gameMainLoopOriginal);
    igi::log(LogLevel::Info, "IGI 1 GameMainLoop fiber scheduler hook installed at 0x0048F530");
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
        log(LogLevel::Warning, "IGI 1 hook is already installed");
        return false;
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

bool HookManager::uninstall() noexcept {
#if defined(__APPLE__)
    macos::uninstallGameTickHook(trampoline_);
    return true;
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!hookInstalled) return true;
    activeScheduler.store(nullptr, std::memory_order_release);
    if (MH_DisableHook(reinterpret_cast<void*>(igi::igi1::gameMainLoop)) != MH_OK ||
        MH_RemoveHook(reinterpret_cast<void*>(igi::igi1::gameMainLoop)) != MH_OK) {
        log(LogLevel::Warning, "GameMainLoop hook restore failed");
        return false;
    }
    hookInstalled = false;
    gameMainLoopOriginal = nullptr;
    trampoline_ = nullptr;
    installed_ = false;
    log(LogLevel::Info, "IGI 1 GameMainLoop scheduler hook uninstalled");
    return true;
#else
    return true;
#endif
}
} // namespace igi
