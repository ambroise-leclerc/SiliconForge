#pragma once
#include <nlohmann/json_fwd.hpp>
#include <siliconforge/core/DescriptionError.hpp>
#include <siliconforge/core/Mcu.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace siliconforge::mcu {
using LoadError = DescriptionError;
[[nodiscard]] std::expected<void, LoadError>                 validate(const nlohmann::json& description);
[[nodiscard]] std::expected<std::unique_ptr<Mcu>, LoadError> load(const std::filesystem::path& path);
[[nodiscard]] std::expected<std::unique_ptr<Mcu>, LoadError> loadJson(std::string_view text);
[[nodiscard]] nlohmann::json                                 builtinDescription(std::string_view name);
}  // namespace siliconforge::mcu
