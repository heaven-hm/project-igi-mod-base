#include <windows.h>

extern "C" bool IGI_MenuManager_Internal_Initialize();
extern "C" void IGI_MenuManager_Internal_Shutdown();

static DWORD WINAPI igiSdkStartup(void*) {
    IGI_MenuManager_Internal_Initialize();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, igiSdkStartup, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        IGI_MenuManager_Internal_Shutdown();
    }
    return TRUE;
}
