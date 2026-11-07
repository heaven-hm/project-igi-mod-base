#pragma once

#include <chrono>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace igi::hotkeys {

// 100 Hz (10 ms) input polling interval matching project-igi-natives.
inline constexpr auto kPollInterval = std::chrono::milliseconds(10);

inline void pollSleep() noexcept {
    std::this_thread::sleep_for(kPollInterval);
}

inline bool isKeyDown(int key) noexcept {
#if defined(_WIN32)
    return key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
#else
    (void)key;
    return false;
#endif
}

inline bool isKeyToggled(int key) noexcept {
#if defined(_WIN32)
    return key != 0 && (GetAsyncKeyState(key) & 0x1) != 0;
#else
    (void)key;
    return false;
#endif
}

inline bool areKeysDown(const std::vector<int>& keys) noexcept {
    if (keys.empty()) return false;
    for (int key : keys) {
        if (!isKeyDown(key)) return false;
    }
    return true;
}

// Rising-edge detector (UP -> DOWN transition), matching Utility::IsKeyPressed.
class KeyTracker final {
public:
    void seed(int key) {
        if (key != 0) states_[key] = isKeyDown(key);
    }

    bool isPressedOnce(int key) {
        if (key == 0) return false;
        const bool down = isKeyDown(key);
        bool& wasDown = states_[key];
        if (down && !wasDown) {
            wasDown = true;
            return true;
        }
        if (!down && wasDown) {
            wasDown = false;
        }
        return false;
    }

private:
    std::unordered_map<int, bool> states_;
};

}  // namespace igi::hotkeys
