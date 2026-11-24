// Programmable supply and variable resistive load (#62, ADR 0010): the voltage regulation to current limitation
// transition is qualified on reference values that follow from Ohm's law, not from the model's own output.

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/circuit/Description.hpp>

#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"

#include <format>
#include <memory>

using namespace webfront::bdd;

using namespace siliconforge;
using siliconforge::test::Approx;
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
        fail(created.error().path, ": ", created.error().message);
    return std::move(*created);
}

double reading(circuit::Circuit& circuit, std::string_view id, std::string_view name) {
    return circuit.state(id, name).value();
}

void configure(circuit::Circuit& circuit, double volts, double limit, bool enabled) {
    require(circuit.set("psu", "voltage", volts));
    require(circuit.set("psu", "currentLimit", limit));
    require(circuit.set("psu", "enabled", enabled ? 1.0 : 0.0));
}
}  // namespace

const Scenario aSupplySetTo5VAnd{"a supply set to 5 V and 100 mA feeds a 100 ohm load", {"circuit", "supply"}, [] {
    given("a channel that is set but not enabled", [&] {
        auto circuit = build(bench());
        require(circuit->set("load", "resistance", 100.0));
        require(circuit->set("psu", "voltage", 5.0));
        require(circuit->set("psu", "currentLimit", 0.1));

        then("its output is open: nothing at the node, nothing delivered", [&] {
            check(circuit->voltageAt("a22").value() == Approx(0.0).margin(1e-6));
            check(reading(*circuit, "psu", "outputCurrent") == 0.0);
            check(reading(*circuit, "psu", "mode") == 0.0);
        });
        when("the channel is enabled", [&] {
            require(circuit->set("psu", "enabled", 1.0));
            then("the node holds the setpoint and the load draws V / R, in constant voltage", [&] {
                check(circuit->voltageAt("a22").value() == Approx(5.0).margin(1e-6));
                check(reading(*circuit, "psu", "outputVoltage") == Approx(5.0).margin(1e-6));
                check(reading(*circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
                check(reading(*circuit, "psu", "mode") == 1.0);
                check(reading(*circuit, "load", "voltage") == Approx(5.0).margin(1e-6));
                check(reading(*circuit, "load", "current") == Approx(0.05).margin(1e-6));
            });
        });
    });
}};

const Scenario aChangeOfLoadMovesTheSupply{"a change of load moves the supply between voltage regulation and current limitation", {"circuit", "supply"}, [] {
    given("a channel at 5 V and 100 mA, enabled, on 100 ohms", [&] {
        auto circuit = build(bench());
        require(circuit->set("load", "resistance", 100.0));
        configure(*circuit, 5.0, 0.1, true);
        require(reading(*circuit, "psu", "mode") == 1.0);

        when("the load falls to 25 ohms, which would draw 200 mA", [&] {
            require(circuit->set("load", "resistance", 25.0));
            then("the current is limited to 100 mA and the voltage falls to 100 mA x 25 ohms", [&] {
                check(reading(*circuit, "psu", "mode") == 2.0);
                check(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-6));
                check(circuit->voltageAt("a22").value() == Approx(2.5).margin(1e-6));
                check(reading(*circuit, "load", "current") == Approx(0.1).margin(1e-6));
            });
            andWhen("the load rises again to 100 ohms", [&] {
                require(circuit->set("load", "resistance", 100.0));
                then("the supply regulates the voltage again", [&] {
                    check(reading(*circuit, "psu", "mode") == 1.0);
                    check(circuit->voltageAt("a22").value() == Approx(5.0).margin(1e-6));
                    check(reading(*circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
                });
            });
        });
        when("the load is 50 ohms, exactly the critical resistance V / limit", [&] {
            require(circuit->set("load", "resistance", 50.0));
            then("the supply is still in constant voltage and delivers the limit", [&] {
                check(reading(*circuit, "psu", "mode") == 1.0);
                check(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-6));
                check(circuit->voltageAt("a22").value() == Approx(5.0).margin(1e-6));
            });
        });
        when("the load is just below the critical resistance, 49 ohms", [&] {
            require(circuit->set("load", "resistance", 49.0));
            then("the supply is limiting", [&] {
                check(reading(*circuit, "psu", "mode") == 2.0);
                check(circuit->voltageAt("a22").value() == Approx(4.9).margin(1e-6));
            });
        });
        when("the limit is lowered to 20 mA", [&] {
            require(circuit->set("psu", "currentLimit", 0.02));
            then("the 100 ohm load now limits it: 20 mA, 2 V", [&] {
                check(reading(*circuit, "psu", "mode") == 2.0);
                check(circuit->voltageAt("a22").value() == Approx(2.0).margin(1e-6));
                check(reading(*circuit, "psu", "outputCurrent") == Approx(0.02).margin(1e-6));
            });
        });
        when("the channel is disabled again", [&] {
            require(circuit->set("psu", "enabled", 0.0));
            then("the output reads zero", [&] {
                check(circuit->voltageAt("a22").value() == Approx(0.0).margin(1e-6));
                check(reading(*circuit, "psu", "outputCurrent") == 0.0);
                check(reading(*circuit, "psu", "mode") == 0.0);
            });
        });
    });
}};

const Scenario aSupplyWithNoReferenceDeliversNothing{"a supply with no reference delivers nothing", {"circuit", "supply"}, [] {
    given("a channel whose negative terminal is not tied to any source", [&] {
        auto circuit = build(bench(false));
        configure(*circuit, 5.0, 0.1, true);
        then("the mode stays off and no current flows", [&] {
            check(reading(*circuit, "psu", "mode") == 0.0);
            check(reading(*circuit, "psu", "outputCurrent") == 0.0);
        });
    });
}};

const Scenario theSettingsOfASupplyStayWithin{"the settings of a supply stay within what a bench channel can do", {"circuit", "supply"}, [] {
    given("a channel", [&] {
        auto circuit = build(bench());
        then("a voltage or a limit beyond its range is refused", [&] {
            check(circuit->set("psu", "voltage", 31.0).error() == circuit::SetError::OutOfRange);
            check(circuit->set("psu", "currentLimit", 0.0).error() == circuit::SetError::OutOfRange);
            check(circuit->set("psu", "outputCurrent", 0.5).error() == circuit::SetError::NotInteractive);
            check(circuit->set("load", "resistance", 0.0).error() == circuit::SetError::OutOfRange);
        });
    });
}};

const Scenario aMultimeterReadsTheVoltageAcrossThe{"a multimeter reads the voltage across the load without disturbing it",
                                                   {"circuit", "supply", "multimeter"}, [] {
    given("the bench demonstration circuit, with its supply at 5 V and 100 mA", [&] {
        auto created = circuit::Circuit::create(circuit::builtinDescription("bench-demo"));
        require(created);
        auto& circuit = **created;
        require(circuit.set("load", "resistance", 100.0));
        configure(circuit, 5.0, 0.1, true);
        then("the multimeter reads the setpoint and the supply delivers V / R", [&] {
            check(reading(circuit, "dmm", "voltage") == Approx(5.0).margin(1e-6));
            check(reading(circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
        });
        when("the load drops until the supply limits its current", [&] {
            require(circuit.set("load", "resistance", 25.0));
            then("the multimeter follows the voltage that falls", [&] {
                check(reading(circuit, "dmm", "voltage") == Approx(2.5).margin(1e-6));
                check(reading(circuit, "psu", "mode") == 2.0);
            });
        });
    });
}};

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

const Scenario aSupplyWithItsTerminalsShortedLimits{"a supply with its terminals shorted limits the current and holds no voltage", {"circuit", "supply"}, [] {
    given("a channel at 5 V and 100 mA whose positive terminal is wired to its negative one", [&] {
        auto description = bench();
        description["wires"].push_back({{"from", "c22"}, {"to", "c24"}, {"color", "red"}});
        auto circuit = build(description);
        configure(*circuit, 5.0, 0.1, true);
        then("it is in current limitation at the limit, with no voltage across it, and the short is reported", [&] {
            check(reading(*circuit, "psu", "mode") == 2.0);
            check(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-9));
            check(reading(*circuit, "psu", "outputVoltage") == Approx(0.0).margin(1e-6));
            check(!circuit->shorts().empty());
        });
    });
}};

const Scenario aSupplyCarriesTheCurrentOfThe{"a supply carries the current of the supply stacked on it", {"circuit", "supply"}, [] {
    given("two 5 V supplies in series feeding 100 ohms", [&] {
        auto circuit = build(stacked());
        require(circuit->set("load", "resistance", 100.0));
        require(circuit->set("psu", "voltage", 5.0));
        require(circuit->set("upper", "voltage", 5.0));
        require(circuit->set("psu", "currentLimit", 1.0));
        require(circuit->set("upper", "currentLimit", 1.0));
        require(circuit->set("psu", "enabled", 1.0));
        require(circuit->set("upper", "enabled", 1.0));
        then("both deliver 100 mA at 10 V across the load", [&] {
            check(circuit->voltageAt("a26").value() == Approx(10.0).margin(1e-6));
            check(reading(*circuit, "upper", "outputCurrent") == Approx(0.1).margin(1e-6));
            check(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-6));
            check(reading(*circuit, "psu", "mode") == 1.0);
        });
        when("the lower supply is limited to 50 mA", [&] {
            require(circuit->set("psu", "currentLimit", 0.05));
            then("it limits, and the whole stack carries 50 mA: 5 V + 5 V would drive 100 mA, 50 mA gives 5 V", [&] {
                check(reading(*circuit, "psu", "mode") == 2.0);
                check(reading(*circuit, "psu", "outputCurrent") == Approx(0.05).margin(1e-6));
                check(reading(*circuit, "load", "current") == Approx(0.05).margin(1e-6));
            });
        });
    });
}};

const Scenario aSupplyWhoseOutputAnotherSourceHolds{"a supply whose output another source holds at another voltage limits its current",
                                                    {"circuit", "supply"}, [] {
    given("a channel at 5 V and 100 mA whose positive terminal is wired to the ground rail", [&] {
        auto description = bench();
        description["wires"].push_back({{"from", "c22"}, {"to", "T-22"}, {"color", "red"}});
        auto circuit = build(description);
        configure(*circuit, 5.0, 0.1, true);
        then("it is in current limitation at its limit instead of staying in constant voltage with no current", [&] {
            check(reading(*circuit, "psu", "mode") == 2.0);
            check(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-9));
        });
        then("the conflict is still reported once the supply limits its current", [&] {
            const auto shorts = circuit->shorts();
            require(!shorts.empty());
            check(shorts.front().find("supply psu") != std::string::npos);
        });
    });
    given("a channel at 5 V wired to a 10 V channel", [&] {
        auto description = bench();
        description["components"].insert(
            description["components"].begin(),
            Json{{"id", "upper"}, {"type", "bench-supply"}, {"at", {{"positive", "a27"}, {"negative", "a29"}}}});
        description["wires"].push_back({{"from", "d22"}, {"to", "d27"}, {"color", "red"}});
        description["wires"].push_back({{"from", "b29"}, {"to", "T-29"}, {"color", "black"}});
        auto circuit = build(description);
        require(circuit->set("upper", "voltage", 10.0));
        require(circuit->set("upper", "currentLimit", 1.0));
        require(circuit->set("upper", "enabled", 1.0));
        configure(*circuit, 5.0, 0.1, true);
        then("the lower one is driven above its setpoint: it stays in constant voltage and pushes no current into the other", [&] {
            check(reading(*circuit, "psu", "mode") == 1.0);
            check(reading(*circuit, "psu", "outputCurrent") == Approx(0.0).margin(1e-9));
            check(circuit->shorts().empty());
        });
    });
}};

const Scenario aVariableLoadCannotBeDescribedWith{"a variable load cannot be described with a resistance that can reach zero",
                                                  {"circuit", "supply", "description"}, [] {
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
    given("a minimum or a default of zero", [&] {
        then("the description is refused", [&] {
            check(!circuit::validateComponent(load(0.0, 10.0)));
            check(!circuit::validateComponent(load(1.0, 0.0)));
        });
        then("so is one with no default, which would start at zero", [&] {
            auto description = load(1.0, 10.0);
            description["state"]["resistance"].erase("default");
            check(!circuit::validateComponent(description));
        });
    });
    given("a strictly positive minimum and default", [&] {
        then("the description is accepted", [&] {
            check(circuit::validateComponent(load(1.0, 10.0)));
        });
    });
}};

namespace {
/// A 5 V and a 10 V channel in parallel on the same load, declared in the order given.
Json parallel(bool tenFirst) {
    auto description = bench();
    const Json upper = {{"id", "upper"}, {"type", "bench-supply"}, {"at", {{"positive", "a27"}, {"negative", "a29"}}}};
    if (tenFirst)
        description["components"].insert(description["components"].begin(), upper);
    else
        description["components"].push_back(upper);
    description["wires"].push_back({{"from", "d22"}, {"to", "d27"}, {"color", "red"}});
    description["wires"].push_back({{"from", "b29"}, {"to", "T-29"}, {"color", "black"}});
    return description;
}

void tenAndFive(circuit::Circuit& circuit) {
    require(circuit.set("upper", "voltage", 10.0));
    require(circuit.set("upper", "currentLimit", 1.0));
    require(circuit.set("upper", "enabled", 1.0));
    require(circuit.set("psu", "voltage", 5.0));
    require(circuit.set("psu", "currentLimit", 0.1));
    require(circuit.set("psu", "enabled", 1.0));
}
}  // namespace

const Scenario twoSuppliesInParallelGiveTheSame{"two supplies in parallel give the same result in whatever order they are declared", {"circuit", "supply"}, [] {
    given("a 5 V and a 10 V channel on one 100 ohm load", [&] {
        auto first  = build(parallel(true));
        auto second = build(parallel(false));
        tenAndFive(*first);
        tenAndFive(*second);
        then("the 10 V one holds the node and feeds the load, the 5 V one delivers nothing, in both orders", [&] {
            for (auto* circuit : {first.get(), second.get()}) {
                check(reading(*circuit, "upper", "mode") == 1.0);
                check(reading(*circuit, "upper", "outputCurrent") == Approx(0.1).margin(1e-9));
                check(reading(*circuit, "psu", "mode") == 1.0);
                check(reading(*circuit, "psu", "outputCurrent") == Approx(0.0).margin(1e-9));
                check(circuit->voltageAt("a22").value() == Approx(10.0).margin(1e-6));
                check(circuit->shorts().empty());
            }
        });
    });
}};

const Scenario aSupplyHeldAFewMillivoltsBelow{"a supply held a few millivolts below its setpoint still limits its current", {"circuit", "supply"}, [] {
    given("a channel at 40 mV and 100 mA whose positive terminal is wired to the ground rail", [&] {
        auto description = bench();
        description["wires"].push_back({{"from", "c22"}, {"to", "T-22"}, {"color", "red"}});
        auto circuit = build(description);
        configure(*circuit, 0.04, 0.1, true);
        then("it is in current limitation at its limit", [&] {
            check(reading(*circuit, "psu", "mode") == 2.0);
            check(reading(*circuit, "psu", "outputCurrent") == Approx(0.1).margin(1e-9));
        });
    });
}};

const Scenario aDisabledSupplyStillReportsTheVoltage{"a disabled supply still reports the voltage the circuit applies across its terminals",
                                                     {"circuit", "supply"}, [] {
    given("a disabled channel in parallel with a 10 V channel", [&] {
        auto circuit = build(parallel(true));
        require(circuit->set("upper", "voltage", 10.0));
        require(circuit->set("upper", "currentLimit", 1.0));
        require(circuit->set("upper", "enabled", 1.0));
        require(circuit->set("psu", "voltage", 5.0));
        then("its output is open, with no current and no mode, but it measures the 10 V", [&] {
            check(reading(*circuit, "psu", "mode") == 0.0);
            check(reading(*circuit, "psu", "outputCurrent") == 0.0);
            check(reading(*circuit, "psu", "outputVoltage") == Approx(10.0).margin(1e-6));
        });
    });
}};

const Scenario aSupplyInConstantVoltageNeverAbsorbs{"a supply in constant voltage never absorbs current: its output opens instead", {"circuit", "supply"}, [] {
    given("a channel at 2 V whose output is pulled up to the 5 V rail through 100 ohms", [&] {
        auto description = bench();
        description["components"].push_back({{"id", "pull"}, {"type", "resistor"}, {"at", {{"a", "e22"}, {"b", "e20"}}}, {"properties", {{"resistance", 100}}}});
        description["wires"].push_back({{"from", "d20"}, {"to", "T+20"}, {"color", "red"}});
        auto circuit = build(description);
        require(circuit->set("load", "resistance", 1e6));
        configure(*circuit, 2.0, 0.1, true);
        then("it delivers nothing, reports no mode, and the circuit sets the voltage at its output", [&] {
            check(reading(*circuit, "psu", "mode") == 0.0);
            check(reading(*circuit, "psu", "outputCurrent") == 0.0);
            check(reading(*circuit, "psu", "outputVoltage") > 4.9);
        });
    });
}};

const Scenario severalSuppliesSharingOneLoadSettleWith{"several supplies sharing one load settle with every limit respected", {"circuit", "supply"}, [] {
    given("six channels of 5 V and 10 mA in parallel on a 50 ohm load, which asks for 100 mA", [&] {
        Json description = {
            {"formatVersion", 1},
            {"name", "Six supplies"},
            {"board", {{"type", "breadboard"}, {"columns", 30}}},
            {"chip", {{"mcu", "atmega328p"}, {"column", 3}}},
            {"components", Json::array()},
            {"wires",
             {{{"from", "d22"}, {"to", "d26"}, {"color", "red"}},
              {{"from", "d24"}, {"to", "d28"}, {"color", "black"}},
              {{"from", "e24"}, {"to", "T-24"}, {"color", "black"}}}}};
        for (const char* row : {"a", "b", "c"})
            for (const int column : {22, 26}) {
                const auto id = std::format("s{}{}", row, column);
                description["components"].push_back({{"id", id},
                                                     {"type", "bench-supply"},
                                                     {"at", {{"positive", std::format("{}{}", row, column)}, {"negative", std::format("{}{}", row, column + 2)}}}});
            }
        description["components"].push_back({{"id", "load"}, {"type", "variable-load"}, {"at", {{"a", "e22"}, {"b", "e28"}}}});
        auto circuit = build(description);
        require(circuit->set("load", "resistance", 50.0));
        for (const char* row : {"a", "b", "c"})
            for (const int column : {22, 26}) {
                const auto id = std::format("s{}{}", row, column);
                require(circuit->set(id, "voltage", 5.0));
                require(circuit->set(id, "currentLimit", 0.01));
                require(circuit->set(id, "enabled", 1.0));
            }
        then("no channel in constant voltage delivers more than its limit", [&] {
            for (const char* row : {"a", "b", "c"})
                for (const int column : {22, 26}) {
                    const auto id = std::format("s{}{}", row, column);
                    if (reading(*circuit, id, "mode") == 1.0)
                        check(reading(*circuit, id, "outputCurrent") <= 0.01 * (1 + 1e-6));
                    check(reading(*circuit, id, "outputCurrent") <= 0.01 * (1 + 1e-6));
                }
        });
    });
}};

const Scenario twoSuppliesInSeriesBeatALower{"two supplies in series beat a lower channel in parallel with them", {"circuit", "supply"}, [] {
    given("two 3 V channels in series and a 5 V channel, all on a 100 ohm load", [&] {
        auto description = stacked();
        description["components"][1]["at"] = {{"positive", "a26"}, {"negative", "a28"}};
        description["components"].push_back({{"id", "low"}, {"type", "bench-supply"}, {"at", {{"positive", "f26"}, {"negative", "f28"}}}});
        description["wires"].push_back({{"from", "g26"}, {"to", "d26"}, {"color", "red"}});
        description["wires"].push_back({{"from", "g28"}, {"to", "T-28"}, {"color", "black"}});
        auto circuit = build(description);
        require(circuit->set("load", "resistance", 100.0));
        for (const char* id : {"psu", "upper", "low"}) {
            require(circuit->set(id, "voltage", std::string_view{id} == "low" ? 5.0 : 3.0));
            require(circuit->set(id, "currentLimit", 1.0));
            require(circuit->set(id, "enabled", 1.0));
        }
        then("the stack holds the node at 6 V, the 5 V channel opens, and nothing is reported as a short", [&] {
            check(circuit->voltageAt("e26").value() == Approx(6.0).margin(1e-6));
            check(reading(*circuit, "load", "current") == Approx(0.06).margin(1e-9));
            check(reading(*circuit, "upper", "mode") == 1.0);
            check(reading(*circuit, "low", "mode") == 0.0);
            check(circuit->shorts().empty());
        });
    });
}};

const Scenario aChannelThatOpenedTakesTheRegulation{"a channel that opened takes the regulation back when the voltage falls below its setpoint",
                                                    {"circuit", "supply"}, [] {
    given("two 1 V channels in series limited to 30 mA and 50 mA, and a third 1 V channel limited to 30 mA, on 25 ohms", [&] {
        auto description = stacked();
        description["components"].push_back({{"id", "low"}, {"type", "bench-supply"}, {"at", {{"positive", "f26"}, {"negative", "f28"}}}});
        description["wires"].push_back({{"from", "g26"}, {"to", "d26"}, {"color", "red"}});
        description["wires"].push_back({{"from", "g28"}, {"to", "T-28"}, {"color", "black"}});
        auto circuit = build(description);
        require(circuit->set("load", "resistance", 25.0));
        const auto channel = [&](const char* id, double limit) {
            require(circuit->set(id, "voltage", 1.0));
            require(circuit->set(id, "currentLimit", limit));
            require(circuit->set(id, "enabled", 1.0));
        };
        channel("psu", 0.03);
        channel("upper", 0.05);
        channel("low", 0.03);
        then("the load gets 1 V and 40 mA: 30 mA from the stack and 10 mA from the third channel", [&] {
            check(circuit->voltageAt("e26").value() == Approx(1.0).margin(1e-6));
            check(reading(*circuit, "load", "current") == Approx(0.04).margin(1e-6));
            check(reading(*circuit, "low", "outputCurrent") == Approx(0.01).margin(1e-6));
            check(reading(*circuit, "low", "mode") == 1.0);
        });
    });
}};

const Scenario aSupplySetTo0VWith{"a supply set to 0 V with its terminals together delivers nothing", {"circuit", "supply"}, [] {
    given("a channel at 0 V and 100 mA whose positive terminal is wired to its negative one", [&] {
        auto description = bench();
        description["wires"].push_back({{"from", "c22"}, {"to", "c24"}, {"color", "red"}});
        auto circuit = build(description);
        configure(*circuit, 0.0, 0.1, true);
        then("it stays in constant voltage with no current, and nothing is reported as a short", [&] {
            check(reading(*circuit, "psu", "mode") == 1.0);
            check(reading(*circuit, "psu", "outputCurrent") == Approx(0.0).margin(1e-9));
            check(reading(*circuit, "psu", "outputVoltage") == Approx(0.0).margin(1e-9));
            check(circuit->shorts().empty());
        });
    });
}};
