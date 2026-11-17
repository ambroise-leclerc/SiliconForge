// Qualification of the CPU core (ADR 0006, docs/QUALIFICATION.md).
//
//   siliconforge_qualification cycles  [--mcu atmega328p] [--out cycles.json]
//   siliconforge_qualification program <firmware.hex> [--out result.json] [--cycles 200000000]
//   siliconforge_qualification matrix  --results <directory> [--coverage <directory>] [--out <directory>]
//
// The exit status is 0 when the check passes, 1 when it fails, 2 on a usage error.

#include "Qualification.hpp"

#include <iostream>

int main(int argc, char** argv) try {
    using namespace siliconforge::qualification;
    const std::string command = argc > 1 ? argv[1] : "";
    const Options     options(argc, argv, 2);
    if (command == "cycles")
        return cycles(options);
    if (command == "program")
        return program(options);
    if (command == "matrix")
        return matrix(options);
    std::cerr << "usage: siliconforge_qualification cycles|program|matrix [options]\n";
    return 2;
} catch (const std::exception& error) {
    std::cerr << "siliconforge_qualification: " << error.what() << '\n';
    return 2;
}
