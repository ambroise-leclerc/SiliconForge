#include "common/JsonErrors.hpp"
#include "EmbeddedCircuits.hpp"
#include <siliconforge/circuit/Description.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace siliconforge::circuit {
namespace {
using detail::child;
using detail::fail;
using detail::Json;

std::string at(const std::string& path, std::size_t index) {
    return path + "/" + std::to_string(index);
}

std::expected<void, LoadError> checkSchema(const nlohmann::json_schema::json_validator& validator, const Json& document) {
    detail::SchemaErrors errors;
    validator.validate(document, errors);
    if (errors.first)
        return std::unexpected(*errors.first);
    return {};
}

std::set<std::string> terminalNames(const Json& component) {
    std::set<std::string> names;
    for (const auto& terminal : component.at("terminals"))
        names.insert(terminal.at("name").get<std::string>());
    return names;
}

/// What an electrical element needs from its component: terminal count, numeric properties and one state entry.
struct ElementRule {
    std::size_t                   terminals;
    std::vector<std::string_view> properties;
    std::string_view              state;
    std::string_view              stateType;
};

ElementRule ruleFor(std::string_view type) {
    if (type == "resistor")
        return {2, {"resistance"}, "", ""};
    if (type == "led")
        return {
            2,
            {"forwardVoltage", "nominalCurrent"},
            "brightness",
            "number"
        };
    if (type == "switch")
        return {2, {}, "pressed", "boolean"};
    if (type == "potentiometer")
        return {3, {"resistance"}, "position", "number"};
    return {2, {}, "", ""};  // wire
}

void checkProperties(const Json& component) {
    for (const auto& [name, property] : component.at("properties").items()) {
        const auto path = child("/properties", name);
        if (property.at("type") == "number") {
            const auto value = property.at("default").get<double>();
            if (property.contains("minimum") && property.contains("maximum") && property.at("minimum").get<double>() > property.at("maximum").get<double>())
                fail(path + "/minimum", "Minimum is above maximum");
            if ((property.contains("minimum") && value < property.at("minimum").get<double>())
                || (property.contains("maximum") && value > property.at("maximum").get<double>()))
                fail(path + "/default", "Default value is out of bounds");
        } else if (property.at("type") == "string" && property.contains("enum")) {
            const auto& values = property.at("enum");
            if (std::ranges::find(values, property.at("default")) == values.end())
                fail(path + "/default", "Default value is not one of the allowed values");
        }
    }
}

void checkFootprint(const Json& component, const std::set<std::string>& terminals) {
    const auto&       footprint = component.at("footprint");
    const std::string kind      = footprint.at("kind");
    if (kind == "free" && footprint.contains("maximum") && terminals.size() != 2)
        fail("/footprint/maximum", "A maximum length needs exactly two terminals");
    if (kind == "span") {
        if (terminals.size() != 2)
            fail("/footprint/kind", "A span footprint needs exactly two terminals");
        if (footprint.at("minimum").get<unsigned>() > footprint.at("maximum").get<unsigned>())
            fail("/footprint/minimum", "Minimum is above maximum");
    } else if (kind == "fixed") {
        const auto&                   offsets = footprint.at("offsets");
        std::set<std::pair<int, int>> used;
        for (const auto& [name, offset] : offsets.items()) {
            if (!terminals.contains(name))
                fail(child("/footprint/offsets", name), "Unknown terminal: " + name);
            if (!used.emplace(offset[0].get<int>(), offset[1].get<int>()).second)
                fail(child("/footprint/offsets", name), "Two terminals share the same hole");
        }
        for (const auto& name : terminals)
            if (!offsets.contains(name))
                fail("/footprint/offsets", "Missing terminal: " + name);
    }
}

void checkModel(const Json& component, const std::set<std::string>& terminals) {
    const auto&           elements   = component.at("model").at("elements");
    const auto&           properties = component.at("properties");
    const auto&           state      = component.at("state");
    std::set<std::string> names;
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto&       element = elements[i];
        const auto        path    = at("/model/elements", i);
        const std::string type    = element.at("type");
        const auto        rule    = ruleFor(type);
        const auto&       between = element.at("between");
        if (between.size() != rule.terminals)
            fail(path + "/between", "A " + type + " connects " + std::to_string(rule.terminals) + " terminals");
        std::set<std::string> connected;
        for (std::size_t k = 0; k < between.size(); ++k) {
            const std::string terminal = between[k];
            if (!terminals.contains(terminal))
                fail(at(path + "/between", k), "Unknown terminal: " + terminal);
            if (!connected.insert(terminal).second)
                fail(at(path + "/between", k), "Terminal used twice by the same element");
        }
        const std::string name = element.value("name", "");
        if (name.empty() && elements.size() > 1)
            fail(path + "/name", "Elements of a multi-element model must be named");
        if (!names.insert(name).second)
            fail(path + "/name", "Duplicate element name");
        for (const auto property : rule.properties)
            if (!properties.contains(property) || properties.at(std::string(property)).at("type") != "number")
                fail(path + "/type", "A " + type + " needs the numeric property '" + std::string(property) + "'");
        if (element.contains("swapWhen")) {
            const std::string property = element.at("swapWhen");
            if (type != "led")
                fail(path + "/swapWhen", "Only a led can be reversed");
            if (!properties.contains(property) || properties.at(property).at("type") != "boolean")
                fail(path + "/swapWhen", "A reversed led needs the boolean property '" + property + "'");
        }
        if (!rule.state.empty()) {
            const auto key = name.empty() ? std::string(rule.state) : name + "." + std::string(rule.state);
            if (!state.contains(key) || state.at(key).at("type") != std::string(rule.stateType))
                fail(path + "/type", "A " + type + " needs the " + std::string(rule.stateType) + " state '" + key + "'");
        }
    }
}

void checkState(const Json& component) {
    for (const auto& [name, entry] : component.at("state").items()) {
        const auto        path = child("/state", name);
        const std::string type = entry.at("type");
        if (entry.contains("minimum") && entry.contains("maximum") && entry.at("minimum").get<double>() > entry.at("maximum").get<double>())
            fail(path + "/minimum", "Minimum is above maximum");
        if (!entry.contains("default"))
            continue;
        const auto& value = entry.at("default");
        if ((type == "number" && !value.is_number()) || (type == "boolean" && !value.is_boolean()) || type == "bytes")
            fail(path + "/default", "Default value does not match the state type");
        if (type == "number"
            && ((entry.contains("minimum") && value.get<double>() < entry.at("minimum").get<double>())
                || (entry.contains("maximum") && value.get<double>() > entry.at("maximum").get<double>())))
            fail(path + "/default", "Default value is out of bounds");
    }
}

/// Row index from the top of the board, so that row offsets of a fixed footprint can be compared.
int rowIndex(char row) {
    return 'j' - row;
}

/// The hole of a circuit, checked against the board. The schema already checked the syntax.
Hole holeAt(const Json& value, const std::string& path, unsigned columns) {
    const auto hole = parseHole(value.get<std::string>());
    if (!hole)
        fail(path, "Invalid hole: " + value.get<std::string>());
    if (hole->column > columns)
        fail(path, "Hole is outside the board: " + value.get<std::string>());
    return *hole;
}

/// Two ends further apart than the maximum length of a free footprint (a jumper wire) are refused.
void checkLength(const Json& footprint, const Hole& first, const Hole& second, const std::string& path) {
    if (!footprint.contains("maximum"))
        return;
    const auto length  = distance(first, second);
    const auto maximum = footprint.at("maximum").get<double>();
    if (length > maximum + 1e-9)
        fail(path, std::format("The ends are {:.1f} holes apart, longer than the {:g} holes of this component", length, maximum));
}

void checkPlacement(const Json& description, const std::map<std::string, Hole>& holes, const std::string& path) {
    const auto&       footprint = description.at("footprint");
    const std::string kind      = footprint.at("kind");
    if (kind == "free") {
        if (holes.size() == 2)
            checkLength(footprint, holes.begin()->second, std::next(holes.begin())->second, path);
        return;
    }
    for (const auto& [terminal, hole] : holes)
        if (hole.isRail())
            fail(child(path, terminal), "This component cannot be plugged in a power rail");
    if (kind == "span") {
        const auto& first  = holes.begin()->second;
        const auto& second = std::next(holes.begin())->second;
        const auto  span   = static_cast<unsigned>(std::abs(static_cast<int>(first.column) - static_cast<int>(second.column)));
        if (first.row != second.row)
            fail(path, "The two terminals must be in the same row");
        if (span < footprint.at("minimum").get<unsigned>() || span > footprint.at("maximum").get<unsigned>())
            fail(path, "The terminals are " + std::to_string(span) + " holes apart, outside the footprint of this component");
        return;
    }
    // Fixed: every terminal sits at its offset from the first one, as described or mirrored along the row.
    const auto& offsets   = footprint.at("offsets");
    const auto  reference = offsets.begin();
    const auto& origin    = holes.at(reference.key());
    for (const int direction : {1, -1}) {
        const bool matches = std::ranges::all_of(holes, [&](const auto& placed) {
            const auto& offset = offsets.at(placed.first);
            const auto  dx     = (offset[0].template get<int>() - reference.value()[0].template get<int>()) * direction;
            const auto  dy     = offset[1].template get<int>() - reference.value()[1].template get<int>();
            return static_cast<int>(placed.second.column) - static_cast<int>(origin.column) == dx && rowIndex(placed.second.row) - rowIndex(origin.row) == dy;
        });
        if (matches)
            return;
    }
    fail(path, "The terminals do not match the footprint of this component");
}

void checkValues(const Json& description, const Json& component, const std::string& path) {
    if (!component.contains("properties"))
        return;
    const auto& known = description.at("properties");
    for (const auto& [name, value] : component.at("properties").items()) {
        const auto valuePath = child(path + "/properties", name);
        if (!known.contains(name))
            fail(valuePath, "Unknown property: " + name);
        const auto&       property = known.at(name);
        const std::string type     = property.at("type");
        if ((type == "number" && !value.is_number()) || (type == "string" && !value.is_string()) || (type == "boolean" && !value.is_boolean()))
            fail(valuePath, "The property '" + name + "' is a " + type);
        if (type == "number"
            && ((property.contains("minimum") && value.get<double>() < property.at("minimum").get<double>())
                || (property.contains("maximum") && value.get<double>() > property.at("maximum").get<double>())))
            fail(valuePath, "Value is out of bounds");
        if (type == "string" && property.contains("enum") && std::ranges::find(property.at("enum"), value) == property.at("enum").end())
            fail(valuePath, "Value is not one of the allowed values");
    }
}

std::expected<Json, LoadError> readFile(const std::filesystem::path& path, std::string_view what) {
    std::ifstream input{path};
    if (!input)
        return std::unexpected(LoadError{"", "Cannot open " + std::string(what) + ": " + path.string()});
    try {
        return Json::parse(std::string{std::istreambuf_iterator<char>{input}, {}});
    } catch (const std::exception& e) {
        return std::unexpected(LoadError{"", e.what()});
    }
}
}  // namespace

std::optional<Hole> parseHole(std::string_view text) {
    Hole hole{};
    if (text.size() >= 3 && (text[0] == 'T' || text[0] == 'B') && (text[1] == '+' || text[1] == '-')) {
        hole.row      = text[0];
        hole.polarity = text[1];
        text.remove_prefix(2);
    } else if (text.size() >= 2 && text[0] >= 'a' && text[0] <= 'j') {
        hole.row = text[0];
        text.remove_prefix(1);
    } else {
        return std::nullopt;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), hole.column);
    if (error != std::errc{} || end != text.data() + text.size() || text[0] == '0' || hole.column == 0 || text.size() > 3)
        return std::nullopt;
    return hole;
}

double distance(const Hole& a, const Hole& b) {
    // Vertical position in hole pitches, as on a real board: the rails are one pitch apart and two pitches from the
    // strips, and the central channel is two pitches wide.
    const auto position = [](const Hole& hole) {
        if (hole.isRail())
            return hole.row == 'T' ? (hole.polarity == '-' ? 0 : 1) : (hole.polarity == '+' ? 15 : 16);
        return hole.row >= 'f' ? 3 + ('j' - hole.row) : 9 + ('e' - hole.row);
    };
    const auto dx = static_cast<double>(a.column) - static_cast<double>(b.column);
    const auto dy = static_cast<double>(position(a) - position(b));
    return std::hypot(dx, dy);
}

std::expected<void, LoadError> validateComponent(const Json& component) {
    try {
        static const nlohmann::json_schema::json_validator validator{Json::parse(embedded::componentSchema)};
        if (auto result = checkSchema(validator, component); !result)
            return result;
        std::set<std::string> terminals;
        for (std::size_t i = 0; i < component.at("terminals").size(); ++i)
            if (!terminals.insert(component.at("terminals")[i].at("name").get<std::string>()).second)
                fail(at("/terminals", i) + "/name", "Duplicate terminal name");
        checkProperties(component);
        checkFootprint(component, terminals);
        checkModel(component, terminals);
        checkState(component);
        return {};
    } catch (const LoadError& e) {
        return std::unexpected(e);
    } catch (const std::exception& e) {
        return std::unexpected(LoadError{"", e.what()});
    }
}

std::expected<void, LoadError> validateCircuit(const Json& circuit, const Library& library) {
    try {
        static const nlohmann::json_schema::json_validator validator{Json::parse(embedded::circuitSchema)};
        if (auto result = checkSchema(validator, circuit); !result)
            return result;
        const auto columns = circuit.at("board").at("columns").get<unsigned>();
        if (circuit.at("chip").at("column").get<unsigned>() > columns)
            fail("/chip/column", "The chip is outside the board");

        std::set<std::string> ids;
        std::set<Hole>        used;
        const auto            occupy = [&](const Hole& hole, const Json& text, const std::string& path) {
            if (!used.insert(hole).second)
                fail(path, "Hole already used: " + text.get<std::string>());
        };
        for (std::size_t i = 0; i < circuit.at("components").size(); ++i) {
            const auto& component = circuit.at("components")[i];
            const auto  path      = at("/components", i);
            if (!ids.insert(component.at("id").get<std::string>()).second)
                fail(path + "/id", "Duplicate component id: " + component.at("id").get<std::string>());
            const std::string type        = component.at("type");
            const auto*       description = library.find(type);
            if (!description)
                fail(path + "/type", "Unknown component type: " + type);
            const auto                  terminals = terminalNames(*description);
            std::map<std::string, Hole> holes;
            for (const auto& [terminal, hole] : component.at("at").items()) {
                const auto holePath = child(path + "/at", terminal);
                if (!terminals.contains(terminal))
                    fail(holePath, "Unknown terminal: " + terminal);
                holes.emplace(terminal, holeAt(hole, holePath, columns));
            }
            for (const auto& terminal : terminals)
                if (!holes.contains(terminal))
                    fail(path + "/at", "Missing terminal: " + terminal);
            checkPlacement(*description, holes, path + "/at");
            for (const auto& [terminal, hole] : holes)
                occupy(hole, component.at("at").at(terminal), child(path + "/at", terminal));
            checkValues(*description, component, path);
        }
        for (std::size_t i = 0; i < circuit.at("wires").size(); ++i) {
            const auto& wire = circuit.at("wires")[i];
            const auto  path = at("/wires", i);
            for (const auto* end : {"from", "to"})
                occupy(holeAt(wire.at(end), path + "/" + end, columns), wire.at(end), path + "/" + end);
            if (const auto* type = library.find("wire"))
                checkLength(type->at("footprint"), holeAt(wire.at("from"), path + "/from", columns), holeAt(wire.at("to"), path + "/to", columns), path);
        }
        return {};
    } catch (const LoadError& e) {
        return std::unexpected(e);
    } catch (const std::exception& e) {
        return std::unexpected(LoadError{"", e.what()});
    }
}

const Library& Library::builtin() {
    static const Library library = [] {
        Library result;
        for (const auto text : embedded::components)
            if (auto added = result.add(Json::parse(text)); !added)
                throw std::logic_error("Invalid built-in component at " + added.error().path + ": " + added.error().message);
        for (const auto& [type, script] : embedded::renderers)
            result.setRenderer(type, std::string{script});
        return result;
    }();
    return library;
}

std::expected<void, LoadError> Library::add(Json component) {
    if (auto result = validateComponent(component); !result)
        return result;
    std::string type = component.at("type");
    if (components.contains(type))
        return std::unexpected(LoadError{"/type", "Component type already defined: " + type});
    components.emplace(std::move(type), std::move(component));
    return {};
}

std::expected<void, LoadError> Library::addFile(const std::filesystem::path& path) {
    auto component = readFile(path, "component description");
    if (!component)
        return std::unexpected(component.error());
    return add(std::move(*component));
}

const Json* Library::find(std::string_view type) const noexcept {
    const auto found = components.find(type);
    return found == components.end() ? nullptr : &found->second;
}

std::string_view Library::renderer(std::string_view type) const noexcept {
    const auto found = renderers.find(type);
    return found == renderers.end() ? std::string_view{} : std::string_view{found->second};
}

void Library::setRenderer(std::string_view type, std::string script) {
    renderers.insert_or_assign(std::string{type}, std::move(script));
}

std::vector<std::string> Library::types() const {
    std::vector<std::string> names;
    for (const auto& [type, description] : components)
        names.push_back(type);
    return names;
}

std::expected<Json, LoadError> parseDescription(std::string_view text, const Library& library) {
    try {
        auto circuit = Json::parse(text);
        if (auto result = validateCircuit(circuit, library); !result)
            return std::unexpected(result.error());
        return circuit;
    } catch (const std::exception& e) {
        return std::unexpected(LoadError{"", e.what()});
    }
}

std::expected<Json, LoadError> loadDescription(const std::filesystem::path& path, const Library& library) {
    auto circuit = readFile(path, "circuit description");
    if (!circuit)
        return circuit;
    if (auto result = validateCircuit(*circuit, library); !result)
        return std::unexpected(result.error());
    return circuit;
}

std::expected<Json, MoveError> move(Json circuit, std::string_view id, const Json& holes, const Library& library) {
    Json* placed = nullptr;
    for (auto& component : circuit.at("components"))
        if (component.at("id") == id)
            placed = &component.at("at");
    if (!placed && id.starts_with("wire-")) {
        // The wires of a circuit are numbered from 1 in the order of "wires", as the interface draws them.
        unsigned   number{};
        const auto digits       = id.substr(5);
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (error == std::errc{} && end == digits.data() + digits.size() && number >= 1 && number <= circuit.at("wires").size())
            placed = &circuit.at("wires")[number - 1];
    }
    if (!placed)
        return std::unexpected(MoveError{
            MoveError::Kind::UnknownComponent,
            {"", "Unknown component: " + std::string(id)}
        });
    if (!holes.is_object() || holes.empty())
        return std::unexpected(MoveError{
            MoveError::Kind::Invalid,
            {"", "The holes are an object mapping terminals to holes"}
        });
    for (const auto& [terminal, hole] : holes.items()) {
        if (!placed->contains(terminal))
            return std::unexpected(MoveError{
                MoveError::Kind::Invalid,
                {"", "Unknown terminal: " + terminal}
            });
        if (!hole.is_string())
            return std::unexpected(MoveError{
                MoveError::Kind::Invalid,
                {"", "A hole is a string such as \"e6\""}
            });
        (*placed)[terminal] = hole;
    }
    if (auto valid = validateCircuit(circuit, library); !valid)
        return std::unexpected(MoveError{MoveError::Kind::Invalid, valid.error()});
    return circuit;
}

Json builtinDescription(std::string_view name) {
    for (const auto& [circuit, text] : embedded::circuits)
        if (circuit == name)
            return Json::parse(text);
    throw std::invalid_argument("Unknown built-in circuit");
}

std::vector<std::string> builtinNames() {
    std::vector<std::string> names;
    for (const auto& [circuit, text] : embedded::circuits)
        names.emplace_back(circuit);
    return names;
}

}  // namespace siliconforge::circuit
