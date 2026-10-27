#include "../support/AvrFixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

SCENARIO("Port pins follow DDRx and PORTx", "[avr][gpio]") {
    GIVEN("PB5 (Arduino D13) configured as an output and driven high") {
        AvrFixture avr{AvrAssembler{}.sbi(0x04, 5).sbi(0x05, 5).sbi(0x03, 5).rjmp(-1)};
        avr.execute(2);
        auto& led = *avr.mcu.pin("D13");
        THEN("the pin is a high output, reachable by datasheet name and board label") {
            CHECK(&led == avr.mcu.pin("PB5"));
            CHECK(led.isOutput());
            CHECK(led.state() == PinState::High);
        }
        AND_WHEN("a one is written to PINB5 (SBI on PINB)") {
            CHECK(avr.execute() == 2);
            THEN("the output toggles") {
                CHECK(led.state() == PinState::Low);
                CHECK((avr.io(0x25) & 0x20) == 0);
            }
        }
    }
    GIVEN("PD2 as an input") {
        AvrFixture avr{AvrAssembler{}.in(16, 0x09).sbi(0x0B, 2).in(17, 0x09).cbi(0x0B, 2).in(18, 0x09)};
        auto& input = *avr.mcu.pin("PD2");
        input.setExternal(true);
        avr.execute();
        THEN("PIND reads the external level") { CHECK((avr.core.r(16) & 0x04) != 0); }
        AND_WHEN("the external level is released and the pull-up enabled") {
            input.setExternal(std::nullopt);
            avr.execute(2);
            CHECK(input.state() == PinState::High);
            CHECK(input.hasPullUp());
            CHECK((avr.core.r(17) & 0x04) != 0);
            AND_WHEN("the pull-up is disabled again, the floating input reads low") {
                avr.execute(2);
                CHECK(input.state() == PinState::Floating);
                CHECK((avr.core.r(18) & 0x04) == 0);
            }
        }
    }
    GIVEN("MCUCR.PUD set") {
        AvrFixture avr{AvrAssembler{}.ldi(16, 0x10).out(0x35, 16).sbi(0x0B, 3)};
        avr.execute(3);
        THEN("pull-ups are globally disabled") { CHECK(avr.mcu.pin("PD3")->state() == PinState::Floating); }
    }
}

TEST_CASE("Pin observers receive the cycle of each change", "[gpio]") {
    AvrFixture avr{AvrAssembler{}.sbi(0x04, 5).sbi(0x05, 5).cbi(0x05, 5)};
    std::vector<std::pair<Cycles, PinState>> changes;
    avr.mcu.pin("D13")->subscribe([&](const Pin& pin, Cycles at) { changes.emplace_back(at, pin.state()); });
    avr.execute(3);
    // DDR set at cycle 0 (Low output, from Floating), PORT high at cycle 2, low at cycle 4.
    REQUIRE(changes.size() == 3);
    CHECK(changes[0] == std::pair{Cycles{0}, PinState::Low});
    CHECK(changes[1] == std::pair{Cycles{2}, PinState::High});
    CHECK(changes[2] == std::pair{Cycles{4}, PinState::Low});
}
