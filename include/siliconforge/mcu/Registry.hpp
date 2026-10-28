#pragma once

#include <siliconforge/core/Mcu.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace siliconforge::mcu {

/// Creates an emulated MCU by name, case-insensitively ("atmega328p"). Returns nullptr for an unknown name.
[[nodiscard]] std::unique_ptr<Mcu> create(std::string_view name);

/// Names accepted by create().
[[nodiscard]] std::vector<std::string_view> available();

/// Adds JSON descriptions from a directory. Invalid files are returned with their localized error.
[[nodiscard]] std::vector<std::pair<std::filesystem::path, LoadError>> addPath(const std::filesystem::path& directory);

}  // namespace siliconforge::mcu
