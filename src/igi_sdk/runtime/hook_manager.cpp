#include "igi_sdk/runtime/hook_manager.hpp"

#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/target_profile.hpp"

#include <cstring>

#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
namespace {
igi::Scheduler* activeScheduler = nullptr;
#if defined(_MSC_VER)
extern "C" void* igiGameRunTrampoline = nullptr;
#else
extern "C" void* igiGameRunTrampoline;
void* igiGameRunTrampoline = nullptr;
#endif
extern "C" void __cdecl igiGameRunScheduler() noexcept;
#if defined(_MSC_VER)
extern "C" __declspec(naked) void gameRunHandlerDetour() {
    __asm { call dword ptr [igiGameRunTrampoline]; call igiGameRunScheduler; ret }
}
#else
extern "C" __attribute__((naked)) void gameRunHandlerDetour() {
    __asm__ volatile(
        "call *_igiGameRunTrampoline\n\t"
        "call _igiGameRunScheduler\n\t"
        "ret\n\t");
}
#endif
extern "C" void __cdecl igiGameRunScheduler() noexcept { activeScheduler->runPendingFromGameHook(); }
}
#endif

namespace igi {
bool HookManager::installPrimaryGameRunHandler() {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) {
        log(LogLevel::Warning, "IGI 1 Game_RunHandler hook is already installed");
        return false;
    }
    if (!igi1::isSupportedExecutable()) {
        log(LogLevel::Error, "IGI 1 executable SHA-256 mismatch; hook refused");
        return false;
    }
    auto* target = reinterpret_cast<std::uint8_t*>(igi1::gameRunHandler);
    // This target is Game_RunHandler(Game *), a cdecl task-slot-1 handler.
    // The naked detour preserves its argument stack exactly.
    constexpr std::uint8_t prologue[] = {0x53, 0x56, 0x57, 0xE8, 0x78, 0x01, 0x00};
    if (std::memcmp(target, prologue, sizeof(prologue)) != 0) {
        log(LogLevel::Error, "IGI 1 Game_RunHandler signature mismatch; hook refused");
        return false;
    }
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, target, sizeof(prologue));
    const auto originalCall = reinterpret_cast<std::uintptr_t>(target + 7) + *reinterpret_cast<std::int32_t*>(target + 3);
    *reinterpret_cast<std::uint8_t*>(trampoline + 3) = 0xE8;
    *reinterpret_cast<std::int32_t*>(trampoline + 4) = static_cast<std::int32_t>(originalCall - (reinterpret_cast<std::uintptr_t>(trampoline) + 7));
    trampoline[7] = 0xE9;
    *reinterpret_cast<std::int32_t*>(trampoline + 8) = static_cast<std::int32_t>((reinterpret_cast<std::uintptr_t>(target + 7)) - (reinterpret_cast<std::uintptr_t>(trampoline + 12)));
    activeScheduler = &scheduler_;
    igiGameRunTrampoline = trampoline;
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, 7, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        igiGameRunTrampoline = nullptr;
        activeScheduler = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        log(LogLevel::Error, "could not make IGI 1 Game_RunHandler writable");
        return false;
    }
    target[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(target + 1) = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour) - (reinterpret_cast<std::uintptr_t>(target) + 5));
    target[5] = 0x90; target[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, 7);
    if (!VirtualProtect(target, 7, oldProtection, &oldProtection)) {
        log(LogLevel::Warning, "IGI 1 Game_RunHandler protection could not be restored");
    }
    trampoline_ = trampoline;
    installed_ = true;
    log(LogLevel::Info, "IGI 1 Game_RunHandler scheduler hook installed");
    return true;
#else
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

void HookManager::uninstall() noexcept {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) {
        auto* target = reinterpret_cast<std::uint8_t*>(igi1::gameRunHandler);
        constexpr std::uint8_t prologue[] = {0x53, 0x56, 0x57, 0xE8, 0x78, 0x01, 0x00};
        DWORD oldProtection = 0;
        VirtualProtect(target, 7, PAGE_EXECUTE_READWRITE, &oldProtection);
        std::memcpy(target, prologue, sizeof(prologue));
        VirtualProtect(target, 7, oldProtection, &oldProtection);
        FlushInstructionCache(GetCurrentProcess(), target, 7);
        VirtualFree(trampoline_, 0, MEM_RELEASE);
        trampoline_ = nullptr; installed_ = false; activeScheduler = nullptr; igiGameRunTrampoline = nullptr;
    }
#endif
}
}
