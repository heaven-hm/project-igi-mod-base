// Reproduces the internal simple menu's generated resource without the game.
// The simple demo ships fewer controls than Heaven but must still compile a
// complete QVM; "invalid menu configuration" here means an item or geometry
// regression in the shared generator.
#include "config/menu_config.hpp"
#include "ui/menu_commands.hpp"
#include "ui/menu_script.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

bool configureSimpleMenu(igi::MenuConfig &config);

namespace igi {
// The layout test never attaches a retail screen.
bool prepareMenuForLevelChange() {
    return true;
}
} // namespace igi

int main() {
    igi::MenuConfig config = igi::loadMenuConfig("config/project-igi-mod-config.ini");
    assert(configureSimpleMenu(config));
    std::printf("simple menu: items=%zu width=%d height=%d screen=%d pages=%zu\n",
                config.items.size(), config.width, config.height, config.screenId,
                igi::menuPageCount(config));
    std::vector<std::uint8_t> qvm;
    std::string error;
    if (!igi::compileIgiMenuScreenScript(config, qvm, error)) {
        std::fprintf(stderr, "simple menu compile failed: %s\n", error.c_str());
        return 1;
    }
    assert(!qvm.empty());
    std::printf("PASS simple menu QVM %zu bytes\n", qvm.size());
    return 0;
}
