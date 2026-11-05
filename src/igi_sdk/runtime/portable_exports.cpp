#include "igi_sdk/runtime/runtime.hpp"

#if !defined(_WIN32)
#if defined(__GNUC__) || defined(__clang__)
#define IGI_SDK_EXPORT __attribute__((visibility("default")))
#else
#define IGI_SDK_EXPORT
#endif

namespace {
igi::Runtime runtime;
}

extern "C" IGI_SDK_EXPORT bool IGI_SDK_Initialize() {
    return runtime.initialize();
}

extern "C" IGI_SDK_EXPORT void IGI_SDK_Shutdown() {
    runtime.shutdown();
}

extern "C" IGI_SDK_EXPORT unsigned long long IGI_SDK_GetGameHookInvocationCount() {
    return runtime.gameHookInvocationCount();
}
#endif
