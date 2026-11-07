#include <windows.h>

#include <cstdint>

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/mvp_natives.hpp"

namespace {
void showStatusMessage() noexcept {
    igi::log(igi::LogLevel::Info, "inject test: F5 received; calling IGI::MISC::STATUS_MESSAGE_SHOW");
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI SDK NATIVE INJECT TEST", "STATUSSCREEN:note.spr");
    igi::log(igi::LogLevel::Info, "inject test: StatusMessageShow returned normally");
}

DWORD WINAPI startup(void*) {
    igi::log(igi::LogLevel::Info, "inject test DLL attached; direct native mode, no scheduler hook");
    igi::log(igi::LogLevel::Info, "inject test ready; press F5 to call StatusMessageShow");
    bool f5WasDown = false;
    for (;;) {
        const bool f5Down = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (f5Down && !f5WasDown) showStatusMessage();
        f5WasDown = f5Down;
        Sleep(30);
    }
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, startup, nullptr, 0, nullptr);
    }
    return TRUE;
}
