// Electrical model of the circuits (ADR 0005, #19, #21, #22): the test bench sees the circuit the user sees. Firmware
// built with avr-gcc runs on the emulated ATmega328P plugged in the committed circuits.

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace siliconforge;
using namespace std::chrono_literals;
using Catch::Approx;
using Json = nlohmann::json;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};
const std::filesystem::path sourceDir{SILICONFORGE_SOURCE_DIR};

testing::Bench benchWith(const char* firmware, std::string_view circuit) {
    const auto path = firmwareDir / firmware;
    if (!std::filesystem::exists(path))
        SKIP(path.string() << " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription(circuit));
    bench.load(path);
    return bench;
}

std::unique_ptr<circuit::Circuit> build(Json description) {
    auto created = circuit::Circuit::create(std::move(description));
    if (!created)
        FAIL(created.error().path << ": " << created.error().message);
    return std::move(*created);
}

/// The digit a 7-segment display shows, or -1.
int shownDigit(testing::Bench& bench, std::string_view id) {
    unsigned segments = 0;
    for (unsigned bit = 0; bit < 7; ++bit)
        if (bench.component(id).state(std::string(1, static_cast<char>('a' + bit)) + ".brightness") > 0.5)
            segments |= 1u << bit;
    constexpr unsigned patterns[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    for (int digit = 0; digit < 10; ++digit)
        if (patterns[digit] == segments)
            return digit;
    return -1;
}
}  // namespace

TEST_CASE("Every committed circuit is valid, built in and solvable", "[circuit][model]") {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator{sourceDir / "circuits"}) {
        INFO(entry.path().string());
        const auto loaded = circuit::loadDescription(entry.path());
        if (!loaded)
            FAIL(loaded.error().path << ": " << loaded.error().message);
        const auto name = entry.path().stem().string();
        CHECK(*loaded == circuit::builtinDescription(name));
        names.push_back(name);
        auto model  = build(*loaded);
        auto device = mcu::create(loaded->at("chip").at("mcu").get<std::string>());
        REQUIRE(device);
        REQUIRE(model->attach(*device));
        CHECK(model->shorts().empty());
        CHECK(model->voltageAt("T+1") == Approx(5.0));
        CHECK(model->voltageAt("B-1") == Approx(0.0));
    }
    std::ranges::sort(names);
    CHECK(names == circuit::builtinNames());
    CHECK(std::ranges::find(names, std::string{"arduino-demo"}) != names.end());
}

TEST_CASE("A potentiometer between the rails divides the supply voltage", "[circuit][model]") {
    auto model = build(circuit::builtinDescription("arduino-demo"));
    CHECK(model->state("pot-a0", "position") == 0.0);
    CHECK(model->voltageAt("g3") == Approx(0.0).margin(1e-3));
    REQUIRE(model->set("pot-a0", "position", 0.5));
    CHECK(model->voltageAt("g3") == Approx(2.5).margin(1e-3));
    CHECK(model->state("pot-a0", "voltage") == Approx(2.5).margin(1e-3));
    REQUIRE(model->set("pot-a0", "position", 1.0));
    CHECK(model->voltageAt("g3") == Approx(5.0).margin(1e-3));

    SECTION("interactions are checked") {
        CHECK(model->set("no-such-part", "position", 0.5).error() == circuit::SetError::UnknownComponent);
        CHECK(model->set("pot-a0", "angle", 0.5).error() == circuit::SetError::UnknownState);
        CHECK(model->set("pot-a0", "voltage", 0.5).error() == circuit::SetError::NotInteractive);
        CHECK(model->set("pot-a0", "position", 1.5).error() == circuit::SetError::OutOfRange);
        CHECK(model->set("button-d2", "pressed", 0.5).error() == circuit::SetError::OutOfRange);
        CHECK(model->state("pot-a0", "position") == 1.0);
    }
}

TEST_CASE("The wiper voltage reaches the analog input of the MCU", "[circuit][model]") {
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription("arduino-demo"));
    bench.turn("pot-a0", 0.3);
    CHECK(bench.pin("A0").voltage(5.0) == Approx(1.5).margin(1e-3));
    CHECK(bench.pin("A0").state() == PinState::Low);
    bench.turn("pot-a0", 0.9);
    CHECK(bench.pin("A0").state() == PinState::High);

    SECTION("applying a voltage to the pin turns the potentiometer that feeds it") {
        REQUIRE(bench.circuit().drive("A0", 4.0));
        CHECK(bench.component("pot-a0").state("position") == Approx(0.8));
        CHECK(bench.pin("A0").voltage(5.0) == Approx(4.0).margin(1e-3));
        CHECK_FALSE(bench.circuit().forced("A0"));
    }
    SECTION("a pin without a potentiometer gets a probe") {
        REQUIRE(bench.circuit().drive("A1", 3.3));
        CHECK(bench.circuit().forced("A1") == 3.3);
        CHECK(bench.pin("A1").voltage(5.0) == Approx(3.3));
        REQUIRE(bench.circuit().force("A1", std::nullopt));
        CHECK(bench.pin("A1").state() == PinState::Floating);
        CHECK_FALSE(bench.circuit().force("Z9", 1.0));
    }
}

TEST_CASE("The components know the MCU pin they are wired to", "[circuit][model]") {
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription("arduino-demo"));
    const auto view = bench.circuit().view();
    std::map<std::string, std::string> pins;
    for (const auto& component : view.at("components"))
        pins[component.at("id")] = component.at("pin");
    CHECK(pins.at("led-d13") == "D13");
    CHECK(pins.at("led-d6") == "D6");
    CHECK(pins.at("button-d2") == "D2");
    CHECK(pins.at("pot-a0") == "A0");
    CHECK(pins.at("r-d13") == "D13");
    CHECK(view.at("types").at("led").at("name") == "LED");
    CHECK(view.at("components")[1].at("properties").at("resistance") == 220);
}

SCENARIO("blink on the demonstration circuit: the LED on D13 is lit half a second out of two", "[circuit][model][firmware]") {
    auto bench = benchWith("blink.hex", "arduino-demo");
    const auto led = bench.component("led-d13");
    (void)bench.circuit().frame();
    for (int period = 0; period < 3; ++period) {
        bench.runFor(250ms);
        CHECK(led.brightness() == 1.0);  // (5 V - 1.8 V) / 225 Ω = 14 mA, above the 10 mA nominal current
        CHECK(led.state("current") == Approx((5.0 - 1.8) / 225.0).epsilon(0.01));
        bench.runFor(500ms);
        CHECK(led.brightness() == 0.0);
        CHECK_FALSE(led.lit());
        bench.runFor(250ms);
    }
    // What the interface displays is the average over the frame: half the time on.
    CHECK(bench.circuit().frame().at("led-d13").at("brightness").get<double>() == Approx(0.5).epsilon(0.005));
    CHECK(bench.component("led-d6").brightness() == 0.0);
    CHECK(bench.circuit().shorts().empty());
}

SCENARIO("pwm on the demonstration circuit: the green LED is as bright as the duty cycle", "[circuit][model][firmware]") {
    auto bench = benchWith("pwm.hex", "arduino-demo");
    bench.runFor(5ms);
    (void)bench.circuit().frame();
    bench.runFor(50ms);
    const auto frame = bench.circuit().frame();
    CHECK(frame.at("led-d6").at("brightness").get<double>() == Approx(0.25).epsilon(0.01));
}

SCENARIO("the push button of the demonstration circuit pulls D2 to ground", "[circuit][model]") {
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription("arduino-demo"));
    CHECK(bench.pin("D2").state() == PinState::Floating);
    bench.press("button-d2");
    CHECK(bench.pin("D2").state() == PinState::Low);
    CHECK(bench.pin("D2").external() == false);
    bench.release("button-d2");
    CHECK(bench.pin("D2").state() == PinState::Floating);
}

SCENARIO("a second circuit: an LED on D9 and two buttons", "[circuit][model][firmware]") {
    auto bench = benchWith("buttons.hex", "led-buttons");
    const auto led = bench.component("led-d9");
    bench.runFor(20ms);
    CHECK(bench.pin("D2").state() == PinState::High);  // Internal pull-up, button released
    CHECK_FALSE(led.lit());

    bench.press("button-d2");
    bench.runFor(20ms);
    CHECK(bench.pin("D2").state() == PinState::Low);
    CHECK(led.brightness() == 1.0);
    bench.release("button-d2");
    bench.runFor(20ms);
    CHECK_FALSE(led.lit());

    bench.press("button-d3");
    bench.runFor(20ms);
    bench.release("button-d3");
    bench.runFor(20ms);
    CHECK(led.lit());  // Latched by the press
    bench.press("button-d3");
    bench.runFor(20ms);
    bench.release("button-d3");
    bench.runFor(20ms);
    CHECK_FALSE(led.lit());
}

SCENARIO("counter on the 7-segment circuit: the digit shown follows the firmware", "[circuit][model][firmware]") {
    auto bench = benchWith("counter.hex", "seven-segment");
    bench.runFor(250ms);
    for (int digit = 0; digit < 12; ++digit) {
        INFO("digit " << digit);
        CHECK(shownDigit(bench, "display") == digit % 10);
        CHECK((bench.component("display").state("dp.brightness") > 0.5) == (digit % 2 == 1));
        bench.runFor(500ms);
    }
    CHECK(bench.serial().output().starts_with("0\r\n1\r\n2\r\n"));
    CHECK(bench.circuit().shorts().empty());
}

TEST_CASE("A common-anode display lights the segments pulled low", "[circuit][model]") {
    auto description = circuit::builtinDescription("seven-segment");
    description["wires"] = Json::array({
        {{"from", "j32"}, {"to", "T+32"}},  // Common to the supply
        {{"from", "j22"}, {"to", "T-22"}}   // Segment a to ground through r-a
    });
    SECTION("with commonAnode") {
        description["components"][1]["properties"]["commonAnode"] = true;
        const auto model = build(description);
        CHECK(model->state("display", "a.brightness") == 1.0);
        CHECK(model->state("display", "b.brightness") == 0.0);
    }
    SECTION("the same wiring leaves a common-cathode display dark") {
        const auto model = build(description);
        CHECK(model->state("display", "a.brightness") == 0.0);
    }
}

SCENARIO("a short circuit between an output and a rail is reported", "[circuit][model][firmware]") {
    const auto path = firmwareDir / "blink.hex";
    if (!std::filesystem::exists(path))
        SKIP(path.string() << " is missing");
    auto description = circuit::builtinDescription("arduino-demo");
    description["wires"].push_back({{"from", "g15"}, {"to", "T-15"}});  // D13 straight to ground
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(description);
    bench.load(path);
    bench.runFor(250ms);  // D13 is high
    REQUIRE(bench.circuit().shorts().size() == 1);
    CHECK(bench.circuit().shorts().front().find("D13") != std::string::npos);
    CHECK(bench.circuit().shorts().front().find("ground rail") != std::string::npos);
    bench.runFor(500ms);  // D13 is low: the two sources agree
    CHECK(bench.circuit().shorts().empty());

    SECTION("two opposite probes on one net") {
        REQUIRE(bench.circuit().force("D2", 5.0));
        bench.press("button-d2");  // D2 to ground while the probe holds it high
        CHECK(bench.circuit().shorts().size() == 1);
    }
}

TEST_CASE("A circuit is refused when it is invalid or when the chip does not fit", "[circuit][model]") {
    auto description = circuit::builtinDescription("arduino-demo");
    SECTION("an invalid description carries its JSON Pointer") {
        description["components"][1]["properties"]["resistance"] = -1;
        const auto created = circuit::Circuit::create(description);
        REQUIRE_FALSE(created);
        CHECK(created.error().path == "/components/1/properties/resistance");
        testing::Bench bench{mcu::create("atmega328p")};
        CHECK_THROWS_AS(bench.attach(description), std::invalid_argument);
        CHECK_THROWS_AS(bench.circuit(), std::logic_error);
    }
    SECTION("the package must fit on the board") {
        description["chip"]["column"] = 20;
        description["components"]     = Json::array();
        description["wires"]          = Json::array();
        auto model  = build(description);
        auto device = mcu::create("atmega328p");
        const auto attached = model->attach(*device);
        REQUIRE_FALSE(attached);
        CHECK(attached.error().path == "/chip/column");
    }
}
