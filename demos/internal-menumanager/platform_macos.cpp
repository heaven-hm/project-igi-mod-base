#include "igi_sdk/config/menu_config.hpp"

#include <cstdint>
#include <dlfcn.h>

#define IGI_DEMO_EXPORT __attribute__((visibility("default")))

namespace {
struct IgiMenuWidget { std::uint32_t widget; int index; std::int32_t x, y, width, height; };
}

extern "C" IGI_DEMO_EXPORT void IGI_SDK_GameFrame() {
    using KeyFn = void (*)(std::uint8_t, int);
    using WidgetListFn = int (*)(IgiMenuWidget*, int, int*);
    using ActivateFn = void (*)(std::uint32_t);
    using KeyState = std::uint8_t*;
    static const igi::MenuConfig config = igi::loadMenuConfig("config.ini");
    static bool keyWasDown = false;
    static int phase = 0;
    if (!config.enabled) return;
    const auto key = reinterpret_cast<KeyFn>(dlsym(RTLD_DEFAULT, "platform_key"));
    const auto widgets = reinterpret_cast<WidgetListFn>(dlsym(RTLD_DEFAULT, "igi_menu_widgets"));
    const auto activate = reinterpret_cast<ActivateFn>(dlsym(RTLD_DEFAULT, "igi_menu_activate_widget"));
    const auto keys = reinterpret_cast<KeyState>(dlsym(RTLD_DEFAULT, "g_key_state"));
    if (!key || !widgets || !activate || !keys) return;
    const bool pressed = (keys[config.hotkey] & 0x80u) != 0;
    if (pressed && !keyWasDown && phase == 0) phase = 1;
    keyWasDown = pressed;
    if (phase == 1) { key(0x01, 1); phase = 2; }
    else if (phase == 2) { key(0x01, 0); phase = 3; }
    else if (phase == 3) {
        IgiMenuWidget list[64]{};
        const int count = widgets(list, 64, nullptr);
        if (count > 0) { activate(list[count - 1].widget); phase = 0; }
    }
}
