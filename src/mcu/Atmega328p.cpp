#include <nlohmann/json.hpp>
#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>
#include <siliconforge/mcu/Description.hpp>
namespace siliconforge::mcu {
Atmega328p::Atmega328p(u64 clockHz)
    : avr::Device(builtinDescription(defaultName), clockHz, clockHz == 0 || clockHz == defaultClockHz ? defaultName : std::string_view{}) {}
}  // namespace siliconforge::mcu
