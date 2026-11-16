#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/circuit/Description.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>

namespace siliconforge::circuit {
namespace detail {
using Json = nlohmann::json;

enum class ElementType : u8 { Wire, Resistor, Led, Switch, Potentiometer };

/// One electrical element of a component, between two or three nodes of the board.
struct Element {
    ElementType             type;
    std::size_t             component;  ///< Index in Model::components, npos for a wire of the circuit.
    std::string             state;      ///< State entry of the component, empty when the element has none.
    std::array<unsigned, 3> nodes{};
    double                  resistance{0};
    double                  forwardVoltage{0};
    double                  nominalCurrent{1};
    double                  value{0};    ///< Brightness, pressed (0 or 1) or position.
    double                  current{0};  ///< LED current, in amperes.
    // Brightness integrated over simulated time, for meanBrightness().
    long double integral{0};
    Cycles      updatedAt{0};
    Cycles      windowStart{0};
};

struct ComponentData {
    std::string              id;
    std::string              type;
    std::vector<std::size_t> elements;
};

struct ChipPin {
    Pin*                  pin;
    unsigned              node;
    bool                  relevant{false};  ///< Something else than the pin is on its net.
    std::optional<double> applied;          ///< Voltage this circuit currently applies to the pin.
};

enum class Level : u8 { Floating, Weak, Divider, Fixed };

struct Net {
    Level    level{Level::Floating};
    double   volts{0};
    unsigned sources{0};       ///< Strong sources on the net.
    bool     conflict{false};  ///< Strong sources disagree in this resolution.
    bool     shorted{false};   ///< The conflict was already reported.
};

constexpr auto     npos    = static_cast<std::size_t>(-1);
constexpr unsigned rails   = 4;  // T+, T-, B+, B-
constexpr double   epsilon = 1e-9;

struct Model : std::enable_shared_from_this<Model> {
    std::string                 name;
    unsigned                    columns{0};
    unsigned                    chipColumn{0};
    std::vector<ComponentData>  components;
    std::vector<Element>        elements;
    std::map<Hole, std::string> usedHoles;  ///< Hole -> JSON Pointer of the lead plugged in it.
    Mcu*                        mcu{nullptr};
    double                      supply{5.0};
    std::vector<ChipPin>        pins;
    std::vector<Fault>          faults;

    std::vector<unsigned>                 netOf;     ///< Node -> net (the node representing it).
    std::vector<Net>                      nets;      ///< Indexed by net.
    std::vector<std::vector<std::size_t>> incident;  ///< Net -> resistors and LEDs with a terminal on it.
    bool                                  topologyDirty{true};
    bool                                  solving{false};
    bool                                  again{false};

    [[nodiscard]] unsigned nodeCount() const noexcept {
        return rails + 2 * columns;
    }

    [[nodiscard]] unsigned nodeOf(const Hole& hole) const noexcept {
        if (hole.isRail())
            return (hole.row == 'T' ? 0U : 2U) + (hole.polarity == '+' ? 0U : 1U);
        return rails + (hole.column - 1) * 2 + (hole.row <= 'e' ? 1U : 0U);
    }

    [[nodiscard]] unsigned nodeOf(std::string_view text) const {
        const auto hole = parseHole(text);
        if (!hole || hole->column > columns)
            throw std::invalid_argument(std::format("No such hole on the board: {}", text));
        return nodeOf(*hole);
    }

    [[nodiscard]] Cycles now() const noexcept {
        return mcu ? mcu->cycles() : 0;
    }

    [[nodiscard]] bool anchored(unsigned net) const noexcept {
        return nets[net].level >= Level::Divider;
    }

    void rebuildTopology() {
        const auto            count = nodeCount();
        std::vector<unsigned> parent(count);
        std::iota(parent.begin(), parent.end(), 0U);
        const auto find = [&](unsigned node) {
            while (parent[node] != node)
                node = parent[node] = parent[parent[node]];
            return node;
        };
        for (const auto& element : elements)
            if (element.type == ElementType::Wire || (element.type == ElementType::Switch && element.value != 0))
                parent[find(element.nodes[0])] = find(element.nodes[1]);
        netOf.resize(count);
        for (unsigned node = 0; node < count; ++node)
            netOf[node] = find(node);
        nets.assign(count, Net{});
        incident.assign(count, {});
        std::vector<unsigned> occupants(count, 0);
        for (unsigned rail = 0; rail < rails; ++rail)
            ++occupants[netOf[rail]];
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const auto& element   = elements[i];
            const auto  terminals = element.type == ElementType::Potentiometer ? 3U : 2U;
            for (unsigned k = 0; k < terminals; ++k)
                ++occupants[netOf[element.nodes[k]]];
            if (element.type == ElementType::Resistor || element.type == ElementType::Led)
                for (unsigned k = 0; k < 2; ++k)
                    incident[netOf[element.nodes[k]]].push_back(i);
        }
        for (const auto& chipPin : pins)
            ++occupants[netOf[chipPin.node]];
        for (auto& chipPin : pins)
            chipPin.relevant = occupants[netOf[chipPin.node]] > 1;
        topologyDirty = false;
    }

    [[nodiscard]] static double railVolts(unsigned rail, double supply) noexcept {
        return rail % 2 == 0 ? supply : 0.0;
    }

    void fix(unsigned net, double volts) {
        auto& state = nets[net];
        ++state.sources;
        if (state.level != Level::Fixed) {
            state.level = Level::Fixed;
            state.volts = volts;
        } else if (std::abs(state.volts - volts) > epsilon) {
            state.conflict = true;
        }
    }

    void reportShort(unsigned net, Cycles at) {
        static constexpr std::array<std::string_view, rails> railNames{"T+", "T-", "B+", "B-"};
        Fault                                                fault{Fault::Kind::ShortCircuit, at, "Short circuit between ", {}};
        const auto                                           add = [&](std::string_view source, double volts) {
            if (!fault.sources.empty())
                fault.description += fault.sources.size() == 1 ? " and " : ", ";
            fault.description += std::format("{} ({:g} V)", source, volts);
            fault.sources.emplace_back(source);
        };
        for (unsigned rail = 0; rail < rails; ++rail)
            if (netOf[rail] == net)
                add(railNames[rail], railVolts(rail, supply));
        for (const auto& chipPin : pins)
            if (netOf[chipPin.node] == net && chipPin.pin->isOutput())
                add(chipPin.pin->name(), chipPin.pin->state() == PinState::High ? supply : 0.0);
        faults.push_back(std::move(fault));
    }

    struct Branch {
        double volts;    ///< Voltage of the anchored net the branch ends on.
        double ohms;     ///< Series resistance.
        double forward;  ///< Forward voltages of the LEDs crossed.
    };

    /// Follows a series chain away from `net` through `via` until a net whose voltage is known. `towardsHigh` tells
    /// which way the current would have to cross an LED; resistor-only walks pass `allowLeds` false.
    [[nodiscard]] std::optional<Branch> walk(unsigned net, std::size_t via, bool allowLeds, bool towardsHigh) const {
        Branch branch{0, 0, 0};
        for (std::size_t steps = 0; steps <= elements.size(); ++steps) {
            const auto& element = elements[via];
            const auto  first   = netOf[element.nodes[0]];
            const auto  second  = netOf[element.nodes[1]];
            if (first == second)
                return std::nullopt;  // Bypassed by a wire.
            if (element.type == ElementType::Led) {
                // Towards the high side, the LED must present its cathode to us; towards the low side, its anode.
                if (!allowLeds || (towardsHigh ? second : first) != net)
                    return std::nullopt;
                branch.forward += element.forwardVoltage;
            } else {
                branch.ohms += element.resistance;
            }
            net = first == net ? second : first;
            if (anchored(net)) {
                branch.volts = nets[net].volts;
                return branch;
            }
            const auto& next = incident[net];
            if (next.size() != 2)
                return std::nullopt;  // Open end, or a mesh this model does not resolve.
            via = next[0] == via ? next[1] : next[0];
        }
        return std::nullopt;
    }

    /// Voltage a potentiometer gives to the net of its wiper, when both its ends are known.
    [[nodiscard]] std::optional<double> dividerFor(unsigned net) const {
        for (const auto& element : elements) {
            if (element.type != ElementType::Potentiometer || netOf[element.nodes[1]] != net)
                continue;
            const auto a = netOf[element.nodes[0]];
            const auto b = netOf[element.nodes[2]];
            if (a != net && b != net && nets[a].level == Level::Fixed && nets[b].level == Level::Fixed)
                return nets[a].volts + element.value * (nets[b].volts - nets[a].volts);
        }
        return std::nullopt;
    }

    /// Voltage the resistors of a net pull it to. A net an LED is plugged in is left alone.
    [[nodiscard]] std::optional<double> pullFor(unsigned net) const {
        double conductance = 0;
        double weighted    = 0;
        for (const auto index : incident[net]) {
            if (elements[index].type == ElementType::Led)
                return std::nullopt;
            const auto branch = walk(net, index, false, false);
            if (!branch || branch->ohms <= 0)
                continue;
            conductance += 1 / branch->ohms;
            weighted    += branch->volts / branch->ohms;
        }
        if (conductance <= 0)
            return std::nullopt;
        return weighted / conductance;
    }

    /// One side of an LED: the voltage it reaches and what is in series on the way.
    [[nodiscard]] std::optional<Branch> sideOf(std::size_t led, unsigned net, bool towardsHigh) const {
        if (anchored(net))
            return Branch{nets[net].volts, 0, 0};
        const auto& around = incident[net];
        if (around.size() != 2)
            return std::nullopt;
        return walk(net, around[0] == led ? around[1] : around[0], true, towardsHigh);
    }

    void resolveLed(std::size_t index, Cycles at) {
        auto&      element = elements[index];
        double     current = 0;
        const auto anode   = netOf[element.nodes[0]];
        const auto cathode = netOf[element.nodes[1]];
        if (anode != cathode) {
            const auto high = sideOf(index, anode, true);
            const auto low  = sideOf(index, cathode, false);
            if (high && low) {
                const auto volts = high->volts - low->volts - element.forwardVoltage - high->forward - low->forward;
                const auto ohms  = high->ohms + low->ohms;
                if (volts > epsilon)
                    current = ohms > 0 ? volts / ohms : std::numeric_limits<double>::infinity();
            }
        }
        if (at > element.updatedAt) {
            element.integral += static_cast<long double>(element.value) * static_cast<long double>(at - element.updatedAt);
            element.updatedAt = at;
        }
        element.current = current;
        element.value   = std::clamp(current / element.nominalCurrent, 0.0, 1.0);
    }

    void resolve(Cycles at) {
        if (topologyDirty)
            rebuildTopology();
        for (auto& net : nets) {
            net.level    = Level::Floating;
            net.volts    = 0;
            net.sources  = 0;
            net.conflict = false;
        }
        for (unsigned rail = 0; rail < rails; ++rail)
            fix(netOf[rail], railVolts(rail, supply));
        for (const auto& chipPin : pins)
            if (chipPin.pin->isOutput())
                fix(netOf[chipPin.node], chipPin.pin->state() == PinState::High ? supply : 0.0);
        for (unsigned net = 0; net < nets.size(); ++net) {
            if (nets[net].conflict && !nets[net].shorted)
                reportShort(net, at);
            nets[net].shorted = nets[net].conflict;
        }

        for (const auto& element : elements) {
            if (element.type != ElementType::Potentiometer)
                continue;
            const auto wiper = netOf[element.nodes[1]];
            if (nets[wiper].level != Level::Floating)
                continue;
            if (const auto volts = dividerFor(wiper)) {
                nets[wiper].level = Level::Divider;
                nets[wiper].volts = *volts;
            }
        }
        // Pulls are computed from the anchored nets only, then applied, so that their order does not matter.
        std::vector<std::pair<unsigned, double>> pulls;
        for (unsigned net = 0; net < nets.size(); ++net)
            if (nets[net].level == Level::Floating && !incident[net].empty())
                if (const auto volts = pullFor(net))
                    pulls.emplace_back(net, *volts);
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (elements[i].type == ElementType::Led)
                resolveLed(i, at);
        for (const auto& [net, volts] : pulls) {
            nets[net].level = Level::Weak;
            nets[net].volts = volts;
        }

        for (auto& chipPin : pins) {
            if (!chipPin.relevant)
                continue;
            const auto            net = netOf[chipPin.node];
            std::optional<double> volts;
            if (chipPin.pin->isOutput() && nets[net].sources == 1) {
                // What the pin would read if it stopped driving: kept ready for the day it becomes an input.
                volts = dividerFor(net);
                if (!volts)
                    volts = pullFor(net);
            } else if (nets[net].level != Level::Floating) {
                volts = nets[net].volts;
            }
            if (volts == chipPin.applied)
                continue;
            chipPin.applied = volts;
            if (volts)
                chipPin.pin->setVoltage(*volts, supply);
            else
                chipPin.pin->setExternal(std::nullopt);
        }
    }

    void solve(Cycles at) {
        if (solving) {
            again = true;
            return;
        }
        solving = true;
        for (int round = 0; round < 8; ++round) {
            again = false;
            resolve(at);
            if (!again)
                break;
        }
        solving = false;
    }

    void pinChanged(std::size_t index, Cycles at) {
        // While resolving, the only changes are the levels this circuit applies to inputs: they drive nothing.
        if (solving || !pins[index].relevant)
            return;
        solve(at);
    }

    [[nodiscard]] Element& stateOf(std::size_t component, std::string_view name) {
        for (const auto index : components[component].elements)
            if (!elements[index].state.empty() && elements[index].state == name)
                return elements[index];
        throw std::out_of_range(std::format("{} has no state '{}'", components[component].id, name));
    }
};

namespace {
double property(const Json& description, const Json& component, std::string_view name) {
    const std::string key{name};
    if (component.contains("properties") && component.at("properties").contains(key))
        return component.at("properties").at(key).get<double>();
    return description.at("properties").at(key).at("default").get<double>();
}

std::shared_ptr<Model> build(const Json& circuit, const Library& library) {
    auto model        = std::make_shared<Model>();
    model->name       = circuit.at("name");
    model->columns    = circuit.at("board").at("columns");
    model->chipColumn = circuit.at("chip").at("column");
    const auto plug   = [&](const Json& hole, std::string path) {
        const auto parsed = *parseHole(hole.get<std::string>());
        model->usedHoles.emplace(parsed, std::move(path));
        return model->nodeOf(parsed);
    };
    for (std::size_t i = 0; i < circuit.at("components").size(); ++i) {
        const auto&                     component   = circuit.at("components")[i];
        const auto&                     description = *library.find(component.at("type").get<std::string>());
        const auto                      path        = "/components/" + std::to_string(i) + "/at/";
        std::map<std::string, unsigned> nodes;
        for (const auto& [terminal, hole] : component.at("at").items())
            nodes.emplace(terminal, plug(hole, path + terminal));
        ComponentData data{component.at("id"), component.at("type"), {}};
        for (const auto& described : description.at("model").at("elements")) {
            Element           element{};
            const std::string type = described.at("type");
            const std::string name = described.value("name", "");
            const auto        key  = [&](std::string_view state) {
                return name.empty() ? std::string(state) : name + "." + std::string(state);
            };
            element.component = model->components.size();
            for (std::size_t k = 0; k < described.at("between").size(); ++k)
                element.nodes[k] = nodes.at(described.at("between")[k].get<std::string>());
            if (type == "wire") {
                element.type = ElementType::Wire;
            } else if (type == "resistor") {
                element.type       = ElementType::Resistor;
                element.resistance = property(description, component, "resistance");
            } else if (type == "led") {
                element.type           = ElementType::Led;
                element.state          = key("brightness");
                element.forwardVoltage = property(description, component, "forwardVoltage");
                element.nominalCurrent = property(description, component, "nominalCurrent");
            } else if (type == "switch") {
                element.type  = ElementType::Switch;
                element.state = key("pressed");
            } else {
                element.type       = ElementType::Potentiometer;
                element.state      = key("position");
                element.resistance = property(description, component, "resistance");
            }
            if (element.type == ElementType::Switch || element.type == ElementType::Potentiometer) {
                const auto& initial = description.at("state").at(element.state);
                if (initial.contains("default"))
                    element.value = initial.at("default").is_boolean() ? (initial.at("default").get<bool>() ? 1.0 : 0.0) : initial.at("default").get<double>();
            }
            data.elements.push_back(model->elements.size());
            model->elements.push_back(std::move(element));
        }
        model->components.push_back(std::move(data));
    }
    for (std::size_t i = 0; i < circuit.at("wires").size(); ++i) {
        const auto& wire = circuit.at("wires")[i];
        const auto  path = "/wires/" + std::to_string(i);
        Element     element{};
        element.type      = ElementType::Wire;
        element.component = npos;
        element.nodes[0]  = plug(wire.at("from"), path + "/from");
        element.nodes[1]  = plug(wire.at("to"), path + "/to");
        model->elements.push_back(std::move(element));
    }
    model->solve(0);
    return model;
}
}  // namespace
}  // namespace detail

using detail::ElementType;
using detail::Json;

// Component -------------------------------------------------------------------------------------------------------------

const std::string& Component::id() const {
    return model->components[index].id;
}

const std::string& Component::type() const {
    return model->components[index].type;
}

double Component::state(std::string_view name) const {
    return model->stateOf(index, name).value;
}

void Component::set(std::string_view name, double value) {
    auto& element = model->stateOf(index, name);
    if (element.type == ElementType::Switch) {
        element.value        = value != 0 ? 1.0 : 0.0;
        model->topologyDirty = true;
    } else if (element.type == ElementType::Potentiometer) {
        element.value = std::clamp(value, 0.0, 1.0);
    } else {
        throw std::invalid_argument(std::format("The state '{}' of {} is not interactive", name, id()));
    }
    model->solve(model->now());
}

double Component::meanBrightness(std::string_view name) {
    auto&      element = model->stateOf(index, name);
    const auto now     = model->now();
    if (now > element.updatedAt) {
        element.integral += static_cast<long double>(element.value) * static_cast<long double>(now - element.updatedAt);
        element.updatedAt = now;
    }
    const auto span     = now - element.windowStart;
    const auto mean     = span ? static_cast<double>(element.integral / static_cast<long double>(span)) : element.value;
    element.integral    = 0;
    element.windowStart = now;
    return mean;
}

double Component::current(std::string_view name) const {
    return model->stateOf(index, name).current;
}

// Circuit ---------------------------------------------------------------------------------------------------------------

std::expected<Circuit, LoadError> Circuit::load(const std::filesystem::path& path) {
    return load(path, Library::builtin());
}

std::expected<Circuit, LoadError> Circuit::load(const std::filesystem::path& path, const Library& library) {
    const auto description = loadDescription(path, library);
    if (!description)
        return std::unexpected(description.error());
    return Circuit{detail::build(*description, library)};
}

std::expected<Circuit, LoadError> Circuit::fromJson(const Json& description) {
    return fromJson(description, Library::builtin());
}

std::expected<Circuit, LoadError> Circuit::fromJson(const Json& description, const Library& library) {
    if (auto result = validateCircuit(description, library); !result)
        return std::unexpected(result.error());
    return Circuit{detail::build(description, library)};
}

Circuit Circuit::builtin(std::string_view name) {
    return Circuit{detail::build(builtinDescription(name), Library::builtin())};
}

const std::string& Circuit::name() const {
    return model->name;
}

std::expected<void, LoadError> Circuit::attach(Mcu& mcu) {
    if (model->mcu)
        return std::unexpected(LoadError{"", "The circuit is already attached to an MCU"});
    Json package;
    try {
        package = Json::parse(mcu.chipJson());
    } catch (const std::exception& e) {
        return std::unexpected(LoadError{"", e.what()});
    }
    if (!package.is_object() || !package.contains("pins") || !package.at("pins").is_array())
        return std::unexpected(LoadError{"/chip/mcu", std::format("{} does not describe its package", mcu.name())});
    const auto& described = package.at("pins");
    const auto  count     = static_cast<unsigned>(described.size());
    const auto  half      = count / 2;
    if (model->chipColumn + half - 1 > model->columns)
        return std::unexpected(
            LoadError{"/chip/column",
                      std::format("{} needs {} columns from column {}, the board has {}", mcu.name(), half, model->chipColumn, model->columns)});

    std::vector<detail::ChipPin> pins;
    for (const auto& pin : described) {
        const auto number = pin.at("n").get<unsigned>();
        const bool bottom = number <= half;
        const Hole hole{bottom ? 'e' : 'f', 0, bottom ? model->chipColumn + number - 1 : model->chipColumn + count - number};
        if (const auto used = model->usedHoles.find(hole); used != model->usedHoles.end())
            return std::unexpected(LoadError{used->second, std::format("Hole covered by the chip: {}{}", hole.row, hole.column)});
        if (!pin.contains("port") || !pin.at("port").is_string())
            continue;  // Power pins: the rails are the sources of this model.
        if (auto* found = mcu.pin(pin.at("port").get<std::string>()))
            pins.push_back({found, model->nodeOf(hole), false, std::nullopt});
    }

    model->mcu           = &mcu;
    model->supply        = mcu.supplyVolts();
    model->pins          = std::move(pins);
    model->topologyDirty = true;
    const auto now       = mcu.cycles();
    for (auto& element : model->elements)
        element.updatedAt = element.windowStart = now;
    for (std::size_t i = 0; i < model->pins.size(); ++i)
        model->pins[i].pin->subscribe([weak = std::weak_ptr{model}, i](const Pin&, Cycles at) {
            if (const auto alive = weak.lock())
                alive->pinChanged(i, at);
        });
    model->solve(now);
    return {};
}

std::vector<std::string> Circuit::componentIds() const {
    std::vector<std::string> ids;
    for (const auto& component : model->components)
        ids.push_back(component.id);
    return ids;
}

Component Circuit::component(std::string_view id) {
    for (std::size_t i = 0; i < model->components.size(); ++i)
        if (model->components[i].id == id)
            return Component{model, i};
    throw std::out_of_range(std::format("The circuit has no component {}", id));
}

bool Circuit::connected(std::string_view first, std::string_view second) const {
    if (model->topologyDirty)
        model->rebuildTopology();
    return model->netOf[model->nodeOf(first)] == model->netOf[model->nodeOf(second)];
}

std::optional<double> Circuit::voltage(std::string_view hole) const {
    const auto& net = model->nets[model->netOf[model->nodeOf(hole)]];
    if (net.level == detail::Level::Floating)
        return std::nullopt;
    return net.volts;
}

const std::vector<Fault>& Circuit::faults() const {
    return model->faults;
}

void Circuit::clearFaults() {
    model->faults.clear();
}

}  // namespace siliconforge::circuit
