#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/igi1_menu_manager.hpp"

#include <cstdint>
#include <memory>

#if defined(__APPLE__)
#include <cstdio>
#include <dlfcn.h>
#endif

#if defined(_WIN32)
#define IGI_DEMO_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define IGI_DEMO_EXPORT __attribute__((visibility("default")))
#else
#define IGI_DEMO_EXPORT
#endif

namespace {
igi::Runtime runtime;
std::unique_ptr<igi::Igi1MenuManager> gameMenu;
igi::MenuConfig menuConfig;
}

#if defined(__APPLE__)
// IGI-RE documents MenuManager_PushScreen (0x00417690) as the script-level
// transition. The native macOS host exposes its live MenuManager item list;
// the bridge presses the appended QVM entry exactly as MenuManager_ReadInput
// does, so the retail QVM executes MenuManager_PushScreen(990).
struct IgiMenuWidget {
    std::uint32_t widget;
    int index;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
};

extern "C" IGI_DEMO_EXPORT void IGI_SDK_GameFrame() {
    using KeyFn = void (*)(std::uint8_t, int);
    using WidgetListFn = int (*)(IgiMenuWidget*, int, int*);
    using ActivateFn = void (*)(std::uint32_t);
    using KeyState = std::uint8_t*;

    static bool keyWasDown = false;
    static int phase = 0;
    static bool configLoaded = false;
    if (!configLoaded) {
        menuConfig = igi::loadMenuConfig("config.ini");
        configLoaded = true;
    }
    const auto key = reinterpret_cast<KeyFn>(dlsym(RTLD_DEFAULT, "platform_key"));
    const auto widgets = reinterpret_cast<WidgetListFn>(dlsym(RTLD_DEFAULT, "igi_menu_widgets"));
    const auto activate = reinterpret_cast<ActivateFn>(dlsym(RTLD_DEFAULT, "igi_menu_activate_widget"));
    const auto keys = reinterpret_cast<KeyState>(dlsym(RTLD_DEFAULT, "g_key_state"));
    if (!key || !widgets || !activate || !keys) return;

    if (!menuConfig.enabled) return;
    const bool pressed = (keys[menuConfig.hotkey] & 0x80u) != 0;
    if (pressed && !keyWasDown && phase == 0) {
        std::fprintf(stderr, "[IGI SDK] configured key received; opening screen %d\n", menuConfig.screenId);
        phase = 1;
    }
    keyWasDown = pressed;

    if (phase == 1) {
        key(0x01, 1); // Escape: IGI opens the in-game MenuManager.
        phase = 2;
        return;
    }
    if (phase == 2) {
        key(0x01, 0);
        phase = 3;
        return;
    }
    if (phase == 3) {
        IgiMenuWidget list[64]{};
        const int count = widgets(list, 64, nullptr);
        if (count <= 0) return;
        // The QVM installer places "IGI SDK Mod Menu" last in pause-menu order.
        activate(list[count - 1].widget);
        std::fputs("[IGI SDK] activated IGI SDK Mod Menu entry\n", stderr);
        phase = 0;
    }
}
#endif

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    menuConfig = igi::loadMenuConfig("config.ini");
#if defined(_WIN32)
    if (!runtime.initialize()) return false;
#else
    runtime.scheduler().bindGameThread();
#endif

    gameMenu = std::make_unique<igi::Igi1MenuManager>(igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame));
    gameMenu->pushScreen(menuConfig.screenId);
    return true;
}

extern "C" IGI_DEMO_EXPORT std::size_t IGI_MenuManager_Internal_RunFrame() {
#if defined(_WIN32)
    return runtime.scheduler().runPendingFromGameHook();
#else
    return runtime.scheduler().runPending();
#endif
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_Internal_Shutdown() {
    gameMenu.reset();
    runtime.shutdown();
}
