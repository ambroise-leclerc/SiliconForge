#include "../support/AvrFixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace siliconforge;
using namespace siliconforge::avr;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

namespace {
constexpr int timer0OverflowVector = 16 * 2;  // Word address of the TIMER0_OVF vector slot
constexpr int main                 = 0x40;

/// Vector table with a Timer0 overflow handler incrementing r20, then `body` at `main`.
AvrAssembler withOverflowHandler(auto body) {
    AvrAssembler program;
    program.jmp(main);
    program.org(timer0OverflowVector).inc(20).reti();
    program.org(main);
    body(program);
    return program;
}
}  // namespace

SCENARIO("Timer0 overflow interrupts run their vector", "[avr][interrupts]") {
    GIVEN("a firmware enabling TOIE0 with no prescaling, then looping") {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16);  // TIMSK0 = TOIE0
            p.ldi(16, 0x01).out(0x25, 16);  // TCCR0B = clk/1
            p.sei().rjmp(-1);
        })};
        WHEN("it runs for 10 overflow periods") {
            avr.mcu.run(256 * 10 + 40);
            THEN("the handler ran once per overflow") { CHECK(avr.core.r(20) == 10); }
        }
    }
    GIVEN("an overflow flag already pending when SEI executes") {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16).ldi(16, 0x01).out(0x25, 16);
            for (int i = 0; i < 140; ++i)  // 280 cycles: the timer overflows while interrupts are disabled
                p.rjmp(0);
            p.sei().inc(21).rjmp(-1);
        })};
        WHEN("SEI executes") {
            avr.mcu.run(400);
            THEN("the instruction after SEI still ran before the handler") {
                CHECK(avr.core.r(21) == 1);
                CHECK(avr.core.r(20) >= 1);
            }
        }
    }
    GIVEN("an interrupt entry") {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16).ldi(16, 0x01).out(0x25, 16).sei().rjmp(-1);
        })};
        avr.mcu.run(250);
        while (avr.core.pc != timer0OverflowVector)
            avr.execute();
        THEN("I is cleared, the return address is stacked and TOV0 was cleared by hardware") {
            CHECK_FALSE(avr.flag(sreg::I));
            CHECK(avr.core.sp == 0x08FD);
            CHECK((avr.io(0x35) & 0x01) == 0);
        }
        AND_THEN("RETI sets I again and returns") {
            avr.execute(2);
            CHECK(avr.flag(sreg::I));
            CHECK(avr.core.sp == 0x08FF);
        }
    }
}

SCENARIO("SLEEP waits for an interrupt without executing instructions", "[avr][interrupts][sleep]") {
    GIVEN("a firmware sleeping in idle mode between Timer0 overflows (clk/64)") {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16).ldi(16, 0x03).out(0x25, 16);
            p.ldi(16, 0x01).out(0x33, 16);  // SMCR = SE, idle
            p.sei().sleep().inc(22).rjmp(-3);
        })};
        WHEN("it runs for 5 overflow periods") {
            avr.mcu.run(256 * 64 * 5 + 100);
            THEN("each overflow woke the CPU up once") {
                CHECK(avr.core.r(20) == 5);
                CHECK(avr.core.r(22) == 5);
            }
        }
    }
}
