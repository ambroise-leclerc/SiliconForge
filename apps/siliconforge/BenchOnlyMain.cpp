// The command-line bench for builds without the WebFront application.
#include "BenchCommand.hpp"

#include <cstdio>
#include <string_view>

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view{argv[1]} == "bench")
        return siliconforge::app::benchcommand::main(argc - 2, argv + 2);
    std::fputs("Usage: siliconforge bench <run|check> [options]\n", stderr);
    return siliconforge::app::benchcommand::usageError;
}
