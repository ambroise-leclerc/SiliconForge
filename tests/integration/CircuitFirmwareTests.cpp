// Real firmware on the demonstration circuit: the bench checks the components the user sees, not only the pins.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <filesystem>

using namespace siliconforge;
using namespace std::chrono_literals;
using Catch::Approx;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};

testing::Bench demoBenchWith(const char* firmware) {
    const auto path = firmwareDir / firmware;
    if (!std::filesystem::exists(path))
        SKIP(path.string() << " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::Circuit::builtin("arduino-demo"));
    bench.load(path);
    return bench;
}
}  // namespace

SCENARIO("blink: the LED of the demonstration circuit is lit half a second out of two", "[firmware][circuit]") {
    auto       bench = demoBenchWith("blink.hex");
    auto       led   = bench.component("led-d13");
    const auto lit   = [&] {
        return led.brightness() > 0.5;
    };
    const auto dark = [&] {
        return !lit();
    };

    REQUIRE(bench.runUntil(dark, 2s));
    REQUIRE(bench.runUntil(lit, 2s));
    const auto on = bench.seconds();
    CHECK(led.brightness() == 1.0);
    REQUIRE(bench.runUntil(dark, 2s));
    const auto off = bench.seconds();
    REQUIRE(bench.runUntil(lit, 2s));
    const auto onAgain = bench.seconds();

    CHECK(off - on == Approx(0.5).epsilon(0.001));
    CHECK(onAgain - off == Approx(0.5).epsilon(0.001));
    CHECK(bench.component("led-d6").brightness() == 0.0);
    CHECK(bench.circuit().faults().empty());
}

SCENARIO("demo: the green LED breathes with the PWM of D6", "[firmware][circuit]") {
    auto bench = demoBenchWith("demo.hex");
    auto led   = bench.component("led-d6");
    // The duty cycle rises for one second, then falls for one second.
    std::vector<double> means;
    for (int tenth = 0; tenth < 20; ++tenth) {
        bench.runFor(90ms);
        (void)led.meanBrightness();  // Start a window.
        bench.runFor(10ms);
        means.push_back(led.meanBrightness());
    }
    CHECK(means[0] < means[3]);
    CHECK(means[3] < means[6]);
    CHECK(means[6] < means[9]);
    CHECK(means[9] > 0.9);
    CHECK(means[12] > means[15]);
    CHECK(means[15] > means[18]);
    CHECK(means[19] < 0.1);
    // The window is a few dozen PWM periods: it follows the duty cycle of the pin.
    CHECK(means[4] == Approx(0.5).margin(0.05));
}

SCENARIO("The bench stimulates the circuit like a user", "[firmware][circuit]") {
    auto bench = demoBenchWith("blink.hex");
    SECTION("turning the potentiometer changes the voltage on A0") {
        bench.component("pot-a0").set("position", 0.4);
        CHECK(bench.pin("A0").voltage(5.0) == Approx(2.0));
    }
    SECTION("pressing the button grounds D2") {
        bench.press("button-d2");
        CHECK(bench.component("button-d2").pressed());
        CHECK(bench.pin("D2").state() == PinState::Low);
        bench.release("button-d2");
        CHECK(bench.pin("D2").state() == PinState::Floating);
    }
    SECTION("a bench has one circuit, and none until attached") {
        CHECK_THROWS_AS(bench.attach(circuit::Circuit::builtin("arduino-demo")), std::logic_error);
        testing::Bench bare{mcu::create("atmega328p")};
        CHECK_THROWS_AS(bare.component("led-d13"), std::logic_error);
    }
}
