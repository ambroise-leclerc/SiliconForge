#pragma once

#include <siliconforge/arch/avr/AvrCore.hpp>
#include <siliconforge/arch/avr/DataSpace.hpp>
#include <siliconforge/arch/avr/InterruptController.hpp>
#include <siliconforge/arch/avr/peripherals/Gpio.hpp>
#include <siliconforge/arch/avr/peripherals/Timer8.hpp>
#include <siliconforge/arch/avr/peripherals/Usart.hpp>
#include <siliconforge/core/Mcu.hpp>

#include <array>
#include <map>
#include <memory>

namespace siliconforge::mcu {

/// Interrupt vector numbers of the ATmega328P.
namespace atmega328p_vectors {
inline constexpr unsigned timer0CompareA = 14;
inline constexpr unsigned timer0CompareB = 15;
inline constexpr unsigned timer0Overflow = 16;
inline constexpr unsigned usartRx        = 18;
inline constexpr unsigned usartUdre      = 19;
inline constexpr unsigned usartTx        = 20;
inline constexpr unsigned adc            = 21;
inline constexpr unsigned eepromReady    = 22;
}  // namespace atmega328p_vectors

/// ATmega328P, as mounted on an Arduino Uno (pins also carry the D0-D13/A0-A5 labels).
///
/// Emulated: AVRe+ core, 32 KiB flash with self-programming, 2 KiB SRAM, 1 KiB EEPROM, ports B/C/D, Timer/Counter0,
/// USART0, ADC (single conversions). Timer/Counter1 and 2, SPI, TWI, the watchdog and the analog comparator are
/// not emulated yet: their registers behave as plain storage so that firmware using them keeps running.
class Atmega328p final : public Mcu {
public:
    static constexpr u64    defaultClockHz = 16'000'000;
    static constexpr double supplyVoltage  = 5.0;

    explicit Atmega328p(u64 clockHz = defaultClockHz);

    [[nodiscard]] std::string_view name() const noexcept override { return "ATmega328P"; }
    [[nodiscard]] std::string_view architecture() const noexcept override { return "avr"; }
    [[nodiscard]] u64              clockHz() const noexcept override { return clock; }

    void load(const ProgramImage& image) override;
    void reset() override;

    RunResult run(Cycles budget) override { return core.run(budget); }
    RunResult step() override { return core.step(); }

    [[nodiscard]] Cycles  cycles() const noexcept override { return core.cycles; }
    [[nodiscard]] Address programCounter() const noexcept override { return core.pc * 2; }

    [[nodiscard]] std::span<Pin>             pins() noexcept override { return pinList; }
    [[nodiscard]] std::span<SerialPort>      serialPorts() noexcept override { return serialList; }
    [[nodiscard]] std::vector<RegisterValue> registers() const override;
    [[nodiscard]] std::vector<u8>            readMemory(MemorySpace space, Address address, std::size_t size) const override;
    [[nodiscard]] Debugger&                  debugger() noexcept override { return debug; }
    [[nodiscard]] std::string                symbolAt(Address address) const override;

    /// Direct access to the AVR core, for tests and architecture-specific tools.
    [[nodiscard]] avr::AvrCore&    avrCore() noexcept { return core; }
    [[nodiscard]] avr::DataSpace&  dataSpace() noexcept { return data; }
    [[nodiscard]] avr::Usart&      usart0() noexcept { return usart; }

private:
    void mapEeprom();
    void mapAdc();
    void updateMiscInterrupts();

    u64                              clock;
    Scheduler                        scheduler;
    Debugger                         debug;
    avr::InterruptController         interrupts;
    avr::DataSpace                   data;
    avr::AvrCore                     core;
    std::vector<Pin>                 pinList;
    std::vector<SerialPort>          serialList;
    avr::GpioPort                    portB;
    avr::GpioPort                    portC;
    avr::GpioPort                    portD;
    avr::Timer8                      timer0;
    avr::Usart                       usart;
    std::array<u8, 1024>             eeprom{};
    Scheduler::EventId               eepromEvent{0};
    Scheduler::EventId               adcEvent{0};
    std::map<Address, std::string>   symbols;
};

}  // namespace siliconforge::mcu
