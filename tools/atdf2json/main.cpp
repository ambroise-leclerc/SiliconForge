// atdf2json: imports the authoritative fields of an ATDF device file; --base keeps the hand-authored simulation data.
#include "Atdf.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view usage = "usage: atdf2json [-h] [--package PACKAGE] [--base BASE] [-o OUTPUT] atdf";

void printHelp() {
    std::println("{}", usage);
    std::println(R"(
Import authoritative ATDF fields; optionally retain hand-authored simulation data with --base.

positional arguments:
  atdf                  the ATDF device file

options:
  -h, --help            show this help message and exit
  --package PACKAGE     ATDF pinout name, e.g. PDIP28
  --base BASE           Retain fields absent from ATDF (timings, factories, reset values, board labels)
  -o, --output OUTPUT   write the description there instead of the standard output)");
}

[[noreturn]] void usageError(const std::string& message) {
    std::println(stderr, "{}\natdf2json: error: {}", usage, message);
    std::exit(2);
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot read " + path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<std::string> atdf, package, base, output;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto value = [&](std::string_view name, std::optional<std::string>& target) {
            if (argument.starts_with(std::string(name) + "=")) {
                target = std::string(argument.substr(name.size() + 1));
                return true;
            }
            if (argument != name)
                return false;
            if (i + 1 >= argc)
                usageError("argument " + std::string(name) + ": expected one argument");
            target = argv[++i];
            return true;
        };
        if (argument == "-h" || argument == "--help") {
            printHelp();
            return 0;
        }
        if (value("--package", package) || value("--base", base) || value("--output", output) || value("-o", output))
            continue;
        if (argument.starts_with("-") && argument != "-")
            usageError("unrecognized arguments: " + std::string(argument));
        if (atdf)
            usageError("unrecognized arguments: " + std::string(argument));
        atdf = std::string(argument);
    }
    if (!atdf)
        usageError("the following arguments are required: atdf");

    try {
        std::optional<siliconforge::atdf::Json> baseDescription;
        if (base)
            baseDescription = siliconforge::atdf::Json::parse(readFile(*base));
        const auto document = siliconforge::atdf::parse(readFile(*atdf));
        const auto text = siliconforge::atdf::dump(
            siliconforge::atdf::convert(document, package, baseDescription ? &*baseDescription : nullptr));
        if (output) {
            std::ofstream file(*output, std::ios::binary);
            if (!(file << text))
                throw std::runtime_error("cannot write " + *output);
        } else {
            std::cout << text;
        }
        if (!base)
            std::println(stderr, "Skeleton only: complete core, clock, supply, CPU, timing, reset values and peripheral "
                                 "factories before loading.");
    } catch (const std::exception& error) {  // Includes nlohmann::json::parse_error.
        std::println(stderr, "atdf2json: {}", error.what());
        return 1;
    }
    return 0;
}
