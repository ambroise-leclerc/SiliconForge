// Electrical model of the circuits (ADR 0005, #19, #21, #22): the test bench sees the circuit the user sees. Firmware
// built with avr-gcc runs on the emulated ATmega328P plugged in the committed circuits.

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"

#include <filesystem>
#include <string>

using namespace webfront::bdd;

using namespace siliconforge;
using namespace std::chrono_literals;
using siliconforge::test::Approx;
using Json = nlohmann::json;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};
const std::filesystem::path sourceDir{SILICONFORGE_SOURCE_DIR};

testing::Bench benchWith(const char* firmware, std::string_view circuit) {
    const auto path = firmwareDir / firmware;
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription(circuit));
    bench.load(path);
    return bench;
}

std::unique_ptr<circuit::Circuit> build(Json description) {
    auto created = circuit::Circuit::create(std::move(description));
    if (!created)
        fail(created.error().path, ": ", created.error().message);
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

const Scenario everyCommittedCircuitIsValidBuiltIn{"Every committed circuit is valid, built in and solvable", {"circuit", "model"}, [] {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator{sourceDir / "circuits"}) {
        const auto note1 = info(entry.path().string());
        const auto loaded = circuit::loadDescription(entry.path());
        if (!loaded)
            fail(loaded.error().path, ": ", loaded.error().message);
        const auto name = entry.path().stem().string();
        check(*loaded == circuit::builtinDescription(name));
        names.push_back(name);
        auto model  = build(*loaded);
        auto device = mcu::create(loaded->at("chip").at("mcu").get<std::string>());
        require(device);
        require(model->attach(*device));
        check(model->shorts().empty());
        check(model->voltageAt("T+1") == Approx(5.0));
        check(model->voltageAt("B-1") == Approx(0.0));
    }
    std::ranges::sort(names);
    check(names == circuit::builtinNames());
    check(std::ranges::find(names, std::string{"arduino-demo"}) != names.end());
}};

const Scenario aPotentiometerBetweenTheRailsDividesThe{"A potentiometer between the rails divides the supply voltage", {"circuit", "model"}, [] {
    auto model = build(circuit::builtinDescription("arduino-demo"));
    check(model->state("pot-a0", "position") == 0.0);
    check(model->voltageAt("g3") == Approx(0.0).margin(1e-3));
    require(model->set("pot-a0", "position", 0.5));
    check(model->voltageAt("g3") == Approx(2.5).margin(1e-3));
    check(model->state("pot-a0", "voltage") == Approx(2.5).margin(1e-3));
    require(model->set("pot-a0", "position", 1.0));
    check(model->voltageAt("g3") == Approx(5.0).margin(1e-3));

    section("interactions are checked", [&] {
        check(model->set("no-such-part", "position", 0.5).error() == circuit::SetError::UnknownComponent);
        check(model->set("pot-a0", "angle", 0.5).error() == circuit::SetError::UnknownState);
        check(model->set("pot-a0", "voltage", 0.5).error() == circuit::SetError::NotInteractive);
        check(model->set("pot-a0", "position", 1.5).error() == circuit::SetError::OutOfRange);
        check(model->set("button-d2", "pressed", 0.5).error() == circuit::SetError::OutOfRange);
        check(model->state("pot-a0", "position") == 1.0);
    });
}};

const Scenario theWiperVoltageReachesTheAnalogInput{"The wiper voltage reaches the analog input of the MCU", {"circuit", "model"}, [] {
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription("arduino-demo"));
    bench.turn("pot-a0", 0.3);
    check(bench.pin("A0").voltage(5.0) == Approx(1.5).margin(1e-3));
    check(bench.pin("A0").state() == PinState::Low);
    bench.turn("pot-a0", 0.9);
    check(bench.pin("A0").state() == PinState::High);

    section("applying a voltage to the pin turns the potentiometer that feeds it", [&] {
        require(bench.circuit().drive("A0", 4.0));
        check(bench.component("pot-a0").state("position") == Approx(0.8));
        check(bench.pin("A0").voltage(5.0) == Approx(4.0).margin(1e-3));
        check(!bench.circuit().forced("A0"));
    });
    section("a pin without a potentiometer gets a probe", [&] {
        require(bench.circuit().drive("A1", 3.3));
        check(bench.circuit().forced("A1") == 3.3);
        check(bench.pin("A1").voltage(5.0) == Approx(3.3));
        require(bench.circuit().force("A1", std::nullopt));
        check(bench.pin("A1").state() == PinState::Floating);
        check(!bench.circuit().force("Z9", 1.0));
    });
}};

const Scenario theComponentsKnowTheMcuPinThey{"The components know the MCU pin they are wired to", {"circuit", "model"}, [] {
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription("arduino-demo"));
    const auto view = bench.circuit().view();
    std::map<std::string, std::string> pins;
    for (const auto& component : view.at("components"))
        pins[component.at("id")] = component.at("pin");
    check(pins.at("led-d13") == "D13");
    check(pins.at("led-d6") == "D6");
    check(pins.at("button-d2") == "D2");
    check(pins.at("pot-a0") == "A0");
    check(pins.at("r-d13") == "D13");
    check(view.at("types").at("led").at("name") == "LED");
    check(view.at("components")[1].at("properties").at("resistance") == 220);
}};

const Scenario blinkOnTheDemonstrationCircuitTheLed{"blink on the demonstration circuit: the LED on D13 is lit half a second out of two",
                                                    {"circuit", "model", "firmware"}, [] {
    auto bench = benchWith("blink.hex", "arduino-demo");
    const auto led = bench.component("led-d13");
    (void)bench.circuit().frame();
    for (int period = 0; period < 3; ++period) {
        bench.runFor(250ms);
        check(led.brightness() == 1.0);  // (5 V - 1.8 V) / 225 Ω = 14 mA, above the 10 mA nominal current
        check(led.state("current") == Approx((5.0 - 1.8) / 225.0).epsilon(0.01));
        bench.runFor(500ms);
        check(led.brightness() == 0.0);
        check(!led.lit());
        bench.runFor(250ms);
    }
    // What the interface displays is the average over the frame: half the time on.
    check(bench.circuit().frame().at("led-d13").at("brightness").get<double>() == Approx(0.5).epsilon(0.005));
    check(bench.component("led-d6").brightness() == 0.0);
    check(bench.circuit().shorts().empty());
}};

const Scenario pwmOnTheDemonstrationCircuitTheGreen{"pwm on the demonstration circuit: the green LED is as bright as the duty cycle",
                                                    {"circuit", "model", "firmware"}, [] {
    auto bench = benchWith("pwm.hex", "arduino-demo");
    bench.runFor(5ms);
    (void)bench.circuit().frame();
    bench.runFor(50ms);
    const auto frame = bench.circuit().frame();
    check(frame.at("led-d6").at("brightness").get<double>() == Approx(0.25).epsilon(0.01));
}};

const Scenario thePushButtonOfTheDemonstrationCircuit{"the push button of the demonstration circuit pulls D2 to ground", {"circuit", "model"}, [] {
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(circuit::builtinDescription("arduino-demo"));
    check(bench.pin("D2").state() == PinState::Floating);
    bench.press("button-d2");
    check(bench.pin("D2").state() == PinState::Low);
    check(bench.pin("D2").external() == false);
    bench.release("button-d2");
    check(bench.pin("D2").state() == PinState::Floating);
}};

const Scenario aSecondCircuitAnLedOnD9{"a second circuit: an LED on D9 and two buttons", {"circuit", "model", "firmware"}, [] {
    auto bench = benchWith("buttons.hex", "led-buttons");
    const auto led = bench.component("led-d9");
    bench.runFor(20ms);
    check(bench.pin("D2").state() == PinState::High);  // Internal pull-up, button released
    check(!led.lit());

    bench.press("button-d2");
    bench.runFor(20ms);
    check(bench.pin("D2").state() == PinState::Low);
    check(led.brightness() == 1.0);
    bench.release("button-d2");
    bench.runFor(20ms);
    check(!led.lit());

    bench.press("button-d3");
    bench.runFor(20ms);
    bench.release("button-d3");
    bench.runFor(20ms);
    check(led.lit());  // Latched by the press
    bench.press("button-d3");
    bench.runFor(20ms);
    bench.release("button-d3");
    bench.runFor(20ms);
    check(!led.lit());
}};

const Scenario counterOnThe7SegmentCircuitThe{"counter on the 7-segment circuit: the digit shown follows the firmware", {"circuit", "model", "firmware"}, [] {
    auto bench = benchWith("counter.hex", "seven-segment");
    bench.runFor(250ms);
    for (int digit = 0; digit < 12; ++digit) {
        const auto note2 = info("digit ", digit);
        check(shownDigit(bench, "display") == digit % 10);
        check((bench.component("display").state("dp.brightness") > 0.5) == (digit % 2 == 1));
        bench.runFor(500ms);
    }
    check(bench.serial().output().starts_with("0\r\n1\r\n2\r\n"));
    check(bench.circuit().shorts().empty());
}};

const Scenario aCommonAnodeDisplayLightsTheSegments{"A common-anode display lights the segments pulled low", {"circuit", "model"}, [] {
    auto description = circuit::builtinDescription("seven-segment");
    description["wires"] = Json::array({
        {{"from", "j32"}, {"to", "T+32"}},  // Common to the supply
        {{"from", "j22"}, {"to", "T-22"}}   // Segment a to ground through r-a
    });
    section("with commonAnode", [&] {
        description["components"][1]["properties"]["commonAnode"] = true;
        const auto model = build(description);
        check(model->state("display", "a.brightness") == 1.0);
        check(model->state("display", "b.brightness") == 0.0);
    });
    section("the same wiring leaves a common-cathode display dark", [&] {
        const auto model = build(description);
        check(model->state("display", "a.brightness") == 0.0);
    });
}};

const Scenario aShortCircuitBetweenAnOutputAnd{"a short circuit between an output and a rail is reported", {"circuit", "model", "firmware"}, [] {
    const auto path = firmwareDir / "blink.hex";
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing");
    auto description = circuit::builtinDescription("arduino-demo");
    description["wires"].push_back({{"from", "g15"}, {"to", "T-15"}});  // D13 straight to ground
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(description);
    bench.load(path);
    bench.runFor(250ms);  // D13 is high
    require(bench.circuit().shorts().size() == 1);
    check(bench.circuit().shorts().front().find("D13") != std::string::npos);
    check(bench.circuit().shorts().front().find("ground rail") != std::string::npos);
    bench.runFor(500ms);  // D13 is low: the two sources agree
    check(bench.circuit().shorts().empty());

    section("two opposite probes on one net", [&] {
        require(bench.circuit().force("D2", 5.0));
        bench.press("button-d2");  // D2 to ground while the probe holds it high
        check(bench.circuit().shorts().size() == 1);
    });
}};

const Scenario aCircuitIsRefusedWhenItIs{"A circuit is refused when it is invalid or when the chip does not fit", {"circuit", "model"}, [] {
    auto description = circuit::builtinDescription("arduino-demo");
    section("an invalid description carries its JSON Pointer", [&] {
        description["components"][1]["properties"]["resistance"] = -1;
        const auto created = circuit::Circuit::create(description);
        require(!created);
        check(created.error().path == "/components/1/properties/resistance");
        testing::Bench bench{mcu::create("atmega328p")};
        checkThrows<std::invalid_argument>([&] { bench.attach(description); });
        checkThrows<std::logic_error>([&] { static_cast<void>(bench.circuit()); });
    });
    section("the package must fit on the board", [&] {
        description["chip"]["column"] = 20;
        description["components"]     = Json::array();
        description["wires"]          = Json::array();
        auto model  = build(description);
        auto device = mcu::create("atmega328p");
        const auto attached = model->attach(*device);
        require(!attached);
        check(attached.error().path == "/chip/column");
    });
}};

const Scenario aSupplyReferencedOnlyByThePull{"a supply referenced only by the pull-up of an input is regulated", {"circuit", "model", "supply"}, [] {
    auto description = circuit::builtinDescription("led-buttons");
    description["components"].push_back({{"id", "psu"}, {"type", "bench-supply"}, {"at", {{"positive", "a25"}, {"negative", "a27"}}}});
    description["components"].push_back({{"id", "r-ground"}, {"type", "resistor"}, {"at", {{"a", "c25"}, {"b", "c29"}}}, {"properties", {{"resistance", 1000}}}});
    description["wires"].push_back({{"from", "d29"}, {"to", "B-29"}, {"color", "black"}});
    description["wires"].push_back({{"from", "d4"}, {"to", "d27"}, {"color", "green"}});
    given("a channel whose negative terminal is on D2, an input with its pull-up, and whose positive terminal returns to ground through 1 kohm", [&] {
        const auto path = firmwareDir / "buttons.hex";
        if (!std::filesystem::exists(path))
            skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
        testing::Bench bench{mcu::create("atmega328p")};
        bench.attach(description);
        bench.load(path);
        bench.runFor(20ms);  // The firmware turns the pull-up on.
        auto psu = bench.component("psu");
        require(bench.circuit().set("psu", "voltage", 1.0));
        require(bench.circuit().set("psu", "currentLimit", 0.1));
        require(bench.circuit().set("psu", "enabled", 1.0));
        then("it holds 1 V across its terminals and delivers what the pull-up pushes through 36 kohm", [&] {
            check(psu.state("mode") == 1.0);
            check(psu.state("outputVoltage") == Approx(1.0).margin(1e-3));
            check(psu.state("outputCurrent") == Approx(6.0 / 36000.0).epsilon(0.01));
        });
    });
}};
