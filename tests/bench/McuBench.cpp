// Manual benchmark, excluded from the default build. Warm timings measure repeated device creation and table updates.
#include "../support/AvrCoreAccess.hpp"
#include <nlohmann/json.hpp>
#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/mcu/Registry.hpp>

#include <chrono>
#include <print>

int main() {
    using Clock                    = std::chrono::steady_clock;
    constexpr unsigned iterations  = 100;
    auto               initial     = siliconforge::mcu::create("atmega328p");  // Warm the shared architecture tables.
    const auto         description = siliconforge::mcu::builtinDescription("atmega328p");
    const auto         startCreate = Clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        auto device = siliconforge::mcu::create("atmega328p");
        if (!device)
            return 1;
    }
    const auto creation    = std::chrono::duration<double, std::micro>(Clock::now() - startCreate).count() / iterations;
    auto&      cpu         = static_cast<siliconforge::avr::Device&>(*initial).avrCore();
    const auto startTiming = Clock::now();
    for (unsigned i = 0; i < iterations; ++i)
        cpu.setTiming(description.at("timing"));
    const auto timing = std::chrono::duration<double, std::micro>(Clock::now() - startTiming).count() / iterations;
    std::println(R"({{"iterations":{},"create_us":{:.3f},"setTiming_us":{:.3f},"per_core_timing_bytes":{}}})",
                 iterations,
                 creation,
                 timing,
                 siliconforge::testing::AvrCoreAccess::timingTableBytes(cpu));
}
