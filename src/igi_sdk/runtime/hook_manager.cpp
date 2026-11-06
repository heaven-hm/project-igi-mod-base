#include "igi_sdk/runtime/hook_manager.hpp"

#include "igi_sdk/runtime/igi1_hooks.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/target_profile.hpp"
#include "macos_hook_backend.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>

namespace {
igi::Scheduler* activeScheduler = nullptr;
// Holds the trampoline of whichever primary hook is installed.
extern "C" void* igiGameRunTrampoline = nullptr;
// Trampoline for the Display_AddFrameCallback detour. After the detour registers
// IGI1SDK_Tick, the hook is never entered again, but we still need a trampoline
// to reach the original function during the registration call.
extern "C" void*        igiDisplayAddFrameCallbackTrampoline = nullptr;

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

// Display_AddFrameCallback is a thin wrapper that calls Display_AddCallback(fn,
// &gDisplayFrameCallbacks). Its retail prologue is `mov 0x4(%esp),%eax; push
// 0xBCAE00; push %eax` (10 bytes, no branch), followed by `call Display_AddCallback`
// at +10. We only patch the first 10 bytes, so the displaced call site is never
// overwritten and the trampoline needs no relocated call.
constexpr std::uint8_t displayAddFrameCallbackPrologue[] = {0x8B, 0x44, 0x24, 0x04, 0x68, 0x00, 0xAE, 0xBC, 0x00, 0x50};

// Display_EndFrame opens with `call Scene_DrawAll` (0x497700) as its first
// instruction, so the displaced region is that call and its rel32 must be
// relocated into the trampoline.
constexpr std::uint8_t displayEndFramePrologue[] = {0xE8, 0xDB, 0x59, 0x00, 0x00};

// Screen_Draw opens with `mov 0xA5EA65,%al` (five bytes with an absolute
// operand, which stays valid at the fixed image base) and then `push esi`.
// The displaced region contains no branch, so nothing needs relocating.
constexpr std::uint8_t screenDrawPrologue[] = {0xA0, 0x65, 0xEA, 0xA5, 0x00};

// Offset 0 is a valid relocation site, so "no relocated call" needs its own
// sentinel instead of 0.
constexpr std::size_t noRelocatedCall = static_cast<std::size_t>(-1);

constexpr HookTarget gameRunHandlerTarget{
    "Game_RunHandler", igi::igi1::gameRunHandler, gameRunHandlerPrologue,
    sizeof(gameRunHandlerPrologue), 3};
constexpr HookTarget textDrawTarget{
    "Text_Draw", igi::igi1::textDraw, textDrawPrologue, sizeof(textDrawPrologue), noRelocatedCall};
constexpr HookTarget screenDrawTarget{
    "Screen_Draw", igi::igi1::screenDraw, screenDrawPrologue, sizeof(screenDrawPrologue), noRelocatedCall};
constexpr HookTarget displayEndFrameTarget{
    "Display_EndFrame", igi::igi1::displayEndFrame, displayEndFramePrologue,
    sizeof(displayEndFramePrologue), 0};
constexpr HookTarget displayAddFrameCallbackTarget{
    "Display_AddFrameCallback", igi::igi1::displayAddFrameCallback,
    displayAddFrameCallbackPrologue, sizeof(displayAddFrameCallbackPrologue), noRelocatedCall};

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

// The patch is written in one pass. Suspending the peer threads first was tried
// and deadlocked: a suspended thread can hold the CRT heap lock that the suspend
// bookkeeping itself needs. The jump is written last, so a thread that is inside
// the prologue either finishes the original instruction or takes the detour.

// Display_AddFrameCallback trampoline call: invokes the real function so we can
// register IGI1SDK_Tick through the game's callback list without keeping the
// detour installed.
extern "C" void __cdecl Display_AddFrameCallback_trampoline_(void (*fn)(void)) noexcept;
#if defined(_MSC_VER)
extern "C" void __cdecl Display_AddFrameCallback_trampoline_(void (*fn)(void)) noexcept {
    __asm {
        push fn
        call igiDisplayAddFrameCallbackTrampoline
    }
}
#else
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl Display_AddFrameCallback_trampoline_(void (*fn)(void)) noexcept {
    __asm__ volatile(
        "pushl %0\n\t"
        "call *%1\n\t"
        :
        : "r"(fn), "r"(igiDisplayAddFrameCallbackTrampoline)
        : "memory");
}
#endif

// IGI1SDK_Tick is the per-frame SDK work function. It is registered through
// Display_AddFrameCallback once at hook install time, then runs on the game
// thread each displayed frame via Display_RunCallbacks(gDisplayFrameCallbacks).
extern "C" void __cdecl IGI1SDK_Tick() noexcept {
    if (activeScheduler) {
        activeScheduler->runPendingFromGameHook();
    }
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

void logStep(const char* name, const char* text) noexcept {
    char message[192]{};
    std::snprintf(message, sizeof(message), "%s hook: %s", name, text);
    igi::log(igi::LogLevel::Debug, message);
}

void logStepValue(const char* name, const char* text, std::uintptr_t value) noexcept {
    char message[192]{};
    std::snprintf(message, sizeof(message), "%s hook: %s0x%08lX", name, text,
                  static_cast<unsigned long>(value));
    igi::log(igi::LogLevel::Debug, message);
}

bool installTarget(const HookTarget& spec, igi::Scheduler& scheduler, void*& installedTrampoline) {
    using igi::LogLevel;

    logStep(spec.name, "begin installation");
    if (!igi::igi1::isSupportedExecutable()) {
        igi::log(LogLevel::Error, "IGI 1 executable SHA-256 mismatch; hook refused");
        return false;
    }
    logStep(spec.name, "executable hash verified");

    auto* target = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(spec.address));
    logStepValue(spec.name, "target=", spec.address);

    if (std::memcmp(target, spec.prologue, spec.displaced) != 0) {
        char message[192]{};
        std::snprintf(message, sizeof(message), "%s hook: signature mismatch; hook refused", spec.name);
        igi::log(LogLevel::Error, message);
        return false;
    }
    logStep(spec.name, "target prologue verified");

    auto* trampoline = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        igi::log(LogLevel::Error, "IGI 1 hook trampoline allocation failed");
        return false;
    }
    logStepValue(spec.name, "trampoline=", reinterpret_cast<std::uintptr_t>(trampoline));

    std::memcpy(trampoline, spec.prologue, spec.displaced);

    if (spec.relocatedCall != noRelocatedCall) {
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
        logStepValue(spec.name, "original call=", destination);
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
    igiDisplayAddFrameCallbackTrampoline = trampoline;
    logStepValue(spec.name, "detour=", reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour));

    DWORD oldProtection = 0;
    if (!VirtualProtect(target, spec.displaced, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        igiDisplayAddFrameCallbackTrampoline = nullptr;
        activeScheduler = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        igi::log(LogLevel::Error, "could not make the IGI 1 hook target writable");
        return false;
    }
    logStep(spec.name, "target memory writable");
    logStep(spec.name, "writing the patch");

    if (!writeJump(target, spec.displaced, reinterpret_cast<std::uintptr_t>(&gameRunHandlerDetour))) {
        VirtualProtect(target, spec.displaced, oldProtection, &oldProtection);
        igiDisplayAddFrameCallbackTrampoline = nullptr;
        activeScheduler = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        igi::log(LogLevel::Error, "IGI 1 hook: detour is out of rel32 range");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), target, spec.displaced);
    if (!VirtualProtect(target, spec.displaced, oldProtection, &oldProtection)) {
        igi::log(LogLevel::Warning, "IGI 1 hook protection could not be restored");
    }

    installedTrampoline = trampoline;
    logStep(spec.name, "patch installed and instruction cache flushed");
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
    if (!installTarget(displayAddFrameCallbackTarget, scheduler_, trampoline)) return false;
    trampoline_ = trampoline;

    Display_AddFrameCallback_trampoline_(IGI1SDK_Tick);
    installed_ = true;
    log(LogLevel::Info, "IGI 1 display frame callback scheduler installed");
    return true;
#else
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

bool HookManager::installTickHook(igi1::TickHook hook) {
#if defined(__APPLE__)
    (void)hook;
    return macos::installGameTickHook(scheduler_, trampoline_);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (installed_) {
        log(LogLevel::Warning, "IGI 1 hook is already installed");
        return false;
    }
    const HookTarget& spec = hook == igi1::TickHook::GameRunHandler         ? gameRunHandlerTarget
                           : hook == igi1::TickHook::TextDraw               ? textDrawTarget
                           : hook == igi1::TickHook::DisplayEndFrame        ? displayEndFrameTarget
                           : hook == igi1::TickHook::ScreenDraw             ? screenDrawTarget
                                                                        : displayAddFrameCallbackTarget;
    (void)spec;
    void* trampoline = nullptr;
    if (!installTarget(spec, scheduler_, trampoline)) return false;
    trampoline_ = trampoline;
    targetSpec_ = &spec;
    installed_ = true;
    log(LogLevel::Info, std::string("IGI 1 ") + spec.name + " scheduler hook installed");
    return true;
#else
    (void)hook;
    log(LogLevel::Warning, "IGI 1 hook installation requires a Windows x86 build");
    return false;
#endif
}

bool HookManager::installDisplayEndFrameHook() {
    return installTickHook(igi1::TickHook::DisplayEndFrame);
}

bool HookManager::installTextDrawHook() {
    return installTickHook(igi1::TickHook::TextDraw);
}

bool HookManager::installPrimaryGameRunHandler() {
    return installTickHook(igi1::TickHook::GameRunHandler);
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
