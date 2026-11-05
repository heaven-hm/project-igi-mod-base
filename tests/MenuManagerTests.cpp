#include "IGI/MenuManager.hpp"
#include "IGI/Scheduler.hpp"

#include <cassert>

void menuManagerModelTest() {
    igi::Scheduler scheduler;
    igi::MenuManager menuManager(scheduler);
    menuManager.createMenu("main", "Main");
    menuManager.addInfo("main", "Status", "Ready");
    assert(!menuManager.addInfo("unknown", "Ignored", "Ignored"));
    const auto snapshot = menuManager.snapshot();
    assert(snapshot.title == "Main");
    assert(snapshot.items.size() == 1);
    assert(snapshot.items[0].kind == igi::MenuItemKind::Info);
}
