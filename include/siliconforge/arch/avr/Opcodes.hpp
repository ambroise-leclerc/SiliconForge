#pragma once

#include <siliconforge/core/Types.hpp>

#include <array>
#include <filesystem>
#include <string_view>

namespace siliconforge::avr {

/// What a 16-bit word is, as the first word of an instruction, for the cores SiliconForge emulates (ADR 0006).
enum class OpcodeKind : u8 {
    Instruction,  ///< Executed. `mnemonic` is its key in the `timing` table of the MCU description.
    Reserved,     ///< Stops the execution with StopReason::InvalidOpcode.
};

struct OpcodeClass {
    OpcodeKind       kind;
    std::string_view mnemonic;  ///< Timing key ("add", "ld x+"...), empty for a reserved opcode.
    /// Smallest core variant that has the instruction: "AVR" (classic), "AVRe" (MOVW, enhanced LPM, SPM, BREAK) or
    /// "AVRe+" (multiplications). Empty for a reserved opcode.
    std::string_view variant;
    /// Other name of this very encoding in the assembler ("lsl" for ADD Rd,Rd, "breq" for BRBS 1,k), or empty.
    std::string_view alias;
    /// For a reserved opcode, the instruction it is on cores that are not emulated ("eijmp", "des"...), or empty.
    std::string_view elsewhere;
};

/// Classifies one opcode. Every value of the 16 bits has a class.
[[nodiscard]] OpcodeClass classify(u16 opcode);

/// Opcodes executed by the cores of this process, for the qualification reports (ADR 0006). Nothing is recorded, and
/// the emulation loop is the usual one, until start() is called.
class OpcodeCoverage {
public:
    static void               start() noexcept;
    [[nodiscard]] static bool recording() noexcept;
    /// One byte per opcode, non-zero when it was executed since start().
    [[nodiscard]] static const std::array<u8, 65536>& executed() noexcept;
    /// Writes executed() to a new file of `directory`, created if needed. The files of several processes are merged by
    /// merge(); a process that executed nothing writes no file.
    static void save(const std::filesystem::path& directory);
    /// Union of the files written by save() in `directory`.
    [[nodiscard]] static std::array<u8, 65536> merge(const std::filesystem::path& directory);
};

}  // namespace siliconforge::avr
