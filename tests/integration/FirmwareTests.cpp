// End-to-end tests: real firmware built with avr-gcc (sources in firmware/, binaries in firmware/prebuilt/) run on the
// emulated ATmega328P and are checked through the test bench, as a developer would check them on a board.

#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace siliconforge;
using namespace std::chrono_literals;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};

testing::Bench benchWith(const char* firmware) {
    const auto path = firmwareDir / firmware;
    if (!std::filesystem::exists(path))
        SKIP(path.string() << " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.load(path);
    return bench;
}
}  // namespace

SCENARIO("blink: the LED on D13 blinks at 1 Hz", "[firmware]") {
    auto bench = benchWith("blink.hex");
    bench.runFor(3s);
    const auto period = bench.period("D13");
    REQUIRE(period);
    CHECK(*period == Catch::Approx(1.0).epsilon(0.001));
    CHECK(*bench.dutyCycle("D13") == Catch::Approx(0.5).epsilon(0.001));
}

SCENARIO("uart_echo: the firmware greets and echoes lines in upper case", "[firmware]") {
    auto bench = benchWith("uart_echo.hex");
    REQUIRE(bench.waitForSerial("ready\r\n", 100ms));
    bench.serial().send("hello\r");
    REQUIRE(bench.waitForSerial("HELLO\r\n", 100ms));
}

SCENARIO("timer0_isr: a compare-match interrupt counts milliseconds while the CPU sleeps", "[firmware]") {
    auto bench = benchWith("timer0_isr.hex");
    REQUIRE(bench.waitForSerial("t=1000\r\n", 1100ms));
    CHECK(bench.seconds() == Catch::Approx(1.0).epsilon(0.01));
}

SCENARIO("The ELF and HEX builds of a firmware behave the same", "[firmware][loader]") {
    auto elf = benchWith("blink.elf");
    auto hex = benchWith("blink.hex");
    elf.runFor(1500ms);
    hex.runFor(1500ms);
    CHECK(elf.transitions("D13").size() == hex.transitions("D13").size());
}

SCENARIO("pwm: Timer0 drives D6 and D5 at 25 % and 75 %", "[firmware][pwm]") {
    auto bench = benchWith("pwm.hex");
    bench.runFor(2ms);
    CHECK(*bench.period("D6") == Catch::Approx(256.0 / 16e6));
    CHECK(*bench.dutyCycle("D6") == Catch::Approx(0.25));
    CHECK(*bench.dutyCycle("D5") == Catch::Approx(0.75));
}

SCENARIO("A reset while the firmware runs restarts it from the reset vector", "[firmware][reset]") {
    auto bench = benchWith("uart_echo.hex");
    REQUIRE(bench.waitForSerial("ready\r\n", 100ms));
    bench.serial().send("abc\r");
    bench.runFor(3ms);  // Reset in the middle of a reception and of a transmission
    bench.mcu().reset();
    CHECK(bench.mcu().cycles() == 0);
    CHECK(bench.serial().output().empty());
    REQUIRE(bench.waitForSerial("ready\r\n", 100ms));
}
