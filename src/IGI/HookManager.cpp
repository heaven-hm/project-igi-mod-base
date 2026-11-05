#include "IGI/HookManager.hpp"

#include "IGI/IGI1Hooks.hpp"
#include "IGI/Logging.hpp"

#include <cstring>

#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
namespace {
igi::Scheduler* activeScheduler = nullptr;
extern "C" void* igiGameRunTrampoline = nullptr;
extern "C" void __cdecl igiGameRunScheduler() noexcept;
extern "C" __declspec(naked) void gameRunHandlerDetour() {
    __asm { call igiGameRunScheduler; jmp dword ptr [igiGameRunTrampoline] }
}
extern "C" void __cdecl igiGameRunScheduler() noexcept { activeScheduler->runPending(); }
}
#endif

namespace igi {
bool HookManager::installPrimaryGameRunHandler() {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    auto* target = reinterpret_cast<std::uint8_t*>(igi1::gameRunHandler);
    // Verified against the EU/Chinese IGI-RE executable identified by the documented SHA-256.
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
    VirtualProtect(target, 7, PAGE_EXECUTE_READWRITE, &oldProtection);
    target[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(target + 1) = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour) - (reinterpret_cast<std::uintptr_t>(target) + 5));
    target[5] = 0x90; target[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, 7);
    VirtualProtect(target, 7, oldProtection, &oldProtection);
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
