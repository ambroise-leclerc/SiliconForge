#pragma once

#include <nlohmann/json_fwd.hpp>
#include <siliconforge/arch/avr/DataSpace.hpp>
#include <siliconforge/arch/avr/InterruptController.hpp>
#include <siliconforge/core/Debugger.hpp>
#include <siliconforge/core/Scheduler.hpp>
#include <siliconforge/core/Types.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace siliconforge::avr {

/// Status register bits.
namespace sreg {
inline constexpr u8 C = 1 << 0;
inline constexpr u8 Z = 1 << 1;
inline constexpr u8 N = 1 << 2;
inline constexpr u8 V = 1 << 3;
inline constexpr u8 S = 1 << 4;
inline constexpr u8 H = 1 << 5;
inline constexpr u8 T = 1 << 6;
inline constexpr u8 I = 1 << 7;
}  // namespace sreg

/// What differs from one AVR device to another, as far as the core is concerned.
struct AvrCoreConfig {
    u32     flashBytes;          ///< Power of two.
    Address ioEnd;               ///< Last extended I/O address (0xFF on the ATmega328P).
    Address ramEnd;              ///< Last SRAM address, also the reset value of SP.
    u8      vectorWords{2};      ///< Size of an interrupt vector slot (2 when vectors hold a JMP).
    u16     flashPageBytes{128};  ///< SPM page size.
    Address spmcsr{0};            ///< Data address of SPMCSR, 0 when self-programming is not available.
    Address smcr{0};              ///< Data address of SMCR (sleep enable in bit 0), 0 when absent.
    Address spl{0x5D}, sph{0x5E}, sreg{0x5F};
    u8      pcBytes{2};
    u8      interruptCycles{4}, wakeUpCycles{4};
};

class AvrCore;
/// Timings are compiled into each decoding entry and passed in a register to the handler.
using InstructionHandler = u8 (*)(AvrCore& core, u16 opcode, u32 timing);
struct InstructionEntry {
    InstructionHandler handler;
    u32                timing;  ///< Packed normal, taken/skip-one, skip-two and SRAM cycle counts.
};

/// AVRe+ CPU core (megaAVR with MUL and MOVW, 16-bit program counter), precise to the cycle at instruction
/// granularity: every instruction accounts for its documented number of cycles, taken branches and skips included.
class AvrCore {
public:
    AvrCore(const AvrCoreConfig& config, DataSpace& data, Scheduler& scheduler, InterruptController& interrupts, Debugger& debugger);

    void reset();
    void setTiming(const nlohmann::json& timing);
    void loadFlash(Address address, std::span<const u8> bytes);
    void eraseFlash();

    RunResult run(Cycles budget) { return run(budget, true); }
    RunResult run(Cycles budget, bool honourBreakpoints);
    /// Executes one instruction, or serves one interrupt, or sleeps until the next event. Ignores breakpoints.
    RunResult step();

    // Architectural state ------------------------------------------------------------------------------------------
    [[nodiscard]] u8& r(unsigned index) noexcept { return regs[index]; }
    [[nodiscard]] u8  r(unsigned index) const noexcept { return regs[index]; }
    [[nodiscard]] u16 pair(unsigned low) const noexcept { return static_cast<u16>(regs[low] | regs[low + 1] << 8); }
    void              setPair(unsigned low, u16 value) noexcept {
        regs[low]     = static_cast<u8>(value);
        regs[low + 1] = static_cast<u8>(value >> 8);
    }

    u32    pc{0};  ///< Word address.
    u16    sp{0};
    u8     sreg{0};
    Cycles cycles{0};
    bool   sleeping{false};

    [[nodiscard]] const std::vector<u16>& flash() const noexcept { return flashWords; }
    [[nodiscard]] u32 pcMask() const noexcept { return static_cast<u32>(flashWords.size() - 1); }
    [[nodiscard]] const AvrCoreConfig& config() const noexcept { return cfg; }

    DataSpace&           data;
    Scheduler&           scheduler;
    InterruptController& interrupts;
    Debugger&            debugger;

    // Helpers used by the instruction handlers ---------------------------------------------------------------------
    u16  fetch() noexcept { return flashWords[pc++ & pcMask()]; }
    void push(u8 value) {
        data.write(sp, value);
        --sp;
    }
    u8 pop() {
        ++sp;
        return data.read(sp);
    }
    void pushPc(u32 address) {
        push(static_cast<u8>(address));
        push(static_cast<u8>(address >> 8));
        if (cfg.pcBytes == 3)
            push(static_cast<u8>(address >> 16));
    }
    u32 popPc() {
        const auto upper = cfg.pcBytes == 3 ? pop() : u8{0};
        const auto high = pop();
        const auto low  = pop();
        return static_cast<u32>(upper << 16 | high << 8 | low) & pcMask();
    }
    [[nodiscard]] u8 flashByte(u32 byteAddress) const noexcept {
        const auto word = flashWords[(byteAddress >> 1) & pcMask()];
        return static_cast<u8>(byteAddress & 1 ? word >> 8 : word);
    }
    void executeSpm();
    [[nodiscard]] u8 memoryCycles(Address address, u32 timing) const noexcept {
        return static_cast<u8>(timing >> (address > cfg.ioEnd ? 24 : 0));
    }

    /// Set by SEI and RETI: the next instruction always executes before a pending interrupt is served.
    bool interruptDelay{false};
    /// Set by instruction handlers that must stop the run (BREAK, invalid opcode).
    std::optional<StopReason> stopRequest;

private:
    void serviceInterrupt();

    std::vector<InstructionEntry> decoded;

    AvrCoreConfig    cfg;
    u8*              regs;
    std::vector<u16> flashWords;
    std::vector<u16> pageBuffer;
    std::optional<u32> resumeAt;  ///< PC of the breakpoint the last run stopped at, executed without stopping again.
};

/// Number of 16-bit words of the instruction starting with `opcode` (JMP, CALL, LDS and STS take two).
[[nodiscard]] constexpr bool isTwoWord(u16 opcode) noexcept {
    return (opcode & 0xFE0E) == 0x940C || (opcode & 0xFE0E) == 0x940E || (opcode & 0xFC0F) == 0x9000;
}

/// Handlers execute one fetched instruction and return its configured cycle count.
/// Decoding table indexed by the 16-bit opcode, built once.
[[nodiscard]] const std::array<InstructionHandler, 65536>& instructionTable();

/// Mnemonic of an opcode (without operands), for traces and tests.
[[nodiscard]] std::string_view mnemonic(u16 opcode);

}  // namespace siliconforge::avr
