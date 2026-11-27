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

struct InstructionCapabilities {
    bool rampz{false};
    bool eind{false};
    bool absoluteJump{true};
    bool spm{true};
};

/// Whether a recognized mnemonic is available on this device.
[[nodiscard]] constexpr bool supportsInstruction(std::string_view name, InstructionCapabilities capabilities) noexcept {
    if (name == "elpm") return capabilities.rampz;
    if (name == "eicall" || name == "eijmp") return capabilities.eind;
    if (name == "call" || name == "jmp") return capabilities.absoluteJump;
    if (name == "spm") return capabilities.spm;
    return name != "(invalid)";
}

/// Classifies one opcode for the selected instruction set. The default has the ATmega328P's capabilities.
[[nodiscard]] OpcodeClass classify(u16 opcode, InstructionCapabilities capabilities);
[[nodiscard]] OpcodeClass classify(u16 opcode, bool hasRampz = false, bool hasEind = false);

/// Opcodes executed by the cores of this process, for the qualification reports (ADR 0006). Nothing is recorded, and
/// the emulation loop is the usual one, until start() is called.
class OpcodeCoverage {
public:
    static void               start() noexcept;
    [[nodiscard]] static bool recording() noexcept;
    /// One byte per opcode, non-zero when it was executed since start().
    [[nodiscard]] static const std::array<u8, 65536>& executed() noexcept;
    /// Writes target bitmaps into directory/builtin/<target> and unattributed hits directly into directory.
    /// merge() combines files from several processes. Empty bitmaps produce no file.
    static void save(const std::filesystem::path& directory);
    /// Union of the files written by save() in `directory`.
    [[nodiscard]] static std::array<u8, 65536> merge(const std::filesystem::path& directory);

private:
    friend class AvrCore;
    /// Resolves the per-target bitmap once before executing a recorded batch.
    static u8* targetBuffer(std::string_view builtinTarget);
};

}  // namespace siliconforge::avr
