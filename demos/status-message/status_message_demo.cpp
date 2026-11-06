#include <windows.h>

#include <cstdint>

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi_re/hashes.hpp"

namespace {
using StatusMessageShow = void (__cdecl*)(int, const char*, const char*, std::uint8_t*);
constexpr std::uintptr_t statusMessageShowAddress = igi::re::HASH::StatusMessageShow;
constexpr std::uintptr_t statusMessageContextAddress = 0x00A758AC;
constexpr std::uintptr_t statusMessageByteAddress = 0x00567C74;
constexpr char statusMessageSprite[] = "STATUSSCREEN:note.spr";

void showStatusMessage() noexcept {
    const auto show = reinterpret_cast<StatusMessageShow>(statusMessageShowAddress);
    const int context = *reinterpret_cast<const int*>(statusMessageContextAddress);
    auto* const statusByte = reinterpret_cast<std::uint8_t*>(statusMessageByteAddress);
    igi::log(igi::LogLevel::Info, "inject test: F5 received; calling reference StatusMessageShow ABI");
    show(context, "IGI SDK NATIVE INJECT TEST", statusMessageSprite, statusByte);
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
