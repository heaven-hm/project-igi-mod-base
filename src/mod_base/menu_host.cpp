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
// both write the "Current menuscreen" parameter at +0x20, followed by the popup
// id and the state enum. This task must be looked up below the game root;
// Flow_GetScreen() returns the Flow's Screen task, not the MenuManager.
struct RetailMenuManagerLayout {
    std::byte taskHeaderAndUnknown[0x20];
    std::int32_t currentScreenId;
    std::int32_t popupScreenId;
    std::int32_t state;
    std::int32_t nextState;
};

static_assert(offsetof(RetailMenuManagerLayout, currentScreenId) == 0x20);
static_assert(offsetof(RetailMenuManagerLayout, state) == 0x28);
static_assert(offsetof(RetailMenuManagerLayout, nextState) == 0x2C);
// MenuManager state enum (IGI-RE menumanager.c). IDLE means no retail menu is
// displayed, even though its screen task may still exist from an earlier open.
constexpr std::int32_t kMenuManagerIdleState = 9;
constexpr std::int32_t kMenuManagerFadeOutState = 6;

// A retail screen is only "ours to preserve" while it is open or opening. A
// fade-out to IDLE is the menu system resuming gameplay; treating that as an
// still-open retail menu left the player's input locked after closing the mod.
bool retailMenuShown(const RetailMenuManagerLayout *layout) {
    if (!layout || layout->state == kMenuManagerIdleState)
        return false;
    if (layout->state == kMenuManagerFadeOutState && layout->nextState == kMenuManagerIdleState)
        return false;
    return true;
}

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
    // Final close/return command. Unlike the page-pop command this must resume
    // gameplay or return to the retail screen that was open before the mod menu.
    std::vector<std::uint8_t> leaveMenu;
    std::string menuSource;
    std::unique_ptr<igi::MenuScriptBridge> menuBridge;
    std::unique_ptr<igi::MenuScriptCallbacks> menuCallbacks;
    igi::MenuScriptRegistration menuRegistration;
    igi::MenuResource menuResource;
    igi::MenuResource reopenResource;
    igi::MenuResource closeResource;
    igi::MenuResource leaveResource;
    std::atomic<std::uint64_t> frameCount{0};
    std::uint64_t lastLiveRefreshTick = 0;
    std::atomic<std::int32_t> observedScreen{0};
    std::atomic<bool> safeToShutdown{true};
    std::atomic<bool> shutdownPending{false};
    // One game-thread cleanup per session. The deferred-close path and the
    // queued RequestShutdown task can both reach the cleanup; running it twice
    // released the QVM resources and deleted the screen task twice.
    std::atomic<bool> shutdownCompleted{false};
    bool menuScreenCreated = false;
    bool menuScreenAttached = false;
    // Id of the one MenuScreen task this instance created. Cleanup must never
    // delete a task at this address that we did not create: the configured id
    // range can already be occupied by the retail pause menu.
    std::int32_t createdScreenId = -1;
    MenuTransition pendingTransition = MenuTransition::None;
    // Page navigation closes the current page screen and re-attaches with the
    // next page, so only one MenuScreen task ever exists in the retail manager.
    int pendingPageDelta = 0;
    bool pageSwitchInFlight = false;
    // The retail pause menu can be pushed on top of the injected screen. It must
    // not be popped or deleted from under the game, so the close waits for the
    // game to leave that screen first.
    bool closeDeferredBehindRetailScreen = false;
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
auto &leaveMenu = g_menu.leaveMenu;
auto &leaveResource = g_menu.leaveResource;
auto &frameCount = g_menu.frameCount;
auto &lastLiveRefreshTick = g_menu.lastLiveRefreshTick;
auto &safeToShutdown = g_menu.safeToShutdown;
auto &shutdownPending = g_menu.shutdownPending;
auto &shutdownCompleted = g_menu.shutdownCompleted;
auto &menuScreenCreated = g_menu.menuScreenCreated;
auto &menuScreenAttached = g_menu.menuScreenAttached;
auto &pendingTransition = g_menu.pendingTransition;
auto &createdScreenId = g_menu.createdScreenId;
auto &pendingPageDelta = g_menu.pendingPageDelta;
auto &pageSwitchInFlight = g_menu.pageSwitchInFlight;
auto &closeDeferredBehindRetailScreen = g_menu.closeDeferredBehindRetailScreen;
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
    // A level change deletes the Game task before the next frame arrives; every
    // per-frame helper dereferences it, so nothing mod-side may run until the
    // new level's Game task exists.
    if (!IGI::GAME::GET()) {
        if (menuConfig.frameUpdate)
            menuConfig.frameUpdate(runtime); // fly state machine is inert here
        return;
    }
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
    if (!leaveResource.active() &&
        !leaveResource.registerData("LOCAL:mod_base/modmenu-leave.qvm", leaveMenu, resources))
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
            if (delta == 0 || pendingTransition != MenuTransition::None)
                return;
            // A page row is a one-shot click, but rebuilding the page happens on
            // the game frame after the retail pop. The page switch used to be
            // deferred until the menu closed, which turned every later close
            // into a hidden page change instead of a close.
            pendingPageDelta = delta;
            pageSwitchInFlight = true;
            igi::log(igi::LogLevel::Info,
                     "internal menu page: requested; switching after the retail screen pop");
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

// The live MenuManager task. FLOW_GET_SCREEN returns the Flow's Screen task, not
// the MenuManager, so reading the current screen id from it produced bogus ids
// and made the host believe the mod screen was gone while it was still shown.
IGI::Task *liveMenuManager() {
    auto *game = IGI::GAME::GET();
    if (!game)
        return nullptr;
    auto *root =
        static_cast<IGI::Task *>(static_cast<GameRootLayout *>(static_cast<void *>(game))->root);
    if (!root)
        return nullptr;
    return IGI::TASK::FIND_CHILD(root, IGI::MENU::GET_TYPE());
}

// A large control set is emitted as several page screens sharing the reserved id
// range, so any of those ids counts as "our menu is on screen".
bool isConfiguredMenuScreenActive() {
    auto *manager = liveMenuManager();
    if (!manager)
        return false;
    const auto currentScreen =
        reinterpret_cast<const RetailMenuManagerLayout *>(manager)->currentScreenId;
    const auto pages = igi::menuPageCount(menuConfig);
    for (std::size_t page = 0; page < pages; ++page) {
        const int id = igi::menuPageScreenId(menuConfig, page);
        if (currentScreen == id && IGI::TASK::FIND_BY_ID(id) != nullptr)
            return true;
    }
    return false;
}

// True while the retail manager is running any of its own screens. IDLE means
// no menu is displayed (even if an old screen task still exists).
bool retailMenuActive() {
    auto *manager = liveMenuManager();
    return manager && reinterpret_cast<const RetailMenuManagerLayout *>(manager)->state !=
                          kMenuManagerIdleState;
}

// True when the retail manager is showing a screen of its own (the in-game pause
// menu) rather than the injected one.
bool anyRetailScreenActive() {
    auto *manager = liveMenuManager();
    if (!manager)
        return false;
    const auto *layout = reinterpret_cast<const RetailMenuManagerLayout *>(manager);
    const auto currentScreen = layout->currentScreenId;
    if (currentScreen <= 0 || !retailMenuShown(layout))
        return false;
    const auto pages = igi::menuPageCount(menuConfig);
    for (std::size_t page = 0; page < pages; ++page) {
        if (currentScreen == igi::menuPageScreenId(menuConfig, page))
            return false;
    }
    return IGI::TASK::FIND_BY_ID(currentScreen) != nullptr;
}

// Removes the injected MenuScreen task while the retail MenuManager is still
// alive. The retail pop only takes the screen off the stack, so a leftover child
// would keep sitting in front of the game's own screens (the pause menu would
// open behind a dead screen). Must run before the QVM resource is released.
void releaseInjectedMenuScreen() {
#if defined(_WIN32)
    // Only delete the screen this instance created. The configured id range can
    // already hold the retail in-game (pause) menu, and deleting every task at
    // those ids used to eject that menu as soon as the mod attached.
    if (!menuScreenCreated || createdScreenId < 0) {
        createdScreenId = -1;
        return;
    }
    auto *screen = IGI::TASK::FIND_BY_ID(createdScreenId);
    if (screen) {
        igi::log(igi::LogLevel::Info, "internal unload: deleting injected menu screen task id " +
                                          std::to_string(createdScreenId));
        IGI::TASK::DELETE_TASK(screen);
    } else {
        igi::log(igi::LogLevel::Info,
                 "internal unload: injected menu screen already detached from id " +
                     std::to_string(createdScreenId));
    }
    createdScreenId = -1;
#else
    (void)0;
#endif
}

bool rebuildMenuResources();

void completeShutdownOnGameThread() {
    if (shutdownCompleted.exchange(true, std::memory_order_acq_rel))
        return;
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
    leaveResource.release();
    menuScreenCreated = false;
    menuScreenAttached = false;
    pendingTransition = MenuTransition::None;
    pageSwitchInFlight = false;
    closeDeferredBehindRetailScreen = false;
    pendingPageDelta = 0;
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
    leaveResource.release();
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
    if (pageSwitchInFlight && pendingPageDelta != 0) {
        pageSwitchInFlight = false;
        if (menuBridge)
            menuBridge->cancel(false);
        if (startPageSwitch())
            return;
    }
    // A plain close must never turn into a page change: drop any stale delta.
    pendingPageDelta = 0;
    pageSwitchInFlight = false;
    // The retail pop only takes the screen off the stack; the task and the
    // created flag must go too, or the next start-key press takes the fragile
    // reopen path (request a screen that was popped, miss the transition, then
    // attach anyway - the "menu opens twice" report).
    releaseInjectedMenuScreen();
    menuScreenCreated = false;
    closeDeferredBehindRetailScreen = false;
    if (menuBridge)
        menuBridge->cancel(false);
    if (anyRetailScreenActive()) {
        // Popping our screen exposed the game's own pause menu again. That menu
        // owns the pause and input lock now; resuming here would close it from
        // under the player.
        igi::abandonMenuPause();
        igi::log(igi::LogLevel::Info,
                 "internal menu close: game's own screen is current; left its pause state "
                 "untouched");
    } else if (!igi::resumeFromMenu(runtime)) {
        igi::log(igi::LogLevel::Warning, "internal menu close: unable to resume game");
    }
    if (shutdownPending.load(std::memory_order_acquire)) {
        completeShutdownOnGameThread();
        return;
    }
    safeToShutdown.store(true, std::memory_order_release);
    igi::log(igi::LogLevel::Info,
             "internal menu close: retail screen transition confirmed; game resumed");
}

bool closeMenuScreen();

// Finishes a close that was deferred because the game's own pause menu was on
// top of the injected screen. Runs only on the game frame.
void processDeferredClose() {
    if (!closeDeferredBehindRetailScreen || pendingTransition != MenuTransition::None)
        return;
    if (anyRetailScreenActive())
        return;
    closeDeferredBehindRetailScreen = false;
    if (isConfiguredMenuScreenActive()) {
        // The game popped back to our screen, so exactly one retail pop removes it.
        if (!runMenuCommand("LOCAL:mod_base/modmenu-leave.qvm", "deferred close")) {
            pageSwitchInFlight = false;
            pendingPageDelta = 0;
            safeToShutdown.store(true, std::memory_order_release);
            igi::log(igi::LogLevel::Warning,
                     "internal menu close: deferred pop was rejected; screen left in place");
            return;
        }
        pendingTransition = MenuTransition::Closing;
        transitionDeadlineFrame = runtime.scheduler().gameHookInvocationCount() + 120;
        safeToShutdown.store(false, std::memory_order_release);
        igi::log(igi::LogLevel::Info,
                 "internal menu close: game's own screen closed; popping the injected screen now");
        return;
    }
    // The retail stack no longer references our screen: delete our task and keep
    // the pause the game's menu established.
    releaseInjectedMenuScreen();
    menuScreenCreated = false;
    menuScreenAttached = false;
    (void)igi::resumeFromMenu(runtime);
    if (shutdownPending.load(std::memory_order_acquire)) {
        completeShutdownOnGameThread();
        return;
    }
    safeToShutdown.store(true, std::memory_order_release);
    igi::log(igi::LogLevel::Info,
             "internal menu close: deferred close completed after the game's screen closed");
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
        if (shutdownPending.load(std::memory_order_acquire)) {
            // A shutdown request arrived while the reopen transition ran; the
            // queued unload callback already returned, so close again from here.
            igi::log(igi::LogLevel::Info,
                     "internal menu reopen: shutdown is pending; closing the reopened screen");
            (void)closeMenuScreen();
            return;
        }
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
    (void)igi::resumeFromMenu(runtime);
    igi::log(igi::LogLevel::Warning,
             "internal menu reopen: retail screen did not transition; forcing a fresh attach");
    // RequestScreen may have created the page task without ever showing it. It
    // would keep its id occupied and make every later attach relocate, so delete
    // it here unless the retail manager is pointing at it.
    const int requested = igi::menuPageScreenId(menuConfig, menuConfig.pageIndex);
    auto *manager = liveMenuManager();
    const int currentScreen =
        manager ? reinterpret_cast<const RetailMenuManagerLayout *>(manager)->currentScreenId : 0;
    if (auto *stale = IGI::TASK::FIND_BY_ID(requested); stale && currentScreen != requested) {
        igi::log(igi::LogLevel::Info, "internal menu reopen: deleting un-attached page task id " +
                                          std::to_string(requested));
        IGI::TASK::DELETE_TASK(stale);
    }
    releaseInjectedMenuScreen();
    menuScreenCreated = false;
    // The retail manager can be left pointing at a screen task that no longer
    // exists. Detach everything and let the frame tick build the screen again,
    // which also clears any wedged state instead of leaving the game stuck.
    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    menuResource.release();
    reopenResource.release();
    closeResource.release();
    leaveResource.release();
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

// Exactly one MenuScreen task is attached at a time, including after a page
// switch. Popping once removes our screen; popping once per configured page
// walked down the retail menu stack and closed the game's own pause menu.
std::string closeCommandScript() {
    return "MenuManager_PopScreen(FALSE);";
}

// Final close command. The retail in-game menu's Resume is
// MenuManager_LeaveMenus(900, MENUMANAGER_IDLE); RequestScreen replaces the
// current screen without pushing, so PopScreen would pop an empty stack and
// corrupt the manager. When the retail pause menu was already open beneath us,
// go back to that screen instead of resuming the game out from under it.
std::string leaveCommandScript() {
    auto *manager = liveMenuManager();
    if (manager) {
        const auto *layout = reinterpret_cast<const RetailMenuManagerLayout *>(manager);
        const int screen = layout->currentScreenId;
        const auto pages = igi::menuPageCount(menuConfig);
        bool isOurScreen = false;
        for (std::size_t page = 0; page < pages; ++page) {
            isOurScreen = isOurScreen || screen == igi::menuPageScreenId(menuConfig, page);
        }
        if (!isOurScreen && retailMenuShown(layout) && screen > 0 &&
            IGI::TASK::FIND_BY_ID(screen) != nullptr) {
            return "MenuManager_RequestScreen(" + std::to_string(screen) + ", FALSE);";
        }
    }
    return "MenuManager_LeaveMenus(900, 9);";
}

bool rebuildMenuResources() {
    std::vector<std::uint8_t> screen;
    std::vector<std::uint8_t> reopen;
    std::vector<std::uint8_t> close;
    std::vector<std::uint8_t> leave;
    std::string error;
    if (!igi::compileIgiMenuScreenScript(menuConfig, screen, error) ||
        !igi::compileIgiMenuCommandScript(
            "MenuManager_RequestScreen(" +
                std::to_string(igi::menuPageScreenId(menuConfig, menuConfig.pageIndex)) +
                ", FALSE);",
            reopen, error) ||
        !igi::compileIgiMenuCommandScript(closeCommandScript(), close, error) ||
        !igi::compileIgiMenuCommandScript(leaveCommandScript(), leave, error)) {
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
    leaveMenu = std::move(leave);
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
    // Our own leftover page screens must not push the base id around: the retail
    // manager keeps pointing at the id the running script was built with, so
    // migrating the base wedges it. Remove them and re-check before relocating.
    releaseInjectedMenuScreen();
    if (isTaskIdRangeFree(menuConfig.screenId, igi::menuScreenIdSpan(menuConfig))) {
        igi::log(igi::LogLevel::Info,
                 "internal menu screen: id range freed by removing the previous page screens");
        return true;
    }
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
        createdScreenId < 0 || !menuConfig.refreshLiveItems)
        return;
    const auto tick = runtime.scheduler().gameHookInvocationCount();
    if (tick - lastLiveRefreshTick < kLiveRefreshTicks)
        return;
    lastLiveRefreshTick = tick;
    if (!menuConfig.refreshLiveItems(menuConfig, runtime))
        return;
    if (!menuBridge->resync(menuConfig))
        return;
    if (auto *screen = IGI::TASK::FIND_BY_ID(createdScreenId)) {
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
    processDeferredClose();
    if (pageSwitchInFlight && pendingTransition == MenuTransition::None && menuScreenAttached) {
        // The page row only queued the request. Replace the page task here on the
        // game frame: the old page is deleted, resources are rebuilt for the new
        // page, and the attach below runs in the same tick. Popping the retail
        // manager was wrong because RequestScreen does not push a stack entry.
        pageSwitchInFlight = false;
        if (!startPageSwitch()) {
            pendingPageDelta = 0;
            igi::log(igi::LogLevel::Warning, "internal menu page switch: request dropped");
        }
    }
    refreshAttachedLiveItems();
    if (closeDeferredBehindRetailScreen)
        return;
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
    if (IGI::GAME::IS_PAUSED() && !igi::menuPauseActive() && !retailMenuActive()) {
        // Our own page switch keeps the pause we already own. The retail pause
        // menu is a valid parent screen for the mod menu; a level load or
        // cutscene pause is not.
        igi::log(igi::LogLevel::Debug,
                 "internal menu screen: game is paused; waiting for active gameplay");
        return;
    }
    if (menuConfig.refreshLiveItems)
        (void)menuConfig.refreshLiveItems(menuConfig, runtime);
    // Rebuild on every attach so the leave command reflects whether the retail
    // pause menu is currently open underneath the mod screen.
    if (!rebuildMenuResources()) {
        initialAttachPending.store(true, std::memory_order_release);
        return;
    }
    // One attach per open request: the toggle path, the every-frame tick and the
    // RunFrame export can all reach this block in the same game frame. The old
    // plain load let two of them build a screen, which is the reported double
    // open on a single F5 press.
    if (!initialAttachPending.exchange(false, std::memory_order_acq_rel))
        return;
    if (!selectFreeMenuScreenId()) {
        initialAttachPending.store(true, std::memory_order_release);
        return;
    }
    igi::log(igi::LogLevel::Info,
             "internal menu frame: active game confirmed; installing script callbacks");
    if (!installMenuScriptCallbacks()) {
        initialAttachPending.store(true, std::memory_order_release);
        return;
    }
    igi::log(igi::LogLevel::Info, "internal menu frame: script callbacks installed");
    auto *root =
        static_cast<IGI::Task *>(static_cast<GameRootLayout *>(static_cast<void *>(game))->root);
    if (!root) {
        igi::log(igi::LogLevel::Error, "internal menu screen: game root task unavailable");
        initialAttachPending.store(true, std::memory_order_release);
        return;
    }
    const auto managerType = IGI::MENU::GET_TYPE();
    auto *manager = IGI::TASK::FIND_CHILD(root, managerType);
    if (!manager) {
        igi::log(igi::LogLevel::Warning,
                 "internal menu screen: game MenuManager unavailable; waiting");
        initialAttachPending.store(true, std::memory_order_release);
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
        initialAttachPending.store(true, std::memory_order_release);
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
    // Only ever track the task at the id the page script creates. RUN_SCRIPT can
    // return the parent task on some paths, and deleting that would take the
    // retail MenuManager down with it.
    createdScreenId = IGI::TASK::FIND_BY_ID(requestedScreenId) ? requestedScreenId : -1;
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
    if (!isConfiguredMenuScreenActive()) {
        pendingTransition = MenuTransition::None;
        pageSwitchInFlight = false;
        pendingPageDelta = 0;
        menuScreenAttached = false;
        if (anyRetailScreenActive()) {
            // The game's own pause menu is on top of our screen. Popping would
            // close that menu and deleting our task would leave the retail stack
            // pointing at a dead screen, so wait for the game to pop back to us.
            closeDeferredBehindRetailScreen = true;
            safeToShutdown.store(false, std::memory_order_release);
            igi::abandonMenuPause();
            igi::log(igi::LogLevel::Info,
                     "internal menu close: deferred while the game's own screen is open");
            return true;
        }
        releaseInjectedMenuScreen();
        menuScreenCreated = false;
        (void)igi::resumeFromMenu(runtime);
        safeToShutdown.store(true, std::memory_order_release);
        igi::log(igi::LogLevel::Info,
                 "internal menu close: no injected screen is active; cleaned up without "
                 "touching the game's screens");
        return true;
    }
    if (!runMenuCommand("LOCAL:mod_base/modmenu-leave.qvm", "close"))
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
    releaseInjectedMenuScreen();
    menuScreenCreated = false;
    menuScreenAttached = false;
    initialAttachPending.store(true, std::memory_order_release);
    menuFrameWork();
    return menuScreenAttached;
}

bool toggleMenuScreenOnGameThread() {
    if (closeDeferredBehindRetailScreen) {
        igi::log(igi::LogLevel::Info,
                 "internal menu toggle: waiting for the game's own screen to close first");
        return false;
    }
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
#if !defined(_WIN32)
bool closeMenuScreen() {
    return false;
}
#endif

} // namespace

namespace igi {
bool prepareMenuForLevelChange() {
#if defined(_WIN32)
    if (!runtime.initialized())
        return true;

    initialAttachPending.store(false, std::memory_order_release);
    // The Game task is torn down immediately after this call, so no close/reopen
    // transition may keep running against it in the next frame.
    pendingTransition = MenuTransition::None;
    pendingPageDelta = 0;
    pageSwitchInFlight = false;
    closeDeferredBehindRetailScreen = false;
    transitionDeadlineFrame = 0;
#if defined(_WIN32)
    unregisterEndFrameCallback();
#endif
    releaseInjectedMenuScreen();
    if (menuBridge)
        menuBridge->cancel(false);
    else if (!resumeFromMenu(runtime))
        log(LogLevel::Warning, "level change: unable to resume game before teardown");
    // Keep the transition state clear; cancel(false) queues no close callback.
    pendingTransition = MenuTransition::None;

    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    if (!menuResource.release())
        log(LogLevel::Error, "level change: menu resource release failed");
    if (!reopenResource.release())
        log(LogLevel::Error, "level change: reopen resource release failed");
    if (!closeResource.release())
        log(LogLevel::Error, "level change: close resource release failed");
    if (!leaveResource.release())
        log(igi::LogLevel::Error, "level change: leave resource release failed");
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
    shutdownCompleted.store(false, std::memory_order_release);
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
    // Do not stage an automatic attach here: the start key that initialized the
    // DLL is the same press that should open the menu exactly once. The caller
    // toggles after a successful initialize.
    initialAttachPending.store(false, std::memory_order_release);
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
    // The Display_AddFrameCallback hook already runs the every-frame task, which
    // calls menuFrameWork(). Calling it again here made one frame perform two
    // attach attempts (the second F5 open) when a host drove both paths.
    const auto ran = runtime.scheduler().runPending();
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
    // One shutdown request per session. Re-queueing while the host polls
    // canUnload ran completeShutdownOnGameThread twice, which freed resources
    // and deleted the injected screen twice (the eject crash).
    if (shutdownPending.exchange(true, std::memory_order_acq_rel))
        return true;
    safeToShutdown.store(false, std::memory_order_release);
    runtime.scheduler().enqueueOnce([] {
        igi::log(igi::LogLevel::Info, "internal unload: game-thread cleanup started");
        if (closeDeferredBehindRetailScreen || pendingTransition != MenuTransition::None) {
            // finishCloseAndShutdown runs from the deferred-close path once the
            // retail stack is back on our screen.
            igi::log(igi::LogLevel::Info,
                     "internal unload: waiting for the in-flight screen transition to finish");
            return;
        }
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
    // Never tear the module down while a screen transition, the injected screen,
    // or the game's own pause menu still relies on it. Releasing the QVM or the
    // MenuManager callbacks there is what left the retail pause menu wedged.
    if (!safeToShutdown.load(std::memory_order_acquire) || menuScreenAttached ||
        pendingTransition != MenuTransition::None || closeDeferredBehindRetailScreen ||
        isConfiguredMenuScreenActive()) {
        safeToShutdown.store(false, std::memory_order_release);
        igi::log(igi::LogLevel::Warning,
                 "internal shutdown deferred: a live mod screen transition still retains the "
                 "SDK; runtime remains resident");
        return;
    }
    // A detached leftover page task is ours and safe to remove before the QVM
    // resource goes away.
    releaseInjectedMenuScreen();
    menuRegistration.stop();
    menuCallbacks.reset();
    menuBridge.reset();
    if (!menuResource.release())
        igi::log(igi::LogLevel::Error,
                 "internal menu resource: release refused because ownership changed");
    reopenResource.release();
    closeResource.release();
    leaveResource.release();
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
    leaveMenu.clear();
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
