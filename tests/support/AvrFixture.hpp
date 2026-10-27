#pragma once

#include "AvrAssembler.hpp"

#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>

namespace siliconforge::test {

/// An ATmega328P running a hand-assembled program.
struct AvrFixture {
    mcu::Atmega328p mcu;
    avr::AvrCore&   core{mcu.avrCore()};

    explicit AvrFixture(const AvrAssembler& program) { mcu.load(program.image()); }

    /// Executes `count` instructions and returns the cycles they took.
    Cycles execute(int count = 1) {
        const auto start = core.cycles;
        for (int i = 0; i < count; ++i)
            mcu.step();
        return core.cycles - start;
    }

    [[nodiscard]] bool flag(u8 mask) const { return core.sreg & mask; }
    [[nodiscard]] u8   io(Address address) { return mcu.dataSpace().read(address); }
};

}  // namespace siliconforge::test
