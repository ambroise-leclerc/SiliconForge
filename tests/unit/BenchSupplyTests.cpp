// Programmable supply and variable resistive load (#62, ADR 0010): the voltage regulation to current limitation
// transition is qualified on reference values that follow from Ohm's law, not from the model's own output.

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/circuit/Description.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace siliconforge;
using Catch::Approx;
using Json = nlohmann::json;

namespace {
/// A supply (a22 +, a24 -) feeding a variable load between b22 and b26; the supply returns to the ground rail.
Json bench(bool grounded = true) {
    Json description = {
        {"formatVersion", 1},
        {"name", "Supply and load"},
        {"board", {{"type", "breadboard"}, {"columns", 30}}},
        {"chip", {{"mcu", "atmega328p"}, {"column", 3}}},
        {"components",
         {{{"id", "psu"}, {"type", "bench-supply"}, {"at", {{"positive", "a22"}, {"negative", "a24"}}}},
          {{"id", "load"}, {"type", "variable-load"}, {"at", {{"a", "b22"}, {"b", "b26"}}}}}},
        {"wires", {{{"from", "c26"}, {"to", "B-26"}, {"color", "black"}}}}
    };
    if (grounded)
        description["wires"].push_back({{"from", "b24"}, {"to", "T-24"}, {"color", "black"}});
    return description;
}

std::unique_ptr<circuit::Circuit> build(Json description) {
    auto created = circuit::Circuit::create(std::move(description));
    if (!created)
        FAIL(created.error().path << ": " << created.error().message);
    return std::move(*created);
}

double reading(circuit::Circuit& circuit, std::string_view id, std::string_view name) {
    return circuit.state(id, name).value();
}

void configure(circuit::Circuit& circuit, double volts, double limit, bool enabled) {
    REQUIRE(circuit.set("psu", "voltage", volts));
    REQUIRE(circuit.set("psu", "currentLimit", limit));
    REQUIRE(circuit.set("psu", "enabled", enabled ? 1.0 : 0.0));
}
}  // namespace

SCENARIO("a supply set to 5 V and 100 mA feeds a 100 ohm load", "[circuit][supply]") {
    GIVEN("a channel that is set but not enabled") {
        auto circuit = build(bench());
        REQUIRE(circuit->set("load", "resistance", 100.0));
        REQUIRE(circuit->set("psu", "voltage", 5.0));
        REQUIRE(circuit->set("psu", "currentLimit", 0.1));

        THEN("its output is open: nothing at the node, nothing delivered") {
            CHECK(circuit->voltageAt("a22").value() == Approx(0.0).margin(1e-6));
            CHECK(reading(*circuit, "psu", "outputCurrent") == 0.0);
            CHECK(reading(*circuit, "psu", "mode") == 0.0);
        }
        WHEN("the channel is enabled") {
            REQUIRE(circuit->set("psu", "enabled", 1.0));
            THEN("the node holds the setpoint and the load draws V / R, in constant voltage") {
                CHECK(circuit->voltageAt("a22").value() == Approx(5.0).margin(1e-6));
                CHECK(reading(*circuit, "psu", "outputVoltage") == Approx(5.0).margin(1e-6));
                CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
                CHECK(reading(*circuit, "psu", "mode") == 1.0);
                CHECK(reading(*circuit, "load", "voltage") == Approx(5.0).margin(1e-6));
                CHECK(reading(*circuit, "load", "current") == Approx(0.05).margin(1e-6));
            }
        }
    }
}

SCENARIO("a change of load moves the supply between voltage regulation and current limitation", "[circuit][supply]") {
    GIVEN("a channel at 5 V and 100 mA, enabled, on 100 ohms") {
        auto circuit = build(bench());
        REQUIRE(circuit->set("load", "resistance", 100.0));
        configure(*circuit, 5.0, 0.1, true);
        REQUIRE(reading(*circuit, "psu", "mode") == 1.0);

        WHEN("the load falls to 25 ohms, which would draw 200 mA") {
            REQUIRE(circuit->set("load", "resistance", 25.0));
            THEN("the current is limited to 100 mA and the voltage falls to 100 mA x 25 ohms") {
                CHECK(reading(*circuit, "psu", "mode") == 2.0);
                CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-6));
                CHECK(circuit->voltageAt("a22").value() == Approx(2.5).margin(1e-6));
                CHECK(reading(*circuit, "load", "current") == Approx(0.1).margin(1e-6));
            }
            AND_WHEN("the load rises again to 100 ohms") {
                REQUIRE(circuit->set("load", "resistance", 100.0));
                THEN("the supply regulates the voltage again") {
                    CHECK(reading(*circuit, "psu", "mode") == 1.0);
                    CHECK(circuit->voltageAt("a22").value() == Approx(5.0).margin(1e-6));
                    CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
                }
            }
        }
        WHEN("the load is 50 ohms, exactly the critical resistance V / limit") {
            REQUIRE(circuit->set("load", "resistance", 50.0));
            THEN("the supply is still in constant voltage and delivers the limit") {
                CHECK(reading(*circuit, "psu", "mode") == 1.0);
                CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-6));
                CHECK(circuit->voltageAt("a22").value() == Approx(5.0).margin(1e-6));
            }
        }
        WHEN("the load is just below the critical resistance, 49 ohms") {
            REQUIRE(circuit->set("load", "resistance", 49.0));
            THEN("the supply is limiting") {
                CHECK(reading(*circuit, "psu", "mode") == 2.0);
                CHECK(circuit->voltageAt("a22").value() == Approx(4.9).margin(1e-6));
            }
        }
        WHEN("the limit is lowered to 20 mA") {
            REQUIRE(circuit->set("psu", "currentLimit", 0.02));
            THEN("the 100 ohm load now limits it: 20 mA, 2 V") {
                CHECK(reading(*circuit, "psu", "mode") == 2.0);
                CHECK(circuit->voltageAt("a22").value() == Approx(2.0).margin(1e-6));
                CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.02).margin(1e-6));
            }
        }
        WHEN("the channel is disabled again") {
            REQUIRE(circuit->set("psu", "enabled", 0.0));
            THEN("the output reads zero") {
                CHECK(circuit->voltageAt("a22").value() == Approx(0.0).margin(1e-6));
                CHECK(reading(*circuit, "psu", "outputCurrent") == 0.0);
                CHECK(reading(*circuit, "psu", "mode") == 0.0);
            }
        }
    }
}

SCENARIO("a supply with no reference delivers nothing", "[circuit][supply]") {
    GIVEN("a channel whose negative terminal is not tied to any source") {
        auto circuit = build(bench(false));
        configure(*circuit, 5.0, 0.1, true);
        THEN("the mode stays off and no current flows") {
            CHECK(reading(*circuit, "psu", "mode") == 0.0);
            CHECK(reading(*circuit, "psu", "outputCurrent") == 0.0);
        }
    }
}

SCENARIO("the settings of a supply stay within what a bench channel can do", "[circuit][supply]") {
    GIVEN("a channel") {
        auto circuit = build(bench());
        THEN("a voltage or a limit beyond its range is refused") {
            CHECK(circuit->set("psu", "voltage", 31.0).error() == circuit::SetError::OutOfRange);
            CHECK(circuit->set("psu", "currentLimit", 0.0).error() == circuit::SetError::OutOfRange);
            CHECK(circuit->set("psu", "outputCurrent", 0.5).error() == circuit::SetError::NotInteractive);
            CHECK(circuit->set("load", "resistance", 0.0).error() == circuit::SetError::OutOfRange);
        }
    }
}

SCENARIO("a multimeter reads the voltage across the load without disturbing it", "[circuit][supply][multimeter]") {
    GIVEN("the bench demonstration circuit, with its supply at 5 V and 100 mA") {
        auto created = circuit::Circuit::create(circuit::builtinDescription("bench-demo"));
        REQUIRE(created);
        auto& circuit = **created;
        REQUIRE(circuit.set("load", "resistance", 100.0));
        configure(circuit, 5.0, 0.1, true);
        THEN("the multimeter reads the setpoint and the supply delivers V / R") {
            CHECK(reading(circuit, "dmm", "voltage") == Approx(5.0).margin(1e-6));
            CHECK(reading(circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
        }
        WHEN("the load drops until the supply limits its current") {
            REQUIRE(circuit.set("load", "resistance", 25.0));
            THEN("the multimeter follows the voltage that falls") {
                CHECK(reading(circuit, "dmm", "voltage") == Approx(2.5).margin(1e-6));
                CHECK(reading(circuit, "psu", "mode") == 2.0);
            }
        }
    }
}

namespace {
/// Two supplies in series (psu on the ground rail, upper on top of it) feeding a load of 100 ohms to ground.
Json stacked() {
    return {
        {"formatVersion", 1},
        {"name", "Stacked supplies"},
        {"board", {{"type", "breadboard"}, {"columns", 30}}},
        {"chip", {{"mcu", "atmega328p"}, {"column", 3}}},
        {"components",
         {{{"id", "psu"}, {"type", "bench-supply"}, {"at", {{"positive", "a22"}, {"negative", "a24"}}}},
          {{"id", "upper"}, {"type", "bench-supply"}, {"at", {{"positive", "a26"}, {"negative", "a28"}}}},
          {{"id", "load"}, {"type", "variable-load"}, {"at", {{"a", "e26"}, {"b", "e29"}}}}}},
        {"wires",
         {{{"from", "b24"}, {"to", "T-24"}, {"color", "black"}},   // psu negative on ground
          {{"from", "b22"}, {"to", "b28"}, {"color", "red"}},      // psu positive on the upper negative
          {{"from", "c29"}, {"to", "B-29"}, {"color", "black"}}}}  // the load returns to ground
    };
}
}  // namespace

SCENARIO("a supply with its terminals shorted limits the current and holds no voltage", "[circuit][supply]") {
    GIVEN("a channel at 5 V and 100 mA whose positive terminal is wired to its negative one") {
        auto description = bench();
        description["wires"].push_back({{"from", "c22"}, {"to", "c24"}, {"color", "red"}});
        auto circuit = build(description);
        configure(*circuit, 5.0, 0.1, true);
        THEN("it is in current limitation at the limit, with no voltage across it, and the short is reported") {
            CHECK(reading(*circuit, "psu", "mode") == 2.0);
            CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-9));
            CHECK(reading(*circuit, "psu", "outputVoltage") == Approx(0.0).margin(1e-6));
            CHECK(!circuit->shorts().empty());
        }
    }
}

SCENARIO("a supply carries the current of the supply stacked on it", "[circuit][supply]") {
    GIVEN("two 5 V supplies in series feeding 100 ohms") {
        auto circuit = build(stacked());
        REQUIRE(circuit->set("load", "resistance", 100.0));
        REQUIRE(circuit->set("psu", "voltage", 5.0));
        REQUIRE(circuit->set("upper", "voltage", 5.0));
        REQUIRE(circuit->set("psu", "currentLimit", 1.0));
        REQUIRE(circuit->set("upper", "currentLimit", 1.0));
        REQUIRE(circuit->set("psu", "enabled", 1.0));
        REQUIRE(circuit->set("upper", "enabled", 1.0));
        THEN("both deliver 100 mA at 10 V across the load") {
            CHECK(circuit->voltageAt("a26").value() == Approx(10.0).margin(1e-6));
            CHECK(reading(*circuit, "upper", "outputCurrent") == Approx(0.1).margin(1e-6));
            CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-6));
            CHECK(reading(*circuit, "psu", "mode") == 1.0);
        }
        WHEN("the lower supply is limited to 50 mA") {
            REQUIRE(circuit->set("psu", "currentLimit", 0.05));
            THEN("it limits, and the whole stack carries 50 mA: 5 V + 5 V would drive 100 mA, 50 mA gives 5 V") {
                CHECK(reading(*circuit, "psu", "mode") == 2.0);
                CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
                CHECK(reading(*circuit, "load", "current") == Approx(0.05).margin(1e-6));
            }
        }
    }
}

SCENARIO("a supply whose output another source holds at another voltage limits its current", "[circuit][supply]") {
    GIVEN("a channel at 5 V and 100 mA whose positive terminal is wired to the ground rail") {
        auto description = bench();
        description["wires"].push_back({{"from", "c22"}, {"to", "T-22"}, {"color", "red"}});
        auto circuit = build(description);
        configure(*circuit, 5.0, 0.1, true);
        THEN("it is in current limitation at its limit instead of staying in constant voltage with no current") {
            CHECK(reading(*circuit, "psu", "mode") == 2.0);
            CHECK(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-9));
            CHECK(!circuit->shorts().empty());
        }
    }
}

SCENARIO("a variable load cannot be described with a resistance that can reach zero", "[circuit][supply][description]") {
    auto load = [](double minimum, double fallback) {
        return Json{
            {"formatVersion", 1},
            {"type", "zero-load"},
            {"name", "Zero load"},
            {"terminals", {{{"name", "a"}}, {{"name", "b"}}}},
            {"properties", Json::object()},
            {"footprint", {{"kind", "span"}, {"minimum", 2}, {"maximum", 12}}},
            {"model", {{"elements", {{{"type", "variable-resistor"}, {"between", {"a", "b"}}}}}}},
            {"state",
             {{"resistance", {{"type", "number"}, {"minimum", minimum}, {"maximum", 1000.0}, {"default", fallback}}},
              {"voltage", {{"type", "number"}, {"default", 0}}},
              {"current", {{"type", "number"}, {"default", 0}}}}}};
    };
    GIVEN("a minimum or a default of zero") {
        THEN("the description is refused") {
            CHECK(!circuit::validateComponent(load(0.0, 10.0)));
            CHECK(!circuit::validateComponent(load(1.0, 0.0)));
        }
    }
    GIVEN("a strictly positive minimum and default") {
        THEN("the description is accepted") {
            CHECK(circuit::validateComponent(load(1.0, 10.0)));
        }
    }
}
