#include "runtime/target_profile.hpp"

#include "runtime/igi_hooks.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace igi::game {
namespace {
#if defined(_WIN32)
std::string moduleFileName(HMODULE module) noexcept {
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(module, path, sizeof(path));
    if (length == 0 || length >= sizeof(path))
        return {};
    std::string lower(path, path + length);
    for (char &ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return lower;
}

std::string moduleBaseName(std::string_view path) noexcept {
    const auto separator = path.find_last_of("\\/");
    return std::string(separator == std::string_view::npos ? path : path.substr(separator + 1));
}

bool moduleForAddress(const void *address, HMODULE &module) noexcept {
    if (!address)
        return false;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCSTR>(address), &module) != 0 &&
           module != nullptr;
}

#endif
} // namespace

bool isHookJumpAt(unsigned long address) noexcept {
#if defined(_WIN32)
    const auto *bytes = reinterpret_cast<const unsigned char *>(address);
    if (!bytes)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(bytes, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        (info.Protect & PAGE_NOACCESS) != 0) {
        return false;
    }
    return bytes[0] == 0xE9;
#else
    (void)address;
    return false;
#endif
}

bool isPatchedEntry(unsigned long address) noexcept {
#if defined(_WIN32)
    const auto *bytes = reinterpret_cast<const unsigned char *>(address);
    if (!bytes)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(bytes, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        (info.Protect & PAGE_NOACCESS) != 0) {
        return false;
    }
    // Recognize every common x86 inline-hook stub. MinHook and Detours use E9;
    // other resolvers allocate the destination in private executable memory,
    // so classifying by module name missed them (the reported hook refusal).
    if (bytes[0] == 0xE9 || bytes[0] == 0xEB)
        return true;
    if (bytes[0] == 0x68 && bytes[5] == 0xC3) // push imm32 ; ret
        return true;
    if (bytes[0] == 0xB8 && bytes[5] == 0xFF && bytes[6] == 0xE0) // mov eax,imm32 ; jmp eax
        return true;
    if (bytes[0] == 0xFF && bytes[1] == 0x25) // jmp [imm32]
        return true;
    return false;
#else
    (void)address;
    return false;
#endif
}

bool hasIgiModHook() noexcept {
#if defined(_WIN32)
    // Per entry, not both: a resident ImGui mod can own only the registration
    // entry while the per-frame dispatcher is still retail code.
    return isPatchedEntry(displayAddFrameCallback) || isPatchedEntry(displayRunCallbacks);
#else
    return false;
#endif
}

bool isIgiExecutableModule() noexcept {
#if defined(_WIN32)
    HMODULE addModule = nullptr;
    HMODULE runModule = nullptr;
    if (moduleForAddress(reinterpret_cast<const void *>(displayAddFrameCallback), addModule) &&
        moduleForAddress(reinterpret_cast<const void *>(displayRunCallbacks), runModule) &&
        addModule == runModule) {
        const std::string name = moduleBaseName(moduleFileName(addModule));
        // The supported target is the retail single-player executable. Wine and
        // the GOG/retail installers all keep the same file name.
        if (name == "igi.exe")
            return true;
    }
    // IGI-RE also supports a manually mapped image, where GetModuleHandleExA
    // cannot name the module. Validate the PE headers at the fixed retail base
    // instead; both display addresses must fall inside that image.
    constexpr std::uint32_t kRetailImageSize = 0x00835000;
    MEMORY_BASIC_INFORMATION baseInfo{};
    if (VirtualQuery(reinterpret_cast<const void *>(imageBase), &baseInfo, sizeof(baseInfo)) !=
            sizeof(baseInfo) ||
        baseInfo.State != MEM_COMMIT || displayAddFrameCallback < imageBase ||
        displayAddFrameCallback >= imageBase + kRetailImageSize ||
        displayRunCallbacks < imageBase || displayRunCallbacks >= imageBase + kRetailImageSize) {
        return false;
    }
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(imageBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return false;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
        reinterpret_cast<const char *>(dos) + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == imageBase &&
           nt->OptionalHeader.SizeOfImage >= kRetailImageSize;
#else
    return false;
#endif
}

bool isSupportedExecutable() noexcept {
#if defined(_WIN32)
    if (!isIgiExecutableModule())
        return false;
    constexpr unsigned char addFrameCallbackPrologue[] = {0x8b, 0x44, 0x24, 0x04, 0x68, 0x00,
                                                          0xae, 0xbc, 0x00, 0x50, 0xe8};
    constexpr unsigned char runCallbacksPrologue[] = {0xe8, 0xbb, 0xff, 0xff, 0xff, 0xe8, 0x26,
                                                      0xfd, 0xff, 0xff, 0xe8, 0xe1, 0x5a, 0x00,
                                                      0x00, 0xa1, 0x00, 0xae, 0xbc, 0x00};
    MEMORY_BASIC_INFORMATION addInfo{};
    MEMORY_BASIC_INFORMATION runInfo{};
    if (VirtualQuery(reinterpret_cast<const void *>(displayAddFrameCallback), &addInfo,
                     sizeof(addInfo)) != sizeof(addInfo) ||
        VirtualQuery(reinterpret_cast<const void *>(displayRunCallbacks), &runInfo,
                     sizeof(runInfo)) != sizeof(runInfo) ||
        addInfo.State != MEM_COMMIT || runInfo.State != MEM_COMMIT) {
        return false;
    }
    const bool exact =
        std::memcmp(reinterpret_cast<const void *>(displayAddFrameCallback),
                    addFrameCallbackPrologue, sizeof(addFrameCallbackPrologue)) == 0 &&
        std::memcmp(reinterpret_cast<const void *>(displayRunCallbacks), runCallbacksPrologue,
                    sizeof(runCallbacksPrologue)) == 0;
    // A hooked entry is no longer the retail prologue; teardown and re-enable
    // must still accept the executable.
    return exact || hasIgiModHook();
#else
    return false;
#endif
}
} // namespace igi::game
