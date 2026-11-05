#include "igi_sdk/runtime/scheduler.hpp"
#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/native_catalogue.hpp"
#include "igi_sdk/natives/native_runner.hpp"
#include "igi_sdk/runtime/hook_manager.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/runtime/scheduler.hpp"
#include "igi_sdk/ui/menu_manager.hpp"
#include "igi_sdk/ui/igi1_menu_manager.hpp"
#include "igi_sdk/natives/native_catalogue.hpp"

#include <cassert>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

void menuManagerModelTest();

int main() {
    igi::Scheduler scheduler;
    scheduler.bindGameThread();
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
    assert(scheduler.runPending() == 0 && count == 2);
    scheduler.enqueueDelayed(std::chrono::milliseconds(10), [&] { ++count; });
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
    auto modMenu = igi::Igi1MenuManager::create(igi::Igi1MenuSet::InGame);
    assert(modMenu.pushScreen(990));
    assert(modMenu.currentScreen()->widgets.size() == 10);
    assert(modMenu.currentScreen()->widgets[0].label == "Weather: Rain");
    assert(modMenu.currentScreen()->widgets[3].label == "Free Camera");
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
    menuManagerModelTest();
    return 0;
}
