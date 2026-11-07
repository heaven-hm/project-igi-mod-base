#include "runtime/target_profile.hpp"

#include "runtime/igi_hooks.hpp"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace igi::game {
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
