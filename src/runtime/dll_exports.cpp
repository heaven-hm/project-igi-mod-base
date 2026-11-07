#include "runtime/runtime.hpp"

#if defined(_WIN32)
#include <windows.h>

namespace {
igi::Runtime runtime;
}

extern "C" __declspec(dllexport) BOOL WINAPI IGI_SDK_Initialize() {
    return runtime.initialize() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void WINAPI IGI_SDK_Shutdown() {
    runtime.shutdown();
}

extern "C" __declspec(dllexport) unsigned long long WINAPI IGI_SDK_GetGameHookInvocationCount() {
    return static_cast<unsigned long long>(runtime.gameHookInvocationCount());
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    // Hook teardown must happen through IGI_SDK_Shutdown, outside the loader
    // lock, before the host calls FreeLibrary or exits.
    return TRUE;
}
#endif
