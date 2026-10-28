// Compile the public MCU APIs in isolation: consuming the registry/device must not pull in full JSON definitions.
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/arch/avr/Device.hpp>

#ifdef INCLUDE_NLOHMANN_JSON_HPP_
#error "Public MCU headers must use json_fwd.hpp instead of json.hpp"
#endif
