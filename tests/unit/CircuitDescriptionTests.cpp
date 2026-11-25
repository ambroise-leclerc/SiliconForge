// Components and circuits described in JSON (ADR 0005): the committed files are valid, and every consistency rule
// reports the JSON Pointer of the value at fault.

#include <webfront/testing/Bdd.hpp>
#include <siliconforge/circuit/Description.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace webfront::bdd;

using namespace siliconforge;
using Json = nlohmann::json;

namespace {
const std::filesystem::path sourceDir{SILICONFORGE_SOURCE_DIR};

Json read(const std::filesystem::path& path) {
    std::ifstream input{path};
    require(input);
    return Json::parse(std::string{std::istreambuf_iterator<char>{input}, {}});
}

Json component(std::string_view type) {
    const auto* description = circuit::Library::builtin().find(type);
    require(description);
    return *description;
}

Json demo() {
    return circuit::builtinDescription("arduino-demo");
}

Json& part(Json& circuit, std::string_view id) {
    auto&      components = circuit.at("components");
    const auto found      = std::ranges::find_if(components, [&](const Json& candidate) {
        return candidate.at("id") == std::string(id);
    });
    require(found != components.end());
    return *found;
}

std::string partPath(const Json& circuit, std::string_view id) {
    const auto& components = circuit.at("components");
    const auto  found      = std::ranges::find_if(components, [&](const Json& candidate) {
        return candidate.at("id") == std::string(id);
    });
    require(found != components.end());
    return "/components/" + std::to_string(std::distance(components.begin(), found));
}

void expectComponentError(const Json& description, std::string_view path, std::string_view message) {
    const auto result = circuit::validateComponent(description);
    require(!result);
    check(result.error().path == path);
    check(result.error().message.find(message) != std::string::npos);
}

void expectCircuitError(const Json& description, std::string_view path, std::string_view message) {
    const auto result = circuit::validateCircuit(description);
    require(!result);
    check(result.error().path == path);
    check(result.error().message.find(message) != std::string::npos);
}
}  // namespace

const Scenario everyCommittedComponentDescriptionIsValidAnd{"Every committed component description is valid and built in", {"circuit", "schema"}, [] {
    std::vector<std::string> types;
    for (const auto& entry : std::filesystem::directory_iterator{sourceDir / "components"}) {
        const auto file = entry.path() / "component.json";
        const auto note1 = info(file.string());
        require(std::filesystem::exists(file));
        const auto description = read(file);
        const auto result      = circuit::validateComponent(description);
        if (!result)
            fail(result.error().path, ": ", result.error().message);
        check(description.at("type") == entry.path().filename().string());
        types.push_back(description.at("type"));
    }
    std::ranges::sort(types);
    check(types == circuit::Library::builtin().types());
    for (const auto* type : {"led", "resistor", "potentiometer", "push-button", "crystal", "wire", "seven-segment"})
        check(circuit::Library::builtin().find(type));
}};

const Scenario circuitsArduinoDemoJsonIsValidAnd{"circuits/arduino-demo.json is valid and is the built-in demonstration circuit", {"circuit", "schema"}, [] {
    const auto loaded = circuit::loadDescription(sourceDir / "circuits" / "arduino-demo.json");
    if (!loaded)
        fail(loaded.error().path, ": ", loaded.error().message);
    check(*loaded == demo());
    check(loaded->at("chip").at("mcu") == "atmega328p");
}};

const Scenario holesAreParsedOnTerminalStripsAnd{"Holes are parsed on terminal strips and power rails", {"circuit"}, [] {
    check(circuit::parseHole("e6") == circuit::Hole{'e', 0, 6});
    check(circuit::parseHole("j30") == circuit::Hole{'j', 0, 30});
    check(circuit::parseHole("T+12") == circuit::Hole{'T', '+', 12});
    check(circuit::parseHole("B-3") == circuit::Hole{'B', '-', 3});
    check(circuit::parseHole("T+12")->isRail());
    check(!(circuit::parseHole("e6")->isRail()));
    for (const auto* invalid : {"", "e", "k3", "e0", "e06", "T12", "B+", "e1234", "e6x", "E6"})
        check(!circuit::parseHole(invalid));
}};

const Scenario componentDescriptionsAreCheckedBeyondTheSchema{"Component descriptions are checked beyond the schema", {"circuit", "schema"}, [] {
    section("the schema locates a missing field", [&] {
        auto d = component("led");
        d.erase("footprint");
        expectComponentError(d, "/footprint", "required property");
    });
    section("terminal names are unique", [&] {
        auto d                    = component("resistor");
        d["terminals"][1]["name"] = "a";
        expectComponentError(d, "/terminals/1/name", "Duplicate terminal");
    });
    section("a default value respects its bounds", [&] {
        auto d                                   = component("resistor");
        d["properties"]["resistance"]["default"] = 0;
        expectComponentError(d, "/properties/resistance/default", "out of bounds");
    });
    section("bounds are ordered", [&] {
        auto d                                   = component("resistor");
        d["properties"]["resistance"]["minimum"] = 1e9;
        expectComponentError(d, "/properties/resistance/minimum", "above maximum");
    });
    section("a default value is one of the allowed values", [&] {
        auto d                              = component("led");
        d["properties"]["color"]["default"] = "pink";
        expectComponentError(d, "/properties/color/default", "allowed values");
    });
    section("a span footprint has two terminals", [&] {
        auto d = component("resistor");
        d["terminals"].push_back({
            {"name", "c"}
        });
        expectComponentError(d, "/footprint/kind", "two terminals");
    });
    section("a span footprint is ordered", [&] {
        auto d                    = component("resistor");
        d["footprint"]["minimum"] = 20;
        expectComponentError(d, "/footprint/minimum", "above maximum");
    });
    section("a fixed footprint places every terminal", [&] {
        auto d = component("potentiometer");
        d["footprint"]["offsets"].erase("wiper");
        expectComponentError(d, "/footprint/offsets", "Missing terminal: wiper");
    });
    section("a fixed footprint only places known terminals", [&] {
        auto d                            = component("potentiometer");
        d["footprint"]["offsets"]["gate"] = {3, 0};
        expectComponentError(d, "/footprint/offsets/gate", "Unknown terminal");
    });
    section("a fixed footprint uses one hole per terminal", [&] {
        auto d                             = component("potentiometer");
        d["footprint"]["offsets"]["wiper"] = {0, 0};
        expectComponentError(d, "/footprint/offsets/wiper", "same hole");
    });
    section("a maximum length is for two terminals", [&] {
        auto d = component("wire");
        d["terminals"].push_back({
            {"name", "tap"}
        });
        expectComponentError(d, "/footprint/maximum", "two terminals");
        d                         = component("wire");
        d["footprint"]["maximum"] = 0;
        expectComponentError(d, "/footprint", "");
    });
    section("an element connects known terminals", [&] {
        auto d                                  = component("led");
        d["model"]["elements"][0]["between"][1] = "gate";
        expectComponentError(d, "/model/elements/0/between/1", "Unknown terminal: gate");
    });
    section("an element connects distinct terminals", [&] {
        auto d                                  = component("led");
        d["model"]["elements"][0]["between"][1] = "anode";
        expectComponentError(d, "/model/elements/0/between/1", "used twice");
    });
    section("a display has the terminals its controller is wired by, and names a modelled controller", [&] {
        for (const auto* type : {"tft-kmrtm28028", "tft-mcufriend"})
            check(circuit::validateComponent(component(type)).has_value());
        auto renamed = Json::parse(component("tft-kmrtm28028").dump());
        renamed      = Json::parse([&] {
            auto text = renamed.dump();
            for (std::size_t at = 0; (at = text.find("\"led\"", at)) != std::string::npos;)
                text.replace(at, 5, "\"bl\"");
            return text;
        }());
        expectComponentError(renamed, "/model/elements/0/between", "needs the terminal 'led'");
        auto noController = component("tft-mcufriend");
        noController["properties"].erase("controller");
        expectComponentError(noController, "/model/elements/0/type", "string property 'controller'");
        auto unknown                                  = component("tft-kmrtm28028");
        unknown["properties"]["controller"]["enum"] = {"ili9341", "st7789"};
        expectComponentError(unknown, "/properties/controller/enum", "Unknown display controller: st7789");
    });
    section("a character LCD has the terminals of its header, and at most two rows of 40 columns", [&] {
        check(circuit::validateComponent(component("lcd1602")).has_value());
        auto renamed = component("lcd1602");
        renamed["model"]["elements"][0]["between"][2] = "vo";
        renamed["terminals"][2]["name"]              = "vo";
        renamed["footprint"]["offsets"]["vo"]         = renamed["footprint"]["offsets"]["v0"];
        renamed["footprint"]["offsets"].erase("v0");
        expectComponentError(renamed, "/model/elements/0/between", "needs the terminal 'v0'");
        auto noRows = component("lcd1602");
        noRows["properties"].erase("rows");
        expectComponentError(noRows, "/model/elements/0/type", "numeric property 'rows'");
        auto fourRows                              = component("lcd1602");
        fourRows["properties"]["rows"]["maximum"] = 4;  // Beyond what the property allows would be refused first
        fourRows["properties"]["rows"]["default"] = 4;
        expectComponentError(fourRows, "/properties/rows/default", "one row or two");
        auto wide                                     = component("lcd1602");
        wide["properties"]["columns"]["maximum"]     = 80;
        wide["properties"]["columns"]["default"]     = 41;
        expectComponentError(wide, "/properties/columns/default", "up to 40 columns");
    });
    section("an element has the terminal count of its type", [&] {
        auto d                               = component("potentiometer");
        d["model"]["elements"][0]["between"] = {"a", "b"};
        expectComponentError(d, "/model/elements/0/between", "connects 3 terminals");
    });
    section("an element finds its properties", [&] {
        auto d = component("led");
        d["properties"].erase("forwardVoltage");
        expectComponentError(d, "/model/elements/0/type", "numeric property 'forwardVoltage'");
    });
    section("an element finds its state", [&] {
        auto d = component("push-button");
        d["state"].erase("pressed");
        expectComponentError(d, "/model/elements/0/type", "boolean state 'pressed'");
    });
    section("the elements of a composite are named and their state is prefixed", [&] {
        auto d = component("led");
        d["terminals"].push_back({
            {"name", "second"}
        });
        d["footprint"] = {
            {   "kind",                                                                  "fixed"},
            {"offsets", {{"anode", {0, 0}}, {"cathode", {1, 0}}, {"second", {2, 0}}}}
        };
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
        check(circuit::validateComponent(d));
    });
    section("a state default matches its type and bounds", [&] {
        auto d                            = component("potentiometer");
        d["state"]["position"]["default"] = 2;
        expectComponentError(d, "/state/position/default", "out of bounds");
        d["state"]["position"]["default"] = true;
        expectComponentError(d, "/state/position/default", "does not match");
    });
    section("a large binary state is accepted, for the screens to come", [&] {
        auto d                    = component("crystal");
        d["state"]["framebuffer"] = {
            {"type", "bytes"}
        };
        d["terminals"][0]["role"] = "bus";
        d["terminals"][0]["bus"]  = "spi.sck";
        check(circuit::validateComponent(d));
    });
}};

const Scenario circuitDescriptionsAreCheckedAgainstTheComponent{"Circuit descriptions are checked against the component library", {"circuit", "schema"}, [] {
    section("the schema locates a missing field and a malformed hole", [&] {
        auto d = demo();
        d["board"].erase("columns");
        expectCircuitError(d, "/board/columns", "required property");
        d                     = demo();
        d["wires"][0]["from"] = "z9";
        const auto result     = circuit::validateCircuit(d);
        require(!result);
        check(result.error().path == "/wires/0/from");
    });
    section("the chip is on the board", [&] {
        auto d              = demo();
        d["chip"]["column"] = 31;
        expectCircuitError(d, "/chip/column", "outside the board");
    });
    section("component ids are unique", [&] {
        auto d                  = demo();
        part(d, "led-d6")["id"] = "led-d13";
        expectCircuitError(d, "/components/4/id", "Duplicate component id: led-d13");
    });
    section("the component type is known", [&] {
        auto d                  = demo();
        part(d, "xtal")["type"] = "flux-capacitor";
        expectCircuitError(d, partPath(d, "xtal") + "/type", "Unknown component type: flux-capacitor");
    });
    section("only the terminals of the type are placed", [&] {
        auto d                           = demo();
        part(d, "led-d13")["at"]["gate"] = "f24";
        expectCircuitError(d, partPath(d, "led-d13") + "/at/gate", "Unknown terminal: gate");
    });
    section("every terminal is placed", [&] {
        auto d = demo();
        part(d, "pot-a0")["at"].erase("wiper");
        expectCircuitError(d, partPath(d, "pot-a0") + "/at", "Missing terminal: wiper");
    });
    section("holes are on the board", [&] {
        auto d                      = demo();
        part(d, "r-d13")["at"]["b"] = "h31";
        expectCircuitError(d, partPath(d, "r-d13") + "/at/b", "outside the board: h31");
        d                   = demo();
        d["wires"][0]["to"] = "B+31";
        expectCircuitError(d, "/wires/0/to", "outside the board: B+31");
    });
    section("a span footprint stays in its row and within its length", [&] {
        auto d                      = demo();
        part(d, "r-d13")["at"]["b"] = "g22";
        expectCircuitError(d, partPath(d, "r-d13") + "/at", "same row");
        part(d, "r-d13")["at"]["b"] = "h16";
        expectCircuitError(d, partPath(d, "r-d13") + "/at", "1 holes apart");
    });
    section("every dipole has its own length limits", [&] {
        auto d                   = demo();
        part(d, "led-d13")["at"] = {
            {  "anode", "f23"},
            {"cathode", "f22"}
        };
        check(circuit::validateCircuit(d));
        part(d, "led-d13")["at"]["cathode"] = "f25";
        check(circuit::validateCircuit(d));
        part(d, "led-d13")["at"]["cathode"] = "f27";
        expectCircuitError(d, partPath(d, "led-d13") + "/at", "4 holes apart");
        d                            = demo();
        part(d, "xtal")["at"]["b"]   = "d17";
        expectCircuitError(d, partPath(d, "xtal") + "/at", "3 holes apart");
        d                            = demo();
        part(d, "button-d2")["at"]["b"] = "c1";
        expectCircuitError(d, partPath(d, "button-d2") + "/at", "3 holes apart");
        d                            = demo();
        part(d, "r-d13")["at"]["b"]  = "h28";
        expectCircuitError(d, partPath(d, "r-d13") + "/at", "13 holes apart");
    });
    section("a jumper wire has a maximum length, the rails and the channel counted", [&] {
        auto d                = demo();
        d["wires"][6]["from"] = "b28";  // b4 to b28: 24 holes, the longest wire
        check(circuit::validateCircuit(d));
        d["wires"][6]["from"] = "b29";
        expectCircuitError(d, "/wires/6", "25.0 holes apart, longer than the 24 holes");
        check(circuit::distance(*circuit::parseHole("T-1"), *circuit::parseHole("B-1")) == 16);
        check(circuit::distance(*circuit::parseHole("f1"), *circuit::parseHole("e1")) == 2);
        check(circuit::distance(*circuit::parseHole("a1"), *circuit::parseHole("e4")) == 5);
    });
    section("a fixed footprint is respected, in either direction", [&] {
        auto d                  = demo();
        part(d, "pot-a0")["at"] = {
            {    "a", "g4"},
            {"wiper", "g3"},
            {    "b", "g2"}
        };
        check(circuit::validateCircuit(d));
        part(d, "pot-a0")["at"] = {
            {    "a", "g2"},
            {"wiper", "g4"},
            {    "b", "g3"}
        };
        expectCircuitError(d, partPath(d, "pot-a0") + "/at", "do not match the footprint");
    });
    section("only wires reach the power rails", [&] {
        auto d                      = demo();
        part(d, "r-d13")["at"]["b"] = "T-22";
        expectCircuitError(d, partPath(d, "r-d13") + "/at/b", "power rail");
    });
    section("a hole takes one lead", [&] {
        auto d                = demo();
        d["wires"][0]["from"] = "f22";
        expectCircuitError(d, "/wires/0/from", "Hole already used: f22");
        d                   = demo();
        d["wires"][1]["to"] = "a13";
        expectCircuitError(d, "/wires/1/to", "Hole already used: a13");
    });
    section("property values are known, typed and bounded", [&] {
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
    });
}};

const Scenario aComponentMovedOnTheBoardGives{"A component moved on the board gives a validated circuit", {"circuit"}, [] {
    section("a whole component, or one of its ends", [&] {
        const auto moved = circuit::move(demo(), "r-d13", {
            {"a", "i15"},
            {"b", "i22"}
        });
        require(moved);
        auto placed = *moved;
        check(part(placed, "r-d13")["at"] == Json{{"a", "i15"}, {"b", "i22"}});
        const auto stretched = circuit::move(demo(), "r-d13", {{"b", "h20"}});
        require(stretched);
        placed = *stretched;
        check(part(placed, "r-d13")["at"] == Json{{"a", "h15"}, {"b", "h20"}});
    });
    section("a wire, numbered from 1 like in the interface", [&] {
        const auto moved = circuit::move(demo(), "wire-7", {{"from", "b10"}});
        require(moved);
        check(moved->at("wires")[6]["from"] == "b10");
        check(moved->at("wires")[6]["to"] == "b4");
    });
    section("an unknown component, an unknown terminal, a broken rule", [&] {
        auto unknown = circuit::move(demo(), "flux", {{"a", "h1"}});
        require(!unknown);
        check(unknown.error().kind == circuit::MoveError::Kind::UnknownComponent);
        unknown = circuit::move(demo(), "wire-99", {{"from", "h1"}});
        require(!unknown);
        check(unknown.error().kind == circuit::MoveError::Kind::UnknownComponent);
        const auto terminal = circuit::move(demo(), "r-d13", {{"gate", "h1"}});
        require(!terminal);
        check(terminal.error().kind == circuit::MoveError::Kind::Invalid);
        const auto tooLong = circuit::move(demo(), "led-d13", {{"cathode", "f26"}});
        require(!tooLong);
        check(tooLong.error().kind == circuit::MoveError::Kind::Invalid);
        check(tooLong.error().error.path == "/components/2/at");
        const auto twice = circuit::move(demo(), "r-d13", {{"b", "f22"}, {"a", "f15"}});
        require(!twice);
        check(twice.error().error.message.find("Hole already used: f22") != std::string::npos);
    });
}};

const Scenario circuitsAndComponentLibrariesAreLoadedFrom{"Circuits and component libraries are loaded from files and text", {"circuit"}, [] {
    section("a missing file and malformed text are reported without a path", [&] {
        const auto missing = circuit::loadDescription(sourceDir / "circuits" / "none.json");
        require(!missing);
        check(missing.error().path.empty());
        check(missing.error().message.find("none.json") != std::string::npos);
        const auto malformed = circuit::parseDescription("{");
        require(!malformed);
        check(malformed.error().path.empty());
    });
    section("text is validated like a file", [&] {
        check(circuit::parseDescription(demo().dump()));
        auto d              = demo();
        d["chip"]["column"] = 99;
        const auto result   = circuit::parseDescription(d.dump());
        require(!result);
        check(result.error().path == "/chip/column");
    });
    section("a library takes additional types, once each, and circuits may then use them", [&] {
        circuit::Library library;
        check(library.types().empty());
        require(library.addFile(sourceDir / "components" / "led" / "component.json"));
        const auto again = library.add(component("led"));
        require(!again);
        check(again.error().path == "/type");
        check(!library.addFile(sourceDir / "components" / "none" / "component.json"));
        check(library.types() == std::vector<std::string>{"led"});

        // The demonstration circuit needs more than LEDs.
        const auto result = circuit::validateCircuit(demo(), library);
        require(!result);
        check(result.error().path == "/components/0/type");
    });
    section("an unknown built-in circuit is refused", [&] {
        checkThrows<std::invalid_argument>([&] { static_cast<void>(circuit::builtinDescription("none")); });
    });
}};
