#include <siliconforge/circuit/Circuit.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <queue>
#include <set>

namespace siliconforge::circuit {
namespace {
using Json = nlohmann::json;

constexpr double pullUpResistance  = 35'000.0;  // Typical internal pull-up of an AVR input.
constexpr double ledOnResistance   = 5.0;       // Dynamic resistance of a conducting LED.
constexpr double leakConductance   = 1e-10;     // Keeps an unconnected net solvable; it reads 0 V.
constexpr double stiffConductance  = 1e6;       // A supply held between two nets that no source fixes: 1 µΩ.
constexpr double minimumResistance = 1e-3;      // A potentiometer at the end of its track.
constexpr double absorbedCurrent   = 1e-9;      // A supply cannot take in more than numerical noise.
constexpr double conflictTolerance = 1e-9;      // A supply whose output is held at another voltage than its setpoint, however little.
constexpr double shortTolerance    = 0.05;      // Two sources closer than this agree.
constexpr std::size_t maxRemembered = 4096;

/// Disjoint sets of small integers.
struct Sets {
    std::vector<int> parent;
    explicit Sets(std::size_t count) : parent(count) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int item) {
        while (parent[static_cast<std::size_t>(item)] != item)
            item = parent[static_cast<std::size_t>(item)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(item)])];
        return item;
    }
    void unite(int a, int b) { parent[static_cast<std::size_t>(find(a))] = find(b); }
};

/// Solves the dense system a·x = b in place by Gaussian elimination with partial pivoting; x replaces b.
void solveLinear(std::vector<double>& a, std::vector<double>& b) {
    const auto n  = b.size();
    const auto at = [&](std::size_t row, std::size_t column) -> double& { return a[row * n + column]; };
    for (std::size_t column = 0; column < n; ++column) {
        auto pivot = column;
        for (auto row = column + 1; row < n; ++row)
            if (std::abs(at(row, column)) > std::abs(at(pivot, column)))
                pivot = row;
        if (pivot != column) {
            for (std::size_t k = column; k < n; ++k)
                std::swap(at(column, k), at(pivot, k));
            std::swap(b[column], b[pivot]);
        }
        const auto diagonal = at(column, column);
        if (diagonal == 0.0)
            continue;  // Cannot happen: every row has a leak to ground.
        for (auto row = column + 1; row < n; ++row) {
            const auto factor = at(row, column) / diagonal;
            if (factor == 0.0)
                continue;
            for (std::size_t k = column; k < n; ++k)
                at(row, k) -= factor * at(column, k);
            b[row] -= factor * b[column];
        }
    }
    for (auto row = n; row-- > 0;) {
        auto sum = b[row];
        for (auto k = row + 1; k < n; ++k)
            sum -= at(row, k) * b[k];
        const auto diagonal = at(row, row);
        b[row]              = diagonal == 0.0 ? 0.0 : sum / diagonal;
    }
}
}  // namespace

struct Circuit::Model {
    struct StateEntry {
        std::string           name;
        double                value{0};
        bool                  boolean{false};
        bool                  interactive{false};
        bool                  averaged{false};  ///< Reported as its time average over a frame (LED brightness).
        std::optional<double> minimum;
        std::optional<double> maximum;
        double                integral{0};
    };
    struct Component {
        std::string             id;
        std::string             type;
        Json                    properties;
        std::vector<StateEntry> state;
        std::vector<int>        terminals;  ///< Nodes, in the order of the type's terminals.
        std::string             pin;        ///< MCU pin the component is wired to, when there is one.
    };
    enum class Kind { Wire, Resistor, Led, Switch, Potentiometer, Supply, Load, Meter };
    struct Element {
        Kind        kind;
        int         nodes[3]{-1, -1, -1};
        std::size_t component;
        int         stateIndex{-1};  ///< brightness, pressed or position.
        int         extraIndex{-1};  ///< current (LED, supply, load) or voltage (potentiometer), when the type declares it.
        int         limitIndex{-1};    ///< Supply: current limit.
        int         enabledIndex{-1};  ///< Supply: output enabled.
        int         voltageIndex{-1};  ///< Supply: voltage obtained; load: voltage across.
        int         modeIndex{-1};     ///< Supply: 0 off, 1 constant voltage, 2 constant current.
        double      resistance{0};
        double      forwardVoltage{0};
        double      nominalCurrent{1};
        bool        conducting{false};
    };
    struct ChipPin {
        unsigned              number;
        int                   node;
        std::string           name;
        std::string           label;
        Pin*                  pin{nullptr};
        std::optional<double> force;
        int                   applied{0};  ///< 0: nothing yet, 1: released, 2: a voltage.
        double                appliedVolts{0};
    };
    struct Solution {
        std::vector<double>      voltage;  ///< Per net.
        std::vector<char>        driven;   ///< Per net: a source reaches it through wires and components.
        std::vector<char>        conducting;
        std::vector<double>      value;  ///< Per element: brightness or wiper voltage.
        std::vector<double>      extra;  ///< Per element: LED, load or supply current.
        std::vector<char>        mode;   ///< Per element: the mode of a supply (0 off, 1 CV, 2 CC).
        std::vector<char>        held;    ///< Per element: a supply in CV whose positive terminal another source holds above its setpoint.
        std::vector<char>        fought;  ///< Per element: a supply in CV whose positive terminal another source holds below its setpoint.
        std::vector<std::string> fight;   ///< Per element: the conflict that made it so.
        std::vector<std::string> shorts;
    };

    unsigned                         columns{0};
    unsigned                         chipColumn{0};
    std::vector<Component>           components;
    std::vector<Element>             elements;
    std::vector<std::pair<int, int>> wires;
    std::vector<ChipPin>             chip;
    Json                             types = Json::object();  ///< Name, description and footprint of the types in use.
    Mcu*                             mcu{nullptr};

    std::vector<int>                          netOf;
    std::size_t                               netCount{0};
    bool                                      topologyDirty{true};
    std::vector<std::optional<double>>        fixed;  ///< Per net, from the last solve.
    std::vector<double>                       voltage;
    std::unordered_map<std::string, Solution> remembered;
    bool                                      solving{false};
    Cycles                                    lastAt{0};
    Cycles                                    frameStart{0};

    [[nodiscard]] std::size_t nodeCount() const { return 2 * static_cast<std::size_t>(columns) + 4; }

    [[nodiscard]] int node(const Hole& hole) const {
        const auto strip = static_cast<int>(columns);
        if (hole.isRail())
            return 2 * strip + (hole.row == 'T' ? 0 : 2) + (hole.polarity == '+' ? 0 : 1);
        return (hole.row >= 'f' ? 0 : strip) + static_cast<int>(hole.column) - 1;
    }
    [[nodiscard]] bool isRail(int nodeIndex) const { return nodeIndex >= 2 * static_cast<int>(columns); }
    [[nodiscard]] bool isSupplyRail(int nodeIndex) const { return isRail(nodeIndex) && (nodeIndex - 2 * static_cast<int>(columns)) % 2 == 0; }

    [[nodiscard]] std::size_t net(int nodeIndex) const { return static_cast<std::size_t>(netOf[static_cast<std::size_t>(nodeIndex)]); }

    void rebuildTopology() {
        Sets sets{nodeCount()};
        for (const auto& [from, to] : wires)
            sets.unite(from, to);
        for (const auto& element : elements) {
            const auto closed = element.kind == Kind::Switch && components[element.component].state[static_cast<std::size_t>(element.stateIndex)].value != 0.0;
            if (element.kind == Kind::Wire || closed)
                sets.unite(element.nodes[0], element.nodes[1]);
        }
        std::vector<int> index(nodeCount(), -1);
        netOf.assign(nodeCount(), 0);
        netCount = 0;
        for (std::size_t i = 0; i < nodeCount(); ++i) {
            auto& slot = index[static_cast<std::size_t>(sets.find(static_cast<int>(i)))];
            if (slot < 0)
                slot = static_cast<int>(netCount++);
            netOf[i] = slot;
        }
        topologyDirty = false;
        remembered.clear();
    }

    ChipPin* chipPin(std::string_view nameOrLabel) {
        for (auto& candidate : chip)
            if (!candidate.name.empty() && (candidate.name == nameOrLabel || (!candidate.label.empty() && candidate.label == nameOrLabel)))
                return &candidate;
        return nullptr;
    }

    /// The drives of the MCU pins: what a remembered solution depends on, besides what clears the memory.
    [[nodiscard]] std::string key() const {
        std::string result;
        result.reserve(chip.size());
        for (const auto& entry : chip) {
            if (!entry.pin)
                result += '-';
            else if (entry.pin->isOutput())
                result += entry.pin->state() == PinState::High ? 'H' : 'L';
            else
                result += entry.pin->hasPullUp() ? 'u' : 'i';
        }
        return result;
    }

    /// The state entry of an element, or nullptr when the type does not declare it.
    StateEntry* entryOf(const Element& element, int index) {
        return index < 0 ? nullptr : &components[element.component].state[static_cast<std::size_t>(index)];
    }

    /// Solves the circuit with the supplies in the modes they take as the load asks for current: a supply starts in
    /// constant voltage, goes to constant current when it would deliver more than its limit, and returns to constant
    /// voltage when the voltage it can hold in constant current exceeds its setpoint.
    Solution solve(double supplyVolts) {
        std::vector<char> mode(elements.size(), 0);
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (elements[i].kind == Kind::Supply && entryOf(elements[i], elements[i].enabledIndex)->value != 0.0)
                // Terminals shorted: it can only limit, unless it is asked for 0 V, which is what a short gives.
                mode[i] = net(elements[i].nodes[0]) == net(elements[i].nodes[1]) && entryOf(elements[i], elements[i].stateIndex)->value > 0.0 ? 2 : 1;
        std::vector<char>        fought(elements.size(), 0);
        std::vector<char>        opened(elements.size(), 0);
        std::vector<std::string> fights(elements.size());
        const auto               supplies = static_cast<int>(std::ranges::count_if(elements, [](const auto& e) { return e.kind == Kind::Supply; }));
        // Modes may go both ways while the supplies settle. If they have not settled after `free` rounds (supplies
        // sharing a load can hand the limit to one another), only the one-way changes are still allowed: a supply that
        // reaches its limit stays there, and each round fixes at least one of them, so the rounds that follow end.
        const int free = 4 * supplies + 8;
        Solution  result;
        for (int round = 0; round < free + supplies + 1; ++round) {
            result           = solveOnce(supplyVolts, mode);
            bool changed     = false;
            const bool oneWay = round >= free;
            for (std::size_t i = 0; i < elements.size(); ++i) {
                if (elements[i].kind != Kind::Supply)
                    continue;
                const auto setpoint = entryOf(elements[i], elements[i].stateIndex)->value;
                const auto limit    = entryOf(elements[i], elements[i].limitIndex)->value;
                if (opened[i]) {
                    // An output opened because something pushed current into it takes the regulation back as soon as
                    // the circuit lets its voltage fall below the setpoint again.
                    if (!oneWay && result.value[i] < setpoint - absorbedCurrent) {
                        mode[i]   = 1;
                        opened[i] = 0;
                        changed   = true;
                    }
                    continue;
                }
                if (result.mode[i] == 0)
                    continue;
                if (mode[i] == 1 && result.fought[i]) {
                    // Another source holds its output at a different voltage: all it can do is limit its current.
                    mode[i]   = 2;
                    fought[i] = 1;
                    fights[i] = result.fight[i];
                    changed   = true;
                } else if (mode[i] == 1 && result.extra[i] > limit * (1 + 1e-9)) {
                    mode[i] = 2;
                    changed = true;
                } else if (mode[i] == 1 && result.extra[i] < -absorbedCurrent) {
                    // Something pushes current into it and it cannot absorb any: its output opens.
                    mode[i]   = 0;
                    opened[i] = 1;
                    changed   = true;
                } else if (mode[i] == 2 && !oneWay && result.value[i] > setpoint) {
                    // Back in constant voltage, and no longer in conflict until it is placed again: what held its output
                    // below its setpoint may have gone (a lower channel that opened).
                    mode[i]   = 1;
                    fought[i] = 0;
                    fights[i].clear();
                    changed   = true;
                }
            }
            if (!changed)
                break;
        }
        // The conflict of a supply that now limits its current is no longer imposed, but it remains a short.
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (fought[i] && !fights[i].empty() && std::ranges::find(result.shorts, fights[i]) == result.shorts.end())
                result.shorts.push_back(fights[i]);
        return result;
    }

    /// The current that leaves a net through the components that touch it, for the voltages of a solution.
    [[nodiscard]] double currentOut(std::size_t target, const Solution& solution, const std::vector<char>& conducting, double supplyVolts) const {
        double total = 0;
        const auto flow = [&](int from, int to, double g) {
            if (net(from) == target && net(to) != target)
                total += g * (solution.voltage[net(from)] - solution.voltage[net(to)]);
            else if (net(to) == target && net(from) != target)
                total += g * (solution.voltage[net(to)] - solution.voltage[net(from)]);
        };
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const auto& element = elements[i];
            switch (element.kind) {
                case Kind::Resistor: flow(element.nodes[0], element.nodes[1], 1.0 / element.resistance); break;
                case Kind::Load: flow(element.nodes[0], element.nodes[1], 1.0 / components[element.component].state[static_cast<std::size_t>(element.stateIndex)].value); break;
                case Kind::Potentiometer: {
                    const auto position = components[element.component].state[static_cast<std::size_t>(element.stateIndex)].value;
                    flow(element.nodes[0], element.nodes[1], 1.0 / std::max(element.resistance * position, minimumResistance));
                    flow(element.nodes[1], element.nodes[2], 1.0 / std::max(element.resistance * (1.0 - position), minimumResistance));
                    break;
                }
                case Kind::Led:
                    if (conducting[i]) {
                        const auto g       = 1.0 / ledOnResistance;
                        const auto through = g * (solution.voltage[net(element.nodes[0])] - solution.voltage[net(element.nodes[1])] - element.forwardVoltage);
                        if (net(element.nodes[0]) == target && net(element.nodes[1]) != target)
                            total += through;
                        else if (net(element.nodes[1]) == target && net(element.nodes[0]) != target)
                            total -= through;
                    }
                    break;
                default: break;
            }
        }
        for (const auto& entry : chip)
            if (entry.pin && !entry.pin->isOutput() && entry.pin->hasPullUp() && net(entry.node) == target)
                total += (solution.voltage[target] - supplyVolts) / pullUpResistance;
        return total;
    }

    Solution solveOnce(double supplyVolts, const std::vector<char>& supplyMode) {
        Solution result;
        result.fought.assign(elements.size(), 0);
        result.held.assign(elements.size(), 0);
        result.fight.assign(elements.size(), {});
        fixed.assign(netCount, std::nullopt);
        std::vector<std::string> origin(netCount);
        const auto               impose = [&](std::size_t target, double volts, std::string what) {
            if (!fixed[target]) {
                fixed[target]  = volts;
                origin[target] = std::move(what);
            } else if (std::abs(*fixed[target] - volts) > shortTolerance) {
                result.shorts.push_back(std::format("Short circuit: {} ({:.3g} V) and {} ({:.3g} V) are connected together", origin[target],
                                                    *fixed[target], what, volts));
            }
        };
        for (int rail = 0; rail < 4; ++rail) {
            const auto nodeIndex = 2 * static_cast<int>(columns) + rail;
            impose(net(nodeIndex), isSupplyRail(nodeIndex) ? supplyVolts : 0.0, isSupplyRail(nodeIndex) ? "the supply rail" : "the ground rail");
        }
        for (const auto& entry : chip)
            if (entry.pin && entry.pin->isOutput())
                impose(net(entry.node), entry.pin->state() == PinState::High ? supplyVolts : 0.0, "output " + (entry.label.empty() ? entry.name : entry.label));
        for (const auto& entry : chip)
            if (entry.force)
                impose(net(entry.node), *entry.force, "the probe on " + (entry.label.empty() ? entry.name : entry.label));
        // A supply in constant voltage holds its positive terminal above its negative one, once that one is fixed:
        // the supplies are placed in turn, so that one may be stacked on another.
        std::vector<char> placed(elements.size(), 0);
        for (std::size_t round = 0; round < elements.size(); ++round) {
            // The supplies that can be placed this round go from the highest voltage to the lowest, whatever their order
            // in the description: a supply cannot sink current, so the highest one holds a net they share and the
            // others, driven above their setpoint, deliver nothing.
            std::vector<std::pair<double, std::size_t>> ready;
            for (std::size_t i = 0; i < elements.size(); ++i) {
                const auto& element = elements[i];
                if (element.kind != Kind::Supply || supplyMode[i] != 1 || placed[i])
                    continue;
                const auto& reference = fixed[net(element.nodes[1])];
                if (reference)
                    ready.emplace_back(*reference + entryOf(element, element.stateIndex)->value, i);
            }
            std::ranges::sort(ready, [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
            for (const auto& [wanted, i] : ready) {
                const auto& element = elements[i];
                const auto  before  = result.shorts.size();
                // Held below its setpoint, it is overloaded and can only limit its current; held above, it is driven by
                // the other source and delivers nothing: it stays in constant voltage.
                const auto& held = fixed[net(element.nodes[0])];
                if (held && *held < wanted - conflictTolerance)
                    result.fought[i] = 1;
                else if (held && *held > wanted + conflictTolerance)
                    result.held[i] = 1;
                // Driven above its setpoint it merely delivers nothing, as a supply behind a diode would: that is no short.
                if (!result.held[i])
                    impose(net(element.nodes[0]), wanted, "supply " + components[element.component].id);
                if (result.shorts.size() > before)
                    result.fight[i] = result.shorts[before];
                placed[i] = true;
            }
            if (ready.empty())
                break;
        }
        // A supply whose negative terminal is not fixed yet (stacked on a supply in constant current, say) is held by
        // a very stiff branch between its terminals, as long as its negative terminal reaches a fixed net through the
        // other components and supplies. One that reaches nothing has no reference: it delivers nothing.
        std::vector<char> active(elements.size(), 0);
        std::vector<char> stiff(elements.size(), 0);
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (elements[i].kind != Kind::Supply || supplyMode[i] == 0)
                continue;
            if (supplyMode[i] == 1 && net(elements[i].nodes[0]) == net(elements[i].nodes[1])) {
                active[i] = 1;  // A 0 V channel with its terminals together: nothing to hold, nothing delivered.
                continue;
            }
            if (fixed[net(elements[i].nodes[1])] && (supplyMode[i] == 2 || placed[i])) {
                active[i] = 1;
                continue;
            }
            Sets group{netCount};
            for (std::size_t j = 0; j < elements.size(); ++j) {
                const auto& other = elements[j];
                const bool  links = other.kind == Kind::Resistor || other.kind == Kind::Load || other.kind == Kind::Led || other.kind == Kind::Potentiometer
                                   || (other.kind == Kind::Supply && j != i && supplyMode[j] != 0);
                if (!links)
                    continue;
                group.unite(static_cast<int>(net(other.nodes[0])), static_cast<int>(net(other.nodes[1])));
                if (other.kind == Kind::Potentiometer)
                    group.unite(static_cast<int>(net(other.nodes[1])), static_cast<int>(net(other.nodes[2])));
            }
            bool referenced = false;
            // An input with its pull-up on is tied to the supply rail through 35 kohm: that is a reference too.
            for (const auto& entry : chip)
                if (entry.pin && !entry.pin->isOutput() && entry.pin->hasPullUp()
                    && group.find(static_cast<int>(net(entry.node))) == group.find(static_cast<int>(net(elements[i].nodes[1]))))
                    referenced = true;
            for (std::size_t n = 0; n < netCount && !referenced; ++n)
                referenced = fixed[n] && group.find(static_cast<int>(n)) == group.find(static_cast<int>(net(elements[i].nodes[1])));
            if (!referenced)
                continue;
            active[i] = 1;
            stiff[i]  = supplyMode[i] == 1;
        }

        // Unknowns: the nets a component or a pull-up touches and that no source fixes.
        std::vector<int> row(netCount, -1);
        std::size_t      unknowns = 0;
        const auto       touch    = [&](int nodeIndex) {
            const auto target = net(nodeIndex);
            if (!fixed[target] && row[target] < 0)
                row[target] = static_cast<int>(unknowns++);
        };
        for (const auto& element : elements) {
            if (element.kind == Kind::Wire || element.kind == Kind::Switch || element.kind == Kind::Supply || element.kind == Kind::Meter)
                continue;
            touch(element.nodes[0]);
            touch(element.nodes[1]);
            if (element.kind == Kind::Potentiometer)
                touch(element.nodes[2]);
        }
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (active[i] && (supplyMode[i] == 2 || stiff[i])) {
                touch(elements[i].nodes[0]);
                touch(elements[i].nodes[1]);
            }
        for (const auto& entry : chip)
            if (entry.pin && !entry.pin->isOutput() && entry.pin->hasPullUp())
                touch(entry.node);

        result.voltage.assign(netCount, 0.0);
        for (std::size_t i = 0; i < netCount; ++i)
            if (fixed[i])
                result.voltage[i] = *fixed[i];

        std::vector<char> conducting(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i)
            conducting[i] = elements[i].conducting;
        std::vector<double> matrix;
        std::vector<double> right;
        for (int iteration = 0; iteration < 24 && unknowns > 0; ++iteration) {
            matrix.assign(unknowns * unknowns, 0.0);
            right.assign(unknowns, 0.0);
            // A conductance between two nets; `to` may be a fixed net or, as a negative value, an explicit voltage.
            const auto conductance = [&](std::size_t from, std::size_t to, double g) {
                if (from == to)
                    return;
                const auto a = row[from];
                const auto b = row[to];
                if (a >= 0) {
                    matrix[static_cast<std::size_t>(a) * unknowns + static_cast<std::size_t>(a)] += g;
                    if (b >= 0)
                        matrix[static_cast<std::size_t>(a) * unknowns + static_cast<std::size_t>(b)] -= g;
                    else
                        right[static_cast<std::size_t>(a)] += g * result.voltage[to];
                }
                if (b >= 0) {
                    matrix[static_cast<std::size_t>(b) * unknowns + static_cast<std::size_t>(b)] += g;
                    if (a >= 0)
                        matrix[static_cast<std::size_t>(b) * unknowns + static_cast<std::size_t>(a)] -= g;
                    else
                        right[static_cast<std::size_t>(b)] += g * result.voltage[from];
                }
            };
            const auto inject = [&](std::size_t into, double current) {
                if (row[into] >= 0)
                    right[static_cast<std::size_t>(row[into])] += current;
            };
            for (std::size_t i = 0; i < unknowns; ++i)
                matrix[i * unknowns + i] += leakConductance;
            for (std::size_t i = 0; i < elements.size(); ++i) {
                const auto& element = elements[i];
                switch (element.kind) {
                    case Kind::Resistor: conductance(net(element.nodes[0]), net(element.nodes[1]), 1.0 / element.resistance); break;
                    case Kind::Load: conductance(net(element.nodes[0]), net(element.nodes[1]), 1.0 / entryOf(element, element.stateIndex)->value); break;
                    case Kind::Supply:
                        if (active[i] && stiff[i]) {  // An ideal source as a Norton pair: a conductance and the current it pushes.
                            const auto setpoint = entryOf(element, element.stateIndex)->value;
                            conductance(net(element.nodes[0]), net(element.nodes[1]), stiffConductance);
                            inject(net(element.nodes[0]), stiffConductance * setpoint);
                            inject(net(element.nodes[1]), -stiffConductance * setpoint);
                        } else if (active[i] && supplyMode[i] == 2) {  // A current source: the limit leaves the negative terminal and enters the positive one.
                            const auto limit = entryOf(element, element.limitIndex)->value;
                            inject(net(element.nodes[0]), limit);
                            inject(net(element.nodes[1]), -limit);
                        }
                        break;
                    case Kind::Potentiometer: {
                        const auto position = components[element.component].state[static_cast<std::size_t>(element.stateIndex)].value;
                        conductance(net(element.nodes[0]), net(element.nodes[1]), 1.0 / std::max(element.resistance * position, minimumResistance));
                        conductance(net(element.nodes[1]), net(element.nodes[2]), 1.0 / std::max(element.resistance * (1.0 - position), minimumResistance));
                        break;
                    }
                    case Kind::Led:
                        if (conducting[i]) {  // A threshold in series with a small resistance.
                            const auto g = 1.0 / ledOnResistance;
                            conductance(net(element.nodes[0]), net(element.nodes[1]), g);
                            inject(net(element.nodes[0]), g * element.forwardVoltage);
                            inject(net(element.nodes[1]), -g * element.forwardVoltage);
                        }
                        break;
                    default: break;
                }
            }
            for (const auto& entry : chip)
                if (entry.pin && !entry.pin->isOutput() && entry.pin->hasPullUp() && row[net(entry.node)] >= 0) {
                    const auto r = static_cast<std::size_t>(row[net(entry.node)]);
                    matrix[r * unknowns + r] += 1.0 / pullUpResistance;
                    right[r] += supplyVolts / pullUpResistance;
                }
            solveLinear(matrix, right);
            for (std::size_t i = 0; i < netCount; ++i)
                if (row[i] >= 0)
                    result.voltage[i] = right[static_cast<std::size_t>(row[i])];
            bool changed = false;
            for (std::size_t i = 0; i < elements.size(); ++i) {
                if (elements[i].kind != Kind::Led)
                    continue;
                const auto across = result.voltage[net(elements[i].nodes[0])] - result.voltage[net(elements[i].nodes[1])];
                const char now    = across > elements[i].forwardVoltage + 1e-9;
                if (now != conducting[i]) {
                    conducting[i] = now;
                    changed       = true;
                }
            }
            if (!changed)
                break;
        }

        // A net is driven when a source reaches it through wires and components; a pull-up alone does not count,
        // so that an input left to its pull-up stays "released" for the MCU.
        Sets reach{netCount};
        for (const auto& element : elements) {
            if (element.kind == Kind::Wire || element.kind == Kind::Switch || element.kind == Kind::Meter)
                continue;  // A voltmeter draws no current: it joins nothing.
            if (element.kind == Kind::Supply && !active[static_cast<std::size_t>(&element - elements.data())])
                continue;  // An output that is off is an open circuit.
            reach.unite(static_cast<int>(net(element.nodes[0])), static_cast<int>(net(element.nodes[1])));
            if (element.kind == Kind::Potentiometer)
                reach.unite(static_cast<int>(net(element.nodes[1])), static_cast<int>(net(element.nodes[2])));
        }
        std::vector<char> sourced(netCount, 0);
        for (std::size_t i = 0; i < netCount; ++i)
            if (fixed[i])
                sourced[static_cast<std::size_t>(reach.find(static_cast<int>(i)))] = 1;
        result.driven.assign(netCount, 0);
        for (std::size_t i = 0; i < netCount; ++i)
            result.driven[i] = sourced[static_cast<std::size_t>(reach.find(static_cast<int>(i)))];

        result.conducting = conducting;
        result.value.assign(elements.size(), 0.0);
        result.extra.assign(elements.size(), 0.0);
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const auto& element = elements[i];
            if (element.kind == Kind::Led && conducting[i]) {
                const auto across  = result.voltage[net(element.nodes[0])] - result.voltage[net(element.nodes[1])];
                auto current = std::max(0.0, (across - element.forwardVoltage) / ledOnResistance);
                if (current < 1e-7)
                    current = 0.0;  // What leaks through a floating net is not light.
                result.extra[i]    = current;
                result.value[i]    = std::min(1.0, current / element.nominalCurrent);
            } else if (element.kind == Kind::Potentiometer) {
                result.value[i] = result.voltage[net(element.nodes[1])];
            } else if (element.kind == Kind::Meter) {
                result.value[i] = result.voltage[net(element.nodes[0])] - result.voltage[net(element.nodes[1])];
            } else if (element.kind == Kind::Load) {
                result.value[i] = result.voltage[net(element.nodes[0])] - result.voltage[net(element.nodes[1])];
                result.extra[i] = result.value[i] / entryOf(element, element.stateIndex)->value;
            }
        }
        // What a supply obtains and delivers. In constant current it is the limit; in constant voltage it is what the
        // components draw from its positive terminal, plus what the supplies stacked on it draw through their
        // negative terminal, less what supplies in parallel deliver (Kirchhoff's current law on its positive net). The
        // supplies depend on one another along a stack, so the currents are settled by repeating the sum.
        result.mode.assign(elements.size(), 0);
        std::vector<double> delivered(elements.size(), 0.0);
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (elements[i].kind == Kind::Supply && active[i] && supplyMode[i] == 2)
                delivered[i] = entryOf(elements[i], elements[i].limitIndex)->value;
            else if (elements[i].kind == Kind::Supply && stiff[i])
                delivered[i] = stiffConductance * (entryOf(elements[i], elements[i].stateIndex)->value
                                                   - (result.voltage[net(elements[i].nodes[0])] - result.voltage[net(elements[i].nodes[1])]));
        for (std::size_t round = 0; round <= elements.size(); ++round)
            for (std::size_t i = 0; i < elements.size(); ++i) {
                if (elements[i].kind != Kind::Supply || !active[i] || supplyMode[i] != 1 || stiff[i] || result.held[i])
                    continue;
                const auto positive = net(elements[i].nodes[0]);
                if (positive == net(elements[i].nodes[1]))
                    continue;  // Its terminals are together: whatever leaves one enters the other.
                auto       current  = currentOut(positive, result, conducting, supplyVolts);
                for (std::size_t j = 0; j < elements.size(); ++j) {
                    if (j == i || elements[j].kind != Kind::Supply || !active[j])
                        continue;
                    if (net(elements[j].nodes[1]) == positive)
                        current += delivered[j];
                    if (net(elements[j].nodes[0]) == positive)
                        current -= delivered[j];
                }
                delivered[i] = current;
            }
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const auto& element = elements[i];
            if (element.kind != Kind::Supply)
                continue;
            // The voltage across the terminals is measured in every mode: an open output still sees what the circuit
            // applies to it. Only the mode and the current stay at zero when it delivers nothing.
            result.value[i] = result.voltage[net(element.nodes[0])] - result.voltage[net(element.nodes[1])];
            if (!active[i])
                continue;
            result.mode[i]  = supplyMode[i];
            result.extra[i] = std::abs(delivered[i]) < 1e-12 ? 0.0 : delivered[i];
            if (net(element.nodes[0]) == net(element.nodes[1]) && supplyMode[i] == 2)
                result.shorts.push_back(std::format("Short circuit: the terminals of supply {} are connected together; it limits the current to {:.3g} A",
                                                    components[element.component].id, delivered[i]));
        }
        return result;
    }

    void accumulate() {
        const auto now = mcu ? mcu->cycles() : Cycles{0};
        if (now < lastAt) {  // The MCU clock restarted without restart().
            resetAverages(now);
            return;
        }
        const auto elapsed = static_cast<double>(now - lastAt);
        if (elapsed > 0)
            for (auto& component : components)
                for (auto& entry : component.state)
                    if (entry.averaged)
                        entry.integral += entry.value * elapsed;
        lastAt = now;
    }

    void resetAverages(Cycles now) {
        lastAt = frameStart = now;
        for (auto& component : components)
            for (auto& entry : component.state)
                entry.integral = 0;
    }

    void apply(const Solution& solution) {
        voltage = solution.voltage;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            auto& element      = elements[i];
            element.conducting = solution.conducting[i];
            auto& states       = components[element.component].state;
            if (element.kind == Kind::Led) {
                states[static_cast<std::size_t>(element.stateIndex)].value = solution.value[i];
                if (element.extraIndex >= 0)
                    states[static_cast<std::size_t>(element.extraIndex)].value = solution.extra[i];
            } else if (element.kind == Kind::Potentiometer && element.extraIndex >= 0) {
                states[static_cast<std::size_t>(element.extraIndex)].value = solution.value[i];
            } else if (element.kind == Kind::Meter) {
                states[static_cast<std::size_t>(element.voltageIndex)].value = solution.value[i];
            } else if (element.kind == Kind::Load || element.kind == Kind::Supply) {
                states[static_cast<std::size_t>(element.voltageIndex)].value = solution.value[i];
                states[static_cast<std::size_t>(element.extraIndex)].value   = solution.extra[i];
                if (element.kind == Kind::Supply)
                    states[static_cast<std::size_t>(element.modeIndex)].value = solution.mode[i];
            }
        }
    }

    void drivePins(const Solution& solution, double supplyVolts) {
        for (auto& entry : chip) {
            if (!entry.pin || entry.pin->isOutput())
                continue;
            const auto target = net(entry.node);
            if (solution.driven[target]) {
                const auto volts = solution.voltage[target];
                if (entry.applied != 2 || entry.appliedVolts != volts) {
                    entry.applied      = 2;
                    entry.appliedVolts = volts;
                    entry.pin->setVoltage(volts, supplyVolts);
                }
            } else if (entry.applied != 1) {
                entry.applied = 1;
                entry.pin->setExternal(std::nullopt);
            }
        }
    }
};

Circuit::~Circuit() = default;

std::expected<std::unique_ptr<Circuit>, LoadError> Circuit::create(Json description, const Library& library) {
    if (auto valid = validateCircuit(description, library); !valid)
        return std::unexpected(valid.error());
    std::unique_ptr<Circuit> circuit{new Circuit};
    circuit->model  = std::make_unique<Model>();
    auto& model     = *circuit->model;
    model.columns    = description.at("board").at("columns").get<unsigned>();
    model.chipColumn = description.at("chip").at("column").get<unsigned>();
    const auto node  = [&](const Json& hole) { return model.node(*parseHole(hole.get<std::string>())); };

    for (const auto& wire : description.at("wires"))
        model.wires.emplace_back(node(wire.at("from")), node(wire.at("to")));
    // The interface draws and moves the wires as components of this type: it needs their footprint too.
    if (const auto* wire = library.find("wire"); wire && !description.at("wires").empty())
        model.types["wire"] = {{"name", wire->at("name")}, {"description", wire->value("description", std::string{})}, {"footprint", wire->at("footprint")}};

    for (const auto& placed : description.at("components")) {
        const auto&      type = *library.find(placed.at("type").get<std::string>());
        model.types[placed.at("type").get<std::string>()] = {
            {"name", type.at("name")}, {"description", type.value("description", std::string{})}, {"footprint", type.at("footprint")}};
        Model::Component component;
        component.id         = placed.at("id");
        component.type       = placed.at("type");
        component.properties = Json::object();
        for (const auto& [name, property] : type.at("properties").items())
            component.properties[name] = property.at("default");
        if (placed.contains("properties"))
            component.properties.update(placed.at("properties"));
        std::map<std::string, int> terminals;
        for (const auto& terminal : type.at("terminals")) {
            const std::string name = terminal.at("name");
            terminals[name]        = node(placed.at("at").at(name));
            component.terminals.push_back(terminals[name]);
        }
        for (const auto& [name, entry] : type.at("state").items()) {
            if (entry.at("type") == "bytes")
                continue;
            Model::StateEntry state;
            state.name        = name;
            state.boolean     = entry.at("type") == "boolean";
            state.interactive = entry.value("interactive", false);
            state.averaged    = name == "brightness" || name.ends_with(".brightness");
            if (entry.contains("minimum"))
                state.minimum = entry.at("minimum").get<double>();
            if (entry.contains("maximum"))
                state.maximum = entry.at("maximum").get<double>();
            if (entry.contains("default"))
                state.value = entry.at("default").is_boolean() ? (entry.at("default").get<bool>() ? 1.0 : 0.0) : entry.at("default").get<double>();
            component.state.push_back(std::move(state));
        }
        const auto stateIndex = [&](const std::string& name) {
            const auto found = std::ranges::find(component.state, name, &Model::StateEntry::name);
            return found == component.state.end() ? -1 : static_cast<int>(found - component.state.begin());
        };
        const auto number = [&](const char* name) { return component.properties.at(name).get<double>(); };
        for (const auto& described : type.at("model").at("elements")) {
            const std::string kind   = described.at("type");
            const std::string prefix = described.contains("name") ? described.at("name").get<std::string>() + "." : std::string{};
            Model::Element    element;
            element.component = model.components.size();
            for (std::size_t i = 0; i < described.at("between").size(); ++i)
                element.nodes[i] = terminals.at(described.at("between")[i].get<std::string>());
            if (kind == "resistor") {
                element.kind       = Model::Kind::Resistor;
                element.resistance = number("resistance");
            } else if (kind == "led") {
                element.kind           = Model::Kind::Led;
                element.forwardVoltage = number("forwardVoltage");
                element.nominalCurrent = number("nominalCurrent");
                element.stateIndex     = stateIndex(prefix + "brightness");
                element.extraIndex     = stateIndex(prefix + "current");
                if (described.contains("swapWhen") && component.properties.at(described.at("swapWhen").get<std::string>()).get<bool>())
                    std::swap(element.nodes[0], element.nodes[1]);
            } else if (kind == "switch") {
                element.kind       = Model::Kind::Switch;
                element.stateIndex = stateIndex(prefix + "pressed");
            } else if (kind == "potentiometer") {
                element.kind       = Model::Kind::Potentiometer;
                element.resistance = number("resistance");
                element.stateIndex = stateIndex(prefix + "position");
                element.extraIndex = stateIndex(prefix + "voltage");
            } else if (kind == "variable-resistor") {
                element.kind         = Model::Kind::Load;
                element.stateIndex   = stateIndex(prefix + "resistance");
                element.voltageIndex = stateIndex(prefix + "voltage");
                element.extraIndex   = stateIndex(prefix + "current");
            } else if (kind == "meter") {
                element.kind         = Model::Kind::Meter;
                element.voltageIndex = stateIndex(prefix + "voltage");
            } else if (kind == "supply") {
                element.kind         = Model::Kind::Supply;
                element.stateIndex   = stateIndex(prefix + "voltage");
                element.limitIndex   = stateIndex(prefix + "currentLimit");
                element.enabledIndex = stateIndex(prefix + "enabled");
                element.voltageIndex = stateIndex(prefix + "outputVoltage");
                element.extraIndex   = stateIndex(prefix + "outputCurrent");
                element.modeIndex    = stateIndex(prefix + "mode");
            } else {
                element.kind = Model::Kind::Wire;
            }
            model.elements.push_back(element);
        }
        model.components.push_back(std::move(component));
    }
    circuit->source = std::move(description);
    circuit->update();
    return circuit;
}

std::expected<void, LoadError> Circuit::attach(Mcu& mcu) {
    detach();
    const auto package = Json::parse(mcu.chipJson(), nullptr, false);
    if (!package.is_object() || !package.contains("pins"))
        return std::unexpected(LoadError{"/chip/mcu", "This microcontroller has no package description"});
    const auto count = static_cast<unsigned>(package.at("pins").size());
    const auto half  = count / 2;
    if (model->chipColumn + half - 1 > model->columns)
        return std::unexpected(LoadError{"/chip/column", std::format("A {}-pin package does not fit on the board from column {}", count, model->chipColumn)});
    for (const auto& described : package.at("pins")) {
        Model::ChipPin entry;
        entry.number      = described.at("n").get<unsigned>();
        const auto bottom = entry.number <= half;
        entry.node        = model->node(Hole{bottom ? 'e' : 'f', 0, bottom ? model->chipColumn + entry.number - 1 : model->chipColumn + count - entry.number});
        if (described.contains("port") && described.at("port").is_string()) {
            entry.name  = described.at("port");
            entry.label = described.value("label", std::string{});
            entry.pin   = mcu.pin(entry.name);
        }
        model->chip.push_back(std::move(entry));
    }
    model->mcu = &mcu;
    model->resetAverages(mcu.cycles());
    model->topologyDirty = true;
    update();

    // The pin a component is wired to: the nearest MCU signal pin, through wires and resistors, never through a rail.
    for (auto& component : model->components) {
        component.pin.clear();
        std::set<std::size_t>   seen;
        std::queue<std::size_t> pending;
        const auto              visit = [&](int nodeIndex) {
            const auto target = model->net(nodeIndex);
            if (seen.insert(target).second)
                pending.push(target);
        };
        for (const auto nodeIndex : component.terminals)
            visit(nodeIndex);
        while (!pending.empty() && component.pin.empty()) {
            const auto target = pending.front();
            pending.pop();
            bool rail = false;
            for (int i = 0; i < 4; ++i)
                rail = rail || model->net(2 * static_cast<int>(model->columns) + i) == target;
            if (rail)
                continue;
            for (const auto& entry : model->chip)
                if (entry.pin && model->net(entry.node) == target && component.pin.empty())
                    component.pin = entry.label.empty() ? entry.name : entry.label;
            for (const auto& element : model->elements)
                if (element.kind == Model::Kind::Resistor || element.kind == Model::Kind::Load) {
                    if (model->net(element.nodes[0]) == target)
                        visit(element.nodes[1]);
                    if (model->net(element.nodes[1]) == target)
                        visit(element.nodes[0]);
                }
        }
    }
    return {};
}

void Circuit::detach() {
    if (!model->mcu)
        return;
    for (auto& entry : model->chip)
        if (entry.pin && entry.applied == 2 && !entry.pin->isOutput())
            entry.pin->setExternal(std::nullopt);
    model->chip.clear();
    model->mcu           = nullptr;
    model->topologyDirty = true;
    for (auto& component : model->components)
        component.pin.clear();
    update();
}

void Circuit::update() {
    if (model->solving)
        return;
    model->solving = true;
    model->accumulate();
    if (model->topologyDirty)
        model->rebuildTopology();
    if (model->remembered.size() > maxRemembered)
        model->remembered.clear();
    const auto [found, inserted] = model->remembered.try_emplace(model->key());
    if (inserted)
        found->second = model->solve(supply);
    else
        model->fixed.clear();  // Only valid right after a solve; drive() solves again when it needs it.
    const auto& solution = found->second;
    model->apply(solution);
    shortCircuits = solution.shorts;
    model->drivePins(solution, supply);
    model->solving = false;
}

void Circuit::restart() {
    model->resetAverages(model->mcu ? model->mcu->cycles() : Cycles{0});
}

std::vector<std::string> Circuit::componentIds() const {
    std::vector<std::string> ids;
    for (const auto& component : model->components)
        ids.push_back(component.id);
    return ids;
}

bool Circuit::has(std::string_view id) const noexcept {
    return std::ranges::find(model->components, id, &Model::Component::id) != model->components.end();
}

std::optional<double> Circuit::state(std::string_view id, std::string_view name) const {
    const auto component = std::ranges::find(model->components, id, &Model::Component::id);
    if (component == model->components.end())
        return std::nullopt;
    const auto entry = std::ranges::find(component->state, name, &Model::StateEntry::name);
    return entry == component->state.end() ? std::nullopt : std::optional{entry->value};
}

bool Circuit::averaged(std::string_view id, std::string_view name) const {
    const auto component = std::ranges::find(model->components, id, &Model::Component::id);
    if (component == model->components.end())
        return false;
    const auto entry = std::ranges::find(component->state, name, &Model::StateEntry::name);
    return entry != component->state.end() && entry->averaged;
}

std::expected<void, SetError> Circuit::set(std::string_view id, std::string_view name, double value) {
    const auto component = std::ranges::find(model->components, id, &Model::Component::id);
    if (component == model->components.end())
        return std::unexpected(SetError::UnknownComponent);
    const auto entry = std::ranges::find(component->state, name, &Model::StateEntry::name);
    if (entry == component->state.end())
        return std::unexpected(SetError::UnknownState);
    if (!entry->interactive)
        return std::unexpected(SetError::NotInteractive);
    if (!std::isfinite(value) || (entry->boolean && value != 0.0 && value != 1.0) || (entry->minimum && value < *entry->minimum)
        || (entry->maximum && value > *entry->maximum))
        return std::unexpected(SetError::OutOfRange);
    if (entry->value == value)
        return {};
    model->accumulate();
    entry->value         = value;
    model->topologyDirty = true;  // A switch changes the nets; everything else only needs the memory cleared, which this does too.
    update();
    return {};
}

bool Circuit::force(std::string_view pin, std::optional<double> volts) {
    auto* entry = model->chipPin(pin);
    if (!entry)
        return false;
    if (entry->force != volts) {
        entry->force = volts;
        model->remembered.clear();
        update();
    }
    return true;
}

bool Circuit::drive(std::string_view pin, double volts) {
    auto* entry = model->chipPin(pin);
    if (!entry)
        return false;
    model->remembered.clear();
    update();  // Solves, so that the sources of every net are known.
    const auto target = model->net(entry->node);
    for (const auto& element : model->elements) {
        if (element.kind != Model::Kind::Potentiometer || model->net(element.nodes[1]) != target || model->fixed.empty())
            continue;
        const auto low  = model->fixed[model->net(element.nodes[0])];
        const auto high = model->fixed[model->net(element.nodes[2])];
        if (!low || !high || std::abs(*high - *low) < shortTolerance || model->fixed[target])
            continue;
        auto& position = model->components[element.component].state[static_cast<std::size_t>(element.stateIndex)];
        model->accumulate();
        position.value = std::clamp((volts - *low) / (*high - *low), 0.0, 1.0);
        entry->force.reset();
        model->remembered.clear();
        update();
        return true;
    }
    return force(pin, volts);
}

std::optional<double> Circuit::forced(std::string_view pin) const {
    const auto* entry = model->chipPin(pin);
    return entry ? entry->force : std::nullopt;
}

std::optional<double> Circuit::voltageAt(std::string_view hole) const {
    const auto parsed = parseHole(hole);
    if (!parsed || (!parsed->isRail() && parsed->column > model->columns))
        return std::nullopt;
    return model->voltage[model->net(model->node(*parsed))];
}

Json Circuit::view() const {
    Json result = {
        {"name", source.at("name")},
        {"description", source.value("description", std::string{})},
        {"board", source.at("board")},
        {"chip", source.at("chip")},
        {"wires", source.at("wires")},
        {"components", Json::array()},
        {"types", Json::object()}
    };
    for (std::size_t i = 0; i < model->components.size(); ++i) {
        const auto& component = model->components[i];
        const auto& placed    = source.at("components")[i];
        Json        states    = Json::object();
        for (const auto& entry : component.state)
            states[entry.name] = {{"boolean", entry.boolean}, {"interactive", entry.interactive}};
        result["components"].push_back({
            {"id", component.id},
            {"type", component.type},
            {"at", placed.at("at")},
            {"properties", component.properties},
            {"view", placed.value("view", Json::object())},
            {"pin", component.pin},
            {"state", std::move(states)}
        });
    }
    result["types"] = model->types;
    return result;
}

Json Circuit::frame() {
    model->accumulate();
    const auto span   = static_cast<double>(model->lastAt - model->frameStart);
    Json       result = Json::object();
    for (auto& component : model->components) {
        Json states = Json::object();
        for (auto& entry : component.state) {
            if (entry.boolean) {
                states[entry.name] = entry.value != 0.0;
                continue;
            }
            const auto value   = entry.averaged && span > 0 ? entry.integral / span : entry.value;
            states[entry.name] = std::round(value * 10000.0) / 10000.0;
            entry.integral     = 0;
        }
        if (!states.empty())
            result[component.id] = std::move(states);
    }
    model->frameStart = model->lastAt;
    return result;
}

}  // namespace siliconforge::circuit
