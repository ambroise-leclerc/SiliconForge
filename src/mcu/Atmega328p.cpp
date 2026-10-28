#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>
#include <siliconforge/mcu/Description.hpp>
namespace siliconforge::mcu {
Atmega328p::Atmega328p(u64 clockHz) : avr::Device(builtinDescription("atmega328p"), clockHz) {}
}  // namespace siliconforge::mcu
