#pragma once

#include <siliconforge/core/Mcu.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace siliconforge::mcu {

/// Creates an emulated MCU by name, case-insensitively ("atmega328p"). Returns nullptr for an unknown name.
[[nodiscard]] std::unique_ptr<Mcu> create(std::string_view name);

/// Names accepted by create().
[[nodiscard]] std::vector<std::string_view> available();

}  // namespace siliconforge::mcu
