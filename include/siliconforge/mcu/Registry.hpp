#pragma once

#include <siliconforge/core/Mcu.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace siliconforge::mcu {

/// Creates an emulated MCU by name, case-insensitively ("atmega328p"). Returns nullptr for an unknown name.
[[nodiscard]] std::unique_ptr<Mcu> create(std::string_view name);

/// Canonical lookup IDs accepted by create(): lowercase, including descriptions discovered by addPath().
/// Use Mcu::name() for the display name with the casing from JSON.
[[nodiscard]] std::vector<std::string_view> available();

/// Adds JSON descriptions from a directory. Invalid files are returned with their localized error.
/// Startup only: call before concurrent create()/available() calls. The catalog is not synchronized.
[[nodiscard]] std::vector<std::pair<std::filesystem::path, LoadError>> addPath(const std::filesystem::path& directory);

}  // namespace siliconforge::mcu
