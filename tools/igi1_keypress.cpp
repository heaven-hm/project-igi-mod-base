// IGI1-Keypress: send a small, named key sequence to the Wine/Windows desktop.
// It is deliberately a separate x86 executable so live tests do not need
// macOS Accessibility permission to drive the injected menu's hotkeys.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
UINT keyCode(const char* name) {
    if (std::strcmp(name, "escape") == 0 || std::strcmp(name, "esc") == 0) return VK_ESCAPE;
    if (std::strcmp(name, "f5") == 0) return VK_F5;
    if (std::strcmp(name, "f12") == 0) return VK_F12;
    if (std::strcmp(name, "end") == 0) return VK_END;
    return 0;
}

struct WindowSearch {
    DWORD processId{};
    HWND window{};
};

BOOL CALLBACK findTopLevelWindow(HWND candidate, LPARAM raw) {
    auto& search = *reinterpret_cast<WindowSearch*>(raw);
    DWORD processId = 0;
    GetWindowThreadProcessId(candidate, &processId);
    if (processId == search.processId && IsWindowVisible(candidate) && GetWindow(candidate, GW_OWNER) == nullptr) {
        search.window = candidate;
        return FALSE;
    }
    return TRUE;
}

bool focusProcessWindow(DWORD processId) {
    WindowSearch search{.processId = processId};
    EnumWindows(findTopLevelWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.window) {
        std::fprintf(stderr, "no visible top-level window for PID %lu\n", processId);
        return false;
    }
    ShowWindow(search.window, SW_RESTORE);
    BringWindowToTop(search.window);
    SetForegroundWindow(search.window);
    SetFocus(search.window);
    return true;
}

bool sendKey(UINT key) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(key);
    if (SendInput(1, &input, sizeof(INPUT)) != 1) return false;
    Sleep(200);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}
} // namespace

int main(int argc, char** argv) {
    int firstKey = 1;
    if (argc >= 4 && std::strcmp(argv[1], "--pid") == 0) {
        const auto processId = static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10));
        if (processId == 0 || !focusProcessWindow(processId)) return 1;
        firstKey = 3;
    }
    if (argc <= firstKey) {
        std::fprintf(stderr, "usage: %s [--pid windows-pid] <escape|f5|f12|end> [... ]\n", argv[0]);
        return 2;
    }
    for (int i = firstKey; i < argc; ++i) {
        const UINT key = keyCode(argv[i]);
        if (!key) {
            std::fprintf(stderr, "unknown key: %s\n", argv[i]);
            return 2;
        }
        if (!sendKey(key)) {
            std::fprintf(stderr, "SendInput failed for %s (error %lu)\n", argv[i], GetLastError());
            return 1;
        }
        std::printf("sent %s\n", argv[i]);
        Sleep(300);
    }
}
