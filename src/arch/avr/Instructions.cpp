// AVR instruction semantics and status flags. Timings are supplied by the compiled MCU description.
// Instruction semantics follow the "AVR Instruction Set Manual" (Microchip DS40002198).

#include <nlohmann/json.hpp>
#include <siliconforge/arch/avr/AvrCore.hpp>

#include <utility>

namespace siliconforge::avr {

namespace {

using namespace sreg;

// Operand extraction ---------------------------------------------------------------------------------------------
constexpr unsigned d5(u16 op) {
    return (op >> 4) & 0x1F;
}
constexpr unsigned r5(u16 op) {
    return (op & 0x0F) | ((op >> 5) & 0x10);
}
constexpr unsigned d4(u16 op) {
    return 16 + ((op >> 4) & 0x0F);
}
constexpr unsigned r4(u16 op) {
    return 16 + (op & 0x0F);
}
constexpr unsigned d3(u16 op) {
    return 16 + ((op >> 4) & 0x07);
}
constexpr unsigned r3(u16 op) {
    return 16 + (op & 0x07);
}
constexpr u8 k8(u16 op) {
    return static_cast<u8>((op & 0x0F) | ((op >> 4) & 0xF0));
}
constexpr unsigned a6(u16 op) {
    return (op & 0x0F) | ((op >> 5) & 0x30);
}
constexpr unsigned a5(u16 op) {
    return (op >> 3) & 0x1F;
}
constexpr unsigned b3(u16 op) {
    return op & 0x07;
}
constexpr unsigned q6(u16 op) {
    return (op & 0x07) | ((op >> 7) & 0x18) | ((op >> 8) & 0x20);
}
constexpr int k7(u16 op) {
    return static_cast<int>((op >> 3) & 0x7F) - ((op & 0x0200) ? 0x80 : 0);
}
constexpr int k12(u16 op) {
    return static_cast<int>(op & 0x0FFF) - ((op & 0x0800) ? 0x1000 : 0);
}

constexpr bool bit(unsigned value, unsigned n) {
    return (value >> n) & 1u;
}

// Flag helpers -----------------------------------------------------------------------------------------------------
void setFlags(AvrCore& core, u8 mask, u8 values) {
    core.sreg = static_cast<u8>((core.sreg & ~mask) | (values & mask));
}

/// N, Z, S from the result and the V already computed.
u8 nzs(u8 result, bool v) {
    const bool n = bit(result, 7);
    return static_cast<u8>((n ? N : 0) | (result == 0 ? Z : 0) | (v ? V : 0) | ((n != v) ? S : 0));
}

u8 add8(AvrCore& core, u8 rd, u8 rr, bool carry) {
    const u8   result = static_cast<u8>(rd + rr + (carry ? 1 : 0));
    const bool h      = (bit(rd, 3) && bit(rr, 3)) || (bit(rr, 3) && !bit(result, 3)) || (!bit(result, 3) && bit(rd, 3));
    const bool c      = (bit(rd, 7) && bit(rr, 7)) || (bit(rr, 7) && !bit(result, 7)) || (!bit(result, 7) && bit(rd, 7));
    const bool v      = (bit(rd, 7) && bit(rr, 7) && !bit(result, 7)) || (!bit(rd, 7) && !bit(rr, 7) && bit(result, 7));
    setFlags(core, H | S | V | N | Z | C, static_cast<u8>(nzs(result, v) | (h ? H : 0) | (c ? C : 0)));
    return result;
}

/// Subtraction; `keepZ` implements SBC/SBCI/CPC where Z is only kept set if the result is zero.
u8 sub8(AvrCore& core, u8 rd, u8 rr, bool carry, bool keepZ) {
    const u8   result = static_cast<u8>(rd - rr - (carry ? 1 : 0));
    const bool h      = (!bit(rd, 3) && bit(rr, 3)) || (bit(rr, 3) && bit(result, 3)) || (bit(result, 3) && !bit(rd, 3));
    const bool c      = (!bit(rd, 7) && bit(rr, 7)) || (bit(rr, 7) && bit(result, 7)) || (bit(result, 7) && !bit(rd, 7));
    const bool v      = (bit(rd, 7) && !bit(rr, 7) && !bit(result, 7)) || (!bit(rd, 7) && bit(rr, 7) && bit(result, 7));
    auto       flags  = static_cast<u8>(nzs(result, v) | (h ? H : 0) | (c ? C : 0));
    if (keepZ && !(core.sreg & Z))
        flags &= static_cast<u8>(~Z);
    setFlags(core, H | S | V | N | Z | C, flags);
    return result;
}

void logicFlags(AvrCore& core, u8 result) {
    setFlags(core, S | V | N | Z, nzs(result, false));
}

/// Flags of the shifts and rotations: C from the shifted-out bit, V = N xor C.
void shiftFlags(AvrCore& core, u8 result, bool carry) {
    const bool n = bit(result, 7);
    setFlags(core, S | V | N | Z | C, static_cast<u8>(nzs(result, n != carry) | (carry ? C : 0)));
}

/// Skips the next instruction and returns its size in words, selecting the timing case.
u8 skip(AvrCore& core) {
    const auto next = core.flash()[core.pc & core.pcMask()];
    const auto size = isTwoWord(next) ? 2u : 1u;
    core.pc         = (core.pc + size) & core.pcMask();
    return static_cast<u8>(size);
}

void jumpRelative(AvrCore& core, int offset) {
    core.pc = static_cast<u32>(static_cast<int>(core.pc) + offset) & core.pcMask();
}

// Arithmetic and logic -----------------------------------------------------------------------------------------------
u8 ADD(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = add8(c, c.r(d5(op)), c.r(r5(op)), false);
    return static_cast<u8>(timing);
}
u8 ADC(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = add8(c, c.r(d5(op)), c.r(r5(op)), c.sreg & C);
    return static_cast<u8>(timing);
}
u8 SUB(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = sub8(c, c.r(d5(op)), c.r(r5(op)), false, false);
    return static_cast<u8>(timing);
}
u8 SBC(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = sub8(c, c.r(d5(op)), c.r(r5(op)), c.sreg & C, true);
    return static_cast<u8>(timing);
}
u8 SUBI(AvrCore& c, u16 op, u32 timing) {
    c.r(d4(op)) = sub8(c, c.r(d4(op)), k8(op), false, false);
    return static_cast<u8>(timing);
}
u8 SBCI(AvrCore& c, u16 op, u32 timing) {
    c.r(d4(op)) = sub8(c, c.r(d4(op)), k8(op), c.sreg & C, true);
    return static_cast<u8>(timing);
}
u8 CP(AvrCore& c, u16 op, u32 timing) {
    sub8(c, c.r(d5(op)), c.r(r5(op)), false, false);
    return static_cast<u8>(timing);
}
u8 CPC(AvrCore& c, u16 op, u32 timing) {
    sub8(c, c.r(d5(op)), c.r(r5(op)), c.sreg & C, true);
    return static_cast<u8>(timing);
}
u8 CPI(AvrCore& c, u16 op, u32 timing) {
    sub8(c, c.r(d4(op)), k8(op), false, false);
    return static_cast<u8>(timing);
}

u8 AND(AvrCore& c, u16 op, u32 timing) {
    logicFlags(c, c.r(d5(op)) &= c.r(r5(op)));
    return static_cast<u8>(timing);
}
u8 OR(AvrCore& c, u16 op, u32 timing) {
    logicFlags(c, c.r(d5(op)) |= c.r(r5(op)));
    return static_cast<u8>(timing);
}
u8 EOR(AvrCore& c, u16 op, u32 timing) {
    logicFlags(c, c.r(d5(op)) ^= c.r(r5(op)));
    return static_cast<u8>(timing);
}
u8 ANDI(AvrCore& c, u16 op, u32 timing) {
    logicFlags(c, c.r(d4(op)) &= k8(op));
    return static_cast<u8>(timing);
}
u8 ORI(AvrCore& c, u16 op, u32 timing) {
    logicFlags(c, c.r(d4(op)) |= k8(op));
    return static_cast<u8>(timing);
}

u8 COM(AvrCore& c, u16 op, u32 timing) {
    auto& rd = c.r(d5(op));
    rd       = static_cast<u8>(~rd);
    setFlags(c, S | V | N | Z | C, static_cast<u8>(nzs(rd, false) | C));
    return static_cast<u8>(timing);
}

u8 NEG(AvrCore& c, u16 op, u32 timing) {
    auto&    rd     = c.r(d5(op));
    const u8 before = rd;
    rd              = static_cast<u8>(0 - before);
    const bool h    = bit(rd, 3) || bit(before, 3);
    setFlags(c, H | S | V | N | Z | C, static_cast<u8>(nzs(rd, rd == 0x80) | (h ? H : 0) | (rd != 0 ? C : 0)));
    return static_cast<u8>(timing);
}

u8 INC(AvrCore& c, u16 op, u32 timing) {
    auto& rd = c.r(d5(op));
    ++rd;
    setFlags(c, S | V | N | Z, nzs(rd, rd == 0x80));
    return static_cast<u8>(timing);
}

u8 DEC(AvrCore& c, u16 op, u32 timing) {
    auto& rd = c.r(d5(op));
    --rd;
    setFlags(c, S | V | N | Z, nzs(rd, rd == 0x7F));
    return static_cast<u8>(timing);
}

u8 ADIW(AvrCore& c, u16 op, u32 timing) {
    const auto d      = 24 + ((op >> 3) & 0x06);
    const auto k      = static_cast<u16>((op & 0x0F) | ((op >> 2) & 0x30));
    const u16  before = c.pair(d);
    const u16  result = static_cast<u16>(before + k);
    c.setPair(d, result);
    const bool v   = !bit(before, 15) && bit(result, 15);
    const bool n   = bit(result, 15);
    const bool k15 = !bit(result, 15) && bit(before, 15);
    setFlags(c, S | V | N | Z | C, static_cast<u8>((n ? N : 0) | (v ? V : 0) | (n != v ? S : 0) | (result == 0 ? Z : 0) | (k15 ? C : 0)));
    return static_cast<u8>(timing);
}

u8 SBIW(AvrCore& c, u16 op, u32 timing) {
    const auto d      = 24 + ((op >> 3) & 0x06);
    const auto k      = static_cast<u16>((op & 0x0F) | ((op >> 2) & 0x30));
    const u16  before = c.pair(d);
    const u16  result = static_cast<u16>(before - k);
    c.setPair(d, result);
    const bool v     = bit(before, 15) && !bit(result, 15);
    const bool n     = bit(result, 15);
    const bool carry = bit(result, 15) && !bit(before, 15);
    setFlags(c, S | V | N | Z | C, static_cast<u8>((n ? N : 0) | (v ? V : 0) | (n != v ? S : 0) | (result == 0 ? Z : 0) | (carry ? C : 0)));
    return static_cast<u8>(timing);
}

u8 LSR(AvrCore& c, u16 op, u32 timing) {
    auto&      rd    = c.r(d5(op));
    const bool carry = rd & 1;
    rd               = static_cast<u8>(rd >> 1);
    shiftFlags(c, rd, carry);
    return static_cast<u8>(timing);
}

u8 ROR(AvrCore& c, u16 op, u32 timing) {
    auto&      rd    = c.r(d5(op));
    const bool carry = rd & 1;
    rd               = static_cast<u8>((rd >> 1) | ((c.sreg & C) ? 0x80 : 0));
    shiftFlags(c, rd, carry);
    return static_cast<u8>(timing);
}

u8 ASR(AvrCore& c, u16 op, u32 timing) {
    auto&      rd    = c.r(d5(op));
    const bool carry = rd & 1;
    rd               = static_cast<u8>((rd >> 1) | (rd & 0x80));
    shiftFlags(c, rd, carry);
    return static_cast<u8>(timing);
}

u8 SWAP(AvrCore& c, u16 op, u32 timing) {
    auto& rd = c.r(d5(op));
    rd       = static_cast<u8>((rd << 4) | (rd >> 4));
    return static_cast<u8>(timing);
}

/// MUL family: R1:R0 = product, C = bit 15 of the product (before the fractional shift), Z from the result.
void storeProduct(AvrCore& c, int product, bool fractional) {
    auto       value = static_cast<u16>(product);
    const bool carry = bit(value, 15);
    if (fractional)
        value = static_cast<u16>(value << 1);
    c.setPair(0, value);
    setFlags(c, Z | C, static_cast<u8>((value == 0 ? Z : 0) | (carry ? C : 0)));
}

u8 MUL(AvrCore& c, u16 op, u32 timing) {
    storeProduct(c, c.r(d5(op)) * c.r(r5(op)), false);
    return static_cast<u8>(timing);
}
u8 MULS(AvrCore& c, u16 op, u32 timing) {
    storeProduct(c, static_cast<i8>(c.r(d4(op))) * static_cast<i8>(c.r(r4(op))), false);
    return static_cast<u8>(timing);
}
u8 MULSU(AvrCore& c, u16 op, u32 timing) {
    storeProduct(c, static_cast<i8>(c.r(d3(op))) * c.r(r3(op)), false);
    return static_cast<u8>(timing);
}
u8 FMUL(AvrCore& c, u16 op, u32 timing) {
    storeProduct(c, c.r(d3(op)) * c.r(r3(op)), true);
    return static_cast<u8>(timing);
}
u8 FMULS(AvrCore& c, u16 op, u32 timing) {
    storeProduct(c, static_cast<i8>(c.r(d3(op))) * static_cast<i8>(c.r(r3(op))), true);
    return static_cast<u8>(timing);
}
u8 FMULSU(AvrCore& c, u16 op, u32 timing) {
    storeProduct(c, static_cast<i8>(c.r(d3(op))) * c.r(r3(op)), true);
    return static_cast<u8>(timing);
}

// Data transfer --------------------------------------------------------------------------------------------------------
u8 MOV(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = c.r(r5(op));
    return static_cast<u8>(timing);
}
u8 MOVW(AvrCore& c, u16 op, u32 timing) {
    c.setPair(((op >> 4) & 0x0F) * 2, c.pair((op & 0x0F) * 2));
    return static_cast<u8>(timing);
}
u8 LDI(AvrCore& c, u16 op, u32 timing) {
    c.r(d4(op)) = k8(op);
    return static_cast<u8>(timing);
}

u8 LDS(AvrCore& c, u16 op, u32 timing) {
    const auto address = c.fetch();
    c.r(d5(op))        = c.data.read(address);
    return c.memoryCycles(address, timing);
}
u8 STS(AvrCore& c, u16 op, u32 timing) {
    const auto address = c.fetch();
    c.data.write(address, c.r(d5(op)));
    return c.memoryCycles(address, timing);
}

/// LD/ST through X (26), Y (28) or Z (30), with optional post-increment or pre-decrement.
enum class Mode { Plain, PostIncrement, PreDecrement };

template <unsigned Pointer, Mode M>
u8 LD(AvrCore& c, u16 op, u32 timing) {
    auto address = c.pair(Pointer);
    if constexpr (M == Mode::PreDecrement)
        c.setPair(Pointer, --address);
    const auto value = c.data.read(address);
    if constexpr (M == Mode::PostIncrement)
        c.setPair(Pointer, static_cast<u16>(address + 1));
    c.r(d5(op)) = value;
    return c.memoryCycles(address, timing);
}

template <unsigned Pointer, Mode M>
u8 ST(AvrCore& c, u16 op, u32 timing) {
    auto       address = c.pair(Pointer);
    const auto value   = c.r(d5(op));  // Read before the pointer update: ST X+, r26 stores the original value.
    if constexpr (M == Mode::PreDecrement)
        c.setPair(Pointer, --address);
    c.data.write(address, value);
    if constexpr (M == Mode::PostIncrement)
        c.setPair(Pointer, static_cast<u16>(address + 1));
    return c.memoryCycles(address, timing);
}

template <unsigned Pointer>
u8 LDD(AvrCore& c, u16 op, u32 timing) {
    const auto address = static_cast<u16>(c.pair(Pointer) + q6(op));
    c.r(d5(op))        = c.data.read(address);
    return c.memoryCycles(address, timing);
}

template <unsigned Pointer>
u8 STD(AvrCore& c, u16 op, u32 timing) {
    const auto address = static_cast<u16>(c.pair(Pointer) + q6(op));
    c.data.write(address, c.r(d5(op)));
    return c.memoryCycles(address, timing);
}

u8 LPM_R0(AvrCore& c, u16, u32 timing) {
    c.r(0) = c.flashByte(c.pair(30));
    return static_cast<u8>(timing);
}
u8 LPM(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = c.flashByte(c.pair(30));
    return static_cast<u8>(timing);
}
u8 LPM_INC(AvrCore& c, u16 op, u32 timing) {
    const auto z = c.pair(30);
    c.r(d5(op))  = c.flashByte(z);
    c.setPair(30, static_cast<u16>(z + 1));
    return static_cast<u8>(timing);
}
u8 SPM(AvrCore& c, u16, u32 timing) {
    c.executeSpm();
    return static_cast<u8>(timing);
}

u8 IN(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = c.data.read(a6(op) + 0x20);
    return static_cast<u8>(timing);
}
u8 OUT(AvrCore& c, u16 op, u32 timing) {
    c.data.write(a6(op) + 0x20, c.r(d5(op)));
    return static_cast<u8>(timing);
}
u8 PUSH(AvrCore& c, u16 op, u32 timing) {
    c.push(c.r(d5(op)));
    return c.memoryCycles(c.sp + 1, timing);
}
u8 POP(AvrCore& c, u16 op, u32 timing) {
    c.r(d5(op)) = c.pop();
    return c.memoryCycles(c.sp, timing);
}

// Bit and bit-test -------------------------------------------------------------------------------------------------
u8 SBI(AvrCore& c, u16 op, u32 timing) {
    c.data.write(a5(op) + 0x20, 0xFF, static_cast<u8>(1 << b3(op)));
    return static_cast<u8>(timing);
}
u8 CBI(AvrCore& c, u16 op, u32 timing) {
    c.data.write(a5(op) + 0x20, 0x00, static_cast<u8>(1 << b3(op)));
    return static_cast<u8>(timing);
}
u8 BST(AvrCore& c, u16 op, u32 timing) {
    setFlags(c, T, bit(c.r(d5(op)), b3(op)) ? T : 0);
    return static_cast<u8>(timing);
}
u8 BLD(AvrCore& c, u16 op, u32 timing) {
    auto&      rd   = c.r(d5(op));
    const auto mask = static_cast<u8>(1 << b3(op));
    rd              = static_cast<u8>((c.sreg & T) ? (rd | mask) : (rd & ~mask));
    return static_cast<u8>(timing);
}
u8 BSET(AvrCore& c, u16 op, u32 timing) {
    const auto flag = static_cast<u8>(1 << ((op >> 4) & 7));
    if (flag == I && !(c.sreg & I))
        c.interruptDelay = true;  // SEI: the next instruction executes before any pending interrupt.
    c.sreg |= flag;
    return static_cast<u8>(timing);
}
u8 BCLR(AvrCore& c, u16 op, u32 timing) {
    c.sreg &= static_cast<u8>(~(1 << ((op >> 4) & 7)));
    return static_cast<u8>(timing);
}

// Branches -------------------------------------------------------------------------------------------------------------
u8 RJMP(AvrCore& c, u16 op, u32 timing) {
    jumpRelative(c, k12(op));
    return static_cast<u8>(timing);
}
u8 RCALL(AvrCore& c, u16 op, u32 timing) {
    c.pushPc(c.pc);
    jumpRelative(c, k12(op));
    return static_cast<u8>(timing);
}
u8 JMP(AvrCore& c, u16 op, u32 timing) {
    const u32 high = ((op >> 3) & 0x3E) | (op & 1);
    c.pc           = ((high << 16) | c.fetch()) & c.pcMask();
    return static_cast<u8>(timing);
}
u8 CALL(AvrCore& c, u16 op, u32 timing) {
    const u32 high   = ((op >> 3) & 0x3E) | (op & 1);
    const u32 target = ((high << 16) | c.fetch()) & c.pcMask();
    c.pushPc(c.pc);
    c.pc = target;
    return static_cast<u8>(timing);
}
u8 IJMP(AvrCore& c, u16, u32 timing) {
    c.pc = c.pair(30) & c.pcMask();
    return static_cast<u8>(timing);
}
u8 ICALL(AvrCore& c, u16, u32 timing) {
    c.pushPc(c.pc);
    c.pc = c.pair(30) & c.pcMask();
    return static_cast<u8>(timing);
}
u8 RET(AvrCore& c, u16, u32 timing) {
    c.pc = c.popPc();
    return static_cast<u8>(timing);
}
u8 RETI(AvrCore& c, u16, u32 timing) {
    c.pc             = c.popPc();
    c.sreg          |= I;
    c.interruptDelay = true;  // One instruction of the main program always executes between two interrupts.
    return static_cast<u8>(timing);
}

u8 BRBS(AvrCore& c, u16 op, u32 timing) {
    if ((c.sreg >> b3(op)) & 1) {
        jumpRelative(c, k7(op));
        return static_cast<u8>(timing >> 8);
    }
    return static_cast<u8>(timing);
}
u8 BRBC(AvrCore& c, u16 op, u32 timing) {
    if (!((c.sreg >> b3(op)) & 1)) {
        jumpRelative(c, k7(op));
        return static_cast<u8>(timing >> 8);
    }
    return static_cast<u8>(timing);
}

u8 CPSE(AvrCore& c, u16 op, u32 timing) {
    return static_cast<u8>(timing >> ((c.r(d5(op)) == c.r(r5(op))) ? 8 * skip(c) : 0));
}
u8 SBRC(AvrCore& c, u16 op, u32 timing) {
    return static_cast<u8>(timing >> ((!bit(c.r(d5(op)), b3(op))) ? 8 * skip(c) : 0));
}
u8 SBRS(AvrCore& c, u16 op, u32 timing) {
    return static_cast<u8>(timing >> ((bit(c.r(d5(op)), b3(op))) ? 8 * skip(c) : 0));
}
u8 SBIC(AvrCore& c, u16 op, u32 timing) {
    return static_cast<u8>(timing >> ((!bit(c.data.read(a5(op) + 0x20), b3(op))) ? 8 * skip(c) : 0));
}
u8 SBIS(AvrCore& c, u16 op, u32 timing) {
    return static_cast<u8>(timing >> ((bit(c.data.read(a5(op) + 0x20), b3(op))) ? 8 * skip(c) : 0));
}

// MCU control --------------------------------------------------------------------------------------------------------
u8 NOP(AvrCore&, u16, u32 timing) {
    return static_cast<u8>(timing);
}
u8 SLEEP(AvrCore& c, u16, u32 timing) {
    if (c.config().smcr != 0 && (c.data.raw(c.config().smcr) & 1))
        c.sleeping = true;
    return static_cast<u8>(timing);
}
u8 WDR(AvrCore&, u16, u32 timing) {
    return static_cast<u8>(timing);
}
u8 BREAK(AvrCore& c, u16, u32 timing) {
    c.stopRequest = StopReason::BreakInstruction;
    return static_cast<u8>(timing);
}
u8 INVALID(AvrCore& c, u16, u32 timing) {
    c.pc          = (c.pc - 1) & c.pcMask();  // Stay on the faulty instruction for the debugger.
    c.stopRequest = StopReason::InvalidOpcode;
    return static_cast<u8>(timing);
}

struct Decoded {
    InstructionHandler handler;
    std::string_view   name;
};

/// Decodes one opcode. Order matters: the more specific patterns come first.
constexpr Decoded decode(u16 op) {
    const auto is = [op](u16 mask, u16 value) {
        return (op & mask) == value;
    };

    if (op == 0x0000)
        return {NOP, "nop"};
    if (is(0xFF00, 0x0100))
        return {MOVW, "movw"};
    if (is(0xFF00, 0x0200))
        return {MULS, "muls"};
    if (is(0xFF88, 0x0300))
        return {MULSU, "mulsu"};
    if (is(0xFF88, 0x0308))
        return {FMUL, "fmul"};
    if (is(0xFF88, 0x0380))
        return {FMULS, "fmuls"};
    if (is(0xFF88, 0x0388))
        return {FMULSU, "fmulsu"};
    if (is(0xFC00, 0x0400))
        return {CPC, "cpc"};
    if (is(0xFC00, 0x0800))
        return {SBC, "sbc"};
    if (is(0xFC00, 0x0C00))
        return {ADD, "add"};
    if (is(0xFC00, 0x1000))
        return {CPSE, "cpse"};
    if (is(0xFC00, 0x1400))
        return {CP, "cp"};
    if (is(0xFC00, 0x1800))
        return {SUB, "sub"};
    if (is(0xFC00, 0x1C00))
        return {ADC, "adc"};
    if (is(0xFC00, 0x2000))
        return {AND, "and"};
    if (is(0xFC00, 0x2400))
        return {EOR, "eor"};
    if (is(0xFC00, 0x2800))
        return {OR, "or"};
    if (is(0xFC00, 0x2C00))
        return {MOV, "mov"};
    if (is(0xF000, 0x3000))
        return {CPI, "cpi"};
    if (is(0xF000, 0x4000))
        return {SBCI, "sbci"};
    if (is(0xF000, 0x5000))
        return {SUBI, "subi"};
    if (is(0xF000, 0x6000))
        return {ORI, "ori"};
    if (is(0xF000, 0x7000))
        return {ANDI, "andi"};

    // LDD/STD with displacement (LD/ST Y and Z without displacement are the q = 0 case).
    if (is(0xD208, 0x8000))
        return {LDD<30>, "ldd z"};
    if (is(0xD208, 0x8008))
        return {LDD<28>, "ldd y"};
    if (is(0xD208, 0x8200))
        return {STD<30>, "std z"};
    if (is(0xD208, 0x8208))
        return {STD<28>, "std y"};

    if (is(0xFE0F, 0x9000))
        return {LDS, "lds"};
    if (is(0xFE0F, 0x9001))
        return {LD<30, Mode::PostIncrement>, "ld z+"};
    if (is(0xFE0F, 0x9002))
        return {LD<30, Mode::PreDecrement>, "ld -z"};
    if (is(0xFE0F, 0x9004))
        return {LPM, "lpm"};
    if (is(0xFE0F, 0x9005))
        return {LPM_INC, "lpm z+"};
    if (is(0xFE0F, 0x9009))
        return {LD<28, Mode::PostIncrement>, "ld y+"};
    if (is(0xFE0F, 0x900A))
        return {LD<28, Mode::PreDecrement>, "ld -y"};
    if (is(0xFE0F, 0x900C))
        return {LD<26, Mode::Plain>, "ld x"};
    if (is(0xFE0F, 0x900D))
        return {LD<26, Mode::PostIncrement>, "ld x+"};
    if (is(0xFE0F, 0x900E))
        return {LD<26, Mode::PreDecrement>, "ld -x"};
    if (is(0xFE0F, 0x900F))
        return {POP, "pop"};

    if (is(0xFE0F, 0x9200))
        return {STS, "sts"};
    if (is(0xFE0F, 0x9201))
        return {ST<30, Mode::PostIncrement>, "st z+"};
    if (is(0xFE0F, 0x9202))
        return {ST<30, Mode::PreDecrement>, "st -z"};
    if (is(0xFE0F, 0x9209))
        return {ST<28, Mode::PostIncrement>, "st y+"};
    if (is(0xFE0F, 0x920A))
        return {ST<28, Mode::PreDecrement>, "st -y"};
    if (is(0xFE0F, 0x920C))
        return {ST<26, Mode::Plain>, "st x"};
    if (is(0xFE0F, 0x920D))
        return {ST<26, Mode::PostIncrement>, "st x+"};
    if (is(0xFE0F, 0x920E))
        return {ST<26, Mode::PreDecrement>, "st -x"};
    if (is(0xFE0F, 0x920F))
        return {PUSH, "push"};

    if (op == 0x9409)
        return {IJMP, "ijmp"};
    if (op == 0x9509)
        return {ICALL, "icall"};
    if (op == 0x9508)
        return {RET, "ret"};
    if (op == 0x9518)
        return {RETI, "reti"};
    if (op == 0x9588)
        return {SLEEP, "sleep"};
    if (op == 0x9598)
        return {BREAK, "break"};
    if (op == 0x95A8)
        return {WDR, "wdr"};
    if (op == 0x95C8)
        return {LPM_R0, "lpm"};
    if (op == 0x95E8)
        return {SPM, "spm"};
    if (is(0xFF8F, 0x9408))
        return {BSET, "bset"};
    if (is(0xFF8F, 0x9488))
        return {BCLR, "bclr"};

    if (is(0xFE0F, 0x9400))
        return {COM, "com"};
    if (is(0xFE0F, 0x9401))
        return {NEG, "neg"};
    if (is(0xFE0F, 0x9402))
        return {SWAP, "swap"};
    if (is(0xFE0F, 0x9403))
        return {INC, "inc"};
    if (is(0xFE0F, 0x9405))
        return {ASR, "asr"};
    if (is(0xFE0F, 0x9406))
        return {LSR, "lsr"};
    if (is(0xFE0F, 0x9407))
        return {ROR, "ror"};
    if (is(0xFE0F, 0x940A))
        return {DEC, "dec"};
    if (is(0xFE0E, 0x940C))
        return {JMP, "jmp"};
    if (is(0xFE0E, 0x940E))
        return {CALL, "call"};

    if (is(0xFF00, 0x9600))
        return {ADIW, "adiw"};
    if (is(0xFF00, 0x9700))
        return {SBIW, "sbiw"};
    if (is(0xFF00, 0x9800))
        return {CBI, "cbi"};
    if (is(0xFF00, 0x9900))
        return {SBIC, "sbic"};
    if (is(0xFF00, 0x9A00))
        return {SBI, "sbi"};
    if (is(0xFF00, 0x9B00))
        return {SBIS, "sbis"};
    if (is(0xFC00, 0x9C00))
        return {MUL, "mul"};

    if (is(0xF800, 0xB000))
        return {IN, "in"};
    if (is(0xF800, 0xB800))
        return {OUT, "out"};
    if (is(0xF000, 0xC000))
        return {RJMP, "rjmp"};
    if (is(0xF000, 0xD000))
        return {RCALL, "rcall"};
    if (is(0xF000, 0xE000))
        return {LDI, "ldi"};
    if (is(0xFC00, 0xF000))
        return {BRBS, "brbs"};
    if (is(0xFC00, 0xF400))
        return {BRBC, "brbc"};
    if (is(0xFE08, 0xF800))
        return {BLD, "bld"};
    if (is(0xFE08, 0xFA00))
        return {BST, "bst"};
    if (is(0xFE08, 0xFC00))
        return {SBRC, "sbrc"};
    if (is(0xFE08, 0xFE00))
        return {SBRS, "sbrs"};

    return {INVALID, "(invalid)"};
}

}  // namespace

const std::array<InstructionHandler, 65536>& instructionTable() {
    static const auto table = [] {
        std::array<InstructionHandler, 65536> handlers{};
        for (u32 op = 0; op < handlers.size(); ++op)
            handlers[op] = decode(static_cast<u16>(op)).handler;
        return handlers;
    }();
    return table;
}

void AvrCore::setTiming(const nlohmann::json& timing) {
    decoded.resize(65536);
    for (u32 op = 0; op < decoded.size(); ++op) {
        const auto d = decode(static_cast<u16>(op));
        if (d.name == "(invalid)") {
            decoded[op] = {d.handler, 0x01010101};
            continue;
        }
        const auto& t    = timing.at(std::string(d.name));
        const auto  base = cfg.pcBytes == 3 ? t.value("pc3Bytes", t.at("cycles").get<u32>()) : t.at("cycles").get<u32>();
        decoded[op]      = {d.handler,
                            base | (t.value("taken", t.value("skipOneWord", base)) << 8) | (t.value("skipTwoWords", base) << 16) | (t.value("sram", base) << 24)};
    }
}

std::string_view mnemonic(u16 opcode) {
    return decode(opcode).name;
}

}  // namespace siliconforge::avr
