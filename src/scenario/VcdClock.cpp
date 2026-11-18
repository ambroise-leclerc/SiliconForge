#include "VcdClock.hpp"

#include <array>
#include <cmath>
#include <format>

namespace siliconforge::scenario::detail {
namespace {
constexpr u64 femtosecondsPerSecond = 1'000'000'000'000'000;
}

VcdClock::VcdClock(u64 frequency) : clockHz(frequency) {
    if (clockHz == 0 || femtosecondsPerSecond % clockHz != 0) {
        scale = "1 fs";
        return;
    }
    const auto period = femtosecondsPerSecond / clockHz;  // In femtoseconds.
    // Every valid timescale, from 100 s down to 1 fs, in femtoseconds.
    constexpr std::array<const char*, 6> units{"s", "ms", "us", "ns", "ps", "fs"};
    u64                                  size = femtosecondsPerSecond * 100;
    for (std::size_t unit = 0; unit < units.size(); ++unit)
        for (const u64 multiplier : {100u, 10u, 1u}) {
            if (period % size == 0) {
                unitsPerCycle = period / size;
                scale         = std::format("{} {}", multiplier, units[unit]);
                return;
            }
            size /= 10;
        }
}

u64 VcdClock::time(Cycles cycles) const noexcept {
    if (unitsPerCycle != 0)
        return cycles * unitsPerCycle;
    // Whole seconds exactly, the rest rounded: the remainder is below one second, well within a double's precision.
    const auto seconds   = cycles / clockHz;
    const auto remainder = cycles % clockHz;
    return seconds * femtosecondsPerSecond + static_cast<u64>(std::llround(static_cast<double>(remainder) * 1e15 / static_cast<double>(clockHz)));
}

}  // namespace siliconforge::scenario::detail
