#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/igi1_menu_manager.hpp"

#include <memory>

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

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    menuConfig = igi::loadMenuConfig("config.ini");
#if defined(_WIN32)
    if (!runtime.initialize()) return false;
#else
    runtime.scheduler().bindGameThread();
#endif
    gameMenu = std::make_unique<igi::Igi1MenuManager>(igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame, menuConfig));
    return gameMenu->pushScreen(menuConfig.screenId);
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
