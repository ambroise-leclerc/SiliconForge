#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>

#include <algorithm>
#include <format>

namespace siliconforge::mcu {

namespace {

namespace io {
constexpr Address pinB = 0x23, ddrB = 0x24, portB = 0x25;
constexpr Address pinC = 0x26, ddrC = 0x27, portC = 0x28;
constexpr Address pinD = 0x29, ddrD = 0x2A, portD = 0x2B;
constexpr Address tifr0 = 0x35;
constexpr Address eecr = 0x3F, eedr = 0x40, eearl = 0x41, eearh = 0x42;
constexpr Address tccr0a = 0x44, tccr0b = 0x45, tcnt0 = 0x46, ocr0a = 0x47, ocr0b = 0x48;
constexpr Address smcr = 0x53, mcucr = 0x55, spmcsr = 0x57;
constexpr Address timsk0 = 0x6E;
constexpr Address adcl = 0x78, adch = 0x79, adcsra = 0x7A, admux = 0x7C;
constexpr Address ucsr0a = 0xC0, ucsr0b = 0xC1, ucsr0c = 0xC2, ubrr0l = 0xC4, ubrr0h = 0xC5, udr0 = 0xC6;
}  // namespace io

constexpr avr::AvrCoreConfig coreConfig{
    .flashBytes = 32 * 1024, .ioEnd = 0xFF, .ramEnd = 0x08FF, .vectorWords = 2, .flashPageBytes = 128, .spmcsr = io::spmcsr, .smcr = io::smcr};

/// Pins in package order of the ports, with their Arduino Uno labels. Indices are used below to wire the ports.
std::vector<Pin> makePins(const Cycles* clock) {
    std::vector<Pin> pins;
    pins.reserve(22);
    for (unsigned bit = 0; bit < 8; ++bit)
        pins.emplace_back(std::format("PB{}", bit), bit < 6 ? std::format("D{}", bit + 8) : std::string{}, clock);
    for (unsigned bit = 0; bit < 6; ++bit)
        pins.emplace_back(std::format("PC{}", bit), std::format("A{}", bit), clock);
    for (unsigned bit = 0; bit < 8; ++bit)
        pins.emplace_back(std::format("PD{}", bit), std::format("D{}", bit), clock);
    return pins;
}

constexpr std::size_t firstPinB = 0, firstPinC = 8, firstPinD = 14;

std::array<Pin*, 8> portPins(std::vector<Pin>& pins, std::size_t first, std::size_t count) {
    std::array<Pin*, 8> result{};
    for (std::size_t bit = 0; bit < count; ++bit)
        result[bit] = &pins[first + bit];
    return result;
}

std::vector<SerialPort> makeSerialPorts() {
    std::vector<SerialPort> ports;
    ports.emplace_back("USART0");
    return ports;
}

// EECR bits
constexpr u8 eere = 1 << 0, eepe = 1 << 1, eempe = 1 << 2, eerie = 1 << 3;
// ADCSRA bits
constexpr u8 adsc = 1 << 6, adif = 1 << 4, adie = 1 << 3, aden = 1 << 7;
constexpr Cycles eepromWriteCycles(u64 clockHz) { return clockHz * 34 / 10'000; }  // 3.4 ms, typical erase and write time

}  // namespace

Atmega328p::Atmega328p(u64 clockHz)
    : clock(clockHz), data(coreConfig.ioEnd, coreConfig.ramEnd), core(coreConfig, data, scheduler, interrupts, debug), pinList(makePins(&core.cycles)),
      serialList(makeSerialPorts()), portB(data, {io::pinB, io::ddrB, io::portB}, portPins(pinList, firstPinB, 8), io::mcucr),
      portC(data, {io::pinC, io::ddrC, io::portC}, portPins(pinList, firstPinC, 6), io::mcucr),
      portD(data, {io::pinD, io::ddrD, io::portD}, portPins(pinList, firstPinD, 8), io::mcucr),
      timer0(data, scheduler, interrupts, core.cycles, {io::tccr0a, io::tccr0b, io::tcnt0, io::ocr0a, io::ocr0b, io::timsk0, io::tifr0},
             {atmega328p_vectors::timer0CompareA, atmega328p_vectors::timer0CompareB, atmega328p_vectors::timer0Overflow}, {0, 1, 8, 64, 256, 1024, 0, 0},
             {&portD, 6}, {&portD, 5}),
      usart(data, scheduler, interrupts, core.cycles, serialList[0], {io::udr0, io::ucsr0a, io::ucsr0b, io::ucsr0c, io::ubrr0l, io::ubrr0h},
            {atmega328p_vectors::usartRx, atmega328p_vectors::usartUdre, atmega328p_vectors::usartTx}, {&portD, 0, 1}) {
    data.map(io::mcucr, {nullptr, [this](u8 value, u8 mask) {
                             auto& stored = data.raw(io::mcucr);
                             stored       = static_cast<u8>((stored & ~mask) | (value & mask));
                             portB.refresh();  // PUD may have changed.
                             portC.refresh();
                             portD.refresh();
                         }});
    mapEeprom();
    mapAdc();
    eeprom.fill(0xFF);
    reset();
}

void Atmega328p::load(const ProgramImage& image) {
    core.eraseFlash();
    eeprom.fill(0xFF);
    for (const auto& segment : image.segments) {
        if (segment.space == MemorySpace::Program)
            core.loadFlash(segment.address, segment.bytes);
        else if (segment.space == MemorySpace::Eeprom)
            for (std::size_t i = 0; i < segment.bytes.size() && segment.address + i < eeprom.size(); ++i)
                eeprom[segment.address + i] = segment.bytes[i];
    }
    symbols = image.symbols;
    reset();
}

void Atmega328p::reset() {
    scheduler.clear();
    interrupts.clear();
    eepromEvent = 0;
    adcEvent    = 0;
    data.clear();
    core.reset();
    portB.reset();
    portC.reset();
    portD.reset();
    timer0.reset();
    usart.reset();
    for (auto& port : serialList)
        port.reset();
    updateMiscInterrupts();
}

void Atmega328p::mapEeprom() {
    data.map(io::eecr, {nullptr, [this](u8 value, u8 mask) {
                            auto& eecr = data.raw(io::eecr);
                            value      = static_cast<u8>((eecr & ~mask) | (value & mask));
                            const auto address = static_cast<std::size_t>((data.raw(io::eearl) | data.raw(io::eearh) << 8) & (eeprom.size() - 1));
                            if ((value & eepe) && (eecr & eempe) && !(eecr & eepe)) {
                                const auto mode = (value >> 4) & 0x03;
                                if (mode == 0)
                                    eeprom[address] = data.raw(io::eedr);
                                else if (mode == 1)
                                    eeprom[address] = 0xFF;
                                else if (mode == 2)
                                    eeprom[address] &= data.raw(io::eedr);
                                eepromEvent = scheduler.schedule(core.cycles + eepromWriteCycles(clock), [this](Cycles) {
                                    eepromEvent = 0;
                                    data.raw(io::eecr) &= static_cast<u8>(~eepe);
                                    updateMiscInterrupts();
                                });
                                value &= static_cast<u8>(~eempe);
                            } else if (!(eecr & eepe)) {
                                value &= static_cast<u8>(~eepe);  // EEPE without EEMPE has no effect.
                            }
                            if (value & eere) {
                                data.raw(io::eedr) = eeprom[address];
                                core.cycles += 4;  // The CPU is halted for four cycles after a read.
                                value &= static_cast<u8>(~eere);
                            }
                            eecr = value;
                            updateMiscInterrupts();
                        }});
}

void Atmega328p::mapAdc() {
    data.map(io::adcsra, {nullptr, [this](u8 value, u8 mask) {
                              auto& adcsra = data.raw(io::adcsra);
                              if (value & mask & adif)  // ADIF is cleared by writing a one to it.
                                  adcsra &= static_cast<u8>(~adif);
                              const auto keep = static_cast<u8>(mask & ~adif);
                              adcsra          = static_cast<u8>((adcsra & ~keep) | (value & keep));
                              if ((adcsra & adsc) && (adcsra & aden) && !adcEvent) {
                                  const auto prescaler = std::max(2u, 1u << (adcsra & 0x07));
                                  adcEvent = scheduler.schedule(core.cycles + 13 * prescaler, [this](Cycles) {
                                      adcEvent      = 0;
                                      const auto mux = data.raw(io::admux) & 0x0F;
                                      double volts  = 0.0;
                                      if (mux < 6)
                                          volts = pinList[firstPinC + mux].voltage(supplyVoltage);
                                      else if (mux == 14)
                                          volts = 1.1;  // Internal bandgap reference
                                      const auto result = static_cast<u16>(std::clamp(volts / supplyVoltage * 1024.0, 0.0, 1023.0));
                                      if (data.raw(io::admux) & (1 << 5)) {  // ADLAR
                                          data.raw(io::adch) = static_cast<u8>(result >> 2);
                                          data.raw(io::adcl) = static_cast<u8>(result << 6);
                                      } else {
                                          data.raw(io::adch) = static_cast<u8>(result >> 8);
                                          data.raw(io::adcl) = static_cast<u8>(result);
                                      }
                                      data.raw(io::adcsra) = static_cast<u8>((data.raw(io::adcsra) & ~adsc) | adif);
                                      updateMiscInterrupts();
                                  });
                              }
                              updateMiscInterrupts();
                          }});
    interrupts.onAcknowledge(atmega328p_vectors::adc, [this] {
        data.raw(io::adcsra) &= static_cast<u8>(~adif);
        updateMiscInterrupts();
    });
}

void Atmega328p::updateMiscInterrupts() {
    const auto adcsra = data.raw(io::adcsra);
    interrupts.set(atmega328p_vectors::adc, (adcsra & adif) && (adcsra & adie));
    const auto eecr = data.raw(io::eecr);
    interrupts.set(atmega328p_vectors::eepromReady, (eecr & eerie) && !(eecr & eepe));
}

std::vector<RegisterValue> Atmega328p::registers() const {
    std::vector<RegisterValue> result;
    result.reserve(40);
    result.push_back({"PC", core.pc * 2, 16});
    result.push_back({"SP", core.sp, 16});
    result.push_back({"SREG", core.sreg, 8});
    result.push_back({"X", core.pair(26), 16});
    result.push_back({"Y", core.pair(28), 16});
    result.push_back({"Z", core.pair(30), 16});
    for (unsigned i = 0; i < 32; ++i)
        result.push_back({std::format("R{}", i), core.r(i), 8});
    return result;
}

std::vector<u8> Atmega328p::readMemory(MemorySpace space, Address address, std::size_t size) const {
    std::vector<u8> bytes;
    bytes.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const auto at = static_cast<Address>(address + i);
        switch (space) {
            case MemorySpace::Program: bytes.push_back(core.flashByte(at)); break;
            case MemorySpace::Data: bytes.push_back(data.peek(at)); break;
            case MemorySpace::Eeprom: bytes.push_back(at < eeprom.size() ? eeprom[at] : 0xFF); break;
        }
    }
    return bytes;
}

std::string Atmega328p::symbolAt(Address address) const {
    auto it = symbols.upper_bound(address);
    if (it == symbols.begin())
        return {};
    --it;
    return address == it->first ? it->second : std::format("{}+{:#x}", it->second, address - it->first);
}

}  // namespace siliconforge::mcu
