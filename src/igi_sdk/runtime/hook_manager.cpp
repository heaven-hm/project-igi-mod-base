#include "igi_sdk/runtime/hook_manager.hpp"

#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/target_profile.hpp"
#include "macos_hook_backend.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
#include <tlhelp32.h>

#include <vector>

namespace {
igi::Scheduler* activeScheduler = nullptr;
// Holds the trampoline of whichever primary hook is installed.
extern "C" void* igiGameRunTrampoline = nullptr;

// Game_RunHandler opens with `push ebx; push esi; push edi; call rel32`, so the
// displaced prologue is 8 bytes and the call inside it has to be relocated.
constexpr std::uint8_t gameRunHandlerPrologue[] = {0x53, 0x56, 0x57, 0xE8, 0x78, 0x01, 0x00, 0x00};

// Text_Draw opens with `mov 0x4(%esp),%eax; push %eax` (five bytes); the real
// `call Text_Render` starts at +5, so the displaced region holds no branch.
constexpr std::uint8_t textDrawPrologue[] = {0x8B, 0x44, 0x24, 0x04, 0x50};

struct HookTarget {
    const char* name;
    std::uint32_t address;
    const std::uint8_t* prologue;
    std::size_t displaced;
    std::size_t relocatedCall; // offset of an E8 inside `prologue`, or 0 for none
};

constexpr HookTarget gameRunHandlerTarget{
    "Game_RunHandler", igi::igi1::gameRunHandler, gameRunHandlerPrologue,
    sizeof(gameRunHandlerPrologue), 3};
constexpr HookTarget textDrawTarget{
    "Text_Draw", igi::igi1::textDraw, textDrawPrologue, sizeof(textDrawPrologue), 0};

// Nothing of the displaced prologue has executed yet, and both targets are
// cdecl, so a whole C function may not silently clobber the incoming register
// state. Save EFLAGS/EAX/ECX/EDX around the scheduler, then tail jump into the
// trampoline so the target returns directly to its own caller.
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
#if defined(_MSC_VER)
extern "C" void __cdecl igiGameRunScheduler() noexcept { activeScheduler->runPendingFromGameHook(); }
#else
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl igiGameRunScheduler() noexcept {
    activeScheduler->runPendingFromGameHook();
}
#endif

// The game thread executes the bytes being patched on every frame, so the write
// must never be observed half-applied. Suspend the peers, patch, flush, resume.
std::vector<HANDLE> suspendPeerThreads() noexcept {
    std::vector<HANDLE> suspended;
    const DWORD processId = GetCurrentProcessId();
    const DWORD selfId = GetCurrentThreadId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return suspended;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == selfId) continue;
        const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        if (SuspendThread(thread) == static_cast<DWORD>(-1))
            CloseHandle(thread);
        else
            suspended.push_back(thread);
    }
    CloseHandle(snapshot);
    return suspended;
}

void resumePeerThreads(std::vector<HANDLE>& threads) noexcept {
    for (const HANDLE thread : threads) {
        ResumeThread(thread);
        CloseHandle(thread);
    }
    threads.clear();
}

// A direct `jmp rel32` is five bytes, which is the whole Text_Draw prologue and
// fits inside the eight-byte Game_RunHandler one. It also leaves every register
// and flag untouched. The absolute `push imm32; ret` fallback is only used if a
// detour ever lands outside rel32 range.
bool writeJump(std::uint8_t* target, std::size_t displaced, std::uintptr_t destination) noexcept {
    const auto rel = static_cast<std::int64_t>(destination) -
                     static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target) + 5);
    if (rel >= std::numeric_limits<std::int32_t>::min() && rel <= std::numeric_limits<std::int32_t>::max()) {
        target[0] = 0xE9;
        const auto rel32 = static_cast<std::int32_t>(rel);
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

void logMessage(const char* text, const char* name = nullptr, std::uintptr_t value = 0) noexcept {
    char message[192]{};
    if (name)
        std::snprintf(message, sizeof(message), "%s hook: %s0x%08lX", name, text,
                      static_cast<unsigned long>(value));
    else
        std::snprintf(message, sizeof(message), "%s", text);
    igi::log(igi::LogLevel::Debug, message);
}

bool installTarget(const HookTarget& spec, igi::Scheduler& scheduler, void*& installedTrampoline) {
    using igi::LogLevel;

    logMessage("begin installation", spec.name);
    if (!igi::igi1::isSupportedExecutable()) {
        igi::log(LogLevel::Error, "IGI 1 executable SHA-256 mismatch; hook refused");
        return false;
    }
    logMessage("executable hash verified", spec.name);

    auto* target = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(spec.address));
    logMessage("target=", spec.name, spec.address);

    if (std::memcmp(target, spec.prologue, spec.displaced) != 0) {
        char message[192]{};
        std::snprintf(message, sizeof(message), "%s hook: signature mismatch; hook refused", spec.name);
        igi::log(LogLevel::Error, message);
        return false;
    }
    logMessage("target prologue verified", spec.name);

    auto* trampoline = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        igi::log(LogLevel::Error, "IGI 1 hook trampoline allocation failed");
        return false;
    }
    logMessage("trampoline=", spec.name, reinterpret_cast<std::uintptr_t>(trampoline));

    std::memcpy(trampoline, spec.prologue, spec.displaced);

    if (spec.relocatedCall != 0) {
        const auto* callSite = target + spec.relocatedCall;
        const auto destination = reinterpret_cast<std::uintptr_t>(callSite + 5) +
                                 static_cast<std::intptr_t>(*reinterpret_cast<const std::int32_t*>(callSite + 1));
        const auto rel = static_cast<std::int64_t>(destination) -
                         static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(trampoline) +
                                                   spec.relocatedCall + 5);
        if (*callSite != 0xE8 ||
            rel < std::numeric_limits<std::int32_t>::min() || rel > std::numeric_limits<std::int32_t>::max()) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            igi::log(LogLevel::Error, "IGI 1 hook: displaced call cannot be relocated");
            return false;
        }
        trampoline[spec.relocatedCall] = 0xE8;
        const auto rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(trampoline + spec.relocatedCall + 1, &rel32, sizeof(rel32));
        logMessage("original call=", spec.name, destination);
    }

    const auto continuation = reinterpret_cast<std::uintptr_t>(target + spec.displaced);
    const auto returnRel = static_cast<std::int64_t>(continuation) -
                           static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(trampoline) +
                                                     spec.displaced + 5);
    if (returnRel < std::numeric_limits<std::int32_t>::min() ||
        returnRel > std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        igi::log(LogLevel::Error, "IGI 1 hook: trampoline too far for a rel32 branch");
        return false;
    }
    trampoline[spec.displaced] = 0xE9;
    const auto returnRel32 = static_cast<std::int32_t>(returnRel);
    std::memcpy(trampoline + spec.displaced + 1, &returnRel32, sizeof(returnRel32));

    activeScheduler = &scheduler;
    igiGameRunTrampoline = trampoline;
    logMessage("detour=", spec.name, reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour));

    DWORD oldProtection = 0;
    if (!VirtualProtect(target, spec.displaced, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        igiGameRunTrampoline = nullptr;
        activeScheduler = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        igi::log(LogLevel::Error, "could not make the IGI 1 hook target writable");
        return false;
    }
    logMessage("target memory writable", spec.name);
    logMessage("suspending peer threads for the patch", spec.name);

    auto suspendedThreads = suspendPeerThreads();
    if (!writeJump(target, spec.displaced, reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour))) {
        resumePeerThreads(suspendedThreads);
        VirtualProtect(target, spec.displaced, oldProtection, &oldProtection);
        igiGameRunTrampoline = nullptr;
        activeScheduler = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        igi::log(LogLevel::Error, "IGI 1 hook: detour is out of rel32 range");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), target, spec.displaced);
    if (!VirtualProtect(target, spec.displaced, oldProtection, &oldProtection)) {
        igi::log(LogLevel::Warning, "IGI 1 hook protection could not be restored");
    }
    resumePeerThreads(suspendedThreads);

    installedTrampoline = trampoline;
    logMessage("patch installed and instruction cache flushed", spec.name);
    return true;
}
} // namespace
#endif

namespace igi {
bool HookManager::installTextDrawHook() {
#if defined(__APPLE__)
    return macos::installGameTickHook(scheduler_, trampoline_);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) {
        log(LogLevel::Warning, "IGI 1 hook is already installed");
        return false;
    }
    void* trampoline = nullptr;
    if (!installTarget(textDrawTarget, scheduler_, trampoline)) return false;
    trampoline_ = trampoline;
    targetSpec_ = &textDrawTarget;
    installed_ = true;
    log(LogLevel::Info, "IGI 1 Text_Draw scheduler hook installed");
    return true;
#else
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

bool HookManager::installPrimaryGameRunHandler() {
#if defined(__APPLE__)
    return macos::installGameTickHook(scheduler_, trampoline_);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) {
        log(LogLevel::Warning, "IGI 1 hook is already installed");
        return false;
    }
    void* trampoline = nullptr;
    if (!installTarget(gameRunHandlerTarget, scheduler_, trampoline)) return false;
    trampoline_ = trampoline;
    targetSpec_ = &gameRunHandlerTarget;
    installed_ = true;
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
        const auto* spec = static_cast<const HookTarget*>(targetSpec_);
        log(LogLevel::Debug, "IGI 1 hook: uninstall begin");
        auto* target = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(spec->address));
        DWORD oldProtection = 0;
        VirtualProtect(target, spec->displaced, PAGE_EXECUTE_READWRITE, &oldProtection);
        std::memcpy(target, spec->prologue, spec->displaced);
        VirtualProtect(target, spec->displaced, oldProtection, &oldProtection);
        FlushInstructionCache(GetCurrentProcess(), target, spec->displaced);
        VirtualFree(trampoline_, 0, MEM_RELEASE);
        trampoline_ = nullptr;
        targetSpec_ = nullptr;
        installed_ = false;
        activeScheduler = nullptr;
        igiGameRunTrampoline = nullptr;
        log(LogLevel::Info, "IGI 1 hook: uninstall complete");
    }
#endif
}
}
