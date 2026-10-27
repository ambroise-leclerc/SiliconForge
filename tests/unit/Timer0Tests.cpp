#include "../support/AvrFixture.hpp"

#include <siliconforge/testing/Bench.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

namespace {
constexpr Address tifr0 = 0x35, tccr0a = 0x44, tccr0b = 0x45, tcnt0 = 0x46, ocr0a = 0x47, ocr0b = 0x48, ddrd = 0x2A;

const AvrAssembler idleLoop = AvrAssembler{}.rjmp(-1);  // Two cycles per iteration

std::unique_ptr<mcu::Atmega328p> idleMcu() {
    auto mcu = std::make_unique<mcu::Atmega328p>();
    mcu->load(idleLoop.image());
    return mcu;
}
}  // namespace

SCENARIO("Timer0 in normal mode counts at the prescaled clock", "[avr][timer0]") {
    AvrFixture avr{idleLoop};
    auto&      data = avr.mcu.dataSpace();
    data.write(tccr0b, 0x03);  // clk/64
    WHEN("6400 cycles elapse") {
        avr.mcu.run(6400);
        THEN("TCNT0 counted 100") { CHECK(data.read(tcnt0) == 100); }
    }
    WHEN("just before and at 256 * 64 cycles") {
        avr.mcu.run(256 * 64 - 2);
        CHECK((data.read(tifr0) & 0x01) == 0);
        avr.mcu.run(2);
        THEN("TOV0 is set when the counter wraps") {
            CHECK((data.read(tifr0) & 0x01) == 1);
            CHECK(data.read(tcnt0) == 0);
        }
        AND_THEN("writing a one clears it") {
            data.write(tifr0, 0x01);
            CHECK((data.read(tifr0) & 0x01) == 0);
        }
    }
}

SCENARIO("Timer0 in CTC mode toggles OC0A", "[avr][timer0]") {
    testing::Bench bench{idleMcu()};
    auto&          data = static_cast<mcu::Atmega328p&>(bench.mcu()).dataSpace();
    data.write(ddrd, 0x40);       // OC0A = PD6 as output
    data.write(ocr0a, 99);        // Match every 100 timer ticks
    data.write(tccr0a, 0x42);     // COM0A = toggle, WGM = CTC
    data.write(tccr0b, 0x02);     // clk/8
    bench.runFor(std::chrono::milliseconds{2});
    THEN("PD6 toggles every 800 cycles: a 100 µs period") {
        REQUIRE(bench.period("PD6"));
        CHECK(*bench.period("PD6") == Catch::Approx(100e-6).epsilon(1e-9));
        CHECK((data.read(tifr0) & 0x02) != 0);  // OCF0A
    }
}

SCENARIO("Timer0 in Fast PWM mode drives OC0A and OC0B with the configured duty cycles", "[avr][timer0][pwm]") {
    testing::Bench bench{idleMcu()};
    auto&          data = static_cast<mcu::Atmega328p&>(bench.mcu()).dataSpace();
    data.write(ddrd, 0x60);    // PD6 (OC0A) and PD5 (OC0B) as outputs
    data.write(ocr0a, 63);     // 64/256 = 25 %
    data.write(ocr0b, 191);    // Inverting: 1 - 192/256 = 25 %
    data.write(tccr0a, 0xB3);  // COM0A = clear on match, COM0B = set on match, WGM = Fast PWM
    data.write(tccr0b, 0x01);  // clk/1: 62.5 kHz
    bench.runFor(std::chrono::milliseconds{1});
    CHECK(*bench.period("D6") == Catch::Approx(256.0 / 16e6));
    CHECK(*bench.dutyCycle("D6") == Catch::Approx(0.25));
    CHECK(*bench.dutyCycle("D5") == Catch::Approx(0.25));
}

SCENARIO("Timer0 in Phase Correct PWM mode", "[avr][timer0][pwm]") {
    testing::Bench bench{idleMcu()};
    auto&          data = static_cast<mcu::Atmega328p&>(bench.mcu()).dataSpace();
    data.write(ddrd, 0x40);
    data.write(ocr0a, 128);
    data.write(tccr0a, 0x81);  // COM0A = clear up-counting, WGM = Phase Correct
    data.write(tccr0b, 0x01);
    bench.runFor(std::chrono::milliseconds{1});
    THEN("the period is 510 timer clocks and the duty cycle OCR0A / 255") {
        CHECK(*bench.period("PD6") == Catch::Approx(510.0 / 16e6));
        CHECK(*bench.dutyCycle("PD6") == Catch::Approx(128.0 / 255.0).epsilon(0.01));
    }
}
