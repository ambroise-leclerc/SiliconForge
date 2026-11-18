#pragma once
// SHA-256 (FIPS 180-4) of the inputs of a bench run: enough to tell whether two runs had the same scenario, circuit and
// firmware, without a cryptography dependency.

#include <span>
#include <string>

namespace siliconforge::scenario::detail {

/// Lowercase hexadecimal digest of `bytes`.
[[nodiscard]] std::string sha256(std::span<const unsigned char> bytes);

}  // namespace siliconforge::scenario::detail
