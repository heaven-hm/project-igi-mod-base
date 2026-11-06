#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/IGI.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/igi1_menu_manager.hpp"
#include "igi_sdk/ui/menu_script.hpp"
#include "igi_sdk/ui/menu_script_bridge.hpp"
#include "igi_sdk/ui/menu_resource.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi1_native_registry.hpp"

#include <atomic>
#include <memory>
#include <sstream>
#include <string>

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
std::string menuSource;
std::unique_ptr<igi::MenuScriptBridge> menuBridge;
std::unique_ptr<igi::MenuScriptCallbacks> menuCallbacks;
igi::MenuScriptRegistration menuRegistration;
igi::MenuResource menuResource;
std::atomic<std::uint64_t> frameCount{0};
bool menuScreenAttached = false;

struct GameRootLayout {
    std::uint8_t prefix[0x44];
    void* root;
};
static_assert(offsetof(GameRootLayout, root) == 0x44);

void invokeConfiguredAction(std::size_t item, std::string_view value) {
    if (item >= menuConfig.items.size()) return;
    const auto& action = menuConfig.items[item].nativeAction;
    igi::log(igi::LogLevel::Debug, "internal menu action: " + action);
    // The callback is reached on Game_RunHandler, so typed wrappers are safe
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
    // Script_Load may request the QSC source when QVM-only mode is disabled.
    std::ostringstream source;
    if (!igi::writeIgi1MenuScreenScript(source, menuConfig)) {
        compiledMenu.clear();
        igi::log(igi::LogLevel::Error, "internal initialize: menu source generation failed");
        return false;
    }
    menuSource = source.str();
    igi::log(igi::LogLevel::Debug, "internal initialize: embedded menu QVM compiled; resource installation awaits the game thread");
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
    igi::log(pushed ? igi::LogLevel::Warning : igi::LogLevel::Error,
             pushed ? "internal initialize: menu model staged; script callbacks install from Game_RunHandler" : "internal initialize: menu model screen selection failed");
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
#if defined(_WIN32)
    // Only the actual Game_RunHandler detour may bind the game thread.
    // A loader or test host calling this export must not impersonate it.
    const auto ran = runtime.scheduler().runPending();
    if (runtime.initialized() && installMenuScriptCallbacks() && !menuScreenAttached) {
        auto* game = IGI::GAME::GET();
        auto* root = game ? static_cast<IGI::Task*>(static_cast<GameRootLayout*>(static_cast<void*>(game))->root) : nullptr;
        if (!root) {
            igi::log(igi::LogLevel::Error, "internal menu screen: game root task unavailable");
        } else if (IGI::TASK::FIND_BY_ID(menuConfig.screenId) != nullptr) {
            igi::log(igi::LogLevel::Error,
                     "internal menu screen: configured task ID is already in use; refusing screen creation");
        } else {
            igi::log(igi::LogLevel::Info, "internal menu screen: attaching screen script to game root");
            auto* screen = IGI::TASK::RUN_SCRIPT(root, "LOCAL:igi_sdk/modmenu.qvm", 0);
            menuScreenAttached = screen != nullptr;
            igi::log(menuScreenAttached ? igi::LogLevel::Info : igi::LogLevel::Error,
                     menuScreenAttached ? "internal menu screen: attached and requested from existing MenuManager" :
                                          "internal menu screen: Task_RunScript failed");
        }
    }
#else
    const auto ran = runtime.scheduler().runPending();
#endif
    if (count <= 5 || (count % 1000) == 0)
        igi::log(igi::LogLevel::Debug, "internal frame: scheduler completed");
    return ran;
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
    gameMenu.reset();
    runtime.shutdown();
    compiledMenu.clear();
    menuSource.clear();
    menuScreenAttached = false;
    igi::log(igi::LogLevel::Info, "internal shutdown: complete");
}
