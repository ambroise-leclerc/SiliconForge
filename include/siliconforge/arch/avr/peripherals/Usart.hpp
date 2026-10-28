#pragma once

#include <siliconforge/arch/avr/DataSpace.hpp>
#include <siliconforge/arch/avr/InterruptController.hpp>
#include <siliconforge/arch/avr/peripherals/Gpio.hpp>
#include <siliconforge/core/Scheduler.hpp>
#include <siliconforge/core/SerialPort.hpp>

#include <deque>
#include <optional>

namespace siliconforge::avr {

/// Asynchronous mode of the megaAVR USART, timed at the configured baud rate.
///
/// A byte written to UDRn reaches the host SerialPort when its whole frame (start, data, parity, stop bits) has been
/// shifted out. Bytes sent by the host are received one frame after another; when firmware does not read UDRn fast
/// enough, the two-level receive FIFO overflows and DORn is set, as on the real device.
class Usart {
public:
    struct Registers {
        Address udr, ucsrA, ucsrB, ucsrC, ubrrL, ubrrH;
    };
    struct Vectors {
        unsigned rxComplete, dataRegisterEmpty, txComplete;
    };
    struct Pins {
        GpioPort* port;
        unsigned  rxBit;
        unsigned  txBit;
    };

    Usart(DataSpace&           data,
          Scheduler&           scheduler,
          InterruptController& interrupts,
          const Cycles&        clock,
          SerialPort&          serial,
          Registers            registers,
          Vectors              vectors,
          Pins                 pins,
          std::array<u16, 2>   clockDividers    = {16, 8},
          std::size_t          receiveFifoDepth = 2);

    /// Keep JSON-provided register reset values when assembling a described device.
    void reset(bool preserveRegisters = false);

    /// Duration of one frame with the current settings.
    [[nodiscard]] Cycles frameCycles() const noexcept;
    [[nodiscard]] u64    baudRate(u64 clockHz) const noexcept;

private:
    void startTransmission(Cycles now);
    void transmissionComplete(Cycles now);
    void scheduleReception(Cycles now);
    void receptionComplete(Cycles now);
    void updateInterrupts();
    void updatePins();

    DataSpace&           data;
    Scheduler&           scheduler;
    InterruptController& interrupts;
    const Cycles&        clock;
    SerialPort&          serial;
    Registers            regs;
    Vectors              vectors;
    Pins                 pins;
    std::array<u16, 2>   clockDividers;
    std::size_t          receiveFifoDepth;

    std::optional<u8>  txBuffer;
    std::optional<u8>  txShift;
    std::deque<u8>     rxFifo;
    Scheduler::EventId txEvent{0};
    Scheduler::EventId rxEvent{0};
};

}  // namespace siliconforge::avr
