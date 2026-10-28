#include <siliconforge/arch/avr/peripherals/Usart.hpp>

namespace siliconforge::avr {

namespace {
// UCSRnA
constexpr u8 rxc       = 1 << 7;
constexpr u8 txc       = 1 << 6;
constexpr u8 udre      = 1 << 5;
constexpr u8 dor       = 1 << 3;
constexpr u8 u2x       = 1 << 1;
constexpr u8 readOnlyA = rxc | udre | (1 << 4) | dor | (1 << 2);
// UCSRnB
constexpr u8 rxcie = 1 << 7;
constexpr u8 txcie = 1 << 6;
constexpr u8 udrie = 1 << 5;
constexpr u8 rxen  = 1 << 4;
constexpr u8 txen  = 1 << 3;
constexpr u8 ucsz2 = 1 << 2;


}  // namespace

Usart::Usart(DataSpace&           data,
             Scheduler&           scheduler,
             InterruptController& interrupts,
             const Cycles&        clock,
             SerialPort&          serial,
             Registers            registers,
             Vectors              vectors,
             Pins                 pins,
             std::array<u16, 2>   clockDividers,
             std::size_t          receiveFifoDepth)
    : data(data),
      scheduler(scheduler),
      interrupts(interrupts),
      clock(clock),
      serial(serial),
      regs(registers),
      vectors(vectors),
      pins(pins),
      clockDividers(clockDividers),
      receiveFifoDepth(receiveFifoDepth) {
    data.map(regs.udr,
             {[this] {
                  if (rxFifo.empty())
                      return u8{0};
                  const auto byte = rxFifo.front();
                  rxFifo.pop_front();
                  if (rxFifo.empty())
                      this->data.raw(regs.ucsrA) &= static_cast<u8>(~rxc);
                  updateInterrupts();
                  return byte;
              },
              [this](u8 value, u8) {
                  if (!(this->data.raw(regs.ucsrB) & txen))
                      return;
                  txBuffer                    = value;
                  this->data.raw(regs.ucsrA) &= static_cast<u8>(~udre);
                  if (!txShift)
                      startTransmission(this->clock);
                  updateInterrupts();
              }});
    data.map(regs.ucsrA, {nullptr, [this](u8 value, u8 mask) {
                              auto&      stored   = this->data.raw(regs.ucsrA);
                              const auto writable = static_cast<u8>(mask & ~readOnlyA & ~txc);
                              stored              = static_cast<u8>((stored & ~writable) | (value & writable));
                              if (value & mask & txc)  // TXC is cleared by writing a one to it.
                                  stored &= static_cast<u8>(~txc);
                              updateInterrupts();
                          }});
    data.map(regs.ucsrB, {nullptr, [this](u8 value, u8 mask) {
                              auto&      stored = this->data.raw(regs.ucsrB);
                              const auto before = stored;
                              stored            = static_cast<u8>((stored & ~mask) | (value & mask));
                              if ((stored & rxen) && !(before & rxen)) {
                                  scheduleReception(this->clock);
                              } else if (!(stored & rxen) && (before & rxen)) {
                                  if (rxEvent)
                                      this->scheduler.cancel(std::exchange(rxEvent, 0));
                                  rxFifo.clear();
                                  this->data.raw(regs.ucsrA) &= static_cast<u8>(~rxc);
                              }
                              updatePins();
                              updateInterrupts();
                          }});

    serial.setInputListener([this] {
        scheduleReception(this->clock);
    });
    interrupts.onAcknowledge(vectors.txComplete, [this] {
        this->data.raw(regs.ucsrA) &= static_cast<u8>(~txc);
        updateInterrupts();
    });
}

void Usart::reset(bool preserveRegisters) {
    for (auto* event : {&txEvent, &rxEvent})
        if (*event)
            scheduler.cancel(std::exchange(*event, 0));
    txBuffer.reset();
    txShift.reset();
    rxFifo.clear();
    if (!preserveRegisters) {
        data.raw(regs.ucsrA) = udre;
        data.raw(regs.ucsrB) = 0;
        data.raw(regs.ucsrC) = 0x06;  // Asynchronous, 8 data bits, no parity, 1 stop bit.
        data.raw(regs.ubrrL) = 0;
        data.raw(regs.ubrrH) = 0;
    }
    updatePins();
    updateInterrupts();
}

Cycles Usart::frameCycles() const noexcept {
    const auto ubrr      = static_cast<Cycles>(data.raw(regs.ubrrL) | (data.raw(regs.ubrrH) & 0x0F) << 8);
    const auto bitCycles = clockDividers[(data.raw(regs.ucsrA) & u2x) ? 1 : 0] * (ubrr + 1);
    const auto ucsrC     = data.raw(regs.ucsrC);
    const auto size      = ((ucsrC >> 1) & 0x03) | (data.raw(regs.ucsrB) & ucsz2);
    const auto dataBits  = size == 7 ? 9u : 5u + (size & 0x03);
    const auto parity    = (ucsrC & 0x30) ? 1u : 0u;
    const auto stopBits  = (ucsrC & 0x08) ? 2u : 1u;
    return bitCycles * (1 + dataBits + parity + stopBits);
}

u64 Usart::baudRate(u64 clockHz) const noexcept {
    const auto ubrr = static_cast<u64>(data.raw(regs.ubrrL) | (data.raw(regs.ubrrH) & 0x0F) << 8);
    return clockHz / (clockDividers[(data.raw(regs.ucsrA) & u2x) ? 1 : 0] * (ubrr + 1));
}

void Usart::startTransmission(Cycles now) {
    txShift = txBuffer;
    txBuffer.reset();
    data.raw(regs.ucsrA) |= udre;
    txEvent               = scheduler.schedule(now + frameCycles(), [this](Cycles at) {
        transmissionComplete(at);
    });
}

void Usart::transmissionComplete(Cycles now) {
    txEvent = 0;
    serial.transmit(*txShift, now);
    txShift.reset();
    if (txBuffer)
        startTransmission(now);
    else
        data.raw(regs.ucsrA) |= txc;
    updateInterrupts();
}

void Usart::scheduleReception(Cycles now) {
    if (rxEvent || !(data.raw(regs.ucsrB) & rxen) || !serial.hasPendingInput())
        return;
    rxEvent = scheduler.schedule(now + frameCycles(), [this](Cycles at) {
        receptionComplete(at);
    });
}

void Usart::receptionComplete(Cycles now) {
    rxEvent         = 0;
    const auto byte = serial.takeInput();
    if (rxFifo.size() < receiveFifoDepth) {
        rxFifo.push_back(byte);
        data.raw(regs.ucsrA) |= rxc;
    } else {
        data.raw(regs.ucsrA) |= dor;  // Data OverRun: the byte is lost.
    }
    updateInterrupts();
    scheduleReception(now);
}

void Usart::updateInterrupts() {
    const auto a = data.raw(regs.ucsrA);
    const auto b = data.raw(regs.ucsrB);
    interrupts.set(vectors.rxComplete, (a & rxc) && (b & rxcie));
    interrupts.set(vectors.dataRegisterEmpty, (a & udre) && (b & udrie));
    interrupts.set(vectors.txComplete, (a & txc) && (b & txcie));
}

void Usart::updatePins() {
    if (!pins.port)
        return;
    // The transmitter takes the TXD pin over and holds it at the idle (high) level between frames.
    pins.port->forceOutput(pins.txBit, (data.raw(regs.ucsrB) & txen) ? std::optional<bool>{true} : std::nullopt);
}

}  // namespace siliconforge::avr
