// End-to-end tests: real firmware built with avr-gcc (sources in firmware/, built by CMake when avr-gcc is present, otherwise taken from firmware/prebuilt/) run on the
// emulated ATmega328P and are checked through the test bench, as a developer would check them on a board.

#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"

#include <filesystem>

using namespace webfront::bdd;

using namespace siliconforge;
using namespace std::chrono_literals;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};

testing::Bench benchWith(const char* firmware) {
    const auto path = firmwareDir / firmware;
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.load(path);
    return bench;
}
}  // namespace

const Scenario blinkTheLedOnD13BlinksAt{"blink: the LED on D13 blinks at 1 Hz", {"firmware"}, [] {
    auto bench = benchWith("blink.hex");
    bench.runFor(3s);
    const auto period = bench.period("D13");
    require(period);
    check(*period == siliconforge::test::Approx(1.0).epsilon(0.001));
    check(*bench.dutyCycle("D13") == siliconforge::test::Approx(0.5).epsilon(0.001));
}};

const Scenario blinkChangingTheDelayInTheC{"blink: changing the delay in the C source changes the blink period", {"firmware", "source"}, [] {
#ifndef SILICONFORGE_FIRMWARE_FROM_SOURCE
    skip("this variant is compiled from firmware/src with avr-gcc, which was not found at configuration");
#else
    // blink_250ms.hex is blink.c built with -DBLINK_HALF_PERIOD_MS=250: "I edit my code, the LED changes".
    auto bench = benchWith("blink_250ms.hex");
    bench.runFor(3s);
    const auto period = bench.period("D13");
    require(period);
    check(*period == siliconforge::test::Approx(0.5).epsilon(0.001));
    check(*bench.dutyCycle("D13") == siliconforge::test::Approx(0.5).epsilon(0.001));
#endif
}};

const Scenario uartEchoTheFirmwareGreetsAndEchoes{"uart_echo: the firmware greets and echoes lines in upper case", {"firmware"}, [] {
    auto bench = benchWith("uart_echo.hex");
    require(bench.waitForSerial("ready\r\n", 100ms));
    bench.serial().send("hello\r");
    require(bench.waitForSerial("HELLO\r\n", 100ms));
}};

const Scenario timer0IsrACompareMatchInterruptCounts{"timer0_isr: a compare-match interrupt counts milliseconds while the CPU sleeps", {"firmware"}, [] {
    auto bench = benchWith("timer0_isr.hex");
    require(bench.waitForSerial("t=1000\r\n", 1100ms));
    check(bench.seconds() == siliconforge::test::Approx(1.0).epsilon(0.01));
}};

const Scenario theElfAndHexBuildsOfA{"The ELF and HEX builds of a firmware behave the same", {"firmware", "loader"}, [] {
    auto elf = benchWith("blink.elf");
    auto hex = benchWith("blink.hex");
    elf.runFor(1500ms);
    hex.runFor(1500ms);
    check(elf.transitions("D13").size() == hex.transitions("D13").size());
}};

const Scenario pwmTimer0DrivesD6AndD5At{"pwm: Timer0 drives D6 and D5 at 25 % and 75 %", {"firmware", "pwm"}, [] {
    auto bench = benchWith("pwm.hex");
    bench.runFor(2ms);
    check(*bench.period("D6") == siliconforge::test::Approx(256.0 / 16e6));
    check(*bench.dutyCycle("D6") == siliconforge::test::Approx(0.25));
    check(*bench.dutyCycle("D5") == siliconforge::test::Approx(0.75));
}};

const Scenario demoD13BlinksAt1HzWhile{"demo: D13 blinks at 1 Hz while D6 breathes with PWM on a 2 s cycle", {"firmware", "pwm"}, [] {
    auto bench = benchWith("demo.hex");
    bench.runFor(4s);
    require(bench.period("D13"));
    check(*bench.period("D13") == siliconforge::test::Approx(1.0).epsilon(0.01));
    check(*bench.dutyCycle("D13") == siliconforge::test::Approx(0.5).epsilon(0.01));
    check(*bench.period("D6") == siliconforge::test::Approx(2048.0 / 16e6));  // clk/8, 256 steps

    // Fraction of [from, to] seconds that D6 spends high, from the recorded transitions.
    const auto clock = static_cast<double>(bench.mcu().clockHz());
    const auto highFraction = [&](double from, double to) {
        double high  = 0;
        auto   level = PinState::Low;
        double since = 0;
        const auto close = [&](double until) {
            if (level == PinState::High && until > std::max(since, from))
                high += std::min(until, to) - std::max(since, from);
        };
        for (const auto& transition : bench.transitions("D6")) {
            const double at = static_cast<double>(transition.at) / clock;
            if (at > to)
                break;
            close(at);
            level = transition.state;
            since = at;
        }
        close(to);
        return high / (to - from);
    };
    check(highFraction(0.00, 0.05) < 0.1);   // The cycle starts dark...
    check(highFraction(0.95, 1.05) > 0.9);   // ...peaks after 1 s (a 1 s cycle would be dark again here)...
    check(highFraction(1.95, 2.05) < 0.1);   // ...is dark again after 2 s...
    check(highFraction(2.95, 3.05) > 0.9);   // ...and peaks again after 3 s.
}};

const Scenario aResetWhileTheFirmwareRunsRestarts{"A reset while the firmware runs restarts it from the reset vector", {"firmware", "reset"}, [] {
    auto bench = benchWith("uart_echo.hex");
    require(bench.waitForSerial("ready\r\n", 100ms));
    bench.serial().send("abc\r");
    bench.runFor(3ms);  // Reset in the middle of a reception and of a transmission
    bench.mcu().reset();
    check(bench.mcu().cycles() == 0);
    check(bench.serial().output().empty());
    require(bench.waitForSerial("ready\r\n", 100ms));
}};
