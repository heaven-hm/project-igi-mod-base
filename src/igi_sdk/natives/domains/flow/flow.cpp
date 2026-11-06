#include "igi_sdk/natives/domains/flow/flow.hpp"

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi1_native_registry.hpp"

#if defined(_MSC_VER)
#define IGI_NATIVE_CDECL __cdecl
#elif defined(__GNUC__) && defined(__i386__)
#define IGI_NATIVE_CDECL __attribute__((cdecl))
#else
#define IGI_NATIVE_CDECL
#endif

namespace igi::natives::flow {
TaskId FRAMES_SET_QUEUED(NativeCaller& caller, int frames) {
    std::uintptr_t address = 0;
    if (caller.resolve("FramesSet", "void FramesSet(int fps)", address) != NativeCallStatus::Success)
        return 0;

    using FramesSet = void(IGI_NATIVE_CDECL *)(int);
    const auto function = reinterpret_cast<FramesSet>(address);
    return caller.queue("FramesSet", [function, frames] {
        function(frames);
    });
}

void FRAMES_SET(int frames) {
    NativeCaller* caller = igi1NativeCaller();
    if (!caller) {
        log(LogLevel::Warning, "FramesSet refused: IGI 1 native runtime is not initialized");
        return;
    }
    (void)FRAMES_SET_QUEUED(*caller, frames);
}
} // namespace igi::natives::flow

#undef IGI_NATIVE_CDECL
