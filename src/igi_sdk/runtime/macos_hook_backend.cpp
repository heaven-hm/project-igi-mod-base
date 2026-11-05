#include "macos_hook_backend.hpp"

#if defined(__APPLE__)
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/scheduler.hpp"

#include <dlfcn.h>
#include <mach/mach.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cstring>

namespace {
constexpr std::array<std::uint8_t, 16> kTickPrologue = {
    0xe9, 0x23, 0x3b, 0xa9, 0xfc, 0x6f, 0x01, 0xa9,
    0xfa, 0x67, 0x02, 0xa9, 0xf8, 0x5f, 0x03, 0xa9};
constexpr std::size_t kPatchSize = 16;
igi::Scheduler* activeScheduler = nullptr;
void* originalTick = nullptr;
std::array<std::uint8_t, kPatchSize> savedBytes{};

void writeBranch(void* address, const void* destination) {
    const std::array<std::uint32_t, 2> instructions = {0x58000050u, 0xD61F0200u}; // ldr x16, #8; br x16
    std::memcpy(address, instructions.data(), sizeof(instructions));
    std::memcpy(static_cast<std::uint8_t*>(address) + sizeof(instructions), &destination, sizeof(destination));
}

extern "C" void macGameTickDetour(void* self, const void* input) noexcept {
    using Tick = void (*)(void*, const void*);
    reinterpret_cast<Tick>(originalTick)(self, input);
    activeScheduler->runPendingFromGameHook();
}

bool protect(void* address, vm_prot_t protection) noexcept {
    const auto page = static_cast<vm_address_t>(reinterpret_cast<std::uintptr_t>(address) & ~(static_cast<std::uintptr_t>(getpagesize()) - 1));
    return vm_protect(mach_task_self(), page, static_cast<vm_size_t>(getpagesize()), false, protection) == KERN_SUCCESS;
}
}

namespace igi::macos {
bool installGameTickHook(Scheduler& scheduler, void*& trampoline) noexcept {
    if (trampoline) return false;
    auto* target = dlsym(RTLD_DEFAULT, "_ZN3igi8gameplay14GameSimulation4tickERKNS0_11PlayerInputE");
    if (!target || std::memcmp(target, kTickPrologue.data(), kPatchSize) != 0) {
        log(LogLevel::Error, "macOS igi.app tick target/signature mismatch; hook refused");
        return false;
    }
    void* bridge = mmap(nullptr, getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (bridge == MAP_FAILED) return false;
    std::memcpy(bridge, target, kPatchSize);
    writeBranch(static_cast<std::uint8_t*>(bridge) + kPatchSize, static_cast<std::uint8_t*>(target) + kPatchSize);
    savedBytes = kTickPrologue;
    activeScheduler = &scheduler;
    originalTick = bridge;
    if (!protect(target, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)) {
        munmap(bridge, getpagesize()); originalTick = nullptr; activeScheduler = nullptr; return false;
    }
    writeBranch(target, reinterpret_cast<void*>(&macGameTickDetour));
    __builtin___clear_cache(static_cast<char*>(target), static_cast<char*>(target) + kPatchSize);
    protect(target, VM_PROT_READ | VM_PROT_EXECUTE);
    trampoline = bridge;
    log(LogLevel::Info, "macOS igi.app GameSimulation::tick scheduler hook installed");
    return true;
}

void uninstallGameTickHook(void*& trampoline) noexcept {
    if (!trampoline) return;
    auto* target = dlsym(RTLD_DEFAULT, "_ZN3igi8gameplay14GameSimulation4tickERKNS0_11PlayerInputE");
    if (target && protect(target, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)) {
        std::memcpy(target, savedBytes.data(), kPatchSize);
        __builtin___clear_cache(static_cast<char*>(target), static_cast<char*>(target) + kPatchSize);
        protect(target, VM_PROT_READ | VM_PROT_EXECUTE);
    }
    munmap(trampoline, getpagesize()); trampoline = nullptr; originalTick = nullptr; activeScheduler = nullptr;
}
}
#else
namespace igi::macos {
bool installGameTickHook(Scheduler&, void*&) noexcept { return false; }
void uninstallGameTickHook(void*&) noexcept {}
}
#endif
