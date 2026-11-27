#include "../support/AvrFixture.hpp"
#include <siliconforge/mcu/Registry.hpp>

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

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

const Scenario timer0OverflowInterruptsRunTheirVector{"Timer0 overflow interrupts run their vector", {"avr", "interrupts"}, [] {
    given("a firmware enabling TOIE0 with no prescaling, then looping", [&] {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16);  // TIMSK0 = TOIE0
            p.ldi(16, 0x01).out(0x25, 16);  // TCCR0B = clk/1
            p.sei().rjmp(-1);
        })};
        when("it runs for 10 overflow periods", [&] {
            avr.mcu.run(256 * 10 + 40);
            then("the handler ran once per overflow", [&] { check(avr.core.r(20) == 10); });
        });
    });
    given("an overflow flag already pending when SEI executes", [&] {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16).ldi(16, 0x01).out(0x25, 16);
            for (int i = 0; i < 140; ++i)  // 280 cycles: the timer overflows while interrupts are disabled
                p.rjmp(0);
            p.sei().inc(21).rjmp(-1);
        })};
        when("SEI executes", [&] {
            avr.mcu.run(400);
            then("the instruction after SEI still ran before the handler", [&] {
                check(avr.core.r(21) == 1);
                check(avr.core.r(20) >= 1);
            });
        });
    });
    given("an interrupt entry", [&] {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16).ldi(16, 0x01).out(0x25, 16).sei().rjmp(-1);
        })};
        avr.mcu.run(250);
        while (avr.core.pc != timer0OverflowVector)
            avr.execute();
        then("I is cleared, the return address is stacked and TOV0 was cleared by hardware", [&] {
            check(!avr.flag(sreg::I));
            check(avr.core.sp == 0x08FD);
            check((avr.io(0x35) & 0x01) == 0);
        });
        andThen("RETI sets I again and returns", [&] {
            avr.execute(2);
            check(avr.flag(sreg::I));
            check(avr.core.sp == 0x08FF);
        });
    });
}};

const Scenario sleepWaitsForAnInterruptWithoutExecuting{"SLEEP waits for an interrupt without executing instructions", {"avr", "interrupts", "sleep"}, [] {
    given("a firmware sleeping in idle mode between Timer0 overflows (clk/64)", [&] {
        AvrFixture avr{withOverflowHandler([](AvrAssembler& p) {
            p.ldi(16, 0x01).sts(0x6E, 16).ldi(16, 0x03).out(0x25, 16);
            p.ldi(16, 0x01).out(0x33, 16);  // SMCR = SE, idle
            p.sei().sleep().inc(22).rjmp(-3);
        })};
        when("it runs for 5 overflow periods", [&] {
            avr.mcu.run(256 * 64 * 5 + 100);
            then("each overflow woke the CPU up once", [&] {
                check(avr.core.r(20) == 5);
                check(avr.core.r(22) == 5);
            });
        });
    });
}};

const Scenario avrXtPreservesInterruptEnableAcrossInterrupts{
    "AVRxt preserves I on entry and RETI and defers another normal interrupt", {"avr", "interrupts"}, [] {
        const auto exercise = [](bool clearInHandler) {
            auto mcu = mcu::create("attiny424");
            require(mcu);
            auto& core = static_cast<avr::Device&>(*mcu).avrCore();
            constexpr unsigned firstVector = 6, secondVector = 13;
            AvrAssembler program;
            program.inc(20).sei().nop().nop();
            program.org(firstVector).inc(18);
            if (clearInHandler)
                program.cli();
            else
                program.nop();
            program.reti().org(secondVector).inc(19).reti();
            mcu->load(program.image());
            const auto stack = core.sp;
            core.interrupts.onAcknowledge(firstVector, [&] { core.interrupts.set(firstVector, false); });
            core.sreg = sreg::I | sreg::C;
            core.interrupts.set(firstVector, true);
            core.interrupts.set(secondVector, true);

            mcu->step();
            check(core.pc == firstVector);
            check((core.sreg & sreg::I) != 0);
            check(core.sp == stack - 2);
            mcu->step();
            check(core.r(18) == 1);
            check(core.r(19) == 0);
            check(core.interrupts.isPending(secondVector));
            mcu->step();
            mcu->step();  // RETI restores PC and releases the active interrupt, preserving I.
            check(core.pc == 0);
            check(core.sp == stack);
            check((core.sreg & sreg::I) == (clearInHandler ? 0 : sreg::I));
            check((core.sreg & sreg::C) != 0);

            mcu->step();
            check(core.r(20) == 1);
            if (clearInHandler) {
                mcu->step();  // SEI executes while the second interrupt is still pending.
                check(core.pc == 2);
                check(core.interrupts.isPending(secondVector));
                mcu->step();  // One instruction after SEI.
            }
            mcu->step();
            check(core.pc == secondVector);

            mcu->reset();  // The second ISR was active at reset.
            core.sreg = sreg::I;
            core.interrupts.set(firstVector, true);
            mcu->step();
            check(core.pc == firstVector);
        };
        section("I stays set if the handler leaves it set", [&] { exercise(false); });
        section("CLI in the handler keeps I clear after RETI", [&] { exercise(true); });
    }};

const Scenario attiny424InterruptResponseIncludesVectorJump{
    "ATtiny424 enters the handler four cycles after the interrupted instruction", {"avr", "interrupts"}, [] {
        auto mcu = mcu::create("attiny424");
        require(mcu);
        auto& core = static_cast<avr::Device&>(*mcu).avrCore();
        constexpr unsigned vector = 6, handler = 16;
        AvrAssembler program;
        program.nop().org(vector).rjmp(handler - vector - 1).org(handler).inc(18).reti();
        mcu->load(program.image());
        core.sreg = sreg::I;
        mcu->step();  // The interrupted instruction finishes.
        const auto start = mcu->cycles();
        core.interrupts.set(vector, true);

        check(mcu->step().elapsed == 2);
        check(core.pc == vector);
        check(mcu->step().elapsed == 2);
        check(core.pc == handler);
        check(mcu->cycles() - start == 4);
        check(core.r(18) == 0);
        mcu->step();
        check(core.r(18) == 1);
    }};
