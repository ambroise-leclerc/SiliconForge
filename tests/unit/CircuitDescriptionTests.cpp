// Components and circuits described in JSON (ADR 0005): the committed files are valid, and every consistency rule
// reports the JSON Pointer of the value at fault.

#include <catch2/catch_test_macros.hpp>
#include <siliconforge/circuit/Description.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace siliconforge;
using Json = nlohmann::json;

namespace {
const std::filesystem::path sourceDir{SILICONFORGE_SOURCE_DIR};

Json read(const std::filesystem::path& path) {
    std::ifstream input{path};
    REQUIRE(input);
    return Json::parse(std::string{std::istreambuf_iterator<char>{input}, {}});
}

Json component(std::string_view type) {
    const auto* description = circuit::Library::builtin().find(type);
    REQUIRE(description);
    return *description;
}

Json demo() {
    return circuit::builtinDescription("arduino-demo");
}

Json& part(Json& circuit, std::string_view id) {
    auto&      components = circuit.at("components");
    const auto found      = std::ranges::find_if(components, [&](const Json& candidate) {
        return candidate.at("id") == id;
    });
    REQUIRE(found != components.end());
    return *found;
}

std::string partPath(const Json& circuit, std::string_view id) {
    const auto& components = circuit.at("components");
    const auto  found      = std::ranges::find_if(components, [&](const Json& candidate) {
        return candidate.at("id") == id;
    });
    REQUIRE(found != components.end());
    return "/components/" + std::to_string(std::distance(components.begin(), found));
}

void expectComponentError(const Json& description, std::string_view path, std::string_view message) {
    const auto result = circuit::validateComponent(description);
    REQUIRE_FALSE(result);
    CHECK(result.error().path == path);
    CHECK(result.error().message.find(message) != std::string::npos);
}

void expectCircuitError(const Json& description, std::string_view path, std::string_view message) {
    const auto result = circuit::validateCircuit(description);
    REQUIRE_FALSE(result);
    CHECK(result.error().path == path);
    CHECK(result.error().message.find(message) != std::string::npos);
}
}  // namespace

TEST_CASE("Every committed component description is valid and built in", "[circuit][schema]") {
    std::vector<std::string> types;
    for (const auto& entry : std::filesystem::directory_iterator{sourceDir / "components"}) {
        const auto file = entry.path() / "component.json";
        INFO(file.string());
        REQUIRE(std::filesystem::exists(file));
        const auto description = read(file);
        const auto result      = circuit::validateComponent(description);
        if (!result)
            FAIL(result.error().path << ": " << result.error().message);
        CHECK(description.at("type") == entry.path().filename().string());
        types.push_back(description.at("type"));
    }
    std::ranges::sort(types);
    CHECK(types == circuit::Library::builtin().types());
    for (const auto* type : {"led", "resistor", "potentiometer", "push-button", "crystal", "wire"})
        CHECK(circuit::Library::builtin().find(type));
}

TEST_CASE("circuits/arduino-demo.json is valid and is the built-in demonstration circuit", "[circuit][schema]") {
    const auto loaded = circuit::loadDescription(sourceDir / "circuits" / "arduino-demo.json");
    if (!loaded)
        FAIL(loaded.error().path << ": " << loaded.error().message);
    CHECK(*loaded == demo());
    CHECK(loaded->at("chip").at("mcu") == "atmega328p");
}

TEST_CASE("The demonstration circuit matches the breadboard still drawn from the MCU description", "[circuit][schema]") {
    // Until the interface renders circuits (#20), ui/breadboard.js draws the "breadboard" section of the MCU.
    const auto board   = mcu::builtinDescription("atmega328p").at("breadboard");
    const auto circuit = demo();
    CHECK(circuit.at("board").at("columns") == board.at("columns"));
    CHECK(circuit.at("chip").at("column") == board.at("chipColumn"));

    const auto placed = [&](std::string_view type, const Json& holes) {
        return std::ranges::any_of(circuit.at("components"), [&](const Json& candidate) {
            return candidate.at("type") == type && candidate.at("at") == holes;
        });
    };
    std::size_t wires = 0;
    for (const auto& drawn : board.at("components")) {
        INFO(drawn.dump());
        const std::string type = drawn.at("type");
        if (type == "wire") {
            ++wires;
            CHECK(std::ranges::any_of(circuit.at("wires"), [&](const Json& wire) {
                return wire.at("from") == drawn.at("from") && wire.at("to") == drawn.at("to") && wire.at("color") == drawn.at("color");
            }));
        } else if (type == "led") {
            const auto found = std::ranges::find_if(circuit.at("components"), [&](const Json& candidate) {
                return candidate.at("type") == "led"
                       && candidate.at("at")
                              == Json{
                                  {  "anode",   drawn.at("anode")},
                                  {"cathode", drawn.at("cathode")}
                };
            });
            REQUIRE(found != circuit.at("components").end());
            CHECK(found->at("properties").at("color") == drawn.at("color"));
            CHECK(found->at("view").at("offset") == drawn.at("offset"));
        } else if (type == "potentiometer") {
            CHECK(placed("potentiometer",
                         Json{
                             {    "a", drawn.at("legs")[0]},
                             {"wiper", drawn.at("legs")[1]},
                             {    "b", drawn.at("legs")[2]}
            }));
        } else {
            CHECK(placed(type == "button" ? "push-button" : type,
                         Json{
                             {"a", drawn.at("from")},
                             {"b",   drawn.at("to")}
            }));
        }
    }
    CHECK(circuit.at("wires").size() == wires);
    CHECK(circuit.at("components").size() == board.at("components").size() - wires);
}

TEST_CASE("Holes are parsed on terminal strips and power rails", "[circuit]") {
    CHECK(circuit::parseHole("e6") == circuit::Hole{'e', 0, 6});
    CHECK(circuit::parseHole("j30") == circuit::Hole{'j', 0, 30});
    CHECK(circuit::parseHole("T+12") == circuit::Hole{'T', '+', 12});
    CHECK(circuit::parseHole("B-3") == circuit::Hole{'B', '-', 3});
    CHECK(circuit::parseHole("T+12")->isRail());
    CHECK_FALSE(circuit::parseHole("e6")->isRail());
    for (const auto* invalid : {"", "e", "k3", "e0", "e06", "T12", "B+", "e1234", "e6x", "E6"})
        CHECK_FALSE(circuit::parseHole(invalid));
}

TEST_CASE("Component descriptions are checked beyond the schema", "[circuit][schema]") {
    SECTION("the schema locates a missing field") {
        auto d = component("led");
        d.erase("footprint");
        expectComponentError(d, "/footprint", "required property");
    }
    SECTION("terminal names are unique") {
        auto d                    = component("resistor");
        d["terminals"][1]["name"] = "a";
        expectComponentError(d, "/terminals/1/name", "Duplicate terminal");
    }
    SECTION("a default value respects its bounds") {
        auto d                                   = component("resistor");
        d["properties"]["resistance"]["default"] = 0;
        expectComponentError(d, "/properties/resistance/default", "out of bounds");
    }
    SECTION("bounds are ordered") {
        auto d                                   = component("resistor");
        d["properties"]["resistance"]["minimum"] = 1e9;
        expectComponentError(d, "/properties/resistance/minimum", "above maximum");
    }
    SECTION("a default value is one of the allowed values") {
        auto d                              = component("led");
        d["properties"]["color"]["default"] = "pink";
        expectComponentError(d, "/properties/color/default", "allowed values");
    }
    SECTION("a span footprint has two terminals") {
        auto d = component("resistor");
        d["terminals"].push_back({
            {"name", "c"}
        });
        expectComponentError(d, "/footprint/kind", "two terminals");
    }
    SECTION("a span footprint is ordered") {
        auto d                    = component("resistor");
        d["footprint"]["minimum"] = 20;
        expectComponentError(d, "/footprint/minimum", "above maximum");
    }
    SECTION("a fixed footprint places every terminal") {
        auto d = component("potentiometer");
        d["footprint"]["offsets"].erase("wiper");
        expectComponentError(d, "/footprint/offsets", "Missing terminal: wiper");
    }
    SECTION("a fixed footprint only places known terminals") {
        auto d                            = component("led");
        d["footprint"]["offsets"]["gate"] = {2, 0};
        expectComponentError(d, "/footprint/offsets/gate", "Unknown terminal");
    }
    SECTION("a fixed footprint uses one hole per terminal") {
        auto d                               = component("led");
        d["footprint"]["offsets"]["cathode"] = {0, 0};
        expectComponentError(d, "/footprint/offsets/cathode", "same hole");
    }
    SECTION("an element connects known terminals") {
        auto d                                  = component("led");
        d["model"]["elements"][0]["between"][1] = "gate";
        expectComponentError(d, "/model/elements/0/between/1", "Unknown terminal: gate");
    }
    SECTION("an element connects distinct terminals") {
        auto d                                  = component("led");
        d["model"]["elements"][0]["between"][1] = "anode";
        expectComponentError(d, "/model/elements/0/between/1", "used twice");
    }
    SECTION("an element has the terminal count of its type") {
        auto d                               = component("potentiometer");
        d["model"]["elements"][0]["between"] = {"a", "b"};
        expectComponentError(d, "/model/elements/0/between", "connects 3 terminals");
    }
    SECTION("an element finds its properties") {
        auto d = component("led");
        d["properties"].erase("forwardVoltage");
        expectComponentError(d, "/model/elements/0/type", "numeric property 'forwardVoltage'");
    }
    SECTION("an element finds its state") {
        auto d = component("push-button");
        d["state"].erase("pressed");
        expectComponentError(d, "/model/elements/0/type", "boolean state 'pressed'");
    }
    SECTION("the elements of a composite are named and their state is prefixed") {
        auto d = component("led");
        d["terminals"].push_back({
            {"name", "second"}
        });
        d["footprint"]["offsets"]["second"] = {2, 0};
        d["model"]["elements"].push_back({
            {   "type",                 "led"},
            {"between", {"second", "cathode"}}
        });
        expectComponentError(d, "/model/elements/0/name", "must be named");
        d["model"]["elements"][0]["name"] = "first";
        d["model"]["elements"][1]["name"] = "first";
        d["state"]["first.brightness"]    = {
            {"type", "number"}
        };
        expectComponentError(d, "/model/elements/1/name", "Duplicate element name");
        d["model"]["elements"][1]["name"] = "second";
        expectComponentError(d, "/model/elements/1/type", "state 'second.brightness'");
        d["state"]["second.brightness"] = {
            {"type", "number"}
        };
        CHECK(circuit::validateComponent(d));
    }
    SECTION("a state default matches its type and bounds") {
        auto d                            = component("potentiometer");
        d["state"]["position"]["default"] = 2;
        expectComponentError(d, "/state/position/default", "out of bounds");
        d["state"]["position"]["default"] = true;
        expectComponentError(d, "/state/position/default", "does not match");
    }
    SECTION("a large binary state is accepted, for the screens to come") {
        auto d                    = component("crystal");
        d["state"]["framebuffer"] = {
            {"type", "bytes"}
        };
        d["terminals"][0]["role"] = "bus";
        d["terminals"][0]["bus"]  = "spi.sck";
        CHECK(circuit::validateComponent(d));
    }
}

TEST_CASE("Circuit descriptions are checked against the component library", "[circuit][schema]") {
    SECTION("the schema locates a missing field and a malformed hole") {
        auto d = demo();
        d["board"].erase("columns");
        expectCircuitError(d, "/board/columns", "required property");
        d                     = demo();
        d["wires"][0]["from"] = "z9";
        const auto result     = circuit::validateCircuit(d);
        REQUIRE_FALSE(result);
        CHECK(result.error().path == "/wires/0/from");
    }
    SECTION("the chip is on the board") {
        auto d              = demo();
        d["chip"]["column"] = 31;
        expectCircuitError(d, "/chip/column", "outside the board");
    }
    SECTION("component ids are unique") {
        auto d                  = demo();
        part(d, "led-d6")["id"] = "led-d13";
        expectCircuitError(d, "/components/4/id", "Duplicate component id: led-d13");
    }
    SECTION("the component type is known") {
        auto d                  = demo();
        part(d, "xtal")["type"] = "flux-capacitor";
        expectCircuitError(d, partPath(d, "xtal") + "/type", "Unknown component type: flux-capacitor");
    }
    SECTION("only the terminals of the type are placed") {
        auto d                           = demo();
        part(d, "led-d13")["at"]["gate"] = "f24";
        expectCircuitError(d, partPath(d, "led-d13") + "/at/gate", "Unknown terminal: gate");
    }
    SECTION("every terminal is placed") {
        auto d = demo();
        part(d, "pot-a0")["at"].erase("wiper");
        expectCircuitError(d, partPath(d, "pot-a0") + "/at", "Missing terminal: wiper");
    }
    SECTION("holes are on the board") {
        auto d                      = demo();
        part(d, "r-d13")["at"]["b"] = "h31";
        expectCircuitError(d, partPath(d, "r-d13") + "/at/b", "outside the board: h31");
        d                   = demo();
        d["wires"][0]["to"] = "B+31";
        expectCircuitError(d, "/wires/0/to", "outside the board: B+31");
    }
    SECTION("a span footprint stays in its row and within its length") {
        auto d                      = demo();
        part(d, "r-d13")["at"]["b"] = "g22";
        expectCircuitError(d, partPath(d, "r-d13") + "/at", "same row");
        part(d, "r-d13")["at"]["b"] = "h16";
        expectCircuitError(d, partPath(d, "r-d13") + "/at", "1 holes apart");
    }
    SECTION("a fixed footprint is respected, in either direction") {
        auto d                   = demo();
        part(d, "led-d13")["at"] = {
            {  "anode", "f23"},
            {"cathode", "f22"}
        };
        CHECK(circuit::validateCircuit(d));
        part(d, "led-d13")["at"] = {
            {  "anode", "f22"},
            {"cathode", "f24"}
        };
        expectCircuitError(d, partPath(d, "led-d13") + "/at", "do not match the footprint");
        d                       = demo();
        part(d, "pot-a0")["at"] = {
            {    "a", "g2"},
            {"wiper", "g4"},
            {    "b", "g3"}
        };
        expectCircuitError(d, partPath(d, "pot-a0") + "/at", "do not match the footprint");
    }
    SECTION("only wires reach the power rails") {
        auto d                      = demo();
        part(d, "r-d13")["at"]["b"] = "T-22";
        expectCircuitError(d, partPath(d, "r-d13") + "/at/b", "power rail");
    }
    SECTION("a hole takes one lead") {
        auto d                = demo();
        d["wires"][0]["from"] = "f22";
        expectCircuitError(d, "/wires/0/from", "Hole already used: f22");
        d                   = demo();
        d["wires"][1]["to"] = "a13";
        expectCircuitError(d, "/wires/1/to", "Hole already used: a13");
    }
    SECTION("property values are known, typed and bounded") {
        auto d                                      = demo();
        part(d, "r-d13")["properties"]["tolerance"] = 5;
        expectCircuitError(d, partPath(d, "r-d13") + "/properties/tolerance", "Unknown property: tolerance");
        d                                            = demo();
        part(d, "r-d13")["properties"]["resistance"] = "220";
        expectCircuitError(d, partPath(d, "r-d13") + "/properties/resistance", "is a number");
        part(d, "r-d13")["properties"]["resistance"] = 0;
        expectCircuitError(d, partPath(d, "r-d13") + "/properties/resistance", "out of bounds");
        d                                         = demo();
        part(d, "led-d13")["properties"]["color"] = "pink";
        expectCircuitError(d, partPath(d, "led-d13") + "/properties/color", "allowed values");
    }
}

TEST_CASE("Circuits and component libraries are loaded from files and text", "[circuit]") {
    SECTION("a missing file and malformed text are reported without a path") {
        const auto missing = circuit::loadDescription(sourceDir / "circuits" / "none.json");
        REQUIRE_FALSE(missing);
        CHECK(missing.error().path.empty());
        CHECK(missing.error().message.find("none.json") != std::string::npos);
        const auto malformed = circuit::parseDescription("{");
        REQUIRE_FALSE(malformed);
        CHECK(malformed.error().path.empty());
    }
    SECTION("text is validated like a file") {
        CHECK(circuit::parseDescription(demo().dump()));
        auto d              = demo();
        d["chip"]["column"] = 99;
        const auto result   = circuit::parseDescription(d.dump());
        REQUIRE_FALSE(result);
        CHECK(result.error().path == "/chip/column");
    }
    SECTION("a library takes additional types, once each, and circuits may then use them") {
        circuit::Library library;
        CHECK(library.types().empty());
        REQUIRE(library.addFile(sourceDir / "components" / "led" / "component.json"));
        const auto again = library.add(component("led"));
        REQUIRE_FALSE(again);
        CHECK(again.error().path == "/type");
        CHECK_FALSE(library.addFile(sourceDir / "components" / "none" / "component.json"));
        CHECK(library.types() == std::vector<std::string>{"led"});

        // The demonstration circuit needs more than LEDs.
        const auto result = circuit::validateCircuit(demo(), library);
        REQUIRE_FALSE(result);
        CHECK(result.error().path == "/components/0/type");
    }
    SECTION("an unknown built-in circuit is refused") {
        CHECK_THROWS_AS(circuit::builtinDescription("none"), std::invalid_argument);
    }
}
