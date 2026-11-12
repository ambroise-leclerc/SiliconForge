// Electrical model of a circuit (ADR 0005): nets, sources, LED current, divider, button, pulls and short circuits,
// checked on the demonstration circuit with the pins of an ATmega328P driven by hand.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/circuit/Description.hpp>
#include <siliconforge/mcu/Registry.hpp>

#include <algorithm>
#include <stdexcept>

using namespace siliconforge;
using Catch::Approx;
using Json = nlohmann::json;

namespace {
Json demo() {
    return circuit::builtinDescription("arduino-demo");
}

Json& part(Json& description, std::string_view id) {
    auto&      components = description.at("components");
    const auto found      = std::ranges::find_if(components, [&](const Json& candidate) {
        return candidate.at("id") == id;
    });
    REQUIRE(found != components.end());
    return *found;
}

circuit::Circuit make(const Json& description, const circuit::Library& library = circuit::Library::builtin()) {
    auto built = circuit::Circuit::fromJson(description, library);
    if (!built)
        FAIL(built.error().path << ": " << built.error().message);
    return std::move(*built);
}

/// A chip on the demonstration circuit, or on a variant of it.
struct Board {
    std::unique_ptr<Mcu> mcu{mcu::create("atmega328p")};
    circuit::Circuit     circuit;

    explicit Board(const Json& description = demo(), const circuit::Library& library = circuit::Library::builtin()) : circuit(make(description, library)) {
        const auto attached = circuit.attach(*mcu);
        if (!attached)
            FAIL(attached.error().path << ": " << attached.error().message);
    }

    Pin& pin(std::string_view name) {
        return *mcu->pin(name);
    }
    void drive(std::string_view name, bool high) {
        pin(name).configure(true, high, false);
    }
};
}  // namespace

TEST_CASE("Holes, wires and the chip form the nets of the demonstration circuit", "[circuit][model]") {
    auto circuit = circuit::Circuit::builtin("arduino-demo");
    CHECK(circuit.name() == "Arduino demo");
    SECTION("a strip joins its five holes, not the other side of the channel nor its neighbours") {
        CHECK(circuit.connected("f15", "j15"));
        CHECK(circuit.connected("a15", "e15"));
        CHECK_FALSE(circuit.connected("e15", "f15"));
        CHECK_FALSE(circuit.connected("f15", "f16"));
    }
    SECTION("a rail is one net along the board") {
        CHECK(circuit.connected("T+1", "T+30"));
        CHECK_FALSE(circuit.connected("T+1", "T-1"));
        CHECK_FALSE(circuit.connected("T+1", "B+1"));
    }
    SECTION("wires join nets: supply of the chip, wiper to A0, button to D2") {
        CHECK(circuit.connected("e12", "B+1"));  // VCC
        CHECK(circuit.connected("e13", "B-1"));  // GND
        CHECK(circuit.connected("g3", "f11"));   // Wiper of the potentiometer to A0 (pin 23)
        CHECK(circuit.connected("e9", "c4"));    // D2 (pin 4) to the button
        CHECK(circuit.connected("c2", "B-30"));  // Other side of the button to ground
        CHECK(circuit.connected("h22", "f22"));  // Resistor of D13 to the anode of its LED
    }
    SECTION("the rails are powered before any chip is plugged") {
        CHECK(circuit.voltage("T+7") == 5.0);
        CHECK(circuit.voltage("B-7") == 0.0);
        CHECK(circuit.voltage("b12") == 5.0);
        CHECK_FALSE(circuit.voltage("f15"));
        CHECK(circuit.faults().empty());
    }
    SECTION("holes off the board are refused") {
        CHECK_THROWS_AS(circuit.connected("f31", "f1"), std::invalid_argument);
        CHECK_THROWS_AS(circuit.voltage("z1"), std::invalid_argument);
    }
}

TEST_CASE("An LED is lit by the current its series resistor lets through", "[circuit][model]") {
    SECTION("D13 high: I = (5 - 1.8) / 220, above the nominal current") {
        Board board;
        auto  led = board.circuit.component("led-d13");
        CHECK(led.brightness() == 0.0);
        board.drive("D13", true);
        CHECK(board.circuit.voltage("h15") == 5.0);
        CHECK(led.current() == Approx(3.2 / 220));
        CHECK(led.brightness() == 1.0);
        CHECK(board.circuit.component("led-d6").brightness() == 0.0);
        board.drive("D13", false);
        CHECK(led.current() == 0.0);
        CHECK(led.brightness() == 0.0);
    }
    SECTION("a larger resistor dims it") {
        auto description                                       = demo();
        part(description, "r-d13")["properties"]["resistance"] = 1000;
        Board board{description};
        board.drive("D13", true);
        CHECK(board.circuit.component("led-d13").current() == Approx(0.0032));
        CHECK(board.circuit.component("led-d13").brightness() == Approx(0.32));
    }
    SECTION("the forward voltage of each LED counts") {
        Board board;
        board.drive("D6", true);
        CHECK(board.circuit.component("led-d6").current() == Approx(2.9 / 220));
    }
    SECTION("an LED plugged backwards stays off") {
        auto description                   = demo();
        part(description, "led-d13")["at"] = {
            {  "anode", "f23"},
            {"cathode", "f22"}
        };
        Board board{description};
        board.drive("D13", true);
        CHECK(board.circuit.component("led-d13").brightness() == 0.0);
    }
    SECTION("an input pin lights nothing") {
        Board board;
        board.pin("D13").configure(false, false, true);
        CHECK(board.circuit.component("led-d13").brightness() == 0.0);
    }
    SECTION("the mean brightness integrates simulated time") {
        Board      board;
        auto       led   = board.circuit.component("led-d13");
        const auto start = board.mcu->cycles();
        board.mcu->run(1000);
        const auto lit = board.mcu->cycles();
        board.drive("D13", true);
        board.mcu->run(3000);
        const auto end = board.mcu->cycles();
        REQUIRE(lit > start);
        REQUIRE(end > lit);
        CHECK(led.meanBrightness() == Approx(static_cast<double>(end - lit) / static_cast<double>(end - start)));
        board.mcu->run(1000);
        CHECK(led.meanBrightness() == 1.0);
        CHECK(led.meanBrightness() == 1.0);  // Empty window: the brightness right now.
    }
}

TEST_CASE("A potentiometer divides the voltage between its ends", "[circuit][model]") {
    Board board;
    auto  pot = board.circuit.component("pot-a0");
    CHECK(pot.position() == 0.0);
    CHECK(board.pin("A0").voltage(5.0) == 0.0);
    pot.set("position", 0.5);
    CHECK(board.pin("A0").voltage(5.0) == Approx(2.5));
    CHECK(board.circuit.voltage("g3") == Approx(2.5));
    CHECK_FALSE(board.pin("A0").level());
    pot.set("position", 0.75);
    CHECK(board.pin("A0").voltage(5.0) == Approx(3.75));
    CHECK(board.pin("A0").level());
    pot.set("position", 7);
    CHECK(pot.position() == 1.0);
    CHECK(board.pin("A0").voltage(5.0) == 5.0);
}

TEST_CASE("A push button connects its two nets while pressed", "[circuit][model]") {
    Board board;
    auto  button = board.circuit.component("button-d2");
    auto& d2     = board.pin("D2");
    CHECK_FALSE(button.pressed());
    CHECK(d2.state() == PinState::Floating);
    d2.configure(false, false, true);  // The firmware enables the pull-up.
    CHECK(d2.state() == PinState::High);

    button.press();
    CHECK(button.pressed());
    CHECK(board.circuit.connected("c4", "c2"));
    CHECK(d2.state() == PinState::Low);
    CHECK(d2.external() == false);

    button.release();
    CHECK_FALSE(board.circuit.connected("c4", "c2"));
    CHECK_FALSE(d2.external());
    CHECK(d2.state() == PinState::High);
    CHECK(board.circuit.faults().empty());
}

TEST_CASE("Resistors pull the nets that nothing drives", "[circuit][model]") {
    auto description = demo();
    // D4 is pin 6, in e11; column 13 is the ground of the chip.
    description["components"].push_back({
        {        "id",                  "pull-down"},
        {      "type",                   "resistor"},
        {        "at", {{"a", "b11"}, {"b", "b13"}}},
        {"properties",      {{"resistance", 10000}}}
    });
    Board board{description};
    auto& d4 = board.pin("D4");
    CHECK(board.circuit.voltage("e11") == 0.0);
    CHECK(d4.external() == false);
    SECTION("the pull loses against the pin once it is an output, and is back afterwards") {
        board.drive("D4", true);
        CHECK(board.circuit.voltage("e11") == 5.0);
        CHECK(board.circuit.faults().empty());
        d4.configure(false, false, true);
        CHECK(d4.state() == PinState::Low);
    }
    SECTION("two resistors make a divider") {
        description["components"].push_back({
            {        "id",                   "pull-up"},
            {      "type",                  "resistor"},
            {        "at", {{"a", "c11"}, {"b", "c8"}}},
            {"properties",     {{"resistance", 30000}}}
        });
        description["wires"].push_back({
            {"from",  "a8"},
            {  "to", "B+8"}
        });  // Column 8 is D1: left as an input here.
        Board divided{description};
        CHECK(divided.circuit.voltage("e11") == Approx(1.25));
        CHECK(divided.pin("D4").voltage(5.0) == Approx(1.25));
    }
}

TEST_CASE("Opposed sources on a net are reported as a short circuit", "[circuit][model]") {
    SECTION("two outputs at different levels") {
        auto description = demo();
        description["wires"].push_back({
            {"from", "j15"},
            {  "to", "j16"}
        });  // D13 to D12
        Board board{description};
        board.drive("D13", true);
        CHECK(board.circuit.faults().empty());  // D12 is still an input.
        board.drive("D12", true);
        CHECK(board.circuit.faults().empty());
        board.mcu->run(100);
        board.drive("D12", false);
        REQUIRE(board.circuit.faults().size() == 1);
        const auto& fault = board.circuit.faults().front();
        CHECK(fault.kind == circuit::Fault::Kind::ShortCircuit);
        CHECK(fault.at == board.mcu->cycles());
        CHECK(fault.sources == std::vector<std::string>{"PB4", "PB5"});
        CHECK(fault.description == "Short circuit between PB4 (0 V) and PB5 (5 V)");

        board.drive("D13", true);  // Still shorted: reported once.
        CHECK(board.circuit.faults().size() == 1);
        board.drive("D13", false);
        board.drive("D13", true);  // Shorted again after having agreed.
        CHECK(board.circuit.faults().size() == 2);
        board.circuit.clearFaults();
        CHECK(board.circuit.faults().empty());
    }
    SECTION("an output against a rail") {
        auto description = demo();
        description["wires"].push_back({
            {"from",  "j15"},
            {  "to", "T-15"}
        });
        Board board{description};
        board.drive("D13", false);
        CHECK(board.circuit.faults().empty());
        board.drive("D13", true);
        REQUIRE(board.circuit.faults().size() == 1);
        CHECK(board.circuit.faults().front().sources == std::vector<std::string>{"T-", "PB5"});
    }
    SECTION("the two rails wired together, before any chip") {
        auto description = demo();
        description["wires"].push_back({
            {"from", "T+1"},
            {  "to", "T-1"}
        });
        const auto circuit = make(description);
        REQUIRE(circuit.faults().size() == 1);
        CHECK(circuit.faults().front().sources == std::vector<std::string>{"T+", "T-"});
    }
}

TEST_CASE("A composite component exposes one state per named element", "[circuit][model]") {
    auto library = circuit::Library::builtin();
    REQUIRE(library.add(Json::parse(R"({
        "formatVersion": 1, "type": "dual-led", "name": "Two LEDs, common cathode",
        "terminals": [{"name": "left"}, {"name": "right"}, {"name": "common"}],
        "properties": {
            "forwardVoltage": {"type": "number", "unit": "V", "default": 2.0},
            "nominalCurrent": {"type": "number", "unit": "A", "default": 0.01}
        },
        "footprint": {"kind": "fixed", "offsets": {"left": [0, 0], "right": [1, 0], "common": [2, 0]}},
        "model": {"elements": [
            {"type": "led", "name": "left", "between": ["left", "common"]},
            {"type": "led", "name": "right", "between": ["right", "common"]}
        ]},
        "state": {"left.brightness": {"type": "number"}, "right.brightness": {"type": "number"}}
    })")));
    auto description = demo();
    description["components"].push_back({
        {  "id",                                                 "pair"},
        {"type",                                             "dual-led"},
        {  "at", {{"left", "j26"}, {"right", "j27"}, {"common", "j28"}}}
    });
    description["wires"].push_back({
        {"from",  "i26"},
        {  "to", "T+26"}
    });
    description["wires"].push_back({
        {"from",  "i28"},
        {  "to", "T-28"}
    });
    Board board{description, library};
    auto  pair = board.circuit.component("pair");
    CHECK(pair.type() == "dual-led");
    CHECK(pair.state("left.brightness") == 1.0);  // Straight across the supply, without a resistor.
    CHECK(pair.state("right.brightness") == 0.0);
    CHECK_THROWS_AS(pair.brightness(), std::out_of_range);
}

TEST_CASE("Components are reached by id and only their interactive state can be set", "[circuit][model]") {
    Board board;
    CHECK(board.circuit.componentIds() == std::vector<std::string>{"xtal", "r-d13", "led-d13", "r-d6", "led-d6", "button-d2", "pot-a0"});
    CHECK(board.circuit.component("led-d13").id() == "led-d13");
    CHECK(board.circuit.component("led-d13").type() == "led");
    CHECK_THROWS_AS(board.circuit.component("led-d7"), std::out_of_range);
    CHECK_THROWS_AS(board.circuit.component("r-d13").state("brightness"), std::out_of_range);
    CHECK_THROWS_AS(board.circuit.component("led-d13").set("brightness", 1), std::invalid_argument);
}

TEST_CASE("Plugging the chip checks that it fits the board", "[circuit][model]") {
    auto mcu = mcu::create("atmega328p");
    SECTION("a package longer than the board") {
        auto description              = demo();
        description["wires"]          = Json::array();
        description["components"]     = Json::array();
        description["chip"]["column"] = 20;
        auto       circuit            = make(description);
        const auto result             = circuit.attach(*mcu);
        REQUIRE_FALSE(result);
        CHECK(result.error().path == "/chip/column");
        CHECK(result.error().message.find("needs 14 columns") != std::string::npos);
    }
    SECTION("a lead in a hole of the chip") {
        auto description = demo();
        description["wires"].push_back({
            {"from", "e6"},
            {  "to", "a1"}
        });
        auto       circuit = make(description);
        const auto result  = circuit.attach(*mcu);
        REQUIRE_FALSE(result);
        CHECK(result.error().path == "/wires/11/from");
        CHECK(result.error().message.find("e6") != std::string::npos);
    }
    SECTION("a circuit is attached once") {
        auto circuit = circuit::Circuit::builtin("arduino-demo");
        REQUIRE(circuit.attach(*mcu));
        CHECK_FALSE(circuit.attach(*mcu));
    }
    SECTION("pins keep working after the circuit is gone") {
        {
            auto circuit = circuit::Circuit::builtin("arduino-demo");
            REQUIRE(circuit.attach(*mcu));
        }
        mcu->pin("D13")->configure(true, true, false);
        CHECK(mcu->pin("D13")->state() == PinState::High);
    }
}
