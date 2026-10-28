#pragma once

#include <siliconforge/arch/avr/Device.hpp>

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

/// Compatibility facade; all device parameters come from the embedded JSON description.
class Atmega328p final : public avr::Device {
public:
    static constexpr u64    defaultClockHz = 16'000'000;
    static constexpr double supplyVoltage  = 5.0;
    /// A zero override uses clockHz from the embedded description.
    explicit Atmega328p(u64 clockHz = 0);
};

}  // namespace siliconforge::mcu
