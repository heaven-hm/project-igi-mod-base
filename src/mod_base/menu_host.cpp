#include "config/menu_config.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/mvp_natives.hpp"
#include "runtime/runtime.hpp"
#include "ui/igi_menu_manager.hpp"
#include "ui/menu_script.hpp"
#include "ui/menu_script_bridge.hpp"
#include "ui/menu_resource.hpp"
#include "logging/logging.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/igi_hashes/hashes.hpp"
#include "platform/windows_dll.hpp"
#include "ui/menu_lifecycle.hpp"
#include "ui/mod_menu.hpp"

#include <atomic>
#include <cmath>
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
#if defined(_WIN32)
struct DisplayCallbackNode {
    DisplayCallbackNode *next = nullptr;
    void(__cdecl *callback)() = nullptr;
};
#endif

enum class IgiMenuScreenState : std::int32_t {
    MainMenu = 3,
    InGame = 4,
    Restart = 5,
};

enum class MenuTransition { None, Closing, Reopening };

// Retail IGI 1 MenuManager layout recovered from the menu-script entry points:
// MenuManager_RequestScreen (0x004174a0) and MenuManager_LeaveMenus (0x00417790)
// both write the "Current menuscreen" parameter at +0x20.  FLOW_GET_SCREEN
// returns this MenuManager object, not the active MenuScreen task.
struct RetailMenuManagerLayout {
    std::byte taskHeaderAndUnknown[0x20];
    std::int32_t currentScreenId;
};

static_assert(offsetof(RetailMenuManagerLayout, currentScreenId) == 0x20);

// Consolidated mod-menu state container (BigBase v2 style).
struct InternalMenuState {
    igi::Runtime runtime;
    std::unique_ptr<igi::IgiMenuManager> gameMenu;
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
    std::uint64_t lastLiveRefreshTick = 0;
    std::atomic<std::int32_t> observedScreen{0};
    std::atomic<bool> safeToShutdown{true};
    std::atomic<bool> shutdownPending{false};
    bool menuScreenCreated = false;
    bool menuScreenAttached = false;
    MenuTransition pendingTransition = MenuTransition::None;
    // Page navigation closes the current page screen and re-attaches with the
    // next page, so only one MenuScreen task ever exists in the retail manager.
    int pendingPageDelta = 0;
    std::uint64_t transitionDeadlineFrame = 0;
    std::atomic<bool> initialAttachPending{false};
#if defined(_WIN32)
    DisplayCallbackNode endFrameNode{};
#endif
};

InternalMenuState g_menu;
auto &runtime = g_menu.runtime;
auto &gameMenu = g_menu.gameMenu;
auto &menuConfig = g_menu.menuConfig;
auto &compiledMenu = g_menu.compiledMenu;
auto &reopenMenu = g_menu.reopenMenu;
auto &closeMenu = g_menu.closeMenu;
auto &menuSource = g_menu.menuSource;
auto &menuBridge = g_menu.menuBridge;
auto &menuCallbacks = g_menu.menuCallbacks;
auto &menuRegistration = g_menu.menuRegistration;
auto &menuResource = g_menu.menuResource;
auto &reopenResource = g_menu.reopenResource;
auto &closeResource = g_menu.closeResource;
auto &frameCount = g_menu.frameCount;
auto &lastLiveRefreshTick = g_menu.lastLiveRefreshTick;
auto &safeToShutdown = g_menu.safeToShutdown;
auto &shutdownPending = g_menu.shutdownPending;
auto &menuScreenCreated = g_menu.menuScreenCreated;
auto &menuScreenAttached = g_menu.menuScreenAttached;
auto &pendingTransition = g_menu.pendingTransition;
auto &pendingPageDelta = g_menu.pendingPageDelta;
auto &transitionDeadlineFrame = g_menu.transitionDeadlineFrame;
auto &initialAttachPending = g_menu.initialAttachPending;

#if defined(_WIN32)
extern "C" void __cdecl onInternalDisplayEndFrame() noexcept;

DisplayCallbackNode **endFrameCallbackHead() noexcept {
    return reinterpret_cast<DisplayCallbackNode **>(
        static_cast<std::uintptr_t>(igi::re::HASH::gDisplayEndFrameCallbacks));
}

bool isEndFrameCallbackRegistered() noexcept {
    DisplayCallbackNode *node = *endFrameCallbackHead();
    for (int guard = 0; node && guard < 64; ++guard) {
        if (node == &g_menu.endFrameNode || node->callback == &onInternalDisplayEndFrame)
            return true;
        node = node->next;
    }
    return false;
}

void ensureEndFrameCallbackRegistered() noexcept {
    if (isEndFrameCallbackRegistered())
        return;
    auto **head = endFrameCallbackHead();
    g_menu.endFrameNode.callback = &onInternalDisplayEndFrame;
    g_menu.endFrameNode.next = *head;
    *head = &g_menu.endFrameNode;
}

void unregisterEndFrameCallback() noexcept {
    auto **head = endFrameCallbackHead();
    DisplayCallbackNode *previous = nullptr;
    DisplayCallbackNode *node = *head;
    for (int guard = 0; node && guard < 64; ++guard) {
        if (node == &g_menu.endFrameNode || node->callback == &onInternalDisplayEndFrame) {
            if (previous)
                previous->next = node->next;
            else
                *head = node->next;
            break;
        }
        previous = node;
        node = node->next;
    }
    g_menu.endFrameNode.next = nullptr;
    g_menu.endFrameNode.callback = nullptr;
}

extern "C" void __cdecl onInternalDisplayEndFrame() noexcept {
    if (!runtime.initialized())
        return;
    // Continuous mod features (free camera) must run even while the menu is open.
    if (menuConfig.frameUpdate)
        menuConfig.frameUpdate(runtime);
    if (menuScreenAttached || !menuConfig.renderEndFrame)
        return;
    menuConfig.renderEndFrame(runtime);
}
#endif

struct GameRootLayout {
    std::uint8_t prefix[0x44];
    void *root;
};

#if defined(_WIN32) && !defined(_WIN64)
static_assert(offsetof(GameRootLayout, root) == 0x44);
#endif

bool installMenuScriptCallbacks() {
    if (menuRegistration.active())
        return true;
    auto *caller = igi::igiNativeCaller();
    if (!caller) {
        igi::log(igi::LogLevel::Warning, "internal menu callbacks: native caller unavailable");
        return false;
    }
    const auto script = igi::igiMenuScriptApi(*caller);
    const auto registry = igi::igiScriptRegistryApi(*caller);
    if (!script.getInt || !script.addItem || !registry.registerFunction) {
        igi::log(igi::LogLevel::Warning, "internal menu callbacks: IGI script APIs unavailable");
        return false;
    }
    if (!menuResource.active()) {
        const auto resources = igi::igiMenuResourceApi(*caller);
        if (!resources.isLoaded || !resources.registerResource || !resources.freeResource ||
            !menuResource.registerData("LOCAL:mod_base/modmenu.qvm", compiledMenu, resources)) {
            igi::log(igi::LogLevel::Error, "internal menu resource: QVM registration failed");
            return false;
        }
        igi::log(igi::LogLevel::Info,
                 "internal menu resource: LOCAL:mod_base/modmenu.qvm registered");
    }
    const auto resources = igi::igiMenuResourceApi(*caller);
    if (!reopenResource.active() &&
        !reopenResource.registerData("LOCAL:mod_base/modmenu-reopen.qvm", reopenMenu, resources))
        return false;
    if (!closeResource.active() &&
        !closeResource.registerData("LOCAL:mod_base/modmenu-close.qvm", closeMenu, resources))
        return false;
    menuBridge = std::make_unique<igi::MenuScriptBridge>(
        menuConfig,
        [](std::size_t item, std::string_view value) {
            runModMenu(menuConfig, runtime, item, value);
        },
        [] {
            if (pendingTransition != MenuTransition::None)
                return;
            pendingTransition = MenuTransition::Closing;
            transitionDeadlineFrame = runtime.scheduler().gameHookInvocationCount() + 120;
            safeToShutdown.store(false, std::memory_order_release);
            igi::log(igi::LogLevel::Info,
                     "internal menu close: awaiting retail screen transition before resuming");
        },
        /*deferUntilApply=*/true,
        [](int delta) {
            if (delta != 0)
                pendingPageDelta = delta;
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

// Command scripts (RequestScreen/PopScreen) create no task, so Task_RunScript
// legitimately returns null. The transition deadline is the real success signal.
bool runMenuCommand(const char *resource, const char *operation) {
    auto *game = IGI::GAME::GET();
    auto *root = game ? static_cast<IGI::Task *>(
                            static_cast<GameRootLayout *>(static_cast<void *>(game))->root)
                      : nullptr;
    if (!root)
        return false;
    auto *manager = IGI::TASK::FIND_CHILD(root, IGI::MENU::GET_TYPE());
    if (!manager)
        return false;
    (void)IGI::TASK::RUN_SCRIPT(manager, resource, 0);
    igi::log(igi::LogLevel::Info, std::string("internal menu command: ") + operation +
                                      " requested; awaiting the retail screen transition");
    return true;
}

// A large control set is emitted as several page screens sharing the reserved id
// range, so any of those ids counts as "our menu is on screen".
bool isConfiguredMenuScreenActive() {
    auto *manager = static_cast<const RetailMenuManagerLayout *>(IGI::OTHER::FLOW_GET_SCREEN());
    if (!manager)
        return false;
    const auto pages = igi::menuPageCount(menuConfig);
    for (std::size_t page = 0; page < pages; ++page) {
        const int id = igi::menuPageScreenId(menuConfig, page);
        if (manager->currentScreenId == id && IGI::TASK::FIND_BY_ID(id) != nullptr)
            return true;
    }
    return false;
}

// Removes the injected MenuScreen task while the retail MenuManager is still
// alive. The retail pop only takes the screen off the stack, so a leftover child
// would keep sitting in front of the game's own screens (the pause menu would
// open behind a dead screen). Must run before the QVM resource is released.
void releaseInjectedMenuScreen() {
#if defined(_WIN32)
    bool removed = false;
    const auto pages = igi::menuPageCount(menuConfig);
    for (std::size_t page = 0; page < pages; ++page) {
        const int id = igi::menuPageScreenId(menuConfig, page);
        auto *screen = IGI::TASK::FIND_BY_ID(id);
        if (!screen)
            continue;
        igi::log(igi::LogLevel::Info,
                 "internal unload: deleting leftover menu screen task id " + std::to_string(id));
        IGI::TASK::DELETE_TASK(screen);
        removed = true;
    }
    if (!removed && menuScreenCreated)
        igi::log(igi::LogLevel::Info,
                 "internal unload: injected menu screen already detached from id " +
                     std::to_string(igi::menuPageScreenId(menuConfig, menuConfig.pageIndex)));
#else
    (void)0;
#endif
}

bool rebuildMenuResources();

void completeShutdownOnGameThread() {
#if defined(_WIN32)
    unregisterEndFrameCallback();
#endif
    releaseInjectedMenuScreen();
    IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI SDK Mod Menu: disabled");
    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    if (!menuResource.release())
        igi::log(igi::LogLevel::Error,
                 "internal menu resource: release refused because ownership changed");
    reopenResource.release();
    closeResource.release();
    menuScreenCreated = false;
    menuScreenAttached = false;
    pendingTransition = MenuTransition::None;
    shutdownPending.store(false, std::memory_order_release);
    safeToShutdown.store(true, std::memory_order_release);
    igi::log(igi::LogLevel::Info, "internal unload: game-thread cleanup finished");
}

// Applies a queued page change: the old screen is already off the retail stack,
// so its task is deleted, resources are rebuilt for the new page and the frame
// tick attaches that page as a fresh screen.
bool startPageSwitch() {
    if (pendingPageDelta == 0)
        return false;
    const auto pages = static_cast<int>(igi::menuPageCount(menuConfig));
    int page = static_cast<int>(menuConfig.pageIndex) + pendingPageDelta;
    pendingPageDelta = 0;
    if (pages <= 1)
        return false;
    page = ((page % pages) + pages) % pages;
    if (page == static_cast<int>(menuConfig.pageIndex))
        return false;
    menuConfig.pageIndex = static_cast<std::size_t>(page);
    releaseInjectedMenuScreen();
    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    menuResource.release();
    reopenResource.release();
    closeResource.release();
    menuScreenCreated = false;
    menuScreenAttached = false;
    if (!rebuildMenuResources()) {
        igi::log(igi::LogLevel::Error, "internal menu page switch: resource rebuild failed");
        return false;
    }
    initialAttachPending.store(true, std::memory_order_release);
    safeToShutdown.store(false, std::memory_order_release);
    igi::log(igi::LogLevel::Info, "internal menu page switch: now showing page " +
                                      std::to_string(page + 1) + " of " + std::to_string(pages));
    return true;
}

void completeCloseTransition() {
    pendingTransition = MenuTransition::None;
    menuScreenAttached = false;
    if (pendingPageDelta != 0) {
        if (menuBridge)
            menuBridge->cancel(false);
        if (startPageSwitch())
            return;
    }
    if (menuBridge)
        menuBridge->cancel(false);
    if (!igi::resumeFromMenu(runtime))
        igi::log(igi::LogLevel::Warning, "internal menu close: unable to resume game");
    if (shutdownPending.load(std::memory_order_acquire)) {
        completeShutdownOnGameThread();
        return;
    }
    safeToShutdown.store(true, std::memory_order_release);
    igi::log(igi::LogLevel::Info,
             "internal menu close: retail screen transition confirmed; game resumed");
}

void processPendingMenuTransition() {
    if (pendingTransition == MenuTransition::None)
        return;

    const auto currentFrame = runtime.scheduler().gameHookInvocationCount();
    const bool screenActive = isConfiguredMenuScreenActive();
    if (pendingTransition == MenuTransition::Closing && !screenActive) {
        completeCloseTransition();
        return;
    }
    if (pendingTransition == MenuTransition::Reopening && screenActive) {
        pendingTransition = MenuTransition::None;
        menuScreenAttached = true;
        safeToShutdown.store(false, std::memory_order_release);
        if (!igi::pauseForMenu(runtime)) {
            igi::log(igi::LogLevel::Error, "internal menu reopen: unable to pause game");
            return;
        }
        if (menuBridge)
            menuBridge->snapshot();
        igi::log(igi::LogLevel::Info,
                 "internal menu reopen: retail screen transition confirmed and game paused");
        return;
    }
    if (currentFrame < transitionDeadlineFrame)
        return;

    const bool wasClosing = pendingTransition == MenuTransition::Closing;
    pendingTransition = MenuTransition::None;
    if (wasClosing) {
        menuScreenAttached = true;
        safeToShutdown.store(false, std::memory_order_release);
        shutdownPending.store(false, std::memory_order_release);
        igi::log(igi::LogLevel::Error, "internal menu close: retail screen did not transition; "
                                       "retaining callbacks and resources");
        return;
    }
    menuScreenAttached = false;
    menuScreenCreated = false;
    (void)igi::resumeFromMenu(runtime);
    igi::log(igi::LogLevel::Warning,
             "internal menu reopen: retail screen did not transition; forcing a fresh attach");
    // The retail manager can be left pointing at a screen task that no longer
    // exists. Detach everything and let the frame tick build the screen again,
    // which also clears any wedged state instead of leaving the game stuck.
    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    menuResource.release();
    reopenResource.release();
    closeResource.release();
    if (rebuildMenuResources()) {
        initialAttachPending.store(true, std::memory_order_release);
        safeToShutdown.store(false, std::memory_order_release);
    }
}

bool writeMenuResourceFile() {
    if (compiledMenu.empty())
        return false;
    std::error_code error;
    std::filesystem::create_directories("mod_base", error);
    if (error)
        return false;
    std::ofstream output("mod_base/modmenu.qvm", std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char *>(compiledMenu.data()),
                 static_cast<std::streamsize>(compiledMenu.size()));
    return output.good();
}

// Page navigation pushes one screen per page, so closing has to pop the whole
// range we pushed or the game is left on a page screen.
std::string closeCommandScript() {
    const auto pages = igi::menuPageCount(menuConfig);
    std::string script;
    for (std::size_t page = 0; page < pages; ++page)
        script += "MenuManager_PopScreen(FALSE);";
    return script;
}

bool rebuildMenuResources() {
    std::vector<std::uint8_t> screen;
    std::vector<std::uint8_t> reopen;
    std::vector<std::uint8_t> close;
    std::string error;
    if (!igi::compileIgiMenuScreenScript(menuConfig, screen, error) ||
        !igi::compileIgiMenuCommandScript(
            "MenuManager_RequestScreen(" +
                std::to_string(igi::menuPageScreenId(menuConfig, menuConfig.pageIndex)) +
                ", FALSE);",
            reopen, error) ||
        !igi::compileIgiMenuCommandScript(closeCommandScript(), close, error)) {
        igi::log(igi::LogLevel::Error,
                 std::string("internal menu resource compilation failed: ") + error);
        return false;
    }
    std::ostringstream source;
    if (!igi::writeIgiMenuScreenScript(source, menuConfig)) {
        igi::log(igi::LogLevel::Error, "internal menu source generation failed");
        return false;
    }
    compiledMenu = std::move(screen);
    reopenMenu = std::move(reopen);
    closeMenu = std::move(close);
    menuSource = source.str();
    if (!writeMenuResourceFile()) {
        igi::log(igi::LogLevel::Warning,
                 "internal menu resource file could not be written; using memory resource");
    }
    return true;
}

bool isTaskIdRangeFree(std::int32_t baseId, std::size_t itemCount) {
    const auto maxId = baseId + static_cast<std::int32_t>(itemCount);
    if (baseId < 0 || maxId >= 4096)
        return false;
    for (std::int32_t id = baseId; id <= maxId; ++id) {
        if (IGI::TASK::FIND_BY_ID(id) != nullptr)
            return false;
    }
    return true;
}

bool selectFreeMenuScreenId() {
    if (isTaskIdRangeFree(menuConfig.screenId, igi::menuScreenIdSpan(menuConfig)))
        return true;
    const auto occupied = menuConfig.screenId;
    const auto span = static_cast<std::int32_t>(igi::menuScreenIdSpan(menuConfig));
    for (std::int32_t candidate = 4095 - span; candidate >= 3000; --candidate) {
        if (!isTaskIdRangeFree(candidate, igi::menuScreenIdSpan(menuConfig)))
            continue;
        menuConfig.screenId = candidate;
        if (!rebuildMenuResources())
            return false;
        igi::log(igi::LogLevel::Warning,
                 "internal menu screen: configured task ID " + std::to_string(occupied) +
                     " is occupied; using free task ID " + std::to_string(candidate));
        return true;
    }
    igi::log(igi::LogLevel::Error, "internal menu screen: no free task ID candidate is available");
    return false;
}

#if defined(_WIN32)
// Frames between live-task-table refreshes for the attached screen (~1s at 60Hz).
constexpr std::uint64_t kLiveRefreshTicks = 60;

// The retail screen fills its ListBoxes from the script bridge, so a live target
// list rebuilt from the task table only reaches the screen after the bridge is
// resynced and the screen's changed event is broadcast again. Without this the
// internal menu kept the list it collected when it first attached.
void refreshAttachedLiveItems() {
    if (!menuScreenAttached || pendingTransition != MenuTransition::None || !menuBridge ||
        !menuConfig.refreshLiveItems)
        return;
    const auto tick = runtime.scheduler().gameHookInvocationCount();
    if (tick - lastLiveRefreshTick < kLiveRefreshTicks)
        return;
    lastLiveRefreshTick = tick;
    if (!menuConfig.refreshLiveItems(menuConfig, runtime))
        return;
    if (!menuBridge->resync(menuConfig))
        return;
    if (auto *screen = IGI::TASK::FIND_BY_ID(menuConfig.screenId)) {
        using GetChangedEvent = std::uint8_t(__cdecl *)();
        using Broadcast = void(__cdecl *)(IGI::Task *, std::uint8_t, void *);
        const auto changedEvent =
            reinterpret_cast<GetChangedEvent>(igi::re::HASH::Param_GetChangedEvent)();
        char changed = 1;
        reinterpret_cast<Broadcast>(igi::re::HASH::Task_Broadcast)(screen, changedEvent, &changed);
    }
    igi::log(igi::LogLevel::Info,
             "internal menu: live target lists refreshed for the attached screen");
}

// Runs on the verified Display_AddFrameCallback thread once the tick fires.
// Registers the script callbacks, then attaches the generated menu screen to
// the game root task. Every step is logged so an in-game failure points at the
// exact call that broke.
void menuFrameWork() {
    if (!runtime.initialized())
        return;
#if defined(_WIN32)
    ensureEndFrameCallbackRegistered();
#endif
    processPendingMenuTransition();
    refreshAttachedLiveItems();
    if (menuScreenAttached || pendingTransition != MenuTransition::None)
        return;
    if (!initialAttachPending.load(std::memory_order_acquire))
        return;
    igi::log(igi::LogLevel::Info, "internal menu frame: checking game state");
    auto *game = IGI::GAME::GET();
    if (!game) {
        igi::log(igi::LogLevel::Error, "internal menu screen: IGI::GAME::GET returned null");
        return;
    }
    if (IGI::GAME::IS_PAUSED()) {
        igi::log(igi::LogLevel::Debug,
                 "internal menu screen: game is paused; waiting for active gameplay");
        return;
    }
    if (menuConfig.refreshLiveItems && menuConfig.refreshLiveItems(menuConfig, runtime) &&
        !rebuildMenuResources()) {
        return;
    }
    if (!selectFreeMenuScreenId())
        return;
    igi::log(igi::LogLevel::Info,
             "internal menu frame: active game confirmed; installing script callbacks");
    if (!installMenuScriptCallbacks())
        return;
    igi::log(igi::LogLevel::Info, "internal menu frame: script callbacks installed");
    auto *root =
        static_cast<IGI::Task *>(static_cast<GameRootLayout *>(static_cast<void *>(game))->root);
    if (!root) {
        igi::log(igi::LogLevel::Error, "internal menu screen: game root task unavailable");
        return;
    }
    const auto managerType = IGI::MENU::GET_TYPE();
    auto *manager = IGI::TASK::FIND_CHILD(root, managerType);
    if (!manager) {
        igi::log(igi::LogLevel::Warning,
                 "internal menu screen: game MenuManager unavailable; waiting");
        return;
    }
    std::ostringstream context;
    context << "internal menu natives: MenuManager_GetType=" << managerType
            << " Task_FindChild manager=" << static_cast<void *>(manager);
    igi::log(igi::LogLevel::Info, context.str());
    igi::log(igi::LogLevel::Info,
             "internal menu natives: invoking Task_RunScript with the verified MenuManager parent");
    auto *screen = IGI::TASK::RUN_SCRIPT(manager, "LOCAL:mod_base/modmenu.qvm", 0);
    const int requestedScreenId = igi::menuPageScreenId(menuConfig, menuConfig.pageIndex);
    if (!screen)
        screen = IGI::TASK::FIND_BY_ID(requestedScreenId);
    if (!screen) {
        igi::log(igi::LogLevel::Error,
                 "internal menu screen: page " + std::to_string(menuConfig.pageIndex + 1) +
                     " script created no screen task for id " + std::to_string(requestedScreenId) +
                     "; keeping the game unpaused");
        initialAttachPending.store(false, std::memory_order_release);
        return;
    }
    if (screen) {
        using GetChangedEvent = std::uint8_t(__cdecl *)();
        using Broadcast = void(__cdecl *)(IGI::Task *, std::uint8_t, void *);
        const auto changedEvent =
            reinterpret_cast<GetChangedEvent>(igi::re::HASH::Param_GetChangedEvent)();
        char changed = 1;
        igi::log(
            igi::LogLevel::Info,
            "internal menu natives: broadcasting Param_GetChangedEvent to bind screen expressions");
        reinterpret_cast<Broadcast>(igi::re::HASH::Task_Broadcast)(screen, changedEvent, &changed);
        igi::log(igi::LogLevel::Info,
                 "internal menu natives: screen parameter initialization completed");
    }
    menuScreenCreated = screen != nullptr;
    menuScreenAttached = screen != nullptr;
    if (menuScreenAttached) {
        initialAttachPending.store(false, std::memory_order_release);
        lastLiveRefreshTick = runtime.scheduler().gameHookInvocationCount();
        safeToShutdown.store(false, std::memory_order_release);
        if (!igi::pauseForMenu(runtime)) {
            igi::log(igi::LogLevel::Error, "internal menu open: unable to pause game");
            return;
        }
        if (menuBridge)
            menuBridge->snapshot();
        IGI::MISC::STATUS_MESSAGE_SHOW(nullptr, "IGI SDK Mod Menu: enabled");
        igi::log(igi::LogLevel::Info, "internal menu open: game paused and human input disabled");
    }
    igi::log(menuScreenAttached ? igi::LogLevel::Info : igi::LogLevel::Error,
             menuScreenAttached
                 ? "internal menu screen: attached and requested from existing MenuManager"
                 : "internal menu screen: Task_RunScript failed");
}

bool closeMenuScreen() {
    if (!runMenuCommand("LOCAL:mod_base/modmenu-close.qvm", "close"))
        return false;
    pendingTransition = MenuTransition::Closing;
    transitionDeadlineFrame = runtime.scheduler().gameHookInvocationCount() + 120;
    safeToShutdown.store(false, std::memory_order_release);
    igi::log(igi::LogLevel::Info,
             "internal menu close: awaiting retail screen transition before cleanup");
    return true;
}

bool reopenMenuScreen() {
    if (isConfiguredMenuScreenActive()) {
        menuScreenAttached = true;
        menuScreenCreated = true;
        return true;
    }
    if (menuScreenCreated) {
        if (runMenuCommand("LOCAL:mod_base/modmenu-reopen.qvm", "reopen")) {
            pendingTransition = MenuTransition::Reopening;
            transitionDeadlineFrame = runtime.scheduler().gameHookInvocationCount() + 120;
            safeToShutdown.store(false, std::memory_order_release);
            igi::log(igi::LogLevel::Info,
                     "internal menu reopen: awaiting retail screen transition before pausing game");
            return true;
        }
        igi::log(igi::LogLevel::Warning,
                 "internal menu reopen: reopen script failed; falling back to fresh attach");
    }
    menuScreenCreated = false;
    menuScreenAttached = false;
    initialAttachPending.store(true, std::memory_order_release);
    menuFrameWork();
    return menuScreenAttached;
}

bool toggleMenuScreenOnGameThread() {
    if (pendingTransition != MenuTransition::None) {
        igi::log(igi::LogLevel::Info,
                 "internal menu toggle: ignored while a retail screen transition is in flight");
        return false;
    }
    if (!IGI::GAME::GET()) {
        igi::log(igi::LogLevel::Warning, "internal menu toggle: menu requires an active game");
        return false;
    }
    if (isConfiguredMenuScreenActive()) {
        return closeMenuScreen();
    }
    return menuScreenAttached ? closeMenuScreen() : reopenMenuScreen();
}
#endif
} // namespace

namespace igi {
bool prepareMenuForLevelChange() {
#if defined(_WIN32)
    if (!runtime.initialized())
        return true;

    initialAttachPending.store(false, std::memory_order_release);
#if defined(_WIN32)
    unregisterEndFrameCallback();
#endif
    releaseInjectedMenuScreen();
    if (menuBridge)
        menuBridge->cancel();
    else if (!resumeFromMenu(runtime))
        log(LogLevel::Warning, "level change: unable to resume game before teardown");

    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    if (!menuResource.release())
        log(LogLevel::Error, "level change: menu resource release failed");
    if (!reopenResource.release())
        log(LogLevel::Error, "level change: reopen resource release failed");
    if (!closeResource.release())
        log(LogLevel::Error, "level change: close resource release failed");
    menuScreenCreated = false;
    menuScreenAttached = false;
    safeToShutdown.store(true, std::memory_order_release);
    log(LogLevel::Info,
        "level change: detached injected menu callbacks and resources before game teardown");
    return true;
#else
    return false;
#endif
}
} // namespace igi

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize() {
    igi::log(igi::LogLevel::Debug, "internal initialize: begin");
    menuConfig = igi::loadMenuConfig("project-igi-mod-config.ini");
    if (!configureModMenu(menuConfig))
        return false;
    igi::log(igi::LogLevel::Debug, menuConfig.enabled ? "internal initialize: config enabled"
                                                      : "internal initialize: config disabled");
    if (menuConfig.screenId == 900 || menuConfig.screenId == 902 || menuConfig.screenId == 903 ||
        menuConfig.screenId == 931 || menuConfig.screenId == 932 || menuConfig.screenId == 934) {
        igi::log(igi::LogLevel::Error,
                 "internal initialize: configured screen_id conflicts with a built-in screen");
        return false;
    }
    if (!rebuildMenuResources())
        return false;
    igi::log(igi::LogLevel::Debug, "internal initialize: embedded menu QVM compiled; resource "
                                   "installation awaits the game thread");
#if defined(_WIN32)
    if (menuConfig.directNativeMode) {
        igi::log(igi::LogLevel::Warning, "internal menu ignores native_mode=direct; UI script "
                                         "calls require the game frame callback");
    }
    igi::log(igi::LogLevel::Info,
             "internal startup stage: installing Display_AddFrameCallback hook");
    if (!runtime.initialize(menuConfig.hook)) {
        igi::log(igi::LogLevel::Error, "internal initialize: runtime initialization failed");
        return false;
    }
    initialAttachPending.store(true, std::memory_order_release);
    igi::log(igi::LogLevel::Info, "internal startup stage: enqueue begin");
    runtime.scheduler().enqueueEveryFrame([] { menuFrameWork(); });
    igi::log(igi::LogLevel::Info, "internal startup stage: enqueue complete");
#else
    runtime.scheduler().bindGameThread();
#endif
    igi::log(igi::LogLevel::Debug, "internal initialize: creating IGI 1 menu manager");
    igi::log(igi::LogLevel::Info, "internal startup stage: menu model begin");
    gameMenu = std::make_unique<igi::IgiMenuManager>(
        igi::IgiMenuManager::create(igi::IgiMenuSet::InGame, menuConfig));
    igi::log(igi::LogLevel::Info, "internal startup stage: menu model complete");
    const bool pushed = gameMenu->pushScreen(menuConfig.screenId);
    igi::log(
        pushed ? igi::LogLevel::Warning : igi::LogLevel::Error,
        pushed
            ? "internal initialize: menu model staged; script callbacks install from the frame tick"
            : "internal initialize: menu model screen selection failed");
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
    igi::log(igi::LogLevel::Info, "internal menu toggle: reopening dismissed screen");
    runtime.scheduler().enqueueOnce([] { (void)reopenMenuScreen(); });
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
    runtime.scheduler().enqueueOnce([] {
        const bool toggled = toggleMenuScreenOnGameThread();
        igi::log(toggled ? igi::LogLevel::Info : igi::LogLevel::Error,
                 toggled ? "internal menu toggle: completed on game frame thread"
                         : "internal menu toggle: rejected on game frame thread");
    });
    return true;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_RequestShutdown() {
#if defined(_WIN32)
    if (!runtime.initialized()) {
        safeToShutdown.store(true, std::memory_order_release);
        return true;
    }
    initialAttachPending.store(false, std::memory_order_release);
    shutdownPending.store(true, std::memory_order_release);
    safeToShutdown.store(false, std::memory_order_release);
    runtime.scheduler().enqueueOnce([] {
        igi::log(igi::LogLevel::Info, "internal unload: game-thread cleanup started");
        const bool wasAttached = menuScreenAttached;
        if (wasAttached && !closeMenuScreen()) {
            igi::log(igi::LogLevel::Error,
                     "internal unload: supported menu close command was rejected");
            shutdownPending.store(false, std::memory_order_release);
            return;
        }
        if (wasAttached)
            return;
        if (!wasAttached)
            (void)igi::resumeFromMenu(runtime);
        completeShutdownOnGameThread();
    });
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

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_CloseMenu() {
#if defined(_WIN32)
    if (!runtime.initialized())
        return false;
    runtime.scheduler().enqueueOnce([] {
        if (menuScreenAttached || isConfiguredMenuScreenActive())
            (void)closeMenuScreen();
    });
    return true;
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_HooksResident() {
#if defined(_WIN32)
    return runtime.hooksResident();
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_CanUnload() {
#if defined(_WIN32)
    return safeToShutdown.load(std::memory_order_acquire) && !runtime.scheduler().isRunning();
#else
    return false;
#endif
}

extern "C" IGI_DEMO_EXPORT void IGI_MenuManager_Internal_Shutdown() {
    igi::log(igi::LogLevel::Debug, "internal shutdown: begin");
    if (!safeToShutdown.load(std::memory_order_acquire)) {
        igi::log(igi::LogLevel::Warning, "internal shutdown deferred: live mod screen retains SDK "
                                         "callbacks; runtime remains resident");
        return;
    }
    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    if (!menuResource.release())
        igi::log(igi::LogLevel::Error,
                 "internal menu resource: release refused because ownership changed");
    reopenResource.release();
    closeResource.release();
    gameMenu.reset();
    if (!runtime.shutdown()) {
        // The game can still call into this module. Keep every resource alive and
        // let the host keep the DLL loaded.
        igi::log(igi::LogLevel::Error,
                 "internal shutdown: hooks still resident; keeping the module loaded");
        return;
    }
    compiledMenu.clear();
    reopenMenu.clear();
    closeMenu.clear();
    menuSource.clear();
    menuScreenCreated = false;
    menuScreenAttached = false;
    initialAttachPending.store(false, std::memory_order_release);
    shutdownPending.store(false, std::memory_order_release);
    safeToShutdown.store(true, std::memory_order_release);
    igi::log(igi::LogLevel::Info, "internal shutdown: complete");
}

namespace igi {
const DllMenuCallbacks &getDllMenuCallbacks() noexcept {
    static const DllMenuCallbacks callbacks{
        &IGI_MenuManager_Internal_Initialize,    &IGI_MenuManager_Internal_Toggle,
        &IGI_MenuManager_Internal_CloseMenu,     &IGI_MenuManager_Internal_RequestShutdown,
        &IGI_MenuManager_Internal_CanUnload,     &IGI_MenuManager_Internal_Shutdown,
        &IGI_MenuManager_Internal_EnsureTick,    &IGI_MenuManager_Internal_TickCount,
        &IGI_MenuManager_Internal_HooksResident,
    };
    return callbacks;
}
} // namespace igi
