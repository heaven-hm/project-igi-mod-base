#include "config/menu_config.hpp"
#include "ui/menu_script.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: igi-menu-script <config.ini> <output.qsc>\n";
        return 2;
    }
    std::ifstream configFile(argv[1]);
    if (!configFile) {
        std::cerr << "cannot open configuration\n";
        return 1;
    }
    const auto config = igi::loadMenuConfig(argv[1]);
    std::ofstream output(argv[2], std::ios::binary);
    if (!output || !igi::writeIgiMenuScript(output, config)) {
        std::cerr << "cannot generate menu script\n";
        return 1;
    }
    return 0;
}
