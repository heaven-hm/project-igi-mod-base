#include "igi_sdk/runtime/scheduler.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/native_catalogue.hpp"
#include "igi_sdk/natives/native_runner.hpp"
#include "igi_sdk/natives/native_caller.hpp"
#include "igi_sdk/runtime/hook_manager.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/runtime/scheduler.hpp"
#include "igi_sdk/ui/menu_manager.hpp"
#include "igi_sdk/ui/igi1_menu_manager.hpp"
#include "igi_sdk/ui/menu_script.hpp"
#include "igi_sdk/ui/menu_values.hpp"
#include "igi_sdk/ui/menu_script_bridge.hpp"
#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/natives/native_catalogue.hpp"

#include <cassert>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <stdexcept>

void menuManagerModelTest();

namespace {
int scriptItemsAdded = 0;
int scriptRegistrations = 0;
int scriptRemovals = 0;
bool scriptSymbolExists = false;
int fakeScriptGetInt(void* args, int index) { return static_cast<int*>(args)[index]; }
void fakeScriptAddItem(const char*, int id) { assert(id == scriptItemsAdded++); }
void fakeRegister(const char*, igi::ScriptCallback callback, void* context, int argc) {
    assert(callback && context && (argc == 1 || argc == 2));
    ++scriptRegistrations;
}
void fakeRemove(const char*) { ++scriptRemovals; }
bool fakeContains(const char*) { return scriptSymbolExists; }
bool fakeMatches(const char*, igi::ScriptCallback, void*) { return scriptSymbolExists; }
igi::IgiScriptValue* fakeCallback(igi::IgiScriptValue* output, void*, void*) { return output; }
}

int main() {
    assert(igi::MenuConfig{}.hook == igi::igi1::TickHook::GameRunHandler);
    igi::Runtime runtime;
    assert(!runtime.initialize(igi::igi1::TickHook::TextDraw));
    assert(!runtime.initialize(igi::igi1::TickHook::ScreenDraw));
    assert(!runtime.initialize(igi::igi1::TickHook::DisplayEndFrame));
    assert(!runtime.initialize(igi::igi1::TickHook::DisplayAddFrameCallback));
    igi::Scheduler hookSelectionScheduler;
    igi::HookManager hookSelection(hookSelectionScheduler);
    assert(!hookSelection.installDisplayAddFrameCallbackTick());
    assert(!hookSelection.installTextDrawHook());
    assert(!hookSelection.installDisplayEndFrameHook());
    igi::Scheduler scheduler;
    scheduler.bindGameThread();
    std::istringstream callerRecords(
        "00402820\tTestNative\ttest\tentry\tvoid TestNative(int value)\n");
    igi::NativeCatalogue callerCatalogue;
    assert(callerCatalogue.load(callerRecords));
    igi::NativeCaller unvalidatedCaller(callerCatalogue, scheduler, false);
    std::uintptr_t resolvedAddress = 0;
    assert(unvalidatedCaller.resolve("TestNative", "void TestNative(int value)", resolvedAddress) ==
           igi::NativeCallStatus::NotTargetBuild);
    igi::NativeCaller validatedCaller(callerCatalogue, scheduler, true);
    assert(validatedCaller.resolve("TestNative", "void TestNative(void)", resolvedAddress) ==
           igi::NativeCallStatus::SignatureMismatch);
    assert(validatedCaller.resolve("Missing", "void Missing(void)", resolvedAddress) ==
           igi::NativeCallStatus::MissingNative);
    assert(validatedCaller.resolve("TestNative", "void TestNative(int value)", resolvedAddress) ==
           igi::NativeCallStatus::UnverifiedNative);
    assert(callerCatalogue.markVerified("TestNative", igi::NativeVerification::Exposed));
    assert(validatedCaller.resolve("TestNative", "void TestNative(int value)", resolvedAddress) ==
           igi::NativeCallStatus::Success && resolvedAddress == 0x00402820);
    int queuedNativeCalls = 0;
    assert(validatedCaller.queue("TestNative", [&] { ++queuedNativeCalls; }) != 0);
    assert(scheduler.runPending() == 1 && queuedNativeCalls == 1);
    int count = 0;
    scheduler.enqueueOnce([&] { ++count; });
    assert(scheduler.runPending() == 1 && count == 1);
    assert(scheduler.runPending() == 0 && count == 1);
    igi::Scheduler hookBoundScheduler;
    int hookCount = 0;
    hookBoundScheduler.enqueueOnce([&] { ++hookCount; });
    assert(hookBoundScheduler.runPending() == 0 && hookCount == 0);
    assert(hookBoundScheduler.runPendingFromGameHook() == 1 && hookCount == 1);
    assert(hookBoundScheduler.gameHookInvocationCount() == 1);
    hookBoundScheduler.reset();
    assert(hookBoundScheduler.gameHookInvocationCount() == 0);
    assert(hookBoundScheduler.runPendingFromGameHook() == 0);
    const auto repeating = scheduler.enqueueEveryFrame([&] { ++count; });
    assert(scheduler.runPending() == 1 && count == 2);
    assert(scheduler.cancel(repeating));
    assert(!scheduler.cancel(repeating));
    assert(scheduler.runPending() == 0 && count == 2);
    const auto delayed = scheduler.enqueueDelayed(std::chrono::milliseconds(10), [&] { ++count; });
    assert(scheduler.runPending() == 0);
    int recursiveRuns = 0;
    scheduler.enqueueOnce([&] { ++recursiveRuns; assert(scheduler.runPending() == 0); });
    assert(scheduler.runPending() == 1 && recursiveRuns == 1);
    // The delayed task is still pending; cancel it so later drains are deterministic.
    assert(scheduler.cancel(delayed));
    igi::Scheduler cancellationScheduler;
    cancellationScheduler.bindGameThread();
    int afterClear = 0;
    cancellationScheduler.enqueueOnce([&] { ++afterClear; cancellationScheduler.clear(); });
    cancellationScheduler.enqueueOnce([&] { ++afterClear; cancellationScheduler.clear(); });
    assert(cancellationScheduler.runPending() == 1);
    assert(afterClear == 1);
    igi::Scheduler threadScheduler;
    int threadTaskRuns = 0;
    threadScheduler.enqueueOnce([&] { ++threadTaskRuns; });
    threadScheduler.bindGameThread();
    std::thread otherThread([&] { assert(threadScheduler.runPending() == 0); });
    otherThread.join();
    assert(threadTaskRuns == 0);
    assert(threadScheduler.runPending() == 1 && threadTaskRuns == 1);
    scheduler.enqueueOnce([] { throw std::runtime_error("test exception"); });
    assert(scheduler.runPending() == 1);
    assert(scheduler.runPending() == 0);
    std::ifstream catalogueFile(std::string(IGI_SDK_SOURCE_DIR) + "/data/IGI1/neonix-igi-re-signatures.tsv");
    igi::NativeCatalogue catalogue;
    assert(catalogue.load(catalogueFile));
    assert(catalogue.size() == 7068);
    assert(catalogue.count(igi::NativeDomain::AI) > 0);
    assert(catalogue.count(igi::NativeDomain::Soldiers) > 0);
    assert(catalogue.count(igi::NativeDomain::Camera) > 0);
    assert(catalogue.count(igi::NativeDomain::Graphs) > 0);
    assert(catalogue.count(igi::NativeDomain::Memory) > 0);
    bool resolved = false;
    igi::NativeRunner runner(catalogue, scheduler);
    assert(runner.queueResolution("Game_Start", [&](const igi::NativeRecord& record) {
        resolved = record.address == 0x00401000;
    }) != 0);
    assert(scheduler.runPending() == 1 && resolved);
    auto mainMenu = igi::Igi1MenuManager::create(igi::Igi1MenuSet::Main);
    assert(mainMenu.currentScreen() != nullptr);
    assert(mainMenu.currentScreen()->id == 900);
    assert(mainMenu.currentScreen()->title == "Main Menu");
    assert(mainMenu.pushScreen(904));
    assert(mainMenu.currentScreen()->title == "Configuration");
    assert(mainMenu.popScreen(true));
    assert(mainMenu.currentScreen()->id == 900);
    // The mod menu is data-driven: its widgets come from the shipped config.ini.
    const auto modConfig = igi::loadMenuConfig(std::string(IGI_SDK_SOURCE_DIR) + "/demos/internal-menumanager/config.ini");
    assert(modConfig.hook == igi::igi1::TickHook::GameRunHandler);
    if (modConfig.items.empty())
        std::fprintf(stderr, "menu config did not load from %s/demos/internal-menumanager/config.ini\n", IGI_SDK_SOURCE_DIR);
    assert(modConfig.items.size() == 8);
    igi::MenuValues menuValues(modConfig);
    assert(menuValues.choiceCount(0) == 3);
    assert(menuValues.choice(0, 1) == "snow");
    assert(menuValues.select(0, 2));
    assert(menuValues.selected(0) == 2);
    assert(!menuValues.select(0, 3));
    assert(!menuValues.selected(1));
    std::vector<std::string> filledChoices;
    assert(menuValues.fill(0, [&](std::string_view value, int id) {
        assert(static_cast<std::size_t>(id) == filledChoices.size());
        filledChoices.emplace_back(value);
    }));
    assert((filledChoices == std::vector<std::string>{"rain", "snow", "clear"}));
    assert(!menuValues.fill(1, [](std::string_view, int) {}));
    assert(!menuValues.fill(0, {}));
    igi::MenuScriptBridge listBridge(modConfig);
    int populated = 0;
    assert(listBridge.fillList(0, [&](std::string_view, int id) { assert(id == populated++); }));
    assert(populated == 3 && listBridge.getList(0) == 0);
    assert(listBridge.setList(0, 1) && listBridge.getList(0) == 1);
    assert(!listBridge.setList(0, -1) && !listBridge.setList(-1, 0));
    assert(listBridge.getList(-1) == -1 && !listBridge.fillList(-1, {}));
    igi::MenuScriptCallbacks callbacks(listBridge, {fakeScriptGetInt, fakeScriptAddItem});
    igi::IgiScriptSymbol symbol{1, 1, 0, 1, fakeCallback, "test", &callbacks};
    assert(igi::menuScriptSymbolMatches(&symbol, fakeCallback, &callbacks));
    assert(!igi::menuScriptSymbolMatches(&symbol, fakeCallback, nullptr));
    symbol.kind = 0;
    assert(!igi::menuScriptSymbolMatches(&symbol, fakeCallback, &callbacks));
    igi::IgiScriptValue scriptResult{};
    int scriptArgs[] = {0, 2};
    scriptItemsAdded = 0;
    assert(callbacks.fill(&scriptResult, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 1 && scriptItemsAdded == 3);
    assert(callbacks.set(&scriptResult, scriptArgs)->integer == 1);
    assert(callbacks.get(&scriptResult, scriptArgs)->integer == 2);
    scriptItemsAdded = 0;
    assert(callbacks.fill(nullptr, scriptArgs) == nullptr && scriptItemsAdded == 0);
    assert(!igi::registerMenuScriptCallbacks(callbacks, {}));
    igi::ScriptRegistryApi registry{fakeRegister, fakeRegister, fakeRemove, fakeContains, fakeMatches};
    scriptSymbolExists = true;
    assert(!igi::registerMenuScriptCallbacks(callbacks, registry) && scriptRegistrations == 0);
    scriptSymbolExists = false;
    assert(igi::registerMenuScriptCallbacks(callbacks, registry) && scriptRegistrations == 3);
    scriptSymbolExists = true;
    igi::unregisterMenuScriptCallbacks(callbacks, registry);
    assert(scriptRemovals == 3);
    igi::MenuScriptRegistration registration;
    scriptSymbolExists = false;
    assert(registration.start(callbacks, registry));
    assert(registration.active() && !registration.start(callbacks, registry));
    scriptSymbolExists = true;
    registration.stop();
    registration.stop();
    assert(!registration.active() && scriptRemovals == 6);
    scriptArgs[1] = -1;
    assert(callbacks.set(&scriptResult, scriptArgs)->integer == 0);
    std::ostringstream menuScript;
    assert(igi::writeIgi1MenuScript(menuScript, modConfig));
    assert(menuScript.str().find("Task_New(990, \"MenuScreen\"") != std::string::npos);
    assert(menuScript.str().find("Give All Weapons") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_SetWeather") == std::string::npos);
    assert(menuScript.str().find("Task_New(-1, \"ToggleBox\"") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_FillList(0)") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_SetList(0,this.nActiveID)") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_GetList(0)\", \"\", \"IGI_SDK_SetList(0,this.nActiveID)") != std::string::npos);
    std::vector<std::uint8_t> compiledMenu;
    std::string compileError;
    assert(igi::compileIgi1MenuScript(modConfig, compiledMenu, compileError));
    assert(!compiledMenu.empty());
    assert(!igi::compileIgi1MenuScript(igi::MenuConfig{}, compiledMenu, compileError));
    assert(compiledMenu.empty() && !compileError.empty());
    auto oversizedChoices = modConfig;
    oversizedChoices.items[0].values.assign(65, "item");
    assert(!igi::compileIgi1MenuScript(oversizedChoices, compiledMenu, compileError));
    oversizedChoices.items[0].values.assign(1, std::string(40, 'x'));
    assert(!igi::compileIgi1MenuScript(oversizedChoices, compiledMenu, compileError));
    auto modMenu = igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame, modConfig);
    assert(modMenu.pushScreen(modConfig.screenId));
    assert(modMenu.currentScreen()->widgets.size() == modConfig.items.size());
    assert(modMenu.currentScreen()->widgets[0].label == "Weather");
    assert(modMenu.currentScreen()->widgets[0].kind == igi::Igi1WidgetKind::ListBox);
    assert(modMenu.currentScreen()->widgets[3].label == "Free Camera");
    assert(modMenu.currentScreen()->widgets[3].kind == igi::Igi1WidgetKind::ToggleBox);
    auto alternateConfig = modConfig;
    alternateConfig.screenId = 991;
    auto alternateMenu = igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame, alternateConfig);
    assert(alternateMenu.pushScreen(991));
    assert(alternateMenu.currentScreen()->title == "IGI SDK Mod Menu");
    alternateConfig.screenId = 902;
    auto conflictingMenu = igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame, alternateConfig);
    assert(conflictingMenu.pushScreen(902));
    assert(conflictingMenu.currentScreen()->title == "Sound Configuration");
    const auto* gameRunHandler = catalogue.findByAddress(0x00416260);
    assert(gameRunHandler != nullptr);
    assert(gameRunHandler->name == "Game_RunHandler");
    assert(gameRunHandler->signature == "void Game_RunHandler(Game *g)");
    assert(gameRunHandler->verification == igi::NativeVerification::Catalogued);
    std::istringstream duplicateRecords(
        "00400000\tFirst\tmanual\tentry\tvoid First(void)\n"
        "00400000\tSecond\tmanual\tentry\tvoid Second(void)\n");
    igi::NativeCatalogue duplicateCatalogue;
    assert(!duplicateCatalogue.load(duplicateRecords));
    std::istringstream aliasedRecords(
        "00400000\tSharedName\tmanual\tentry\tvoid SharedName(void)\n"
        "00400001\tSharedName\tmanual\tentry\tvoid SharedName(void)\n");
    igi::NativeCatalogue aliasedCatalogue;
    assert(aliasedCatalogue.load(aliasedRecords) && aliasedCatalogue.size() == 2);
    assert(aliasedCatalogue.findByName("SharedName") == nullptr);
    assert(aliasedCatalogue.findByAddress(0x00400000) != nullptr);
    igi::NativeRunner ambiguousRunner(aliasedCatalogue, scheduler);
    assert(ambiguousRunner.queueResolution("SharedName", [](const igi::NativeRecord&) {}) == 0);
    menuManagerModelTest();
    return 0;
}
