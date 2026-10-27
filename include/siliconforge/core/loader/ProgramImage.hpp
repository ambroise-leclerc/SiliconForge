#pragma once

#include <siliconforge/core/Types.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace siliconforge {

/// Memory spaces a program image can target. Harvard architectures (AVR) keep code and data apart.
enum class MemorySpace : u8 { Program, Data, Eeprom };

struct Segment {
    MemorySpace     space{MemorySpace::Program};
    Address         address{0};
    std::vector<u8> bytes;
};

/// A firmware, independent of the file format it was loaded from.
struct ProgramImage {
    std::vector<Segment>            segments;
    std::map<Address, std::string>  symbols;  ///< Program-space addresses of function symbols (ELF only).

    /// Total number of bytes for one space.
    [[nodiscard]] std::size_t size(MemorySpace space = MemorySpace::Program) const {
        std::size_t total = 0;
        for (const auto& segment : segments)
            if (segment.space == space)
                total += segment.bytes.size();
        return total;
    }
};

struct LoadError {
    std::string message;
    std::size_t line{0};  ///< Line of an Intel HEX file, 0 when not applicable.
};

template <typename T>
using LoadResult = std::expected<T, LoadError>;

/// Parses Intel HEX text (record types 00 to 05).
LoadResult<ProgramImage> parseIntelHex(std::string_view text);

/// Parses a 32-bit little-endian ELF executable. `dataSpaceOffset` is the virtual address at which the toolchain maps
/// the data space (0x800000 for avr-gcc) and `eepromOffset` the one of the EEPROM (0x810000 for avr-gcc).
LoadResult<ProgramImage> parseElf(std::span<const u8> bytes, Address dataSpaceOffset = 0x800000, Address eepromOffset = 0x810000);

/// Loads a file, choosing the format from its content (ELF magic number) or else as Intel HEX.
LoadResult<ProgramImage> loadProgram(const std::filesystem::path& path);

}  // namespace siliconforge
