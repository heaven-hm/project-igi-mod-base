#include "igi_sdk/natives/native_catalogue.hpp"
#include "igi_sdk/natives/native_runner.hpp"
#include "igi_sdk/natives/domains/ai/ai.hpp"
#include "igi_sdk/natives/domains/camera/camera.hpp"
#include "igi_sdk/natives/domains/graphs/graphs.hpp"
#include "igi_sdk/natives/domains/soldiers/soldiers.hpp"
#include "igi_sdk/natives/domains/vehicles/vehicles.hpp"

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
              << "AI=" << catalogue.count(igi::natives::ai::domain)
              << " soldiers=" << catalogue.count(igi::natives::soldiers::domain)
              << " vehicles=" << catalogue.count(igi::natives::vehicles::domain)
              << " camera=" << catalogue.count(igi::natives::camera::domain)
              << " graphs=" << catalogue.count(igi::natives::graphs::domain)
              << '\n';

    igi::Scheduler scheduler;
    scheduler.bindGameThread();
    igi::NativeRunner runner(catalogue, scheduler);
    runner.queueResolution("Game_Start", [](const igi::NativeRecord& record) {
        std::cout << "Resolved native: " << record.name << " @ 0x"
                  << std::hex << record.address << std::dec << '\n'
                  << "Signature: " << record.signature << '\n';
    });
    scheduler.runPending();
    std::cout << "This process does not attach to or write to the game.\n";
    return 0;
}
