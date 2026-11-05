#include "igi_sdk/runtime/hook_manager.hpp"

#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/target_profile.hpp"
#include "macos_hook_backend.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

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
// Game_RunHandler starts with `push ebx; push esi; push edi; call rel32`, so the
// displaced prologue is 8 bytes: three 1-byte pushes plus a 5-byte near call.
constexpr std::size_t hookBytes = 8;

#if defined(_MSC_VER)
extern "C" void __cdecl igiGameRunScheduler() noexcept;
#else
// The game offers no 16-byte stack alignment guarantee at the hook site, so the
// compiled scheduler realigns the stack before it touches SSE state.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl igiGameRunScheduler() noexcept;
#endif
#if defined(_MSC_VER)
extern "C" __declspec(naked) void gameRunHandlerDetour() {
    __asm { call igiGameRunScheduler; call dword ptr [igiGameRunTrampoline]; ret }
}
#else
extern "C" __attribute__((naked)) void gameRunHandlerDetour() {
    __asm__ volatile(
        "call _igiGameRunScheduler\n\t"
        "call *_igiGameRunTrampoline\n\t"
        "ret\n\t");
}
#endif
#if defined(_MSC_VER)
extern "C" void __cdecl igiGameRunScheduler() noexcept { activeScheduler->runPendingFromGameHook(); }
#else
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl igiGameRunScheduler() noexcept {
    activeScheduler->runPendingFromGameHook();
}
#endif

// `push imm32; ret` is an absolute jump that leaves every register untouched.
// `mov eax, imm32; jmp eax` would destroy EAX at the handler's entry point.
void writeAbsoluteJump(std::uint8_t* target, std::uintptr_t destination) noexcept {
    target[0] = 0x68;
    const auto address = static_cast<std::uint32_t>(destination);
    std::memcpy(target + 1, &address, sizeof(address));
    target[5] = 0xC3;
    target[6] = 0x90;
    target[7] = 0x90;
}

void logAddress(const char* label, std::uintptr_t address) noexcept {
    char message[96]{};
    std::snprintf(message, sizeof(message), "%s0x%08lX", label, static_cast<unsigned long>(address));
    igi::log(igi::LogLevel::Debug, message);
}
}
#endif

namespace igi {
bool HookManager::installPrimaryGameRunHandler() {
#if defined(__APPLE__)
    return macos::installGameTickHook(scheduler_, trampoline_);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    log(LogLevel::Debug, "Game_RunHandler hook: begin installation");
    if (installed_) {
        log(LogLevel::Warning, "IGI 1 Game_RunHandler hook is already installed");
        return false;
    }
    if (!igi1::isSupportedExecutable()) {
        log(LogLevel::Error, "IGI 1 executable SHA-256 mismatch; hook refused");
        return false;
    }
    log(LogLevel::Debug, "Game_RunHandler hook: executable hash verified");
    auto* target = reinterpret_cast<std::uint8_t*>(igi1::gameRunHandler);
    logAddress("Game_RunHandler hook: target=", reinterpret_cast<std::uintptr_t>(target));
    // This target is Game_RunHandler(Game *), a cdecl task-slot-1 handler.
    // The naked detour preserves its argument stack exactly.
    // Full displaced prologue: push ebx; push esi; push edi; call Game_UpdateCheats.
    // The call is E8+rel32 (5 bytes), so 7 bytes would cut it in half.
    constexpr std::uint8_t prologue[] = {0x53, 0x56, 0x57, 0xE8, 0x78, 0x01, 0x00, 0x00};
    if (std::memcmp(target, prologue, sizeof(prologue)) != 0) {
        log(LogLevel::Error, "IGI 1 Game_RunHandler signature mismatch; hook refused");
        return false;
    }
    log(LogLevel::Debug, "Game_RunHandler hook: target prologue verified");
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;
    logAddress("Game_RunHandler hook: trampoline=", reinterpret_cast<std::uintptr_t>(trampoline));
    // Relocated prologue: pushes, then the original near call, then resume.
    std::memcpy(trampoline, target, 3);
    // A rel32 call is relative to the end of its own instruction, target + 8.
    const auto originalCall = reinterpret_cast<std::uintptr_t>(target + hookBytes) +
                              static_cast<std::intptr_t>(*reinterpret_cast<const std::int32_t*>(target + 3));
    logAddress("Game_RunHandler hook: original call=", originalCall);
    const auto trampolineBase = reinterpret_cast<std::intptr_t>(trampoline);
    const auto continuation = reinterpret_cast<std::intptr_t>(target + hookBytes);
    // call rel32 at trampoline+3 (ends at +8); jmp rel32 at trampoline+8 (ends at +13).
    const auto callRel = static_cast<std::int64_t>(originalCall) - static_cast<std::int64_t>(trampolineBase + 8);
    const auto jumpRel = static_cast<std::int64_t>(continuation) - static_cast<std::int64_t>(trampolineBase + 13);
    if (callRel < std::numeric_limits<std::int32_t>::min() || callRel > std::numeric_limits<std::int32_t>::max() ||
        jumpRel < std::numeric_limits<std::int32_t>::min() || jumpRel > std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        log(LogLevel::Error, "Game_RunHandler hook: trampoline too far for a rel32 branch");
        return false;
    }
    trampoline[3] = 0xE8;
    const auto callRel32 = static_cast<std::int32_t>(callRel);
    std::memcpy(trampoline + 4, &callRel32, sizeof(callRel32));
    trampoline[8] = 0xE9;
    const auto jumpRel32 = static_cast<std::int32_t>(jumpRel);
    std::memcpy(trampoline + 9, &jumpRel32, sizeof(jumpRel32));
    activeScheduler = &scheduler_;
    igiGameRunTrampoline = trampoline;
    logAddress("Game_RunHandler hook: detour=", reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour));
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, hookBytes, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        igiGameRunTrampoline = nullptr;
        activeScheduler = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        log(LogLevel::Error, "could not make IGI 1 Game_RunHandler writable");
        return false;
    }
    log(LogLevel::Debug, "Game_RunHandler hook: target memory writable");
    writeAbsoluteJump(target, reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour));
    FlushInstructionCache(GetCurrentProcess(), target, hookBytes);
    if (!VirtualProtect(target, hookBytes, oldProtection, &oldProtection)) {
        log(LogLevel::Warning, "IGI 1 Game_RunHandler protection could not be restored");
    }
    trampoline_ = trampoline;
    installed_ = true;
    log(LogLevel::Debug, "Game_RunHandler hook: patch installed and instruction cache flushed");
    log(LogLevel::Info, "IGI 1 Game_RunHandler scheduler hook installed");
    return true;
#else
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

void HookManager::uninstall() noexcept {
#if defined(__APPLE__)
    macos::uninstallGameTickHook(trampoline_);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) {
        log(LogLevel::Debug, "Game_RunHandler hook: uninstall begin");
        auto* target = reinterpret_cast<std::uint8_t*>(igi1::gameRunHandler);
        constexpr std::uint8_t prologue[] = {0x53, 0x56, 0x57, 0xE8, 0x78, 0x01, 0x00, 0x00};
        DWORD oldProtection = 0;
        VirtualProtect(target, hookBytes, PAGE_EXECUTE_READWRITE, &oldProtection);
        std::memcpy(target, prologue, sizeof(prologue));
        VirtualProtect(target, hookBytes, oldProtection, &oldProtection);
        FlushInstructionCache(GetCurrentProcess(), target, hookBytes);
        VirtualFree(trampoline_, 0, MEM_RELEASE);
        trampoline_ = nullptr; installed_ = false; activeScheduler = nullptr; igiGameRunTrampoline = nullptr;
        log(LogLevel::Info, "Game_RunHandler hook: uninstall complete");
    }
#endif
}
}
