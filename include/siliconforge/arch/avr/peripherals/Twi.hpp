#pragma once

#include <siliconforge/arch/avr/DataSpace.hpp>
#include <siliconforge/arch/avr/InterruptController.hpp>
#include <siliconforge/arch/avr/peripherals/Gpio.hpp>
#include <siliconforge/core/Scheduler.hpp>

#include <functional>
#include <vector>

namespace siliconforge::avr {

/// The TWI (I2C) of a megaAVR in master mode.
///
/// With TWEN set the peripheral takes SDA and SCL as open-drain lines: it pulls them low or releases them, and a released
/// line is high only if something pulls it up (the pull-up PORTx gives it, or a resistor or a device of the circuit).
/// Each action starts when the firmware clears TWINT by writing it to one: a START (TWSTA), a STOP (TWSTO, both for a STOP
/// followed by a START), the transmission of TWDR (the address and direction after a START, data after an acknowledged
/// SLA+W) or the reception of a byte (after an acknowledged SLA+R; acknowledged if TWEA is set). Every edge is a pin change
/// at its own cycle, SCL running at F_CPU / (16 + 2 * TWBR * 4^TWPS); SDA changes while SCL is low, and is sampled when SCL
/// rises. At the end of the action TWINT is set and TWSR holds the status code of the datasheet (0x08, 0x10, 0x18, 0x20,
/// 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58), except after a STOP, which clears TWSTO and leaves 0xF8. A bit sent as one
/// that reads zero loses the arbitration (0x38): the lines are released. Writing TWDR while TWINT is clear sets TWWC.
/// TWINT stays set when the interrupt is taken, as on the device: the routine clears it. Slave mode, clock stretching by a
/// device and the bus-busy detection of another master are not modelled.
class Twi {
public:
    struct Registers {
        Address twbr, twsr, twar, twdr, twcr;
    };
    struct Pins {
        GpioPort* port;
        unsigned  sda, scl;
    };

    Twi(DataSpace& data, Scheduler& scheduler, InterruptController& interrupts, const Cycles& clock, Registers registers, unsigned vector, Pins pins);

    /// Keep JSON-provided register reset values when assembling a described device.
    void reset(bool preserveRegisters = false);

    /// Cycles of one SCL period with the present TWBR and TWPS.
    [[nodiscard]] Cycles bitCycles() const noexcept;

private:
    enum class Phase { Idle, Started, Transmitting, Receiving, Done };

    void controlChanged(u8 written);
    void act();
    void start(bool repeated);
    void stop(bool thenStart);
    void transmit(u8 value, bool address);
    void receive(bool acknowledge);
    void complete(u8 status);
    void abort();
    void line(unsigned bit, bool released);
    [[nodiscard]] bool sda() const;
    void at(Cycles when, std::function<void()> action);
    void updateInterrupt();

    DataSpace&           data;
    Scheduler&           scheduler;
    InterruptController& interrupts;
    const Cycles&        clock;
    Registers            regs;
    unsigned             vector;
    Pins                 pins;

    bool                            busy{false};   ///< An action is under way.
    bool                            owner{false};  ///< This master holds the bus: SCL is low between two actions.
    bool                            reading{false};  ///< The address sent asked to read.
    bool                            lost{false};
    u8                              shift{0};
    u8                              status{0xF8};
    std::vector<Scheduler::EventId> events;
};

}  // namespace siliconforge::avr
