#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/menu_manager.hpp"

#include <memory>

#if defined(_WIN32)
#define IGI_DEMO_EXPORT __declspec(dllexport)
#else
#define IGI_DEMO_EXPORT
#endif

namespace {
igi::Runtime runtime;
std::unique_ptr<igi::MenuManager> menu;
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    if (!runtime.initialize()) return false;

    menu = std::make_unique<igi::MenuManager>(runtime.scheduler());
    menu->createMenu("main", "IGI SDK Internal Demo");
    menu->addInfo("main", "Status", "Runtime initialized");
    menu->addAction("main", "Shutdown", [] { runtime.shutdown(); });
    return true;
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_Internal_Shutdown() {
    menu.reset();
    runtime.shutdown();
}
