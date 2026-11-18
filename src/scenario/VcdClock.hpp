#pragma once
// Time of a Value Change Dump. The format allows a timescale of 1, 10 or 100 seconds, milliseconds, microseconds,
// nanoseconds, picoseconds or femtoseconds, and nothing else: "62500 ps" is not a timescale.

#include <siliconforge/core/Types.hpp>

#include <string>

namespace siliconforge::scenario::detail {

/// Converts clock cycles to the time of a VCD file.
class VcdClock {
public:
    /// The largest valid timescale that divides the clock period (100 ps for 16 MHz: one cycle is 625 units), or 1 fs
    /// with times rounded to the nearest femtosecond when the period is not a whole number of femtoseconds.
    explicit VcdClock(u64 frequency);

    /// "100 ps".
    [[nodiscard]] const std::string& timescale() const noexcept {
        return scale;
    }
    /// Whether every cycle falls on a whole number of units.
    [[nodiscard]] bool exact() const noexcept {
        return unitsPerCycle != 0;
    }
    /// The time of a cycle, in units of the timescale.
    [[nodiscard]] u64 time(Cycles cycles) const noexcept;

private:
    u64         clockHz;
    u64         unitsPerCycle{0};  ///< 0 when the period is not a whole number of femtoseconds.
    std::string scale;
};

}  // namespace siliconforge::scenario::detail
