#include "igi_sdk/natives/native_catalogue.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string cataloguePath = argc > 1
        ? argv[1]
        : std::string(IGI_SDK_SOURCE_DIR) + "/data/IGI1/neonix-igi-re-signatures.tsv";

    std::ifstream input(cataloguePath);
    igi::NativeCatalogue catalogue;
    if (!input || !catalogue.load(input)) {
        std::cerr << "Unable to load IGI 1 catalogue: " << cataloguePath << '\n';
        return 1;
    }

    std::cout << "IGI MenuManager external-style companion\n"
              << "Catalogued IGI 1 records: " << catalogue.size() << '\n'
              << "This process does not attach to or write to the game.\n";
    return 0;
}
