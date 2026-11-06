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

#include <atomic>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

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
bool menuScreenAttached = false;

enum class IgiMenuScreenState : std::int32_t {
    MainMenu = 3,
    InGame = 4,
    Restart = 5,
};

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
// The 0x44 game-root offset is a retail IGI 1 Win32 x86 ABI fact. On 64-bit
// hosts the pointer alignment pads the prefix to 0x48, so the check only
// applies to the target executable build.
#if defined(_WIN32) && !defined(_WIN64)
static_assert(offsetof(GameRootLayout, root) == 0x44);
#endif

void invokeConfiguredAction(std::size_t item, std::string_view value) {
    if (item >= menuConfig.items.size()) return;
    const auto& action = menuConfig.items[item].nativeAction;
    igi::log(igi::LogLevel::Debug, "internal menu action: " + action);
    // The callback is reached on Display_AddFrameCallback, so typed wrappers are safe
    // to invoke here. Values remain config-driven; no menu labels are used as
    // dispatch keys.
    if (action == "IGI_SDK_SetWeather(this.value)") {
        if (value != "on" && value != "off") {
            igi::log(igi::LogLevel::Warning, "weather selection must be on or off; native call skipped");
            return;
        }
        const char weather[] = {'r', static_cast<char>(value == "on"), '\0'};
        IGI::GAME::SET_RAINING(weather);
    } else if (action == "IGI_SDK_EnableUnlimitedAmmo()") {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
    } else if (action == "IGI_SDK_ToggleFrameLimit()") {
        IGI::MISC::FRAMES_SET(value == "false" ? 60 : 30);
    } else if (action == "IGI_SDK_SetMusicVolume(this.value)") {
        float volume = 0.0F;
        if (value == "mute") volume = 0.0F;
        else if (value == "low") volume = 0.35F;
        else if (value == "normal") volume = 1.0F;
        else {
            igi::log(igi::LogLevel::Warning, "unknown music volume selection; native call skipped");
            return;
        }
        IGI::AUDIO::MUSIC_SET_VOLUME(volume, volume);
    } else if (action == "IGI_SDK_RefreshDisplay()") {
        IGI::GAME::SET_DISPLAY_MODE();
    } else if (action == "IGI_SDK_LogGameState()") {
        std::ostringstream state;
        state << "IGI state: events="
              << static_cast<int>(IGI::GAME::GET_EVENT0()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT1()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT2()) << ','
              << static_cast<int>(IGI::GAME::GET_EVENT3())
              << " world=" << IGI::GAME::GET_WORLD();
        igi::log(igi::LogLevel::Info, state.str());
    } else {
        igi::log(igi::LogLevel::Warning, "internal menu action has no native adapter: " + action);
    }
}

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
    menuBridge = std::make_unique<igi::MenuScriptBridge>(menuConfig, invokeConfiguredAction);
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
    IGI::SCRIPT::SET_SYMBOL_CONTEXT("MenuManager_PushScreen", manager);
    IGI::SCRIPT::SET_SYMBOL_CONTEXT("MenuManager_PopScreen", manager);
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
    igi::log(igi::LogLevel::Info, "internal menu frame: active game confirmed; installing script callbacks");
    if (!installMenuScriptCallbacks()) return;
    igi::log(igi::LogLevel::Info, "internal menu frame: script callbacks installed");
    auto* root = static_cast<IGI::Task*>(
        static_cast<GameRootLayout*>(static_cast<void*>(game))->root);
    if (!root) {
        igi::log(igi::LogLevel::Error, "internal menu screen: game root task unavailable");
        return;
    }
    if (IGI::TASK::FIND_BY_ID(menuConfig.screenId) != nullptr) {
        igi::log(igi::LogLevel::Error,
                 "internal menu screen: configured task ID is already in use; refusing screen creation");
        menuScreenAttached = true;
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
    IGI::SCRIPT::SET_SYMBOL_CONTEXT("MenuManager_PushScreen", manager);
    igi::log(igi::LogLevel::Info, "internal menu natives: MenuManager_PushScreen context bound; invoking Task_RunScript");
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
    igi::log(menuScreenAttached ? igi::LogLevel::Info : igi::LogLevel::Error,
             menuScreenAttached ? "internal menu screen: attached and requested from existing MenuManager" :
                                  "internal menu screen: Task_RunScript failed");
}
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    igi::log(igi::LogLevel::Debug, "internal initialize: begin");
    menuConfig = igi::loadMenuConfig("config.ini");
    igi::log(igi::LogLevel::Debug, menuConfig.enabled ? "internal initialize: config enabled" : "internal initialize: config disabled");
    if (menuConfig.screenId == 900 || menuConfig.screenId == 902 || menuConfig.screenId == 903 ||
        menuConfig.screenId == 931 || menuConfig.screenId == 932 || menuConfig.screenId == 934) {
        igi::log(igi::LogLevel::Error, "internal initialize: configured screen_id conflicts with a built-in screen");
        return false;
    }
    std::string compileError;
    if (!igi::compileIgi1MenuScreenScript(menuConfig, compiledMenu, compileError)) {
        igi::log(igi::LogLevel::Error, std::string("internal initialize: embedded menu compilation failed: ") + compileError);
        return false;
    }
    if (!igi::compileIgi1MenuCommandScript("MenuManager_PushScreen(" + std::to_string(menuConfig.screenId) + ");", reopenMenu, compileError) ||
        !igi::compileIgi1MenuCommandScript("MenuManager_PopScreen(FALSE);", closeMenu, compileError)) {
        igi::log(igi::LogLevel::Error, std::string("internal initialize: menu command compilation failed: ") + compileError);
        return false;
    }
    if (!writeMenuResourceFile()) {
        igi::log(igi::LogLevel::Warning, "internal initialize: could not write igi_sdk/modmenu.qvm; using in-memory resource only");
    } else {
        igi::log(igi::LogLevel::Info, "internal initialize: wrote igi_sdk/modmenu.qvm");
    }
    // Script_Load may request the QSC source when QVM-only mode is disabled.
    igi::log(igi::LogLevel::Info, "internal startup stage: generating menu QSC source");
    std::ostringstream source;
    if (!igi::writeIgi1MenuScreenScript(source, menuConfig)) {
        compiledMenu.clear();
        igi::log(igi::LogLevel::Error, "internal initialize: menu source generation failed");
        return false;
    }
    igi::log(igi::LogLevel::Info, "internal startup stage: menu QSC source generated");
    menuSource = source.str();
    igi::log(igi::LogLevel::Info, "internal startup stage: menu QSC source retained");
    igi::log(igi::LogLevel::Debug, "internal initialize: embedded menu QVM compiled; resource installation awaits the game thread");
#if defined(_WIN32)
    const bool directNativeMode = menuConfig.directNativeMode;
    if (directNativeMode) {
        std::int32_t activeScreen = 0;
        if (!readMenuScreenState(activeScreen)) {
            igi::log(igi::LogLevel::Warning, "internal direct mode deferred: menu screen state is unavailable");
            return false;
        }
        std::ostringstream screenMessage;
        screenMessage << "internal direct mode: observed menu screen=" << activeScreen;
        igi::log(igi::LogLevel::Info, screenMessage.str());
        if (activeScreen != static_cast<std::int32_t>(IgiMenuScreenState::InGame)) {
            igi::log(igi::LogLevel::Warning,
                     "internal direct mode deferred: attach requires the in-game menu screen (4), never Main Menu (3) or Restart (5)");
            return false;
        }
    }
    igi::log(igi::LogLevel::Info, directNativeMode
        ? "internal startup stage: direct native menu mode enabled; no scheduler hook"
        : "internal startup stage: installing Display_AddFrameCallback hook");
    if (!(directNativeMode ? runtime.initializeDirectNativeMode() : runtime.initialize(menuConfig.hook))) {
        igi::log(igi::LogLevel::Error, "internal initialize: runtime initialization failed");
        return false;
    }
    if (directNativeMode) {
        igi::log(igi::LogLevel::Info, "internal startup stage: directly attaching menu from F5 worker");
        menuFrameWork();
    } else {
        igi::log(igi::LogLevel::Info, "internal startup stage: enqueue begin");
        runtime.scheduler().enqueueEveryFrame([] { menuFrameWork(); });
        igi::log(igi::LogLevel::Info, "internal startup stage: enqueue complete");
    }
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
    if (IGI::TASK::FIND_BY_ID(menuConfig.screenId) != nullptr) {
        return runMenuCommand("LOCAL:igi_sdk/modmenu-reopen.qvm", "reopen");
    }
    menuScreenAttached = false;
    igi::log(igi::LogLevel::Info, "internal menu toggle: reopening dismissed screen");
    menuFrameWork();
    return menuScreenAttached;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_RequestShutdown() {
#if defined(_WIN32)
    if (!runtime.initialized()) return true;
    if (IGI::TASK::FIND_BY_ID(menuConfig.screenId) == nullptr) return true;
    return runMenuCommand("LOCAL:igi_sdk/modmenu-close.qvm", "close");
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_CanUnload() {
#if defined(_WIN32)
    return IGI::TASK::FIND_BY_ID(menuConfig.screenId) == nullptr;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_Internal_Shutdown() {
    igi::log(igi::LogLevel::Debug, "internal shutdown: begin");
    if (menuScreenAttached) {
        // The live MenuScreen and its QVM retain references to SDK script
        // callbacks. IGI has no recovered screen-detach API here, so tearing
        // down callbacks, resource bytes, or the game hook would leave live
        // game objects pointing into released SDK state. Keep the runtime and
        // resource resident for this process instead of creating dangling
        // callbacks. A full detach path can replace this once task teardown is
        // verified against the retail executable.
        igi::log(igi::LogLevel::Warning,
                 "internal shutdown deferred: live mod screen retains SDK callbacks; runtime remains resident");
        return;
    }
    // Script handlers hold pointers into the DLL; remove them before releasing
    // the bridge and callback objects.
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
