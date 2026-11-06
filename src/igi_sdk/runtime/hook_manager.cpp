#include "igi_sdk/runtime/hook_manager.hpp"

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/runtime/target_profile.hpp"
#include "macos_hook_backend.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>

namespace {
std::atomic<igi::Scheduler*> activeScheduler{nullptr};
std::atomic<std::uint32_t> hookUsers{0};
thread_local bool insideGameHook = false;
extern "C" {
void* igiGameRunTrampoline = nullptr;
}

constexpr std::uint8_t gameRunHandlerPrologue[] = {
    0x53, 0x56, 0x57, 0xE8, 0x78, 0x01, 0x00, 0x00};
constexpr std::size_t displacedBytes = sizeof(gameRunHandlerPrologue);
std::array<std::uint8_t, displacedBytes> installedPatch{};

#if defined(_MSC_VER)
extern "C" __declspec(naked) void gameRunHandlerDetour() {
    __asm {
        pushfd
        push eax
        push ecx
        push edx
        call igiGameRunScheduler
        pop edx
        pop ecx
        pop eax
        popfd
        jmp dword ptr [igiGameRunTrampoline]
    }
}
#else
extern "C" __attribute__((naked)) void gameRunHandlerDetour() {
    __asm__ volatile(
        "pushfl\n\t"
        "pushl %eax\n\t"
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "call _igiGameRunScheduler\n\t"
        "popl %edx\n\t"
        "popl %ecx\n\t"
        "popl %eax\n\t"
        "popfl\n\t"
        "jmp *_igiGameRunTrampoline\n\t");
}
#endif

#if defined(_MSC_VER)
extern "C" void __cdecl igiGameRunScheduler() noexcept;
#else
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl igiGameRunScheduler() noexcept;
#endif

extern "C" void __cdecl igiGameRunScheduler() noexcept {
    hookUsers.fetch_add(1, std::memory_order_acquire);
    insideGameHook = true;
    if (igi::Scheduler* scheduler = activeScheduler.load(std::memory_order_acquire)) {
        scheduler->runPendingFromGameHook();
    }
    insideGameHook = false;
    hookUsers.fetch_sub(1, std::memory_order_release);
}

void logHook(igi::LogLevel level, const char* message) noexcept {
    igi::log(level, message);
}

bool writeJump(std::uint8_t* target, std::size_t displaced,
               std::uintptr_t destination) noexcept {
    const auto relative = static_cast<std::int64_t>(destination) -
                          static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target) + 5);
    if (relative >= std::numeric_limits<std::int32_t>::min() &&
        relative <= std::numeric_limits<std::int32_t>::max()) {
        target[0] = 0xE9;
        const auto rel32 = static_cast<std::int32_t>(relative);
        std::memcpy(target + 1, &rel32, sizeof(rel32));
        for (std::size_t i = 5; i < displaced; ++i) target[i] = 0x90;
        return true;
    }
    if (displaced < 6) return false;
    target[0] = 0x68;
    const auto absolute = static_cast<std::uint32_t>(destination);
    std::memcpy(target + 1, &absolute, sizeof(absolute));
    target[5] = 0xC3;
    for (std::size_t i = 6; i < displaced; ++i) target[i] = 0x90;
    return true;
}

bool executableBytesReadable(const void* address, std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || info.RegionSize < size) return false;
    const DWORD protection = info.Protect & 0xffu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool installGameRunHandler(igi::Scheduler& scheduler, void*& output) {
    using igi::LogLevel;
    if (!igi::igi1::isSupportedExecutable()) {
        logHook(LogLevel::Error, "Game_RunHandler hook refused: executable hash mismatch");
        return false;
    }
    auto* target = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(igi::igi1::gameRunHandler));
    if (!executableBytesReadable(target, displacedBytes) ||
        std::memcmp(target, gameRunHandlerPrologue, displacedBytes) != 0) {
        logHook(LogLevel::Error, "Game_RunHandler hook refused: verified prologue mismatch");
        return false;
    }

    auto* trampoline = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        logHook(LogLevel::Error, "Game_RunHandler hook refused: trampoline allocation failed");
        return false;
    }
    std::memcpy(trampoline, gameRunHandlerPrologue, displacedBytes);
    const auto* callSite = target + 3;
    const auto callDestination = reinterpret_cast<std::uintptr_t>(callSite + 5) +
        static_cast<std::intptr_t>(*reinterpret_cast<const std::int32_t*>(callSite + 1));
    const auto relocated = static_cast<std::int64_t>(callDestination) -
        static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(trampoline + 3) + 5);
    if (relocated < std::numeric_limits<std::int32_t>::min() ||
        relocated > std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        logHook(LogLevel::Error, "Game_RunHandler hook refused: call relocation out of range");
        return false;
    }
    const auto relocated32 = static_cast<std::int32_t>(relocated);
    std::memcpy(trampoline + 4, &relocated32, sizeof(relocated32));
    const auto continuation = reinterpret_cast<std::uintptr_t>(target + displacedBytes);
    const auto returnRelative = static_cast<std::int64_t>(continuation) -
        static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(trampoline + displacedBytes) + 5);
    if (returnRelative < std::numeric_limits<std::int32_t>::min() ||
        returnRelative > std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        logHook(LogLevel::Error, "Game_RunHandler hook refused: continuation out of range");
        return false;
    }
    trampoline[displacedBytes] = 0xE9;
    const auto return32 = static_cast<std::int32_t>(returnRelative);
    std::memcpy(trampoline + displacedBytes + 1, &return32, sizeof(return32));

    DWORD oldProtection = 0;
    if (!VirtualProtect(target, displacedBytes, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        logHook(LogLevel::Error, "Game_RunHandler hook refused: target protection failed");
        return false;
    }
    // Publish the continuation before making the detour reachable.
    igiGameRunTrampoline = trampoline;
    activeScheduler.store(&scheduler, std::memory_order_release);
    if (!writeJump(target, displacedBytes, reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour))) {
        activeScheduler.store(nullptr, std::memory_order_release);
        igiGameRunTrampoline = nullptr;
        VirtualProtect(target, displacedBytes, oldProtection, &oldProtection);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        logHook(LogLevel::Error, "Game_RunHandler hook refused: branch encoding failed");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), target, displacedBytes);
    VirtualProtect(target, displacedBytes, oldProtection, &oldProtection);
    std::memcpy(installedPatch.data(), target, displacedBytes);
    output = trampoline;
    logHook(LogLevel::Info, "IGI 1 Game_RunHandler scheduler hook installed at 0x00416260");
    return true;
}
} // namespace
#endif

namespace igi {
bool HookManager::installDisplayAddFrameCallbackTick() {
    log(LogLevel::Warning, "display callbacks are not gameplay scheduler hooks");
    return false;
}

bool HookManager::installTickHook(igi1::TickHook hook) {
    if (hook != igi1::TickHook::GameRunHandler) {
        log(LogLevel::Warning, "refusing non-Game_RunHandler scheduler hook");
        return false;
    }
#if defined(__APPLE__)
    return macos::installGameTickHook(scheduler_, trampoline_);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) return false;
    void* trampoline = nullptr;
    if (!installGameRunHandler(scheduler_, trampoline)) return false;
    trampoline_ = trampoline;
    installed_ = true;
    return true;
#else
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

bool HookManager::installDisplayEndFrameHook() { return false; }
bool HookManager::installTextDrawHook() { return false; }
bool HookManager::installPrimaryGameRunHandler() {
    return installTickHook(igi1::TickHook::GameRunHandler);
}

bool HookManager::uninstall() noexcept {
#if defined(__APPLE__)
    macos::uninstallGameTickHook(trampoline_);
    return true;
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!installed_) return true;
    if (insideGameHook) {
        log(LogLevel::Error, "Game_RunHandler shutdown refused from its active callback");
        return false;
    }
    auto* target = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(igi1::gameRunHandler));
    DWORD oldProtection = 0;
    if (VirtualProtect(target, displacedBytes, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        if (std::memcmp(target, installedPatch.data(), displacedBytes) == 0) {
            std::memcpy(target, gameRunHandlerPrologue, displacedBytes);
            FlushInstructionCache(GetCurrentProcess(), target, displacedBytes);
        } else {
            log(LogLevel::Warning, "Game_RunHandler hook bytes changed; refusing to overwrite another patch");
            VirtualProtect(target, displacedBytes, oldProtection, &oldProtection);
            return false;
        }
        VirtualProtect(target, displacedBytes, oldProtection, &oldProtection);
    } else {
        log(LogLevel::Warning, "Game_RunHandler hook protection restore failed");
        return false;
    }
    while (hookUsers.load(std::memory_order_acquire) != 0) Sleep(0);
    activeScheduler.store(nullptr, std::memory_order_release);
    // Do not free the trampoline: a thread may already have tail-jumped into
    // it after leaving the guarded scheduler section. It is process-lifetime
    // executable storage, preventing teardown use-after-free.
    trampoline_ = nullptr;
    installed_ = false;
    log(LogLevel::Info, "IGI 1 Game_RunHandler hook uninstalled");
    return true;
#else
    return true;
#endif
}
} // namespace igi
