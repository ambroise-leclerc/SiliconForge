#include "../support/AvrFixture.hpp"

#include <siliconforge/testing/Bench.hpp>

#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"

using namespace webfront::bdd;

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

const Scenario timer0InNormalModeCountsAtThe{"Timer0 in normal mode counts at the prescaled clock", {"avr", "timer0"}, [] {
    AvrFixture avr{idleLoop};
    auto&      data = avr.mcu.dataSpace();
    data.write(tccr0b, 0x03);  // clk/64
    when("6400 cycles elapse", [&] {
        avr.mcu.run(6400);
        then("TCNT0 counted 100", [&] { check(data.read(tcnt0) == 100); });
    });
    when("just before and at 256 * 64 cycles", [&] {
        avr.mcu.run(256 * 64 - 2);
        check((data.read(tifr0) & 0x01) == 0);
        avr.mcu.run(2);
        then("TOV0 is set when the counter wraps", [&] {
            check((data.read(tifr0) & 0x01) == 1);
            check(data.read(tcnt0) == 0);
        });
        andThen("writing a one clears it", [&] {
            data.write(tifr0, 0x01);
            check((data.read(tifr0) & 0x01) == 0);
        });
    });
}};

const Scenario timer0InCtcModeTogglesOc0a{"Timer0 in CTC mode toggles OC0A", {"avr", "timer0"}, [] {
    testing::Bench bench{idleMcu()};
    auto&          data = static_cast<mcu::Atmega328p&>(bench.mcu()).dataSpace();
    data.write(ddrd, 0x40);       // OC0A = PD6 as output
    data.write(ocr0a, 99);        // Match every 100 timer ticks
    data.write(tccr0a, 0x42);     // COM0A = toggle, WGM = CTC
    data.write(tccr0b, 0x02);     // clk/8
    bench.runFor(std::chrono::milliseconds{2});
    then("PD6 toggles every 800 cycles: a 100 µs period", [&] {
        require(bench.period("PD6"));
        check(*bench.period("PD6") == siliconforge::test::Approx(100e-6).epsilon(1e-9));
        check((data.read(tifr0) & 0x02) != 0);  // OCF0A
    });
}};

const Scenario timer0InFastPwmModeDrivesOc0a{"Timer0 in Fast PWM mode drives OC0A and OC0B with the configured duty cycles", {"avr", "timer0", "pwm"}, [] {
    testing::Bench bench{idleMcu()};
    auto&          data = static_cast<mcu::Atmega328p&>(bench.mcu()).dataSpace();
    data.write(ddrd, 0x60);    // PD6 (OC0A) and PD5 (OC0B) as outputs
    data.write(ocr0a, 63);     // 64/256 = 25 %
    data.write(ocr0b, 191);    // Inverting: 1 - 192/256 = 25 %
    data.write(tccr0a, 0xB3);  // COM0A = clear on match, COM0B = set on match, WGM = Fast PWM
    data.write(tccr0b, 0x01);  // clk/1: 62.5 kHz
    bench.runFor(std::chrono::milliseconds{1});
    check(*bench.period("D6") == siliconforge::test::Approx(256.0 / 16e6));
    check(*bench.dutyCycle("D6") == siliconforge::test::Approx(0.25));
    check(*bench.dutyCycle("D5") == siliconforge::test::Approx(0.25));
}};

const Scenario timer0InPhaseCorrectPwmMode{"Timer0 in Phase Correct PWM mode", {"avr", "timer0", "pwm"}, [] {
    testing::Bench bench{idleMcu()};
    auto&          data = static_cast<mcu::Atmega328p&>(bench.mcu()).dataSpace();
    data.write(ddrd, 0x40);
    data.write(ocr0a, 128);
    data.write(tccr0a, 0x81);  // COM0A = clear up-counting, WGM = Phase Correct
    data.write(tccr0b, 0x01);
    bench.runFor(std::chrono::milliseconds{1});
    then("the period is 510 timer clocks and the duty cycle OCR0A / 255", [&] {
        check(*bench.period("PD6") == siliconforge::test::Approx(510.0 / 16e6));
        check(*bench.dutyCycle("PD6") == siliconforge::test::Approx(128.0 / 255.0).epsilon(0.01));
    });
}};

const Scenario timer0TimestampsOneCyclePwmPulsesInside{"Timer0 timestamps one-cycle PWM pulses inside multi-cycle instructions", {"avr", "timer0", "pwm"}, [] {
    auto mcu = idleMcu();
    mcu->load(AvrAssembler{}.nop().rjmp(-2).image());  // Three-cycle loop shifts the instruction alignment each period.
    auto& data = mcu->dataSpace();
    std::vector<std::pair<Cycles, PinState>> edges;
    mcu->pin("D6")->subscribe([&](const Pin& pin, Cycles at) { edges.emplace_back(at, pin.state()); });
    data.write(ddrd, 0x40);
    data.write(ocr0a, 0);
    data.write(tccr0a, 0x83);
    data.write(tccr0b, 0x01);
    edges.clear();
    mcu->run(1024);
    require(edges.size() >= 6);
    for (std::size_t i = 0; i + 1 < edges.size(); i += 2) {
        check(edges[i].second == PinState::High);
        check(edges[i + 1].second == PinState::Low);
        check(edges[i + 1].first - edges[i].first == 1);
        if (i >= 2)
            check(edges[i].first - edges[i - 2].first == 256);
    }
    check(mcu->cycles() == 1024);
}};
