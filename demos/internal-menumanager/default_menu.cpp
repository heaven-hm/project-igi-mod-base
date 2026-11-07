#include "igi_sdk/ui/demo_menu.hpp"
#include "igi_sdk/ui/menu_native_actions.hpp"

bool configureDemoMenu(igi::MenuConfig&) { return true; }
void runDemoMenuAction(const igi::MenuConfig& config, igi::Runtime& runtime, std::size_t item, std::string_view value) {
    igi::executeMenuNativeAction(config, runtime, item, value);
}
