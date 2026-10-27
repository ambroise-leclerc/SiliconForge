#include "../support/AvrFixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace siliconforge;
using namespace siliconforge::avr;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

SCENARIO("8-bit additions set the status flags as documented", "[avr][alu]") {
    GIVEN("ADD of two positive numbers overflowing into the sign bit") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x7F).ldi(17, 0x01).add(16, 17)};
        THEN("the result is 0x80 with V, N and H set, C and Z clear, S = N xor V = 0") {
            CHECK(avr.execute(3) == 3);
            CHECK(avr.core.r(16) == 0x80);
            CHECK(avr.flag(sreg::V));
            CHECK(avr.flag(sreg::N));
            CHECK(avr.flag(sreg::H));
            CHECK_FALSE(avr.flag(sreg::C));
            CHECK_FALSE(avr.flag(sreg::Z));
            CHECK_FALSE(avr.flag(sreg::S));
        }
    }
    GIVEN("ADD producing a carry and a zero result, then ADC propagating it") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0xFF).ldi(17, 0x01).add(16, 17).ldi(18, 0).ldi(19, 0).adc(18, 19)};
        avr.execute(3);
        THEN("ADD gives 0 with C, Z and H") {
            CHECK(avr.core.r(16) == 0);
            CHECK(avr.flag(sreg::C));
            CHECK(avr.flag(sreg::Z));
            CHECK(avr.flag(sreg::H));
        }
        AND_THEN("ADC adds the carry") {
            avr.execute(3);
            CHECK(avr.core.r(18) == 1);
            CHECK_FALSE(avr.flag(sreg::C));
        }
    }
}

SCENARIO("Subtractions and comparisons chain through the carry and zero flags", "[avr][alu]") {
    GIVEN("a 16-bit comparison 0x0100 - 0x0001 with CP/CPC") {
        AvrFixture avr{AvrAssembler{}.ldi(24, 0x00).ldi(25, 0x01).ldi(26, 0x01).ldi(27, 0x00).cp(24, 26).cpc(25, 27)};
        avr.execute(5);
        THEN("CP borrows") {
            CHECK(avr.flag(sreg::C));
            CHECK_FALSE(avr.flag(sreg::Z));
        }
        AND_THEN("CPC consumes the borrow: 1 - 0 - 1 = 0 but Z stays clear because the low byte differed") {
            avr.execute();
            CHECK_FALSE(avr.flag(sreg::C));
            CHECK_FALSE(avr.flag(sreg::Z));
        }
    }
    GIVEN("SBCI on equal 16-bit values") {
        AvrFixture avr{AvrAssembler{}.ldi(24, 0x34).ldi(25, 0x12).subi(24, 0x34).sbci(25, 0x12)};
        avr.execute(4);
        THEN("the 16-bit result is zero with Z set") {
            CHECK(avr.core.pair(24) == 0);
            CHECK(avr.flag(sreg::Z));
            CHECK_FALSE(avr.flag(sreg::C));
        }
    }
    GIVEN("SUB with a signed overflow: 0x80 - 0x01") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x80).ldi(17, 0x01).sub(16, 17)};
        avr.execute(3);
        THEN("the result is 0x7F with V set, N clear, H set") {
            CHECK(avr.core.r(16) == 0x7F);
            CHECK(avr.flag(sreg::V));
            CHECK_FALSE(avr.flag(sreg::N));
            CHECK(avr.flag(sreg::S));
            CHECK(avr.flag(sreg::H));
            CHECK_FALSE(avr.flag(sreg::C));
        }
    }
}

SCENARIO("Single-operand instructions", "[avr][alu]") {
    WHEN("COM, NEG, INC, DEC, SWAP run") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x0F).com(16).ldi(17, 0x01).neg(17).ldi(18, 0x7F).inc(18).ldi(19, 0x80).dec(19).ldi(20, 0xA5).swap(20)};
        avr.execute(2);
        CHECK(avr.core.r(16) == 0xF0);
        CHECK(avr.flag(sreg::C));
        avr.execute(2);
        CHECK(avr.core.r(17) == 0xFF);
        CHECK(avr.flag(sreg::C));
        CHECK(avr.flag(sreg::H));
        avr.execute(2);
        CHECK(avr.core.r(18) == 0x80);
        CHECK(avr.flag(sreg::V));
        avr.execute(2);
        CHECK(avr.core.r(19) == 0x7F);
        CHECK(avr.flag(sreg::V));
        avr.execute(2);
        CHECK(avr.core.r(20) == 0x5A);
    }
    WHEN("shifts and rotations run") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x81).lsr(16).ldi(17, 0x81).asr(17).sec().ldi(18, 0x02).ror(18)};
        avr.execute(2);
        THEN("LSR shifts a zero in, bit 0 into C, and V = N xor C") {
            CHECK(avr.core.r(16) == 0x40);
            CHECK(avr.flag(sreg::C));
            CHECK(avr.flag(sreg::V));
        }
        avr.execute(2);
        THEN("ASR keeps the sign bit") { CHECK(avr.core.r(17) == 0xC0); }
        avr.execute(3);
        THEN("ROR shifts the previous carry in") {
            CHECK(avr.core.r(18) == 0x81);
            CHECK_FALSE(avr.flag(sreg::C));
        }
    }
}

SCENARIO("Word arithmetic on register pairs", "[avr][alu]") {
    WHEN("ADIW crosses 0xFFFF") {
        AvrFixture avr{AvrAssembler{}.ldi(24, 0xFF).ldi(25, 0xFF).adiw(24, 1)};
        THEN("it takes two cycles and sets C and Z") {
            avr.execute(2);
            CHECK(avr.execute() == 2);
            CHECK(avr.core.pair(24) == 0);
            CHECK(avr.flag(sreg::C));
            CHECK(avr.flag(sreg::Z));
        }
    }
    WHEN("SBIW goes below zero on Z") {
        AvrFixture avr{AvrAssembler{}.ldi(30, 0x00).ldi(31, 0x00).sbiw(30, 63)};
        avr.execute(3);
        THEN("the pair wraps and C, N are set") {
            CHECK(avr.core.pair(30) == static_cast<u16>(-63));
            CHECK(avr.flag(sreg::C));
            CHECK(avr.flag(sreg::N));
        }
    }
    WHEN("MOVW copies a pair") {
        AvrFixture avr{AvrAssembler{}.ldi(30, 0x34).ldi(31, 0x12).movw(26, 30)};
        avr.execute(3);
        CHECK(avr.core.pair(26) == 0x1234);
    }
}

SCENARIO("Multiplications write R1:R0 in two cycles", "[avr][alu]") {
    WHEN("MUL 200 * 200") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 200).ldi(17, 200).mul(16, 17)};
        avr.execute(2);
        CHECK(avr.execute() == 2);
        CHECK(avr.core.pair(0) == 40000);
        CHECK(avr.flag(sreg::C));  // Bit 15 of the product
    }
    WHEN("MULS -2 * 3") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0xFE).ldi(17, 3).muls(16, 17)};
        avr.execute(3);
        CHECK(avr.core.pair(0) == static_cast<u16>(-6));
    }
    WHEN("MULSU -1 * 255") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0xFF).ldi(17, 0xFF).mulsu(16, 17)};
        avr.execute(3);
        CHECK(avr.core.pair(0) == static_cast<u16>(-255));
    }
    WHEN("FMUL 0.5 * 0.5 in 1.7 format") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x40).ldi(17, 0x40).fmul(16, 17)};
        avr.execute(3);
        THEN("the 1.15 result is 0.25") { CHECK(avr.core.pair(0) == 0x2000); }
    }
    WHEN("FMULS -1 * -1 overflows to -1 (0x8000)") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x80).ldi(17, 0x80).fmuls(16, 17)};
        avr.execute(3);
        CHECK(avr.core.pair(0) == 0x8000);
    }
}

SCENARIO("Data transfers through the pointer registers", "[avr][memory]") {
    WHEN("ST X+ and LD -X walk SRAM") {
        AvrFixture avr{AvrAssembler{}.ldi(26, 0x00).ldi(27, 0x01).ldi(16, 0xAA).ldi(17, 0x55).stXInc(16).stXInc(17).ldXDec(18).ldXDec(19)};
        avr.execute(4);
        THEN("each access takes two cycles and updates X") {
            CHECK(avr.execute(2) == 4);
            CHECK(avr.core.pair(26) == 0x0102);
            CHECK(avr.mcu.readMemory(MemorySpace::Data, 0x100, 2) == std::vector<u8>{0xAA, 0x55});
            avr.execute(2);
            CHECK(avr.core.r(18) == 0x55);
            CHECK(avr.core.r(19) == 0xAA);
            CHECK(avr.core.pair(26) == 0x0100);
        }
    }
    WHEN("LDD Y+q and STD Z+q use a displacement") {
        AvrFixture avr{AvrAssembler{}.ldi(30, 0x00).ldi(31, 0x02).ldi(16, 0x42).stdZ(63, 16).ldi(28, 0x3E).ldi(29, 0x02).lddY(17, 1)};
        avr.execute(7);
        CHECK(avr.mcu.readMemory(MemorySpace::Data, 0x23F, 1)[0] == 0x42);
        CHECK(avr.core.r(17) == 0x42);
    }
    WHEN("LDS and STS address the data space directly") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x99).sts(0x0800, 16).lds(17, 0x0800)};
        avr.execute(1);
        CHECK(avr.execute(2) == 4);
        CHECK(avr.core.r(17) == 0x99);
    }
    WHEN("registers are reachable as data addresses 0 to 31") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x12).sts(5, 16)};
        avr.execute(2);
        CHECK(avr.core.r(5) == 0x12);
    }
    WHEN("LPM Z+ reads program memory bytes in little-endian order") {
        AvrFixture avr{AvrAssembler{}.ldi(30, 8).ldi(31, 0).lpmZInc(16).lpmZInc(17).raw(0xBEEF)};
        avr.execute(2);
        CHECK(avr.execute() == 3);
        avr.execute();
        CHECK(avr.core.r(16) == 0xEF);
        CHECK(avr.core.r(17) == 0xBE);
        CHECK(avr.core.pair(30) == 10);
    }
}

SCENARIO("Stack, calls and returns", "[avr][flow]") {
    GIVEN("SP initialised to RAMEND on reset") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x11).push(16).pop(17)};
        CHECK(avr.core.sp == 0x08FF);
        avr.execute();
        CHECK(avr.execute() == 2);
        CHECK(avr.core.sp == 0x08FE);
        CHECK(avr.execute() == 2);
        CHECK(avr.core.r(17) == 0x11);
        CHECK(avr.core.sp == 0x08FF);
    }
    GIVEN("a CALL to a subroutine") {
        // 0: call 4 / 2: rjmp -1 (loop) / 3: nop / 4: ldi r16, 7 / 5: ret
        AvrFixture avr{AvrAssembler{}.call(4).rjmp(-1).nop().ldi(16, 7).ret()};
        THEN("CALL takes four cycles and pushes the return address, high byte at the lower address") {
            CHECK(avr.execute() == 4);
            CHECK(avr.core.pc == 4);
            CHECK(avr.core.sp == 0x08FD);
            CHECK(avr.mcu.readMemory(MemorySpace::Data, 0x08FE, 2) == std::vector<u8>{0x00, 0x02});
        }
        AND_THEN("RET takes four cycles and returns after the CALL") {
            avr.execute(2);
            CHECK(avr.execute() == 4);
            CHECK(avr.core.pc == 2);
            CHECK(avr.core.r(16) == 7);
        }
    }
    GIVEN("RCALL and ICALL") {
        // 0: rcall +2 -> 3 / 1: ldi r30,6 / 2: icall -> 6 / 3: ret / 4,5: nop / 6: ret
        AvrFixture avr{AvrAssembler{}.rcall(2).ldi(30, 6).icall().ret().nop().nop().ret()};
        CHECK(avr.execute() == 3);
        CHECK(avr.core.pc == 3);
        avr.execute(2);
        CHECK(avr.execute() == 3);
        CHECK(avr.core.pc == 6);
    }
}

SCENARIO("Branches and skips account for their extra cycles", "[avr][flow]") {
    WHEN("BRNE is taken, then not taken") {
        // 0: ldi r16,2 / 1: dec r16 / 2: brne -2 (to 1) / 3: nop
        AvrFixture avr{AvrAssembler{}.ldi(16, 2).dec(16).brne(-2).nop()};
        avr.execute(2);
        CHECK(avr.execute() == 2);
        CHECK(avr.core.pc == 1);
        avr.execute();
        CHECK(avr.execute() == 1);
        CHECK(avr.core.pc == 3);
    }
    WHEN("CPSE skips a one-word then a two-word instruction") {
        AvrFixture avr{AvrAssembler{}.cpse(0, 1).nop().cpse(0, 1).jmp(0).nop()};
        CHECK(avr.execute() == 2);
        CHECK(avr.core.pc == 2);
        CHECK(avr.execute() == 3);
        CHECK(avr.core.pc == 5);
    }
    WHEN("SBRS/SBRC test a register bit") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x04).sbrs(16, 2).nop().sbrc(16, 2).nop()};
        avr.execute();
        CHECK(avr.execute() == 2);
        CHECK(avr.core.pc == 3);
        CHECK(avr.execute() == 1);
        CHECK(avr.core.pc == 4);
    }
    WHEN("RJMP, JMP and IJMP jump") {
        AvrFixture avr{AvrAssembler{}.rjmp(1).nop().jmp(5).nop().ijmp()};
        CHECK(avr.execute() == 2);
        CHECK(avr.core.pc == 2);
        CHECK(avr.execute() == 3);
        CHECK(avr.core.pc == 5);
        CHECK(avr.execute() == 2);
        CHECK(avr.core.pc == 0);
    }
}

SCENARIO("Bit and status register instructions", "[avr][bits]") {
    WHEN("BST and BLD copy a bit through T") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x80).bst(16, 7).ldi(17, 0).bld(17, 0)};
        avr.execute(4);
        CHECK(avr.flag(sreg::T));
        CHECK(avr.core.r(17) == 0x01);
    }
    WHEN("SREG is read and written as an I/O register") {
        AvrFixture avr{AvrAssembler{}.sec().in(16, 0x3F).ldi(17, 0x02).out(0x3F, 17)};
        avr.execute(4);
        CHECK(avr.core.r(16) == sreg::C);
        CHECK(avr.core.sreg == sreg::Z);
    }
    WHEN("SP is written through SPL/SPH") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x34).ldi(17, 0x02).out(0x3D, 16).out(0x3E, 17)};
        avr.execute(4);
        CHECK(avr.core.sp == 0x0234);
    }
}

SCENARIO("Stopping instructions", "[avr][debug]") {
    WHEN("BREAK executes") {
        AvrFixture avr{AvrAssembler{}.nop().break_().nop()};
        const auto result = avr.mcu.run(100);
        THEN("the run stops on it") {
            CHECK(result.reason == StopReason::BreakInstruction);
            CHECK(avr.core.pc == 2);
        }
    }
    WHEN("an opcode is not part of the instruction set") {
        AvrFixture avr{AvrAssembler{}.nop().raw(0xFFFF)};
        const auto result = avr.mcu.run(100);
        THEN("the run stops with the PC on it") {
            CHECK(result.reason == StopReason::InvalidOpcode);
            CHECK(avr.core.pc == 1);
        }
    }
    WHEN("a breakpoint is set") {
        AvrFixture avr{AvrAssembler{}.nop().nop().inc(16).rjmp(-2)};
        avr.mcu.debugger().addBreakpoint(4);
        const auto first = avr.mcu.run(1000);
        THEN("the run stops before the instruction, and resuming executes it") {
            CHECK(first.reason == StopReason::Breakpoint);
            CHECK(avr.mcu.programCounter() == 4);
            CHECK(avr.core.r(16) == 0);
            CHECK(avr.mcu.run(1000).reason == StopReason::Breakpoint);
            CHECK(avr.core.r(16) == 1);
        }
    }
}

TEST_CASE("Every opcode decodes to a mnemonic", "[avr][decoder]") {
    CHECK(mnemonic(0x0000) == "nop");
    CHECK(mnemonic(0x9478) == "bset");
    CHECK(mnemonic(0x940C) == "jmp");
    CHECK(mnemonic(0x8000) == "ldd z");
    CHECK(mnemonic(0x9409) == "ijmp");
    CHECK(mnemonic(0xFFFF) == "(invalid)");
    CHECK(mnemonic(0x9419) == "(invalid)");  // EIJMP does not exist on the ATmega328P
    CHECK(isTwoWord(0x9000));
    CHECK(isTwoWord(0x9200));
    CHECK(isTwoWord(0x940E));
    CHECK_FALSE(isTwoWord(0x9001));
}
