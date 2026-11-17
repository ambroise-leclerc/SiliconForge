// Runs one compiled qualification program and reads its verdict (ADR 0006, #25; convention in
// firmware/qualification/qualification.h).

#include "Qualification.hpp"
#include "support/OpcodeCoverageSession.hpp"

#include <siliconforge/core/loader/ProgramImage.hpp>
#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>

#include <format>
#include <iostream>

namespace siliconforge::qualification {

namespace {
constexpr Address verdictAddress = 0xFF, firstFailAddress = 0xFE, checksHigh = 0xFD, checksLow = 0xFC;

std::string_view reasonName(StopReason reason) {
    switch (reason) {
        case StopReason::BudgetReached: return "cycle budget exhausted";
        case StopReason::Breakpoint: return "breakpoint";
        case StopReason::Watchpoint: return "watchpoint";
        case StopReason::BreakInstruction: return "BREAK";
        case StopReason::InvalidOpcode: return "invalid opcode";
        case StopReason::Requested: return "stop requested";
    }
    return "unknown";
}
}  // namespace

int program(const Options& options) {
    const testing::OpcodeCoverageSession coverage;
    if (options.positional.size() != 1)
        throw std::invalid_argument("program: one firmware file expected");
    const std::filesystem::path path   = options.positional.front();
    const auto                  budget = options.number("cycles", 200'000'000);
    auto                        image  = loadProgram(path);
    if (!image)
        throw std::runtime_error(std::format("{}: {}", path.string(), image.error().message));

    mcu::Atmega328p device;
    device.load(*image);
    device.reset();
    const auto  result   = device.run(budget);
    auto&       data     = device.dataSpace();
    const auto  verdict  = data.raw(verdictAddress);
    const auto  checks   = static_cast<unsigned>(data.raw(checksLow) | data.raw(checksHigh) << 8);
    const auto  failed   = data.raw(firstFailAddress);
    const bool  passed   = result.reason == StopReason::BreakInstruction && verdict == 0x01 && checks > 0;
    const auto  program  = path.stem().string();  // "arith-O2"
    const auto  dash     = program.rfind('-');
    const auto  status   = passed ? "pass" : verdict == 0x02 ? "fail" : "no verdict";

    writeJson(options.get("out"), {{"program", program.substr(0, dash)},
                                   {"level", dash == std::string::npos ? "" : program.substr(dash + 1)},
                                   {"status", status},
                                   {"checks", checks},
                                   {"firstFailedCheck", failed},
                                   {"stop", reasonName(result.reason)},
                                   {"cycles", device.cycles()},
                                   {"pc", std::format("0x{:04x}", device.programCounter())}});
    std::cout << std::format("{}: {} ({} checks, {} cycles, stopped on {})", program, status, checks, device.cycles(), reasonName(result.reason));
    if (failed)
        std::cout << std::format(", first failed check: {}", failed);
    std::cout << '\n';
    return passed ? 0 : 1;
}

}  // namespace siliconforge::qualification
