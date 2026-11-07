#include "runtime/scheduler.hpp"
#include "logging/logging.hpp"
#include "natives/native_catalogue.hpp"
#include "natives/native_runner.hpp"
#include "natives/native_caller.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/mvp_natives.hpp"
#include "runtime/hook_manager.hpp"
#include "runtime/runtime.hpp"
#include "runtime/scheduler.hpp"
#include "ui/menu_manager.hpp"
#include "ui/igi_menu_manager.hpp"
#include "ui/menu_script.hpp"
#include "ui/menu_values.hpp"
#include "ui/menu_script_bridge.hpp"
#include "ui/menu_resource.hpp"
#include "config/menu_config.hpp"
#include "natives/native_catalogue.hpp"

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
std::vector<std::string> csvFields(const std::string& line) {
    std::vector<std::string> fields(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { fields.back() += '"'; ++i; }
            else quoted = !quoted;
        } else if (c == ',' && !quoted) fields.emplace_back();
        else if (c != '\r') fields.back() += c;
    }
    return fields;
}
int scriptItemsAdded = 0;
int scriptRegistrations = 0;
int scriptRemovals = 0;
bool scriptSymbolExists = false;
igi::ScriptCallback registeredCallbacks[9]{};
void* registeredContext = nullptr;
int fakeResourceObject = 0;
void* fakeResourceCurrent = nullptr;
void* fakeResourceData = nullptr;
std::string fakeResourceRegisteredName;
int fakeResourceFreeCount = 0;
bool fakeResourceHidden = false;
int fakeResourceLookup(const char*, void** out) {
    if (out) *out = fakeResourceHidden ? nullptr : fakeResourceCurrent;
    return !fakeResourceHidden && fakeResourceCurrent != nullptr;
}
void* fakeResourceRegister(const char* name, int size, void* data) {
    assert(size == 3 && data);
    fakeResourceRegisteredName = name;
    fakeResourceData = data;
    return fakeResourceCurrent = &fakeResourceObject;
}
void fakeResourceFree(void* resource) {
    assert(resource == &fakeResourceObject);
    fakeResourceCurrent = nullptr;
    ++fakeResourceFreeCount;
}
int fakeScriptGetInt(void* args, int index) { return static_cast<int*>(args)[index]; }
int throwingScriptGetInt(void*, int) { throw std::runtime_error("script argument failure"); }
void fakeScriptAddItem(const char*, int id) { assert(id == scriptItemsAdded++); }
void fakeRegister(const char*, igi::ScriptCallback callback, void* context, int argc) {
    assert(callback && context && (argc == 0 || argc == 1 || argc == 2));
    registeredCallbacks[scriptRegistrations % 9] = callback;
    registeredContext = context;
    ++scriptRegistrations;
}
void fakeRemove(const char*) { ++scriptRemovals; }
bool fakeContains(const char*) { return scriptSymbolExists; }
bool fakeMatches(const char*, igi::ScriptCallback, void*) { return scriptSymbolExists; }
igi::IgiScriptValue* fakeCallback(igi::IgiScriptValue* output, void*, void*) { return output; }
}

int main() {
    {
        const char* configPath = "igi-mod-config-test.ini";
        const char* logPath = "igi-mod-custom-test.log";
        {
            std::ofstream config(configPath);
            config << "[mod_menu]\nlog_file=" << logPath << "\n";
        }
        const auto config = igi::loadMenuConfig(configPath);
        assert(config.logFile == logPath);
        igi::setLogFile(config.logFile);
        igi::log(igi::LogLevel::Info, "custom log filename test");
        igi::closeLogFile();
        std::ifstream log(logPath);
        std::stringstream contents;
        contents << log.rdbuf();
        assert(contents.str().find("custom log filename test") != std::string::npos);
        log.close();
        std::remove(configPath);
        std::remove(logPath);
        igi::setLogFile("project-igi-mod.log");
    }
    igi::MenuResourceApi resourceApi{fakeResourceLookup, fakeResourceRegister, fakeResourceFree};
    igi::MenuResource resource;
    assert(!resource.registerData("", {1, 2, 3}, resourceApi));
    assert(!resource.registerData(std::string("SDK:bad\0name", 12), {1, 2, 3}, resourceApi));
    assert(!resource.registerData("SDK:good", std::string("LOCAL:bad\0name", 14), {1, 2, 3}, resourceApi));
    assert(resource.registerData("SDK:menu.qvm", "LOCAL:resolved/menu.qvm", {1, 2, 3}, resourceApi));
    assert(fakeResourceRegisteredName == "LOCAL:resolved/menu.qvm");
    assert(resource.active() && static_cast<std::uint8_t*>(fakeResourceData)[1] == 2);
    igi::MenuResource collision;
    assert(!collision.registerData("SDK:menu.qvm", {1, 2, 3}, resourceApi));
    int foreign = 0;
    fakeResourceCurrent = &foreign;
    assert(!resource.release() && resource.active() && fakeResourceFreeCount == 0);
    fakeResourceCurrent = &fakeResourceObject;
    assert(resource.release() && !resource.active() && fakeResourceFreeCount == 1);
    assert(resource.release() && fakeResourceFreeCount == 1);
    fakeResourceHidden = true;
    igi::MenuResource mismatchedAlias;
    assert(!mismatchedAlias.registerData("SDK:missing.qvm", "LOCAL:other.qvm", {1, 2, 3}, resourceApi));
    assert(!mismatchedAlias.active() && fakeResourceFreeCount == 2);
    fakeResourceHidden = false;
    assert(igi::MenuConfig{}.hook == igi::game::TickHook::DisplayAddFrameCallback);
    igi::Runtime runtime;
    assert(!runtime.initialize(igi::game::TickHook::TextDraw));
    assert(!runtime.initialize(igi::game::TickHook::ScreenDraw));
    assert(!runtime.initialize(igi::game::TickHook::DisplayEndFrame));
    assert(!runtime.initialize(igi::game::TickHook::DisplayAddFrameCallback));
    igi::Scheduler hookSelectionScheduler;
    igi::HookManager hookSelection(hookSelectionScheduler);
    assert(!hookSelection.installDisplayAddFrameCallbackTick());
    assert(!hookSelection.installTextDrawHook());
    assert(!hookSelection.installDisplayEndFrameHook());
    igi::Scheduler scheduler;
    scheduler.bindGameThread();
    assert(!scheduler.isRunning());
    scheduler.enqueueOnce([&] { assert(scheduler.isRunning()); });
    assert(scheduler.runPending() == 1);
    assert(!scheduler.isRunning());
    std::istringstream callerRecords(
        "00402820\tTestNative\ttest\tentry\tvoid TestNative(int value)\n");
    igi::NativeCatalogue callerCatalogue;
    assert(callerCatalogue.load(callerRecords));
    igi::NativeCaller unvalidatedCaller(callerCatalogue, scheduler, false);
    assert(!igi::igiScriptRegistryApi(unvalidatedCaller).registerFunction);
    assert(!igi::igiMenuScriptApi(unvalidatedCaller).getInt);
    assert(!igi::igiMenuResourceApi(unvalidatedCaller).registerResource);
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
    assert(callerCatalogue.add({0x00402830, "TestNative", "test", "entry",
                                "void TestNative(int value)", igi::NativeVerification::Exposed}));
    assert(validatedCaller.resolveByAddress(0x00402830, "void TestNative(int value)", resolvedAddress) ==
           igi::NativeCallStatus::Success && resolvedAddress == 0x00402830);
    assert(validatedCaller.resolveByAddress(0x00402830, "void TestNative(void)", resolvedAddress) ==
           igi::NativeCallStatus::SignatureMismatch && resolvedAddress == 0);
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
    std::ifstream csvFile(std::string(IGI_SDK_SOURCE_DIR) + "/data/igi-natives.csv");
    std::ostringstream catalogueText;
    std::string csvLine;
    if (csvFile) {
        std::getline(csvFile, csvLine);
        while (std::getline(csvFile, csvLine)) {
            const auto fields = csvFields(csvLine);
            assert(fields.size() == 5);
            catalogueText << fields[0] << '\t' << fields[1] << '\t' << fields[2] << '\t' << fields[3] << '\t' << fields[4] << '\n';
        }
    }
    std::istringstream catalogueFile(catalogueText.str());
    igi::NativeCatalogue catalogue;
    igi::NativeCatalogue exposed;
    assert(igi::loadIgiVerifiedNativeRegistry(exposed));
    assert(exposed.size() >= 300 && exposed.size() == IGI::NativeBindingCount());
    if (!catalogueText.str().empty()) {
    assert(catalogue.load(catalogueFile));
    assert(catalogue.size() == 7068);
    const auto* taskDelete = catalogue.findByAddress(0x004015F0);
    assert(taskDelete && taskDelete->name == "Task_Delete" && taskDelete->signature == "void Task_Delete(Task *task)");
    for (const auto& binding : exposed.records()) {
        assert(igi::nativeDomainForName(binding.name) != igi::NativeDomain::Memory);
        assert(!binding.name.starts_with("crt_"));
        assert(!binding.runtimeVerified);
        const auto* source = catalogue.findByAddress(binding.address);
        assert(source && source->name == binding.name && source->signature == binding.signature);
    }
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
    }
    auto mainMenu = igi::IgiMenuManager::create(igi::IgiMenuSet::Main);
    assert(mainMenu.currentScreen() != nullptr);
    assert(mainMenu.currentScreen()->id == 900);
    assert(mainMenu.currentScreen()->title == "Main Menu");
    assert(mainMenu.pushScreen(904));
    assert(mainMenu.currentScreen()->title == "Configuration");
    assert(mainMenu.popScreen(true));
    assert(mainMenu.currentScreen()->id == 900);
    // Menu items are defined per mod menu in C++, not read from config.ini.
    auto modConfig = igi::loadMenuConfig(std::string(IGI_SDK_SOURCE_DIR) + "/config/project-igi-mod-config.ini");
    assert(modConfig.hook == igi::game::TickHook::DisplayAddFrameCallback);
    assert(modConfig.logFile == "project-igi-mod.log");
    assert(modConfig.items.empty());
    modConfig.items = {
        {"weather", igi::ModMenuItemKind::List, "Rain", "IGI_SDK_SetWeather(this.value)", {"off", "on"}},
        {"ammo", igi::ModMenuItemKind::Action, "Enable Unlimited Ammo", "IGI_SDK_EnableUnlimitedAmmo()", {}},
        {"frames", igi::ModMenuItemKind::Toggle, "Frame Limit", "IGI_SDK_ToggleFrameLimit()", {"true"}, true},
        {"music", igi::ModMenuItemKind::List, "Music Volume", "IGI_SDK_SetMusicVolume(this.value)", {"mute", "low", "normal"}},
        {"display", igi::ModMenuItemKind::Action, "Refresh Display", "IGI_SDK_RefreshDisplay()", {}},
        {"diagnostics", igi::ModMenuItemKind::Action, "Log Game State", "IGI_SDK_LogGameState()", {}},
    };
    assert(modConfig.items.size() == 6);
    igi::MenuValues menuValues(modConfig);
    assert(menuValues.toggle(2) == true);
    assert(menuValues.setToggle(2, false) && menuValues.toggle(2) == false);
    assert(!menuValues.setToggle(0, true) && !menuValues.toggle(0));
    assert(menuValues.choiceCount(0) == 2);
    assert(menuValues.choice(0, 1) == "on");
    assert(menuValues.select(0, 1));
    assert(menuValues.selected(0) == 1);
    assert(!menuValues.select(0, 2));
    assert(!menuValues.selected(1));
    std::vector<std::string> filledChoices;
    assert(menuValues.fill(0, [&](std::string_view value, int id) {
        assert(static_cast<std::size_t>(id) == filledChoices.size());
        filledChoices.emplace_back(value);
    }));
    assert((filledChoices == std::vector<std::string>{"off", "on"}));
    assert(!menuValues.fill(1, [](std::string_view, int) {}));
    assert(!menuValues.fill(0, {}));
    auto extendedChoices = modConfig;
    extendedChoices.items[0].values.resize(65);
    for (std::size_t index = 0; index < extendedChoices.items[0].values.size(); ++index)
        extendedChoices.items[0].values[index] = "choice-" + std::to_string(index);
    extendedChoices.items[0].defaultSelection = 64;
    igi::MenuValues extendedValues(extendedChoices);
    assert(extendedValues.choiceCount(0) == 65);
    assert(extendedValues.selected(0) == 64);
    assert(extendedValues.choice(0, 64) == "choice-64");
    std::string selectedWeather;
    igi::MenuScriptBridge listBridge(modConfig, [&](std::size_t item, std::string_view value) {
        if (item == 0) selectedWeather = value;
    });
    assert(listBridge.getToggle(2) == 1);
    assert(listBridge.setToggle(2, 0) && listBridge.getToggle(2) == 0);
    assert(!listBridge.setToggle(2, 2) && listBridge.getToggle(2) == 0);
    assert(listBridge.getToggle(0) == -1);
    int populated = 0;
    assert(listBridge.fillList(0, [&](std::string_view, int id) { assert(id == populated++); }));
    assert(populated == 2 && listBridge.getList(0) == 0);
    assert(listBridge.setList(0, 1) && listBridge.getList(0) == 1);
    std::size_t toggledItem = 99;
    std::string toggledValue;
    igi::MenuScriptBridge toggleBridge(modConfig, [&](std::size_t item, std::string_view value) {
        toggledItem = item;
        toggledValue = value;
    });
    assert(toggleBridge.setToggle(2, 0));
    assert(toggleBridge.getToggle(2) == 0 && toggledItem == 2 && toggledValue == "false");
    std::size_t activatedItem = 99;
    std::string activatedValue;
    igi::MenuScriptBridge actionBridge(modConfig, [&](std::size_t item, std::string_view value) {
        activatedItem = item;
        activatedValue = value;
    });
    assert(actionBridge.activate(1) && activatedItem == 1 && activatedValue.empty());
    assert(actionBridge.activate(2) && activatedItem == 2 && activatedValue == "false");
    assert(!actionBridge.activate(-1));
    assert(selectedWeather == "on");
    std::vector<std::pair<std::size_t, std::string>> appliedEvents;
    bool closedCalled = false;
    igi::MenuScriptBridge deferredBridge(modConfig, [&](std::size_t item, std::string_view value) {
        appliedEvents.emplace_back(item, std::string(value));
    }, [&] {
        closedCalled = true;
    }, /*deferUntilApply=*/true);

    deferredBridge.snapshot();
    assert(deferredBridge.getToggle(2) == 1);
    assert(deferredBridge.setToggle(2, 0));
    assert(appliedEvents.empty());
    assert(deferredBridge.setList(0, 1));
    assert(appliedEvents.empty());

    deferredBridge.cancel();
    assert(closedCalled);
    assert(deferredBridge.getToggle(2) == 1);
    assert(deferredBridge.getList(0) == 0);
    assert(appliedEvents.empty());

    closedCalled = false;
    assert(deferredBridge.setToggle(2, 0));
    assert(deferredBridge.setList(0, 1));
    deferredBridge.apply();
    assert(closedCalled);
    assert(deferredBridge.getToggle(2) == 0);
    assert(deferredBridge.getList(0) == 1);
    assert(appliedEvents.size() == 2);
    assert(appliedEvents[0].first == 0 && appliedEvents[0].second == "on");
    assert(appliedEvents[1].first == 2 && appliedEvents[1].second == "false");

    igi::MenuScriptBridge failingBridge(modConfig, [](std::size_t, std::string_view) {
        throw std::runtime_error("feature rejected selection");
    });
    try { failingBridge.setList(0, 1); assert(false); }
    catch (const std::runtime_error&) {}
    assert(failingBridge.getList(0) == 0);
    assert(!listBridge.setList(0, -1) && !listBridge.setList(-1, 0));
    assert(listBridge.getList(-1) == -1 && !listBridge.fillList(-1, {}));
    igi::MenuScriptCallbacks callbacks(listBridge, {fakeScriptGetInt, fakeScriptAddItem});
    igi::IgiScriptSymbol symbol{1, 1, 0, 1, fakeCallback, "test", &callbacks};
    assert(igi::menuScriptSymbolMatches(&symbol, fakeCallback, &callbacks));
    assert(!igi::menuScriptSymbolMatches(&symbol, fakeCallback, nullptr));
    symbol.kind = 0;
    assert(!igi::menuScriptSymbolMatches(&symbol, fakeCallback, &callbacks));
    igi::IgiScriptValue scriptResult{};
    int scriptArgs[] = {0, 1};
    scriptItemsAdded = 0;
    assert(callbacks.fill(&scriptResult, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 1 && scriptItemsAdded == 2);
    assert(callbacks.set(&scriptResult, scriptArgs)->integer == 1);
    assert(callbacks.get(&scriptResult, scriptArgs)->integer == 1);
    scriptItemsAdded = 0;
    assert(callbacks.fill(nullptr, scriptArgs) == nullptr && scriptItemsAdded == 0);
    assert(!igi::registerMenuScriptCallbacks(callbacks, {}));
    igi::ScriptRegistryApi registry{fakeRegister, fakeRegister, fakeRemove, fakeContains, fakeMatches};
    scriptSymbolExists = true;
    assert(!igi::registerMenuScriptCallbacks(callbacks, registry) && scriptRegistrations == 0);
    scriptSymbolExists = false;
    assert(igi::registerMenuScriptCallbacks(callbacks, registry) && scriptRegistrations == 9);
    assert(registeredCallbacks[0] && registeredContext == &callbacks);
    igi::MenuScriptCallbacks throwingCallbacks(listBridge, {throwingScriptGetInt, fakeScriptAddItem});
    assert(registeredCallbacks[0](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 0);
    assert(registeredCallbacks[1](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == -1);
    assert(registeredCallbacks[2](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(registeredCallbacks[3] && registeredCallbacks[3](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == -1);
    assert(registeredCallbacks[4] && registeredCallbacks[4](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 0);
    assert(registeredCallbacks[5] && registeredCallbacks[5](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 0);
    assert(registeredCallbacks[6] && registeredCallbacks[6](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 1);
    assert(registeredCallbacks[7] && registeredCallbacks[7](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 1);
    assert(registeredCallbacks[8] && registeredCallbacks[8](&scriptResult, &throwingCallbacks, scriptArgs) == &scriptResult);
    assert(scriptResult.integer == 1);
    scriptSymbolExists = true;
    igi::unregisterMenuScriptCallbacks(callbacks, registry);
    assert(scriptRemovals == 9);
    igi::MenuScriptRegistration registration;
    scriptSymbolExists = false;
    assert(registration.start(callbacks, registry));
    assert(registration.active() && !registration.start(callbacks, registry));
    scriptSymbolExists = true;
    registration.stop();
    registration.stop();
    assert(!registration.active() && scriptRemovals == 18);
    scriptArgs[1] = -1;
    assert(callbacks.set(&scriptResult, scriptArgs)->integer == 0);
    std::ostringstream menuScript;
    assert(igi::writeIgiMenuScript(menuScript, modConfig));
    assert(menuScript.str().find("IGI_SDK_GetToggle(2)") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_SetToggle(2,this.isOn)") != std::string::npos);
    std::ostringstream screenOnlyScript;
    std::string compileError;
    assert(igi::writeIgiMenuScreenScript(screenOnlyScript, modConfig));
    assert(screenOnlyScript.str().find("Task_New(-1, \"MenuManager\"") == std::string::npos);
    assert(screenOnlyScript.str().find("Task_New(990, \"MenuScreen\"") != std::string::npos);
    assert(screenOnlyScript.str().find("MenuManager_RequestScreen(990, FALSE)") != std::string::npos);
    std::vector<std::uint8_t> screenOnlyQvm;
    assert(igi::compileIgiMenuScreenScript(modConfig, screenOnlyQvm, compileError));
    assert(screenOnlyQvm.size() > 16);
    assert(menuScript.str().find("Task_New(990, \"MenuScreen\"") != std::string::npos);
    assert(menuScript.str().find("Enable Unlimited Ammo") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_SetWeather") == std::string::npos);
    assert(menuScript.str().find("IGI_SDK_Action(5)") != std::string::npos);
    assert(menuScript.str().find("Task_New(993, \"ToggleBox\"") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_FillList(0)") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_SetList(0,this.nActiveID)") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_GetList(0)\", \"IGI_SDK_SetList(0,this.nActiveID)\", \"IGI_SDK_SetList(0,this.nActiveID)") != std::string::npos);
    assert(menuScript.str().find("Task_New(-1, \"MenuFrame\", \"ControlsFrame\"") != std::string::npos);
    assert(menuScript.str().find("Task_New(-1, \"MenuText\", \"restart\", \"Restart\"") != std::string::npos);
    assert(menuScript.str().find("Task_New(-1, \"MenuText\", \"okay\", \"Okay\"") != std::string::npos);
    assert(menuScript.str().find("Task_New(-1, \"MenuText\", \"cancel\", \"Cancel\"") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 12)") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_Apply() - MenuManager_LeaveMenus(900, 9)") != std::string::npos);
    assert(menuScript.str().find("IGI_SDK_Cancel() - MenuManager_LeaveMenus(900, 9)") != std::string::npos);
    std::vector<std::uint8_t> compiledMenu;
    if (!igi::compileIgiMenuScript(modConfig, compiledMenu, compileError)) {
        std::fprintf(stderr, "menu compile error: %s\n", compileError.c_str());
        assert(false);
    }
    assert(!compiledMenu.empty());
    assert(!igi::compileIgiMenuScript(igi::MenuConfig{}, compiledMenu, compileError));
    assert(compiledMenu.empty() && !compileError.empty());
    auto oversizedChoices = modConfig;
    oversizedChoices.items[0].values.assign(65, "item");
    assert(!igi::compileIgiMenuScript(oversizedChoices, compiledMenu, compileError));
    oversizedChoices.items[0].values.assign(1, std::string(40, 'x'));
    assert(!igi::compileIgiMenuScript(oversizedChoices, compiledMenu, compileError));
    auto modMenu = igi::IgiMenuManager::create(igi::IgiMenuSet::InGame, modConfig);
    assert(modMenu.pushScreen(modConfig.screenId));
    assert(modMenu.currentScreen()->widgets.size() == modConfig.items.size());
    assert(modMenu.currentScreen()->widgets[0].label == "Rain");
    assert(modMenu.currentScreen()->widgets[0].kind == igi::IgiWidgetKind::ListBox);
    assert(modMenu.currentScreen()->widgets[3].label == "Music Volume");
    assert(modMenu.currentScreen()->widgets[3].kind == igi::IgiWidgetKind::ListBox);
    auto alternateConfig = modConfig;
    alternateConfig.screenId = 991;
    auto alternateMenu = igi::IgiMenuManager::create(igi::IgiMenuSet::InGame, alternateConfig);
    assert(alternateMenu.pushScreen(991));
    assert(alternateMenu.currentScreen()->title == "IGI SDK Mod Menu");
    alternateConfig.screenId = 902;
    auto conflictingMenu = igi::IgiMenuManager::create(igi::IgiMenuSet::InGame, alternateConfig);
    assert(conflictingMenu.pushScreen(902));
    assert(conflictingMenu.currentScreen()->title == "Sound Configuration");
    if (catalogue.size() != 0) {
    const auto* gameRunHandler = catalogue.findByAddress(0x00416260);
    assert(gameRunHandler != nullptr);
    assert(gameRunHandler->name == "Game_RunHandler");
    assert(gameRunHandler->signature == "void Game_RunHandler(Game *g)");
    assert(gameRunHandler->verification == igi::NativeVerification::Catalogued);
    }
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
