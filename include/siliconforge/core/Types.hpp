#pragma once

#include <cstdint>

namespace siliconforge {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;

/// Number of core clock cycles elapsed since reset. Every simulated duration is expressed in cycles.
using Cycles = std::uint64_t;

/// Byte address in one of the target's address spaces.
using Address = std::uint32_t;

/// Why a run stopped before or at the end of its cycle budget.
enum class StopReason : u8 {
    BudgetReached,  ///< The requested cycles elapsed.
    Breakpoint,     ///< The program counter reached a breakpoint.
    Watchpoint,     ///< A watched data address was accessed.
    BreakInstruction,
    InvalidOpcode,
    Requested,  ///< Debugger::requestStop() was called.
};

struct RunResult {
    StopReason reason;
    Cycles     elapsed;
};

}  // namespace siliconforge
