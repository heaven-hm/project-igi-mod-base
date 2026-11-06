#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/igi1_menu_manager.hpp"
#include "igi_sdk/logging/logging.hpp"

#include <atomic>
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
std::atomic<std::uint64_t> frameCount{0};
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    igi::log(igi::LogLevel::Debug, "internal initialize: begin");
    menuConfig = igi::loadMenuConfig("config.ini");
    igi::log(igi::LogLevel::Debug, menuConfig.enabled ? "internal initialize: config enabled" : "internal initialize: config disabled");
#if defined(_WIN32)
    if (!runtime.initialize(menuConfig.hook)) {
        igi::log(igi::LogLevel::Error, "internal initialize: runtime initialization failed");
        return false;
    }
#else
    runtime.scheduler().bindGameThread();
#endif
    igi::log(igi::LogLevel::Debug, "internal initialize: creating IGI 1 menu manager");
    gameMenu = std::make_unique<igi::Igi1MenuManager>(igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame, menuConfig));
    const bool pushed = gameMenu->pushScreen(menuConfig.screenId);
    igi::log(pushed ? igi::LogLevel::Info : igi::LogLevel::Error,
             pushed ? "internal initialize: mod menu screen activated" : "internal initialize: mod menu screen activation failed");
    if (pushed) {
        const auto ticked = runtime.scheduler().runPendingFromGameHook();
        igi::log(igi::LogLevel::Debug,
                 std::string("internal initialize: scheduler ticked on init; pending=") +
                     std::to_string(ticked));
    }
    return pushed;
}

extern "C" IGI_DEMO_EXPORT std::size_t IGI_MenuManager_Internal_RunFrame() {
    const auto count = frameCount.fetch_add(1, std::memory_order_relaxed) + 1;
#if defined(_WIN32)
    const auto ran = runtime.scheduler().runPendingFromGameHook();
#else
    const auto ran = runtime.scheduler().runPending();
#endif
    if (count <= 5 || (count % 1000) == 0)
        igi::log(igi::LogLevel::Debug, "internal frame: scheduler completed");
    return ran;
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_Internal_Shutdown() {
    igi::log(igi::LogLevel::Debug, "internal shutdown: begin");
    gameMenu.reset();
    runtime.shutdown();
    igi::log(igi::LogLevel::Info, "internal shutdown: complete");
}
