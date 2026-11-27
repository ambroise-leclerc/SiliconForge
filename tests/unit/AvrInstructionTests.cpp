#include "../support/AvrFixture.hpp"

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

using namespace siliconforge;
using namespace siliconforge::avr;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

const Scenario s8BitAdditionsSetTheStatusFlags{"8-bit additions set the status flags as documented", {"avr", "alu"}, [] {
    given("ADD of two positive numbers overflowing into the sign bit", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x7F).ldi(17, 0x01).add(16, 17)};
        then("the result is 0x80 with V, N and H set, C and Z clear, S = N xor V = 0", [&] {
            check(avr.execute(3) == 3);
            check(avr.core.r(16) == 0x80);
            check(avr.flag(sreg::V));
            check(avr.flag(sreg::N));
            check(avr.flag(sreg::H));
            check(!avr.flag(sreg::C));
            check(!avr.flag(sreg::Z));
            check(!avr.flag(sreg::S));
        });
    });
    given("ADD producing a carry and a zero result, then ADC propagating it", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0xFF).ldi(17, 0x01).add(16, 17).ldi(18, 0).ldi(19, 0).adc(18, 19)};
        avr.execute(3);
        then("ADD gives 0 with C, Z and H", [&] {
            check(avr.core.r(16) == 0);
            check(avr.flag(sreg::C));
            check(avr.flag(sreg::Z));
            check(avr.flag(sreg::H));
        });
        andThen("ADC adds the carry", [&] {
            avr.execute(3);
            check(avr.core.r(18) == 1);
            check(!avr.flag(sreg::C));
        });
    });
}};

const Scenario subtractionsAndComparisonsChainThroughTheCarry{"Subtractions and comparisons chain through the carry and zero flags", {"avr", "alu"}, [] {
    given("a 16-bit comparison 0x0100 - 0x0001 with CP/CPC", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(24, 0x00).ldi(25, 0x01).ldi(26, 0x01).ldi(27, 0x00).cp(24, 26).cpc(25, 27)};
        avr.execute(5);
        then("CP borrows", [&] {
            check(avr.flag(sreg::C));
            check(!avr.flag(sreg::Z));
        });
        andThen("CPC consumes the borrow: 1 - 0 - 1 = 0 but Z stays clear because the low byte differed", [&] {
            avr.execute();
            check(!avr.flag(sreg::C));
            check(!avr.flag(sreg::Z));
        });
    });
    given("SBCI on equal 16-bit values", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(24, 0x34).ldi(25, 0x12).subi(24, 0x34).sbci(25, 0x12)};
        avr.execute(4);
        then("the 16-bit result is zero with Z set", [&] {
            check(avr.core.pair(24) == 0);
            check(avr.flag(sreg::Z));
            check(!avr.flag(sreg::C));
        });
    });
    given("SUB with a signed overflow: 0x80 - 0x01", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x80).ldi(17, 0x01).sub(16, 17)};
        avr.execute(3);
        then("the result is 0x7F with V set, N clear, H set", [&] {
            check(avr.core.r(16) == 0x7F);
            check(avr.flag(sreg::V));
            check(!avr.flag(sreg::N));
            check(avr.flag(sreg::S));
            check(avr.flag(sreg::H));
            check(!avr.flag(sreg::C));
        });
    });
}};

const Scenario singleOperandInstructions{"Single-operand instructions", {"avr", "alu"}, [] {
    when("COM, NEG, INC, DEC, SWAP run", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x0F).com(16).ldi(17, 0x01).neg(17).ldi(18, 0x7F).inc(18).ldi(19, 0x80).dec(19).ldi(20, 0xA5).swap(20)};
        avr.execute(2);
        check(avr.core.r(16) == 0xF0);
        check(avr.flag(sreg::C));
        avr.execute(2);
        check(avr.core.r(17) == 0xFF);
        check(avr.flag(sreg::C));
        check(avr.flag(sreg::H));
        avr.execute(2);
        check(avr.core.r(18) == 0x80);
        check(avr.flag(sreg::V));
        avr.execute(2);
        check(avr.core.r(19) == 0x7F);
        check(avr.flag(sreg::V));
        avr.execute(2);
        check(avr.core.r(20) == 0x5A);
    });
    when("shifts and rotations run", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x81).lsr(16).ldi(17, 0x81).asr(17).sec().ldi(18, 0x02).ror(18)};
        avr.execute(2);
        then("LSR shifts a zero in, bit 0 into C, and V = N xor C", [&] {
            check(avr.core.r(16) == 0x40);
            check(avr.flag(sreg::C));
            check(avr.flag(sreg::V));
        });
        avr.execute(2);
        then("ASR keeps the sign bit", [&] { check(avr.core.r(17) == 0xC0); });
        avr.execute(3);
        then("ROR shifts the previous carry in", [&] {
            check(avr.core.r(18) == 0x81);
            check(!avr.flag(sreg::C));
        });
    });
}};

const Scenario wordArithmeticOnRegisterPairs{"Word arithmetic on register pairs", {"avr", "alu"}, [] {
    when("ADIW crosses 0xFFFF", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(24, 0xFF).ldi(25, 0xFF).adiw(24, 1)};
        then("it takes two cycles and sets C and Z", [&] {
            avr.execute(2);
            check(avr.execute() == 2);
            check(avr.core.pair(24) == 0);
            check(avr.flag(sreg::C));
            check(avr.flag(sreg::Z));
        });
    });
    when("SBIW goes below zero on Z", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(30, 0x00).ldi(31, 0x00).sbiw(30, 63)};
        avr.execute(3);
        then("the pair wraps and C, N are set", [&] {
            check(avr.core.pair(30) == static_cast<u16>(-63));
            check(avr.flag(sreg::C));
            check(avr.flag(sreg::N));
        });
    });
    when("MOVW copies a pair", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(30, 0x34).ldi(31, 0x12).movw(26, 30)};
        avr.execute(3);
        check(avr.core.pair(26) == 0x1234);
    });
}};

const Scenario multiplicationsWriteR1R0InTwoCycles{"Multiplications write R1:R0 in two cycles", {"avr", "alu"}, [] {
    when("MUL 200 * 200", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 200).ldi(17, 200).mul(16, 17)};
        avr.execute(2);
        check(avr.execute() == 2);
        check(avr.core.pair(0) == 40000);
        check(avr.flag(sreg::C));  // Bit 15 of the product
    });
    when("MULS -2 * 3", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0xFE).ldi(17, 3).muls(16, 17)};
        avr.execute(3);
        check(avr.core.pair(0) == static_cast<u16>(-6));
    });
    when("MULSU -1 * 255", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0xFF).ldi(17, 0xFF).mulsu(16, 17)};
        avr.execute(3);
        check(avr.core.pair(0) == static_cast<u16>(-255));
    });
    when("FMUL 0.5 * 0.5 in 1.7 format", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x40).ldi(17, 0x40).fmul(16, 17)};
        avr.execute(3);
        then("the 1.15 result is 0.25", [&] { check(avr.core.pair(0) == 0x2000); });
    });
    when("FMULS -1 * -1 overflows to -1 (0x8000)", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x80).ldi(17, 0x80).fmuls(16, 17)};
        avr.execute(3);
        check(avr.core.pair(0) == 0x8000);
    });
}};

const Scenario dataTransfersThroughThePointerRegisters{"Data transfers through the pointer registers", {"avr", "memory"}, [] {
    when("ST X+ and LD -X walk SRAM", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(26, 0x00).ldi(27, 0x01).ldi(16, 0xAA).ldi(17, 0x55).stXInc(16).stXInc(17).ldXDec(18).ldXDec(19)};
        avr.execute(4);
        then("each access takes two cycles and updates X", [&] {
            check(avr.execute(2) == 4);
            check(avr.core.pair(26) == 0x0102);
            check(avr.mcu.readMemory(MemorySpace::Data, 0x100, 2) == std::vector<u8>{0xAA, 0x55});
            avr.execute(2);
            check(avr.core.r(18) == 0x55);
            check(avr.core.r(19) == 0xAA);
            check(avr.core.pair(26) == 0x0100);
        });
    });
    when("LDD Y+q and STD Z+q use a displacement", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(30, 0x00).ldi(31, 0x02).ldi(16, 0x42).stdZ(63, 16).ldi(28, 0x3E).ldi(29, 0x02).lddY(17, 1)};
        avr.execute(7);
        check(avr.mcu.readMemory(MemorySpace::Data, 0x23F, 1)[0] == 0x42);
        check(avr.core.r(17) == 0x42);
    });
    when("LDS and STS address the data space directly", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x99).sts(0x0800, 16).lds(17, 0x0800)};
        avr.execute(1);
        check(avr.execute(2) == 4);
        check(avr.core.r(17) == 0x99);
    });
    when("registers are reachable as data addresses 0 to 31", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x12).sts(5, 16)};
        avr.execute(2);
        check(avr.core.r(5) == 0x12);
    });
    when("LPM Z+ reads program memory bytes in little-endian order", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(30, 8).ldi(31, 0).lpmZInc(16).lpmZInc(17).raw(0xBEEF)};
        avr.execute(2);
        check(avr.execute() == 3);
        avr.execute();
        check(avr.core.r(16) == 0xEF);
        check(avr.core.r(17) == 0xBE);
        check(avr.core.pair(30) == 10);
    });
}};

const Scenario stackCallsAndReturns{"Stack, calls and returns", {"avr", "flow"}, [] {
    given("SP initialised to RAMEND on reset", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x11).push(16).pop(17)};
        check(avr.core.sp == 0x08FF);
        avr.execute();
        check(avr.execute() == 2);
        check(avr.core.sp == 0x08FE);
        check(avr.execute() == 2);
        check(avr.core.r(17) == 0x11);
        check(avr.core.sp == 0x08FF);
    });
    given("a CALL to a subroutine", [&] {
        // 0: call 4 / 2: rjmp -1 (loop) / 3: nop / 4: ldi r16, 7 / 5: ret
        AvrFixture avr{AvrAssembler{}.call(4).rjmp(-1).nop().ldi(16, 7).ret()};
        then("CALL takes four cycles and pushes the return address, high byte at the lower address", [&] {
            check(avr.execute() == 4);
            check(avr.core.pc == 4);
            check(avr.core.sp == 0x08FD);
            check(avr.mcu.readMemory(MemorySpace::Data, 0x08FE, 2) == std::vector<u8>{0x00, 0x02});
        });
        andThen("RET takes four cycles and returns after the CALL", [&] {
            avr.execute(2);
            check(avr.execute() == 4);
            check(avr.core.pc == 2);
            check(avr.core.r(16) == 7);
        });
    });
    given("RCALL and ICALL", [&] {
        // 0: rcall +2 -> 3 / 1: ldi r30,6 / 2: icall -> 6 / 3: ret / 4,5: nop / 6: ret
        AvrFixture avr{AvrAssembler{}.rcall(2).ldi(30, 6).icall().ret().nop().nop().ret()};
        check(avr.execute() == 3);
        check(avr.core.pc == 3);
        avr.execute(2);
        check(avr.execute() == 3);
        check(avr.core.pc == 6);
    });
}};

const Scenario branchesAndSkipsAccountForTheirExtra{"Branches and skips account for their extra cycles", {"avr", "flow"}, [] {
    when("BRNE is taken, then not taken", [&] {
        // 0: ldi r16,2 / 1: dec r16 / 2: brne -2 (to 1) / 3: nop
        AvrFixture avr{AvrAssembler{}.ldi(16, 2).dec(16).brne(-2).nop()};
        avr.execute(2);
        check(avr.execute() == 2);
        check(avr.core.pc == 1);
        avr.execute();
        check(avr.execute() == 1);
        check(avr.core.pc == 3);
    });
    when("CPSE skips a one-word then a two-word instruction", [&] {
        AvrFixture avr{AvrAssembler{}.cpse(0, 1).nop().cpse(0, 1).jmp(0).nop()};
        check(avr.execute() == 2);
        check(avr.core.pc == 2);
        check(avr.execute() == 3);
        check(avr.core.pc == 5);
    });
    when("SBRS/SBRC test a register bit", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x04).sbrs(16, 2).nop().sbrc(16, 2).nop()};
        avr.execute();
        check(avr.execute() == 2);
        check(avr.core.pc == 3);
        check(avr.execute() == 1);
        check(avr.core.pc == 4);
    });
    when("RJMP, JMP and IJMP jump", [&] {
        AvrFixture avr{AvrAssembler{}.rjmp(1).nop().jmp(5).nop().ijmp()};
        check(avr.execute() == 2);
        check(avr.core.pc == 2);
        check(avr.execute() == 3);
        check(avr.core.pc == 5);
        check(avr.execute() == 2);
        check(avr.core.pc == 0);
    });
}};

const Scenario bitAndStatusRegisterInstructions{"Bit and status register instructions", {"avr", "bits"}, [] {
    when("BST and BLD copy a bit through T", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x80).bst(16, 7).ldi(17, 0).bld(17, 0)};
        avr.execute(4);
        check(avr.flag(sreg::T));
        check(avr.core.r(17) == 0x01);
    });
    when("SREG is read and written as an I/O register", [&] {
        AvrFixture avr{AvrAssembler{}.sec().in(16, 0x3F).ldi(17, 0x02).out(0x3F, 17)};
        avr.execute(4);
        check(avr.core.r(16) == sreg::C);
        check(avr.core.sreg == sreg::Z);
    });
    when("SP is written through SPL/SPH", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x34).ldi(17, 0x02).out(0x3D, 16).out(0x3E, 17)};
        avr.execute(4);
        check(avr.core.sp == 0x0234);
    });
}};

const Scenario stoppingInstructions{"Stopping instructions", {"avr", "debug"}, [] {
    when("BREAK executes", [&] {
        AvrFixture avr{AvrAssembler{}.nop().break_().nop()};
        const auto result = avr.mcu.run(100);
        then("the run stops on it", [&] {
            check(result.reason == StopReason::BreakInstruction);
            check(avr.core.pc == 2);
        });
    });
    when("an opcode is not part of the instruction set", [&] {
        AvrFixture avr{AvrAssembler{}.nop().raw(0xFFFF)};
        const auto result = avr.mcu.run(100);
        then("the run stops with the PC on it", [&] {
            check(result.reason == StopReason::InvalidOpcode);
            check(avr.core.pc == 1);
        });
    });
    when("a breakpoint is set", [&] {
        AvrFixture avr{AvrAssembler{}.nop().nop().inc(16).rjmp(-2)};
        avr.mcu.debugger().addBreakpoint(4);
        const auto first = avr.mcu.run(1000);
        then("the run stops before the instruction, and resuming executes it", [&] {
            check(first.reason == StopReason::Breakpoint);
            check(avr.mcu.programCounter() == 4);
            check(avr.core.r(16) == 0);
            check(avr.mcu.run(1000).reason == StopReason::Breakpoint);
            check(avr.core.r(16) == 1);
        });
    });
}};

const Scenario everyOpcodeDecodesToAMnemonic{"Every opcode decodes to a mnemonic", {"avr", "decoder"}, [] {
    check(mnemonic(0x0000) == "nop");
    check(mnemonic(0x9478) == "bset");
    check(mnemonic(0x940C) == "jmp");
    check(mnemonic(0x8000) == "ldd z");
    check(mnemonic(0x9409) == "ijmp");
    check(mnemonic(0xFFFF) == "(invalid)");
    check(mnemonic(0x9419) == "eijmp");
    check(mnemonic(0x9519) == "eicall");
    check(isTwoWord(0x9000));
    check(isTwoWord(0x9200));
    check(isTwoWord(0x940E));
    check(!isTwoWord(0x9001));
}};

const Scenario wdrOnlyResetsTheWatchdogWhichIs{"WDR only resets the watchdog, which is not emulated", {"avr", "mcu-control"}, [] {
    given("a state set by a few instructions", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0xA5).sec().wdr().nop()};
        avr.execute(2);
        const auto before = avr.core.sreg;
        when("WDR executes", [&] {
            const auto cycles = avr.execute();
            then("it takes one cycle and changes nothing but the program counter", [&] {
                check(cycles == 1);
                check(avr.core.pc == 3);
                check(avr.core.r(16) == 0xA5);
                check(avr.core.sreg == before);
                check(avr.core.sp == avr.core.config().ramEnd);
            });
        });
    });
}};

const Scenario codeWrittenBySpmIsExecutedNot{"Code written by SPM is executed, not the instructions it replaced", {"avr", "spm"}, [] {
    // The core decodes the flash once (AvrCore::decodeFlash): a page erase or write must decode that page again.
    given("a page that holds LDI r20, 0x11 and a program that rewrites it with LDI r20, 0x5A", [&] {
        constexpr u16 replacement = 0xE54A;  // LDI r20, 0x5A
        constexpr unsigned spmcsr = 0x37;      // I/O address
        AvrAssembler program;
        program.ldi(16, 0x01).ldi(30, 0x00).ldi(31, 0x02)  // Z = byte 0x0200, the page of word 0x0100
            .ldi(24, replacement & 0xFF).ldi(25, replacement >> 8).movw(0, 24).out(spmcsr, 16).raw(0x95E8)  // fill the buffer
            .ldi(30, 0x02).ldi(24, 0x98).ldi(25, 0x95).movw(0, 24).out(spmcsr, 16).raw(0x95E8)            // BREAK after it
            .ldi(30, 0x00).ldi(16, 0x03).out(spmcsr, 16).raw(0x95E8)                                       // page erase
            .ldi(16, 0x05).out(spmcsr, 16).raw(0x95E8)                                                     // page write
            .jmp(0x0100)
            .org(0x0100)
            .ldi(20, 0x11)
            .break_();
        AvrFixture avr{program};
        when("it runs", [&] {
            const auto result = avr.mcu.run(1000);
            then("the new instruction executes", [&] {
                check(result.reason == StopReason::BreakInstruction);
                check(avr.core.r(20) == 0x5A);
                check(avr.core.flash()[0x0100] == replacement);
            });
        });
    });
}};
