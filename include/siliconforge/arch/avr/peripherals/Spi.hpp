#pragma once

#include <siliconforge/arch/avr/DataSpace.hpp>
#include <siliconforge/arch/avr/InterruptController.hpp>
#include <siliconforge/arch/avr/peripherals/Gpio.hpp>
#include <siliconforge/core/Scheduler.hpp>

#include <vector>

namespace siliconforge::avr {

/// The SPI of a megaAVR in master mode (ADR 0010, #64).
///
/// A byte written to SPDR is shifted out on MOSI and SCK at the rate SPR and SPI2X select, in the mode CPOL and CPHA
/// select, most or least significant bit first (DORD). Every edge of the clock and every change of the data line is a
/// pin change at its own cycle, so that a logic analyser or a simulated device on the bus sees the real waveform; MISO
/// is sampled at the sampling edge. SPIF is set once the eighth bit is complete (SPDR then holds what was received) and
/// is cleared by reading SPSR and then accessing SPDR, or by the interrupt vector. A write to SPDR during a transfer
/// sets WCOL and is ignored; WCOL is cleared the same way as SPIF, even while the transfer is still under way. Clearing
/// SPE (or MSTR) during a transfer aborts it: the edges still to come are cancelled, SPIF stays clear and the pins are
/// released. The direction of the pins stays with DDRx, as on the real device. Slave mode is not
/// modelled: with MSTR clear the peripheral does nothing.
class Spi {
public:
    struct Registers {
        Address spcr, spsr, spdr;
    };
    struct Pins {
        GpioPort* port;
        unsigned  mosi, miso, sck;
    };

    Spi(DataSpace& data, Scheduler& scheduler, InterruptController& interrupts, const Cycles& clock, Registers registers, unsigned vector, Pins pins);

    /// Keep JSON-provided register reset values when assembling a described device.
    void reset(bool preserveRegisters = false);

    /// Cycles of one SCK period with the present SPR and SPI2X: 2, 4, 8, 16, 32, 64 or 128.
    [[nodiscard]] Cycles bitCycles() const noexcept;

private:
    void controlChanged();
    void start(u8 value, Cycles now);
    void finish();
    void updateInterrupt();
    void releasePins();
    void clearSeenFlags();

    DataSpace&           data;
    Scheduler&           scheduler;
    InterruptController& interrupts;
    const Cycles&        clock;
    Registers            regs;
    unsigned             vector;
    Pins                 pins;

    bool                            busy{false};
    u8                              statusSeen{0};  ///< The flags (SPIF, WCOL) SPSR showed since the last access to SPDR: that access clears them.
    u8                              received{0};
    u8                              shiftIn{0};
    std::vector<Scheduler::EventId> events;
};

}  // namespace siliconforge::avr
