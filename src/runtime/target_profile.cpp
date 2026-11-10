#include "runtime/target_profile.hpp"

#include "runtime/igi_hooks.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace igi::game {
namespace {
#if defined(_WIN32)
// True when addr is a relative jump into a loaded module whose file name looks
// like this project's injected menu DLLs (igi-*-mod-*.dll).
bool isJumpIntoIgiMod(const void *addr) noexcept {
    const auto *bytes = static_cast<const unsigned char *>(addr);
    if (!bytes || bytes[0] != 0xE9)
        return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, bytes + 1, sizeof(relative));
    const auto *destination = bytes + 5 + relative;
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(destination), &module) ||
        !module) {
        return false;
    }
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(module, path, sizeof(path));
    if (length == 0 || length >= sizeof(path))
        return false;
    std::string lower(path, path + length);
    for (char &ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return lower.find("igi-") != std::string::npos || lower.find("igi_") != std::string::npos ||
           lower.find("heaven") != std::string::npos ||
           lower.find("project-igi") != std::string::npos;
}
#endif
} // namespace

bool hasIgiModHook() noexcept {
#if defined(_WIN32)
    return isJumpIntoIgiMod(reinterpret_cast<const void *>(displayAddFrameCallback)) &&
           isJumpIntoIgiMod(reinterpret_cast<const void *>(displayRunCallbacks));
#else
    return false;
#endif
}

bool isSupportedExecutable() noexcept {
#if defined(_WIN32)
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
    return std::memcmp(reinterpret_cast<const void *>(displayAddFrameCallback),
                       addFrameCallbackPrologue, sizeof(addFrameCallbackPrologue)) == 0 &&
           std::memcmp(reinterpret_cast<const void *>(displayRunCallbacks), runCallbacksPrologue,
                       sizeof(runCallbacksPrologue)) == 0;
#else
    return false;
#endif
}
} // namespace igi::game
