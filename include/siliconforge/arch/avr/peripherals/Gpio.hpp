#pragma once

#include <siliconforge/arch/avr/DataSpace.hpp>
#include <siliconforge/core/Pin.hpp>

#include <array>
#include <optional>

namespace siliconforge::avr {

/// One 8-bit AVR I/O port (PINx, DDRx, PORTx).
///
/// Alternate functions take a pin over through overrides: a timer output compare drives the level while DDR keeps
/// the direction (`overrideLevel`), a USART transmitter forces the pin to be an output (`forceOutput`).
class GpioPort {
public:
    struct Registers {
        Address pin;
        Address ddr;
        Address port;
    };

    /// `pins[i]` is the package pin of bit i, nullptr when the bit has no pin. `mcucr` holds PUD in bit 4.
    GpioPort(DataSpace& data, Registers registers, std::array<Pin*, 8> pins, Address mcucr);

    void reset();

    void overrideLevel(unsigned bit, std::optional<bool> level);
    void forceOutput(unsigned bit, std::optional<bool> level);

    /// Re-evaluates every pin, e.g. after a change of MCUCR.PUD.
    void refresh();

    [[nodiscard]] u8 input() const noexcept;
    [[nodiscard]] u8 ddr() const noexcept { return data.raw(regs.ddr); }
    [[nodiscard]] u8 port() const noexcept { return data.raw(regs.port); }

private:
    void update(unsigned bit);

    DataSpace&                         data;
    Registers                          regs;
    std::array<Pin*, 8>                pins;
    Address                            mcucr;
    std::array<std::optional<bool>, 8> levelOverrides;
    std::array<std::optional<bool>, 8> outputOverrides;
};

}  // namespace siliconforge::avr
