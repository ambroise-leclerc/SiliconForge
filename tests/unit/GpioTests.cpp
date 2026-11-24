#include "../support/AvrFixture.hpp"

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

const Scenario portPinsFollowDdrxAndPortx{"Port pins follow DDRx and PORTx", {"avr", "gpio"}, [] {
    given("PB5 (Arduino D13) configured as an output and driven high", [&] {
        AvrFixture avr{AvrAssembler{}.sbi(0x04, 5).sbi(0x05, 5).sbi(0x03, 5).rjmp(-1)};
        avr.execute(2);
        auto& led = *avr.mcu.pin("D13");
        then("the pin is a high output, reachable by datasheet name and board label", [&] {
            check(&led == avr.mcu.pin("PB5"));
            check(led.isOutput());
            check(led.state() == PinState::High);
        });
        andWhen("a one is written to PINB5 (SBI on PINB)", [&] {
            check(avr.execute() == 2);
            then("the output toggles", [&] {
                check(led.state() == PinState::Low);
                check((avr.io(0x25) & 0x20) == 0);
            });
        });
    });
    given("PD2 as an input", [&] {
        AvrFixture avr{AvrAssembler{}.in(16, 0x09).sbi(0x0B, 2).in(17, 0x09).cbi(0x0B, 2).in(18, 0x09)};
        auto& input = *avr.mcu.pin("PD2");
        input.setExternal(true);
        avr.execute();
        then("PIND reads the external level", [&] { check((avr.core.r(16) & 0x04) != 0); });
        andWhen("the external level is released and the pull-up enabled", [&] {
            input.setExternal(std::nullopt);
            avr.execute(2);
            check(input.state() == PinState::High);
            check(input.hasPullUp());
            check((avr.core.r(17) & 0x04) != 0);
            andWhen("the pull-up is disabled again, the floating input reads low", [&] {
                avr.execute(2);
                check(input.state() == PinState::Floating);
                check((avr.core.r(18) & 0x04) == 0);
            });
        });
    });
    given("MCUCR.PUD set", [&] {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x10).out(0x35, 16).sbi(0x0B, 3)};
        avr.execute(3);
        then("pull-ups are globally disabled", [&] { check(avr.mcu.pin("PD3")->state() == PinState::Floating); });
    });
}};

const Scenario pinObserversReceiveTheCycleOfEach{"Pin observers receive the cycle of each change", {"gpio"}, [] {
    AvrFixture avr{AvrAssembler{}.sbi(0x04, 5).sbi(0x05, 5).cbi(0x05, 5)};
    std::vector<std::pair<Cycles, PinState>> changes;
    avr.mcu.pin("D13")->subscribe([&](const Pin& pin, Cycles at) { changes.emplace_back(at, pin.state()); });
    avr.execute(3);
    // DDR set at cycle 0 (Low output, from Floating), PORT high at cycle 2, low at cycle 4.
    require(changes.size() == 3);
    check(changes[0] == std::pair{Cycles{0}, PinState::Low});
    check(changes[1] == std::pair{Cycles{2}, PinState::High});
    check(changes[2] == std::pair{Cycles{4}, PinState::Low});
}};
