#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/IGI.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/igi1_menu_manager.hpp"
#include "igi_sdk/ui/menu_script.hpp"
#include "igi_sdk/ui/menu_script_bridge.hpp"
#include "igi_sdk/ui/menu_resource.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi1_native_registry.hpp"
#include "igi_sdk/natives/igi_re/hashes.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"
#include "igi_sdk/ui/demo_menu.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(_WIN32)
#define IGI_DEMO_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define IGI_DEMO_EXPORT __attribute__((visibility("default")))
#else
#define IGI_DEMO_EXPORT
#endif

namespace {
enum class IgiMenuScreenState : std::int32_t {
    MainMenu = 3,
    InGame = 4,
    Restart = 5,
};

// Consolidated mod-menu state container (BigBase v2 style).
struct InternalMenuState {
    igi::Runtime runtime;
    std::unique_ptr<igi::Igi1MenuManager> gameMenu;
    igi::MenuConfig menuConfig;
    // Keep the compiled resource available for the game-thread loader. It must not
    // disappear when the initialization function returns.
    std::vector<std::uint8_t> compiledMenu;
    std::vector<std::uint8_t> reopenMenu;
    std::vector<std::uint8_t> closeMenu;
    std::string menuSource;
    std::unique_ptr<igi::MenuScriptBridge> menuBridge;
    std::unique_ptr<igi::MenuScriptCallbacks> menuCallbacks;
    igi::MenuScriptRegistration menuRegistration;
    igi::MenuResource menuResource;
    igi::MenuResource reopenResource;
    igi::MenuResource closeResource;
    std::atomic<std::uint64_t> frameCount{0};
    std::atomic<std::int32_t> observedScreen{0};
    bool menuScreenAttached = false;
};

InternalMenuState g_menu;
auto& runtime = g_menu.runtime;
auto& gameMenu = g_menu.gameMenu;
auto& menuConfig = g_menu.menuConfig;
auto& compiledMenu = g_menu.compiledMenu;
auto& reopenMenu = g_menu.reopenMenu;
auto& closeMenu = g_menu.closeMenu;
auto& menuSource = g_menu.menuSource;
auto& menuBridge = g_menu.menuBridge;
auto& menuCallbacks = g_menu.menuCallbacks;
auto& menuRegistration = g_menu.menuRegistration;
auto& menuResource = g_menu.menuResource;
auto& reopenResource = g_menu.reopenResource;
auto& closeResource = g_menu.closeResource;
auto& frameCount = g_menu.frameCount;
auto& menuScreenAttached = g_menu.menuScreenAttached;

bool readMenuScreenState(std::int32_t& screen) noexcept {
#if defined(_WIN32) && !defined(_WIN64)
    constexpr std::uintptr_t gameStatePointerAddress = 0x00567C8C;
    constexpr std::uintptr_t menuScreenOffset = 0x28;
    const auto gameState = *reinterpret_cast<const std::uintptr_t*>(gameStatePointerAddress);
    if (gameState == 0) return false;
    screen = *reinterpret_cast<const std::int32_t*>(gameState + menuScreenOffset);
    return true;
#else
    (void)screen;
    return false;
#endif
}

struct GameRootLayout {
    std::uint8_t prefix[0x44];
    void* root;
};

#if defined(_WIN32) && !defined(_WIN64)
static_assert(offsetof(GameRootLayout, root) == 0x44);
#endif

bool installMenuScriptCallbacks() {
    if (menuRegistration.active()) return true;
    auto* caller = igi::igi1NativeCaller();
    if (!caller) {
        igi::log(igi::LogLevel::Warning, "internal menu callbacks: native caller unavailable");
        return false;
    }
    const auto script = igi::igi1MenuScriptApi(*caller);
    const auto registry = igi::igi1ScriptRegistryApi(*caller);
    if (!script.getInt || !script.addItem || !registry.registerFunction) {
        igi::log(igi::LogLevel::Warning, "internal menu callbacks: IGI script APIs unavailable");
        return false;
    }
    if (!menuResource.active()) {
        const auto resources = igi::igi1MenuResourceApi(*caller);
        if (!resources.isLoaded || !resources.registerResource || !resources.freeResource ||
            !menuResource.registerData("LOCAL:igi_sdk/modmenu.qvm", compiledMenu, resources)) {
            igi::log(igi::LogLevel::Error, "internal menu resource: QVM registration failed");
            return false;
        }
        igi::log(igi::LogLevel::Info, "internal menu resource: LOCAL:igi_sdk/modmenu.qvm registered");
    }
    const auto resources = igi::igi1MenuResourceApi(*caller);
    if (!reopenResource.active() && !reopenResource.registerData("LOCAL:igi_sdk/modmenu-reopen.qvm", reopenMenu, resources)) return false;
    if (!closeResource.active() && !closeResource.registerData("LOCAL:igi_sdk/modmenu-close.qvm", closeMenu, resources)) return false;
    menuBridge = std::make_unique<igi::MenuScriptBridge>(
        menuConfig,
        [](std::size_t item, std::string_view value) {
            runDemoMenuAction(menuConfig, runtime, item, value);
        },
        [] {
            if (!igi::resumeFromMenu(runtime)) {
                igi::log(igi::LogLevel::Error, "internal menu close rejected: unable to resume game");
                return;
            }
            menuScreenAttached = false;
            igi::log(igi::LogLevel::Info, "internal menu close: game resumed");
        });
    menuCallbacks = std::make_unique<igi::MenuScriptCallbacks>(*menuBridge, script);
    if (!menuRegistration.start(*menuCallbacks, registry)) {
        menuCallbacks.reset();
        menuBridge.reset();
        igi::log(igi::LogLevel::Error, "internal menu callbacks: registration failed");
        return false;
    }
    igi::log(igi::LogLevel::Info, "internal menu callbacks: registered on game thread");
    return true;
}

bool runMenuCommand(const char* resource, const char* operation) {
    auto* game = IGI::GAME::GET();
    auto* root = game ? static_cast<IGI::Task*>(static_cast<GameRootLayout*>(static_cast<void*>(game))->root) : nullptr;
    if (!root) return false;
    auto* manager = IGI::TASK::FIND_CHILD(root, IGI::MENU::GET_TYPE());
    if (!manager) return false;
    const bool ok = IGI::TASK::RUN_SCRIPT(manager, resource, 0) != nullptr;
    igi::log(ok ? igi::LogLevel::Info : igi::LogLevel::Error,
             std::string("internal menu command: ") + operation + (ok ? " requested" : " failed"));
    return ok;
}

bool writeMenuResourceFile() {
    if (compiledMenu.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories("igi_sdk", error);
    if (error) return false;
    std::ofstream output("igi_sdk/modmenu.qvm", std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(compiledMenu.data()),
                 static_cast<std::streamsize>(compiledMenu.size()));
    return output.good();
}

bool rebuildMenuResources() {
    std::vector<std::uint8_t> screen;
    std::vector<std::uint8_t> reopen;
    std::vector<std::uint8_t> close;
    std::string error;
    if (!igi::compileIgi1MenuScreenScript(menuConfig, screen, error) ||
        !igi::compileIgi1MenuCommandScript("MenuManager_PushScreen(" + std::to_string(menuConfig.screenId) + ");", reopen, error) ||
        !igi::compileIgi1MenuCommandScript("MenuManager_PopScreen(FALSE);", close, error)) {
        igi::log(igi::LogLevel::Error, std::string("internal menu resource compilation failed: ") + error);
        return false;
    }
    std::ostringstream source;
    if (!igi::writeIgi1MenuScreenScript(source, menuConfig)) {
        igi::log(igi::LogLevel::Error, "internal menu source generation failed");
        return false;
    }
    compiledMenu = std::move(screen);
    reopenMenu = std::move(reopen);
    closeMenu = std::move(close);
    menuSource = source.str();
    if (!writeMenuResourceFile()) {
        igi::log(igi::LogLevel::Warning, "internal menu resource file could not be written; using memory resource");
    }
    return true;
}

bool selectFreeMenuScreenId() {
    if (IGI::TASK::FIND_BY_ID(menuConfig.screenId) == nullptr) return true;
    const auto occupied = menuConfig.screenId;
    constexpr std::int32_t candidates[] = {32512, 32256, 32000, 31744, 31232};
    for (const auto candidate : candidates) {
        if (IGI::TASK::FIND_BY_ID(candidate) != nullptr) continue;
        menuConfig.screenId = candidate;
        if (!rebuildMenuResources()) return false;
        igi::log(igi::LogLevel::Warning,
                 "internal menu screen: configured task ID " + std::to_string(occupied) +
                 " is occupied; using free task ID " + std::to_string(candidate));
        return true;
    }
    igi::log(igi::LogLevel::Error, "internal menu screen: no free task ID candidate is available");
    return false;
}

#if defined(_WIN32)
// Runs on the verified Display_AddFrameCallback thread once the tick fires.
// Registers the script callbacks, then attaches the generated menu screen to
// the game root task. Every step is logged so an in-game failure points at the
// exact call that broke.
void menuFrameWork() {
    if (!runtime.initialized() || menuScreenAttached) return;
    igi::log(igi::LogLevel::Info, "internal menu frame: checking game state");
    auto* game = IGI::GAME::GET();
    if (!game) {
        igi::log(igi::LogLevel::Error, "internal menu screen: IGI::GAME::GET returned null");
        return;
    }
    if (IGI::GAME::IS_PAUSED()) {
        igi::log(igi::LogLevel::Debug, "internal menu screen: game is paused; waiting for active gameplay");
        return;
    }
    if (!selectFreeMenuScreenId()) return;
    igi::log(igi::LogLevel::Info, "internal menu frame: active game confirmed; installing script callbacks");
    if (!installMenuScriptCallbacks()) return;
    igi::log(igi::LogLevel::Info, "internal menu frame: script callbacks installed");
    auto* root = static_cast<IGI::Task*>(
        static_cast<GameRootLayout*>(static_cast<void*>(game))->root);
    if (!root) {
        igi::log(igi::LogLevel::Error, "internal menu screen: game root task unavailable");
        return;
    }
    const auto managerType = IGI::MENU::GET_TYPE();
    auto* manager = IGI::TASK::FIND_CHILD(root, managerType);
    if (!manager) {
        igi::log(igi::LogLevel::Warning, "internal menu screen: game MenuManager unavailable; waiting");
        return;
    }
    std::ostringstream context;
    context << "internal menu natives: MenuManager_GetType=" << managerType
            << " Task_FindChild manager=" << static_cast<void*>(manager);
    igi::log(igi::LogLevel::Info, context.str());
    igi::log(igi::LogLevel::Info, "internal menu natives: invoking Task_RunScript with the verified MenuManager parent");
    auto* screen = IGI::TASK::RUN_SCRIPT(manager, "LOCAL:igi_sdk/modmenu.qvm", 0);
    if (screen) {
        using GetChangedEvent = std::uint8_t (__cdecl*)();
        using Broadcast = void (__cdecl*)(IGI::Task*, std::uint8_t, void*);
        const auto changedEvent = reinterpret_cast<GetChangedEvent>(igi::re::HASH::Param_GetChangedEvent)();
        char changed = 1;
        igi::log(igi::LogLevel::Info, "internal menu natives: broadcasting Param_GetChangedEvent to bind screen expressions");
        reinterpret_cast<Broadcast>(igi::re::HASH::Task_Broadcast)(screen, changedEvent, &changed);
        igi::log(igi::LogLevel::Info, "internal menu natives: screen parameter initialization completed");
    }
    menuScreenAttached = screen != nullptr;
    if (menuScreenAttached) {
        if (!igi::pauseForMenu(runtime)) {
            igi::log(igi::LogLevel::Error, "internal menu open: unable to pause game");
            return;
        }
        igi::log(igi::LogLevel::Info, "internal menu open: game paused");
    }
    igi::log(menuScreenAttached ? igi::LogLevel::Info : igi::LogLevel::Error,
             menuScreenAttached ? "internal menu screen: attached and requested from existing MenuManager" :
                                  "internal menu screen: Task_RunScript failed");
}
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    igi::log(igi::LogLevel::Debug, "internal initialize: begin");
    menuConfig = igi::loadMenuConfig("project-igi-sdk-config.ini");
#if defined(_WIN32)
    std::int32_t currentScreen = 0;
    if (!readMenuScreenState(currentScreen) || currentScreen != static_cast<std::int32_t>(IgiMenuScreenState::InGame)) {
        igi::log(igi::LogLevel::Warning, "menu requires an active game");
        return false;
    }
#endif
    if (!configureDemoMenu(menuConfig)) return false;
    igi::log(igi::LogLevel::Debug, menuConfig.enabled ? "internal initialize: config enabled" : "internal initialize: config disabled");
    if (menuConfig.screenId == 900 || menuConfig.screenId == 902 || menuConfig.screenId == 903 ||
        menuConfig.screenId == 931 || menuConfig.screenId == 932 || menuConfig.screenId == 934) {
        igi::log(igi::LogLevel::Error, "internal initialize: configured screen_id conflicts with a built-in screen");
        return false;
    }
    if (!rebuildMenuResources()) return false;
    igi::log(igi::LogLevel::Debug, "internal initialize: embedded menu QVM compiled; resource installation awaits the game thread");
#if defined(_WIN32)
    if (menuConfig.directNativeMode) {
        igi::log(igi::LogLevel::Warning,
                 "internal menu ignores native_mode=direct; Task_RunScript requires the Display_AddFrameCallback game thread");
    }
    igi::log(igi::LogLevel::Info, "internal startup stage: installing Display_AddFrameCallback hook");
    if (!runtime.initialize(menuConfig.hook)) {
        igi::log(igi::LogLevel::Error, "internal initialize: runtime initialization failed");
        return false;
    }
    igi::log(igi::LogLevel::Info, "internal startup stage: enqueue begin");
    runtime.scheduler().enqueueEveryFrame([] { menuFrameWork(); });
    igi::log(igi::LogLevel::Info, "internal startup stage: enqueue complete");
#else
    runtime.scheduler().bindGameThread();
#endif
    igi::log(igi::LogLevel::Debug, "internal initialize: creating IGI 1 menu manager");
    igi::log(igi::LogLevel::Info, "internal startup stage: menu model begin");
    gameMenu = std::make_unique<igi::Igi1MenuManager>(igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame, menuConfig));
    igi::log(igi::LogLevel::Info, "internal startup stage: menu model complete");
    const bool pushed = gameMenu->pushScreen(menuConfig.screenId);
    igi::log(pushed ? igi::LogLevel::Warning : igi::LogLevel::Error,
             pushed ? "internal initialize: menu model staged; script callbacks install from the frame tick" : "internal initialize: menu model screen selection failed");
    if (!pushed) {
        gameMenu.reset();
        runtime.shutdown();
        compiledMenu.clear();
        menuSource.clear();
    }
    return pushed;
}

extern "C" IGI_DEMO_EXPORT std::size_t IGI_MenuManager_Internal_RunFrame() {
    const auto count = frameCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto ran = runtime.scheduler().runPending();
#if defined(_WIN32)
    // External callers that already own the game thread can drive the same
    // work the Display_AddFrameCallback tick performs.
    menuFrameWork();
#endif
    if (count <= 5 || (count % 1000) == 0)
        igi::log(igi::LogLevel::Debug, "internal frame: scheduler completed");
    return ran;
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Reopen() {
#if defined(_WIN32)
    if (!runtime.initialized()) {
        igi::log(igi::LogLevel::Warning, "internal menu toggle: runtime is not initialized");
        return false;
    }
    if (menuScreenAttached) {
        runtime.scheduler().enqueueOnce([] { runMenuCommand("LOCAL:igi_sdk/modmenu-reopen.qvm", "reopen"); });
        return true;
    }
    menuScreenAttached = false;
    igi::log(igi::LogLevel::Info, "internal menu toggle: reopening dismissed screen");
    runtime.scheduler().enqueueOnce([] { menuFrameWork(); });
    return true;
#else
    return false;
#endif
}

// Toggles the mod menu: closes it when the configured screen task is live and
// opens it otherwise. The configured start key drives this so each press flips
// the menu state instead of only reopening an already-visible screen.
extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Toggle() {
#if defined(_WIN32)
    if (!runtime.initialized()) {
        igi::log(igi::LogLevel::Warning, "internal menu toggle: runtime is not initialized");
        return false;
    }
    if (menuScreenAttached) {
        igi::log(igi::LogLevel::Info, "internal menu toggle: closing visible screen");
        runtime.scheduler().enqueueOnce([] { runMenuCommand("LOCAL:igi_sdk/modmenu-close.qvm", "close"); });
        return true;
    }
    menuScreenAttached = false;
    igi::log(igi::LogLevel::Info, "internal menu toggle: opening dismissed screen");
    runtime.scheduler().enqueueOnce([] { menuFrameWork(); });
    return true;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_RequestShutdown() {
#if defined(_WIN32)
    if (!runtime.initialized() || !menuScreenAttached) return true;
    // Task_RunScript and the menu task system must be driven from the display
    // frame callback thread. Calling them straight from the key-polling worker
    // races the game's task list and crashes on exit, so request the close
    // through the scheduler and let the next frame tick run it.
    runtime.scheduler().enqueueOnce([] { runMenuCommand("LOCAL:igi_sdk/modmenu-close.qvm", "close"); });
    igi::log(igi::LogLevel::Info, "internal menu close: queued for the display frame thread");
    return true;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_EnsureTick() {
#if defined(_WIN32)
    return runtime.initialized() && runtime.ensureTickRegistered();
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT std::uint64_t IGI_MenuManager_Internal_TickCount() {
#if defined(_WIN32)
    return runtime.gameHookInvocationCount();
#else
    return 0;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_CanUnload() {
#if defined(_WIN32)
    return !menuScreenAttached || IGI::TASK::FIND_BY_ID(menuConfig.screenId) == nullptr;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_Internal_Shutdown() {
    igi::log(igi::LogLevel::Debug, "internal shutdown: begin");
    if (menuScreenAttached && IGI::TASK::FIND_BY_ID(menuConfig.screenId) != nullptr) {
        igi::log(igi::LogLevel::Warning,
                 "internal shutdown deferred: live mod screen retains SDK callbacks; runtime remains resident");
        return;
    }
    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    if (!menuResource.release())
        igi::log(igi::LogLevel::Error, "internal menu resource: release refused because ownership changed");
    reopenResource.release();
    closeResource.release();
    gameMenu.reset();
    runtime.shutdown();
    compiledMenu.clear();
    reopenMenu.clear();
    closeMenu.clear();
    menuSource.clear();
    menuScreenAttached = false;
    igi::log(igi::LogLevel::Info, "internal shutdown: complete");
}
