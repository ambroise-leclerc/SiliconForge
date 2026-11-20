#include "common/JsonErrors.hpp"
#include "EmbeddedScenario.hpp"
#include "Sha256.hpp"
#include "VcdClock.hpp"
#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/scenario/Scenario.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <system_error>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace siliconforge::scenario {
namespace {
using Json = nlohmann::json;
using siliconforge::detail::child;

/// No duration of a scenario may exceed one hour of simulated time.
constexpr double maximumSeconds = 3600;

[[noreturn]] void reject(Problem::Cause cause, std::string path, std::string message, std::string file = {}) {
    throw Problem{cause, std::move(file), std::move(path), std::move(message), {}};
}

std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
        return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{stream}, {}};
}

std::string hash(std::string_view bytes) {
    return scenario::detail::sha256({reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()});
}

std::string formatNumber(double value) {
    return std::format("{:.12g}", value);
}

double seconds(Cycles cycles, u64 clockHz) {
    return static_cast<double>(cycles) / static_cast<double>(clockHz);
}

/// A logged change of a pin.
using Transition = PinChange;

struct Step {
    enum class Kind { Wait, Set, WaitFor, Measure };
    enum class Condition { Pin, Serial, Component };
    Kind                  kind{Kind::Wait};
    std::string           label;
    std::optional<Cycles> at;
    Cycles                duration{0};  ///< wait, waitFor timeout, measure window.
    // set
    std::string           component;
    std::string           stateName;
    double                value{0};
    std::string           probe;
    std::optional<double> volts;
    // waitFor
    Condition             condition{Condition::Pin};
    std::size_t           pin{0};  ///< Index in Mcu::pins().
    PinState              pinState{PinState::Low};
    std::string           text;
    std::size_t           port{0};
    std::size_t           watched{0};  ///< Component condition: index in State::watchedStates.
    std::optional<double> above;
    std::optional<double> below;
    // measure
    std::string           id;
    std::string           quantity;
    std::optional<double> minimum;
    std::optional<double> maximum;
};

std::string_view kindName(Step::Kind kind) {
    switch (kind) {
        case Step::Kind::Wait:
            return "wait";
        case Step::Kind::Set:
            return "set";
        case Step::Kind::WaitFor:
            return "waitFor";
        case Step::Kind::Measure:
            return "measure";
    }
    return "";
}

std::string_view stateName(PinState state) {
    switch (state) {
        case PinState::Low:
            return "low";
        case PinState::High:
            return "high";
        case PinState::Floating:
            return "floating";
    }
    return "";
}

std::string_view stopName(StopReason reason) {
    switch (reason) {
        case StopReason::BudgetReached:
            return "budget reached";
        case StopReason::Breakpoint:
            return "breakpoint";
        case StopReason::Watchpoint:
            return "watchpoint";
        case StopReason::BreakInstruction:
            return "BREAK instruction";
        case StopReason::InvalidOpcode:
            return "invalid opcode";
        case StopReason::Requested:
            return "stop requested";
    }
    return "";
}

std::string_view unitOf(std::string_view quantity) {
    if (quantity == "dutyCycle")
        return "%";
    if (quantity == "frequency")
        return "Hz";
    return "s";
}

/// "250us", "1.5 ms", "16000 cycles" in clock cycles. The schema already checked the syntax.
Cycles parseDuration(const Json& value, const std::string& path, u64 clockHz) {
    const auto text  = value.get<std::string>();
    const auto split = text.find_first_not_of("0123456789.");
    const auto unit  = std::string_view{text}.substr(text.find_first_not_of(' ', split));
    double     number{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + split, number);
    if (error == std::errc::result_out_of_range || (error == std::errc{} && !std::isfinite(number)))
        reject(Problem::Cause::Capacity, path, std::format("{} is beyond the bench limit of {} s of simulated time", text, maximumSeconds));
    if (error != std::errc{} || end != text.data() + split)
        reject(Problem::Cause::Schema, path, std::format("{} is not a duration", text));
    double duration = 0;
    if (unit == "cycles") {
        if (number != std::floor(number))
            reject(Problem::Cause::Schema, path, "A number of cycles is an integer");
        duration = number / static_cast<double>(clockHz);
    } else
        duration = number * (unit == "ns" ? 1e-9 : unit == "us" ? 1e-6 : unit == "ms" ? 1e-3 : 1.0);
    if (duration > maximumSeconds)
        reject(Problem::Cause::Capacity, path, std::format("{} is beyond the bench limit of {} s of simulated time", text, maximumSeconds));
    return unit == "cycles" ? static_cast<Cycles>(number) : static_cast<Cycles>(std::llround(duration * static_cast<double>(clockHz)));
}

std::optional<std::size_t> pinIndex(Mcu& mcu, std::string_view name) {
    auto* pin = mcu.pin(name);
    if (!pin)
        return std::nullopt;
    return static_cast<std::size_t>(pin - mcu.pins().data());
}

double numericValue(const Json& value) {
    return value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0) : value.get<double>();
}

/// What the scenario refers to, resolved against an MCU plugged in its circuit.
class Checker {
public:
    Checker(Mcu& mcu, circuit::Circuit& model, const Json& circuitDescription, std::string circuitFile)
        : mcu(mcu), model(model), description(circuitDescription), circuitFile(std::move(circuitFile)) {}

    /// The description of a state entry an interactive change may target, checked against the type's bounds.
    void setting(const std::string& id, const std::string& name, const Json& value, const std::string& path) {
        const auto& entry  = interactiveState(id, name, path);
        const auto  number = numericValue(value);
        if (entry.at("type") == "boolean") {
            if (!value.is_boolean() && number != 0.0 && number != 1.0)
                reject(Problem::Cause::Capacity, path, std::format("{}.{} is a boolean: true, false, 0 or 1", id, name));
            return;
        }
        if (value.is_boolean())
            reject(Problem::Cause::Schema, path, std::format("{}.{} is a number", id, name));
        const auto minimum = entry.contains("minimum") ? std::optional{entry.at("minimum").get<double>()} : std::nullopt;
        const auto maximum = entry.contains("maximum") ? std::optional{entry.at("maximum").get<double>()} : std::nullopt;
        if ((minimum && number < *minimum) || (maximum && number > *maximum))
            reject(Problem::Cause::Capacity,
                   path,
                   std::format("{} is outside the range of {}.{}: [{}, {}]",
                               formatNumber(number),
                               id,
                               name,
                               minimum ? formatNumber(*minimum) : "-inf",
                               maximum ? formatNumber(*maximum) : "+inf"));
    }

    void component(const std::string& id, const std::string& path) {
        if (!model.has(id))
            reject(Problem::Cause::Resource, path, std::format("The circuit {} has no component {}", circuitFile, id));
    }

    /// A state a bounded wait can watch: its present value, not a time average.
    void observable(const std::string& id, const std::string& name, const std::string& path) {
        component(id, path);
        if (!model.state(id, name))
            reject(Problem::Cause::Resource, path, std::format("Component {} has no state {}", id, name));
        if (model.averaged(id, name))
            reject(Problem::Cause::Capacity,
                   path,
                   std::format("{}.{} is a time average: its present value follows every pulse of a PWM output, so a wait could not tell "
                               "the average reached. Measure the duty cycle of the pin that drives it, or wait for the level of that pin",
                               id,
                               name));
    }

    std::size_t pin(const std::string& name, const std::string& path) {
        const auto index = pinIndex(mcu, name);
        if (!index)
            reject(Problem::Cause::Resource, path, std::format("{} has no pin {}", mcu.name(), name));
        return *index;
    }

    /// A probe on a pin of the chip: the pin is on the board and the voltage within the supply.
    void probe(const std::string& name, std::optional<double> volts, const std::string& path, const std::string& voltsPath) {
        pin(name, path);
        if (!model.force(name, std::nullopt))
            reject(Problem::Cause::Wiring, path, std::format("Pin {} is not on the board", name));
        if (volts && (*volts < 0 || *volts > model.supplyVoltage()))
            reject(Problem::Cause::Capacity,
                   voltsPath,
                   std::format("A probe applies 0 to {} V (the supply of the circuit), not {} V", formatNumber(model.supplyVoltage()), formatNumber(*volts)));
    }

private:
    const Json& interactiveState(const std::string& id, const std::string& name, const std::string& path) {
        component(id, path);
        const auto& components = description.at("components");
        const auto  found      = std::ranges::find_if(components, [&](const Json& entry) {
            return entry.at("id").get<std::string>() == id;
        });
        if (found == components.end())
            reject(Problem::Cause::Resource, path, std::format("{} is not a component of the circuit {}", id, circuitFile));
        const auto* type = circuit::Library::builtin().find(found->at("type").get<std::string>());
        if (!type || !type->at("state").contains(name))
            reject(Problem::Cause::Resource, path, std::format("Component {} has no state {}", id, name));
        const auto& entry = type->at("state").at(name);
        if (!entry.value("interactive", false))
            reject(Problem::Cause::Resource, path, std::format("State {} of component {} cannot be changed from outside", name, id));
        return entry;
    }

    Mcu&              mcu;
    circuit::Circuit& model;
    const Json&       description;
    std::string       circuitFile;
};

/// The expected bounds of a measurement, checked against what the instrument can resolve over its window.
void expectation(Step& step, const Json& expect, const std::string& path, u64 clockHz) {
    if (expect.contains("value")) {
        const auto value     = expect.at("value").get<double>();
        const auto tolerance = expect.at("tolerance").get<double>();
        step.minimum         = value - tolerance;
        step.maximum         = value + tolerance;
    } else {
        step.minimum = expect.at("min").get<double>();
        step.maximum = expect.at("max").get<double>();
        if (*step.minimum > *step.maximum)
            reject(Problem::Cause::Schema, path + "/min", "min is above max");
    }
    const auto clock = static_cast<double>(clockHz);
    if (step.quantity == "dutyCycle") {
        const auto resolution = 100.0 / static_cast<double>(step.duration);
        if ((*step.maximum - *step.minimum) / 2 < resolution)
            reject(Problem::Cause::Capacity,
                   path,
                   std::format("A duty cycle over {} cycles is resolved to {:.3g} %: the tolerance cannot be smaller", step.duration, resolution));
        return;
    }
    // Refused only when no expected value can be measured. A pin changes at most once per cycle: nothing above half
    // the clock. Two rising edges one period apart fit, with the right phase, in a closed window of one period: a
    // window shorter than the period of the highest expected frequency never holds two. Whether a window caught its
    // edges is then for the measurement to tell.
    const auto lowest  = step.quantity == "frequency" ? *step.minimum : (*step.maximum > 0 ? 1 / *step.maximum : INFINITY);
    const auto highest = step.quantity == "frequency" ? *step.maximum : (*step.minimum > 0 ? 1 / *step.minimum : INFINITY);
    if (lowest > clock / 2)
        reject(Problem::Cause::Capacity, path, std::format("A pin of a {} Hz MCU cannot toggle above {} Hz", clockHz, formatNumber(clock / 2)));
    if (highest > 0 && static_cast<double>(step.duration) < clock / highest)
        reject(Problem::Cause::Capacity,
               path,
               std::format("The window of {} cycles is shorter than one period of a {} Hz signal ({:.6g} cycles), the highest expected: it can "
                           "never hold two rising edges",
                           step.duration,
                           formatNumber(highest),
                           clock / highest));
}

/// A pin measured over [from, to] from its logged changes and its state at reset.
void measure(Measurement& result, const std::vector<Transition>& changes, PinState initial, u64 clockHz) {
    // The window is closed, [from, to]: a change at `from` belongs to it, and so does an edge there.
    auto prior  = initial;  // Just before `from`.
    auto cursor = changes.begin();
    for (; cursor != changes.end() && cursor->at < result.from; ++cursor)
        prior = cursor->state;
    std::vector<Transition> inside;
    for (; cursor != changes.end() && cursor->at <= result.to; ++cursor)
        inside.push_back(*cursor);
    auto start = prior;  // At `from`, once its own changes are applied.
    for (const auto& change : inside)
        if (change.at == result.from)
            start = change.state;
    const auto constant = std::ranges::none_of(inside, [&](const Transition& change) {
        return change.at > result.from;
    });
    // A rising edge goes from low to high. A pin that floats has no defined level: neither its edges nor its time
    // count as a signal, so a window where it floats, even briefly, cannot be measured.
    std::vector<std::size_t> rising;
    auto                     before = prior;
    for (std::size_t i = 0; i < inside.size(); ++i) {
        if (before == PinState::Low && inside[i].state == PinState::High)
            rising.push_back(i);
        before = inside[i].state;
    }
    result.risingEdges = static_cast<unsigned>(rising.size());

    if (start == PinState::Floating && constant) {
        result.detail = "The pin floats during the whole window";
        return;
    }
    if (start == PinState::Floating || std::ranges::any_of(inside, [](const Transition& change) {
            return change.state == PinState::Floating;
        })) {
        result.detail = "The pin floats during part of the window: its level is not defined there";
        return;
    }
    if (constant && result.quantity == "dutyCycle") {
        result.value = start == PinState::High ? 100.0 : 0.0;
        return;
    }
    if (rising.size() < 2) {
        result.detail = std::format("{} rising edge{} in the window: at least two are needed", rising.size(), rising.size() == 1 ? "" : "s");
        return;
    }
    const auto first = inside[rising.front()].at;
    const auto last  = inside[rising.back()].at;
    // Several stimuli applied at one boundary can give rising edges on the same cycle: periods of no duration.
    if (last == first) {
        result.detail = std::format("The {} rising edges in the window all fall on cycle {}: they span no time", rising.size(), first);
        return;
    }
    const auto span = static_cast<double>(last - first);
    if (result.quantity == "dutyCycle") {
        Cycles high = 0;
        for (auto i = rising.front(); i < rising.back(); ++i)
            if (inside[i].state == PinState::High)
                high += inside[i + 1].at - inside[i].at;
        result.value = 100.0 * static_cast<double>(high) / span;
    } else {
        const auto frequency = static_cast<double>(rising.size() - 1) * static_cast<double>(clockHz) / span;
        result.value         = result.quantity == "frequency" ? frequency : 1 / frequency;
    }
}

}  // namespace

void measurePin(Measurement& result, const std::vector<PinChange>& changes, PinState initial, u64 clockHz) {
    result.unit = std::string{unitOf(result.quantity)};
    measure(result, changes, initial, clockHz);
}

namespace {

std::string vcd(Mcu& mcu, const std::vector<PinState>& initial, const std::vector<std::vector<Transition>>& history, Cycles end) {
    const detail::VcdClock clock{mcu.clockHz()};
    std::string            out = std::format("$comment SiliconForge bench trace, {} Hz clock{} $end\n",
                                  mcu.clockHz(),
                                  clock.exact() ? "" : ": times rounded to the femtosecond");
    out                       += std::format("$timescale {} $end\n", clock.timescale());
    out                       += "$scope module mcu $end\n";
    const auto pins            = mcu.pins();
    const auto code            = [](std::size_t index) {
        std::string id;
        do {
            id    += static_cast<char>(33 + index % 94);
            index /= 94;
        } while (index > 0);
        return id;
    };
    for (std::size_t i = 0; i < pins.size(); ++i) {
        auto name = pins[i].label().empty() ? pins[i].name() : pins[i].label() + "_" + pins[i].name();
        std::ranges::replace(name, ' ', '_');
        out += std::format("$var wire 1 {} {} $end\n", code(i), name);
    }
    out                              += "$upscope $end\n$enddefinitions $end\n#0\n";
    constexpr std::string_view levels = "01z";
    for (std::size_t i = 0; i < pins.size(); ++i)
        out += std::format("{}{}\n", levels[static_cast<std::size_t>(initial[i])], code(i));
    std::vector<std::pair<Cycles, std::string>> events;
    for (std::size_t i = 0; i < history.size(); ++i)
        for (const auto& change : history[i])
            events.emplace_back(change.at, std::format("{}{}", levels[static_cast<std::size_t>(change.state)], code(i)));
    std::ranges::stable_sort(events, {}, &std::pair<Cycles, std::string>::first);
    u64 time = 0;
    for (const auto& [at, change] : events) {
        if (clock.time(at) != time)
            out += std::format("#{}\n", time = clock.time(at));
        out += change + "\n";
    }
    if (clock.time(end) != time)
        out += std::format("#{}\n", clock.time(end));
    return out;
}

std::string base64(std::string_view bytes) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string                out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto count = std::min<std::size_t>(3, bytes.size() - i);
        u32        group = 0;
        for (std::size_t k = 0; k < 3; ++k)
            group = group << 8 | (k < count ? static_cast<u8>(bytes[i + k]) : 0u);
        for (std::size_t k = 0; k < 4; ++k)
            out += k <= count ? alphabet[group >> (18 - 6 * k) & 0x3F] : '=';
    }
    return out;
}

/// The bytes as UTF-8 text: valid sequences kept, every other byte replaced by U+FFFD.
std::string readableText(std::string_view bytes) {
    std::string out;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto lead   = static_cast<u8>(bytes[i]);
        const auto length = lead < 0x80 ? 1u : (lead >> 5) == 0x6 ? 2u : (lead >> 4) == 0xE ? 3u : (lead >> 3) == 0x1E ? 4u : 0u;
        bool       valid  = length != 0 && i + length <= bytes.size();
        u32        code   = length == 1 ? lead : length == 2 ? lead & 0x1Fu : length == 3 ? lead & 0x0Fu : lead & 0x07u;
        for (std::size_t k = 1; valid && k < length; ++k) {
            const auto next = static_cast<u8>(bytes[i + k]);
            valid           = (next & 0xC0) == 0x80;
            code            = code << 6 | (next & 0x3Fu);
        }
        // No overlong form, no surrogate, nothing beyond U+10FFFF.
        constexpr std::array<u32, 5> smallest{0, 0, 0x80, 0x800, 0x10000};
        valid = valid && code >= smallest[length] && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF);
        if (valid) {
            out.append(bytes.substr(i, length));
            i += length;
        } else {
            out += "\xEF\xBF\xBD";
            ++i;
        }
    }
    return out;
}

Json timeJson(Cycles cycles, u64 clockHz) {
    return {
        {"cycles", cycles},
        {"seconds", seconds(cycles, clockHz)}
    };
}
}  // namespace

std::string_view engineVersion() noexcept {
    return SILICONFORGE_VERSION;
}

std::string_view toString(Problem::Cause cause) noexcept {
    switch (cause) {
        case Problem::Cause::Unreadable:
            return "unreadable";
        case Problem::Cause::Version:
            return "version";
        case Problem::Cause::Schema:
            return "schema";
        case Problem::Cause::Wiring:
            return "wiring";
        case Problem::Cause::Resource:
            return "resource";
        case Problem::Cause::Capacity:
            return "capacity";
        case Problem::Cause::Schedule:
            return "schedule";
    }
    return "";
}

std::string_view toString(Verdict verdict) noexcept {
    switch (verdict) {
        case Verdict::Pass:
            return "pass";
        case Verdict::Fail:
            return "fail";
        case Verdict::Incompatible:
            return "incompatible";
    }
    return "";
}

int exitCode(Verdict verdict) noexcept {
    switch (verdict) {
        case Verdict::Pass:
            return 0;
        case Verdict::Fail:
            return 1;
        case Verdict::Incompatible:
            return 2;
    }
    return 2;
}

std::string_view toString(StepRecord::Outcome outcome) noexcept {
    switch (outcome) {
        case StepRecord::Outcome::Done:
            return "done";
        case StepRecord::Outcome::Failed:
            return "failed";
        case StepRecord::Outcome::Skipped:
            return "skipped";
    }
    return "";
}

struct Scenario::State {
    std::string                                               name;
    std::vector<Input>                                        inputs;
    std::string                                               circuitFile;
    Json                                                      circuitDescription;
    std::string                                               mcuName;
    ProgramImage                                              image;
    std::vector<std::tuple<std::string, std::string, double>> initialComponents;
    std::vector<std::pair<std::string, double>>               initialProbes;
    std::vector<Step>                                         steps;

    // One run.
    std::unique_ptr<Mcu>                 device;
    std::unique_ptr<circuit::Circuit>    model;
    std::vector<std::vector<Transition>> history;
    std::vector<std::vector<Cycles>>     transmitted;  ///< Per serial port, the cycle of each byte of output().
    /// The condition of the waitFor in progress, evaluated by the observers at the cycle of each change, so that a
    /// state reached and left between two instruction boundaries is not missed.
    std::function<bool()> watch;
    std::optional<Cycles> satisfiedAt;
    /// The component states a waitFor of the scenario watches, and their values at each change since reset: what a
    /// step that begins after the core went past its start needs to know the value at its start.
    std::vector<std::pair<std::string, std::string>>    watchedStates;
    std::vector<std::vector<std::pair<Cycles, double>>> stateHistory;

    void recordStates(Cycles at) {
        for (std::size_t k = 0; k < watchedStates.size(); ++k) {
            const auto value = model->state(watchedStates[k].first, watchedStates[k].second).value_or(0);
            if (stateHistory[k].empty() || stateHistory[k].back().second != value)
                stateHistory[k].emplace_back(at, value);
        }
    }

    void notice(Cycles at) {
        if (watch && !satisfiedAt && watch()) {
            satisfiedAt = at;
            device->debugger().requestStop();
        }
    }

    /// A fresh MCU plugged in a fresh circuit, with the initial state applied and the firmware loaded.
    void setUp() {
        model.reset();
        device       = mcu::create(mcuName);
        auto created = circuit::Circuit::create(circuitDescription);
        model        = std::move(*created);
        (void)model->attach(*device);
        history.assign(device->pins().size(), {});
        for (auto& pin : device->pins()) {
            const auto index = static_cast<std::size_t>(&pin - device->pins().data());
            pin.subscribe([this, index](const Pin& changed, Cycles at) {
                history[index].push_back({at, changed.state()});
                notice(at);
            });
            pin.subscribeDrive([this](const Pin&, Cycles at) {
                if (model) {
                    model->update();
                    recordStates(at);
                }
                notice(at);
            });
        }
        transmitted.assign(device->serialPorts().size(), {});
        for (std::size_t port = 0; port < transmitted.size(); ++port)
            device->serialPorts()[port].onTransmit([this, port](u8, Cycles at) {
                transmitted[port].push_back(at);
                notice(at);
            });
        for (const auto& [id, entry, value] : initialComponents)
            (void)model->set(id, entry, value);
        for (const auto& [pin, volts] : initialProbes)
            model->force(pin, volts);
        device->load(image);
        model->restart();
        for (auto& changes : history)
            changes.clear();
        for (auto& log : transmitted)
            log.clear();
        stateHistory.assign(watchedStates.size(), {});
        recordStates(0);
        watch = {};
        satisfiedAt.reset();
    }

    void parse(const Json& document, const std::filesystem::path& directory);
};

void Scenario::State::parse(const Json& document, const std::filesystem::path& directory) {
    name = document.at("name").get<std::string>();

    // Circuit, then the MCU its chip names, then the firmware.
    circuitFile            = document.at("circuit").get<std::string>();
    const auto circuitText = readFile(directory / circuitFile);
    if (!circuitText)
        reject(Problem::Cause::Unreadable, "/circuit", std::format("Cannot read the circuit {}", circuitFile));
    inputs.push_back({"circuit", circuitFile, hash(*circuitText)});
    auto description = circuit::parseDescription(*circuitText);
    if (!description)
        reject(Problem::Cause::Wiring, description.error().path, description.error().message, circuitFile);
    circuitDescription = std::move(*description);
    mcuName            = circuitDescription.at("chip").at("mcu").get<std::string>();
    auto checkMcu      = mcu::create(mcuName);
    if (!checkMcu) {
        std::string known;
        for (const auto available : mcu::available())
            known += std::format("{}{}", known.empty() ? "" : ", ", available);
        reject(Problem::Cause::Resource, "/chip/mcu", std::format("Unknown microcontroller {} (available: {})", mcuName, known), circuitFile);
    }
    const auto firmwareFile = document.at("firmware").get<std::string>();
    const auto firmwareData = readFile(directory / firmwareFile);
    if (!firmwareData)
        reject(Problem::Cause::Unreadable, "/firmware", std::format("Cannot read the firmware {}", firmwareFile));
    inputs.push_back({"firmware", firmwareFile, hash(*firmwareData)});
    auto loaded = loadProgram(directory / firmwareFile);
    if (!loaded)
        reject(Problem::Cause::Unreadable, loaded.error().line ? std::format("/line/{}", loaded.error().line) : "", loaded.error().message, firmwareFile);
    image = std::move(*loaded);
    // The device checks every segment against its memories before it changes anything.
    try {
        checkMcu->load(image);
    } catch (const std::exception& error) {
        Address end = 0;
        for (const auto& segment : image.segments)
            end = std::max<Address>(end, segment.address + static_cast<Address>(segment.bytes.size()));
        reject(Problem::Cause::Capacity,
               "",
               std::format("{} cannot load this firmware (it ends at {:#x}): {}", checkMcu->name(), end, error.what()),
               firmwareFile);
    }

    // The chip on the board.
    auto checkModel = circuit::Circuit::create(circuitDescription);
    if (!checkModel)
        reject(Problem::Cause::Wiring, checkModel.error().path, checkModel.error().message, circuitFile);
    if (auto attached = (*checkModel)->attach(*checkMcu); !attached)
        reject(Problem::Cause::Wiring, attached.error().path, attached.error().message, circuitFile);
    const auto clockHz = checkMcu->clockHz();
    Checker    check{*checkMcu, **checkModel, circuitDescription, circuitFile};

    if (document.contains("initial")) {
        const auto& initial = document.at("initial");
        for (const auto& [id, entries] : initial.value("components", Json::object()).items())
            for (const auto& [entry, value] : entries.items()) {
                const auto path = child(child("/initial/components", id), entry);
                check.setting(id, entry, value, path);
                initialComponents.emplace_back(id, entry, numericValue(value));
            }
        for (const auto& [pin, volts] : initial.value("probes", Json::object()).items()) {
            const auto path = child("/initial/probes", pin);
            check.probe(pin, volts.get<double>(), path, path);
            initialProbes.emplace_back(pin, volts.get<double>());
        }
    }

    // Steps: references, capacity, and a schedule the previous steps can keep (their shortest duration).
    std::set<std::string> ids;
    Cycles                earliest = 0;
    const auto&           list     = document.at("steps");
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& entry = list[i];
        const auto  path  = std::format("/steps/{}", i);
        Step        step;
        step.label = entry.value("label", "");
        if (entry.contains("at")) {
            step.at = parseDuration(entry.at("at"), path + "/at", clockHz);
            if (*step.at < earliest)
                reject(Problem::Cause::Schedule,
                       path + "/at",
                       std::format("Step {} is scheduled at {} cycles, but the steps before it last at least {} cycles", i, *step.at, earliest));
            earliest = *step.at;
        }
        if (entry.contains("wait")) {
            step.kind     = Step::Kind::Wait;
            step.duration = parseDuration(entry.at("wait"), path + "/wait", clockHz);
            earliest     += step.duration;
        } else if (entry.contains("set")) {
            step.kind       = Step::Kind::Set;
            const auto& set = entry.at("set");
            if (set.contains("component")) {
                step.component = set.at("component").get<std::string>();
                step.stateName = set.at("state").get<std::string>();
                check.setting(step.component, step.stateName, set.at("value"), path + "/set");
                step.value = numericValue(set.at("value"));
            } else {
                step.probe = set.at("probe").get<std::string>();
                if (!set.at("volts").is_null())
                    step.volts = set.at("volts").get<double>();
                check.probe(step.probe, step.volts, path + "/set/probe", path + "/set/volts");
            }
        } else if (entry.contains("waitFor")) {
            step.kind             = Step::Kind::WaitFor;
            step.duration         = parseDuration(entry.at("timeout"), path + "/timeout", clockHz);
            const auto& waitFor   = entry.at("waitFor");
            const auto  wherePath = path + "/waitFor";
            if (step.duration == 0)
                reject(Problem::Cause::Schema, path + "/timeout", "A bounded wait needs a timeout above zero");
            if (waitFor.contains("pin")) {
                step.condition    = Step::Condition::Pin;
                step.pin          = check.pin(waitFor.at("pin").get<std::string>(), wherePath + "/pin");
                const auto wanted = waitFor.at("state").get<std::string>();
                step.pinState     = wanted == "high" ? PinState::High : wanted == "low" ? PinState::Low : PinState::Floating;
            } else if (waitFor.contains("serial")) {
                step.condition = Step::Condition::Serial;
                step.text      = waitFor.at("serial").get<std::string>();
                step.port      = waitFor.value("port", std::size_t{0});
                if (!checkMcu->serial(step.port))
                    reject(Problem::Cause::Resource, wherePath + "/port", std::format("{} has no serial port {}", checkMcu->name(), step.port));
            } else {
                step.condition = Step::Condition::Component;
                step.component = waitFor.at("component").get<std::string>();
                step.stateName = waitFor.at("state").get<std::string>();
                check.observable(step.component, step.stateName, wherePath);
                const auto watchedState = std::pair{step.component, step.stateName};
                const auto found        = std::ranges::find(watchedStates, watchedState);
                step.watched            = static_cast<std::size_t>(found - watchedStates.begin());
                if (found == watchedStates.end())
                    watchedStates.push_back(watchedState);
                if (waitFor.contains("above"))
                    step.above = waitFor.at("above").get<double>();
                if (waitFor.contains("below"))
                    step.below = waitFor.at("below").get<double>();
            }
        } else {
            step.kind             = Step::Kind::Measure;
            const auto& measure   = entry.at("measure");
            const auto  wherePath = path + "/measure";
            step.id               = measure.at("id").get<std::string>();
            if (!ids.insert(step.id).second)
                reject(Problem::Cause::Schema, wherePath + "/id", std::format("Measurement {} is already defined", step.id));
            step.pin      = check.pin(measure.at("pin").get<std::string>(), wherePath + "/pin");
            step.quantity = measure.at("quantity").get<std::string>();
            step.duration = parseDuration(measure.at("window"), wherePath + "/window", clockHz);
            if (step.duration == 0)
                reject(Problem::Cause::Capacity, wherePath + "/window", "A measurement window cannot be empty");
            if (measure.contains("expect"))
                expectation(step, measure.at("expect"), wherePath + "/expect", clockHz);
            earliest += step.duration;
        }
        steps.push_back(std::move(step));
    }
}

namespace {
/// Reads, checks and resolves a scenario; collects the identity of every input it could read.
std::unique_ptr<Scenario::State> load(std::string_view text, const std::filesystem::path& directory, std::vector<Input>& inputs) {
    Json document;
    try {
        document = Json::parse(text);
    } catch (const Json::parse_error& error) {
        reject(Problem::Cause::Unreadable, "", std::format("Not a JSON document: {}", error.what()));
    }
    if (!document.is_object())
        reject(Problem::Cause::Schema, "", "A scenario is a JSON object");
    if (const auto version = document.find("formatVersion"); version != document.end() && *version != Json(formatVersion))
        reject(Problem::Cause::Version,
               "/formatVersion",
               std::format("Unsupported scenario formatVersion {}: this engine ({}) reads version {}", version->dump(), engineVersion(), formatVersion));
    static const nlohmann::json_schema::json_validator validator{Json::parse(embedded::scenarioSchema)};
    siliconforge::detail::SchemaErrors                 errors;
    validator.validate(document, errors);
    if (errors.first)
        reject(Problem::Cause::Schema, errors.first->path, errors.first->message);

    auto state = std::make_unique<Scenario::State>();
    try {
        state->parse(document, directory);
    } catch (const Problem&) {
        inputs.insert(inputs.end(), state->inputs.begin(), state->inputs.end());
        throw;
    }
    inputs.insert(inputs.end(), state->inputs.begin(), state->inputs.end());
    state->inputs = inputs;
    return state;
}
}  // namespace

Scenario::Scenario()  = default;
Scenario::~Scenario() = default;

const std::string& Scenario::name() const noexcept {
    return state->name;
}

std::expected<std::unique_ptr<Scenario>, Problem> Scenario::prepare(const std::filesystem::path& file) {
    const auto text = readFile(file);
    if (!text)
        return std::unexpected(Problem{Problem::Cause::Unreadable, {}, {}, std::format("Cannot read the scenario {}", file.string()), {}});
    std::vector<Input> inputs{
        {"scenario", file.generic_string(), hash(*text)}
    };
    try {
        std::unique_ptr<Scenario> scenario{new Scenario};
        scenario->state = load(*text, file.parent_path(), inputs);
        return scenario;
    } catch (Problem& problem) {
        problem.inputs = std::move(inputs);
        return std::unexpected(std::move(problem));
    }
}

std::expected<std::unique_ptr<Scenario>, Problem> Scenario::prepare(std::string_view text, const std::filesystem::path& directory, std::string name) {
    std::vector<Input> inputs{
        {"scenario", std::move(name), hash(text)}
    };
    try {
        std::unique_ptr<Scenario> scenario{new Scenario};
        scenario->state = load(text, directory, inputs);
        return scenario;
    } catch (Problem& problem) {
        problem.inputs = std::move(inputs);
        return std::unexpected(std::move(problem));
    }
}

Report Scenario::run() {
    auto& s = *state;
    s.setUp();
    auto& device = *s.device;
    auto& model  = *s.model;

    Report report;
    report.verdict = Verdict::Pass;
    report.name    = s.name;
    report.inputs  = s.inputs;
    report.mcu     = std::string{device.name()};
    report.clockHz = device.clockHz();
    std::vector<PinState> initial;
    for (const auto& pin : device.pins())
        initial.push_back(pin.state());

    const auto stopped = [&](StopReason reason) {
        const auto symbol = device.symbolAt(device.programCounter());
        return std::format("The MCU stopped ({}) at PC {:#06x}{}{}", stopName(reason), device.programCounter(), symbol.empty() ? "" : " in ", symbol);
    };
    // Runs until `target`; why the MCU stopped before, if it did.
    const auto advanceTo = [&](Cycles target) -> std::optional<std::string> {
        while (device.cycles() < target) {
            const auto result = device.run(target - device.cycles());
            if (result.reason != StopReason::BudgetReached)
                return stopped(result.reason);
        }
        return std::nullopt;
    };

    // The steps follow the scheduled timeline, in cycles. The core acts at instruction boundaries, so a run can end a
    // few cycles past its target: the timeline does not inherit that overshoot, the measurements use the logged changes
    // at their exact cycles, and a stimulus reports the boundary where it took effect.
    Cycles                     timeline = 0;
    std::optional<std::size_t> failedStep;
    for (std::size_t i = 0; i < s.steps.size(); ++i) {
        const auto& step  = s.steps[i];
        const auto  begin = step.at.value_or(timeline);
        StepRecord  record{i, std::string{kindName(step.kind)}, step.label, StepRecord::Outcome::Skipped, begin, begin, std::nullopt, {}};
        if (failedStep) {
            record.start = record.end = timeline;
            record.detail             = std::format("Not run: step {} failed", *failedStep);
            report.steps.push_back(std::move(record));
            continue;
        }
        std::optional<std::string> failure;
        if (begin < timeline) {
            record.start = record.end = timeline;
            failure                   = std::format("Scheduled at {} cycles, but the steps before it ended at {} cycles", begin, timeline);
        } else
            failure = advanceTo(begin);
        if (!failure) {
            switch (step.kind) {
                case Step::Kind::Wait:
                    record.end = begin + step.duration;
                    failure    = advanceTo(record.end);
                    break;
                case Step::Kind::Set:
                    record.applied = device.cycles();
                    if (step.probe.empty()) {
                        (void)model.set(step.component, step.stateName, step.value);
                        record.detail = std::format("{}.{} = {}", step.component, step.stateName, formatNumber(step.value));
                    } else {
                        model.force(step.probe, step.volts);
                        record.detail = step.volts ? std::format("probe on {} at {} V", step.probe, formatNumber(*step.volts))
                                                   : std::format("probe on {} removed", step.probe);
                    }
                    s.recordStates(*record.applied);
                    if (*record.applied != begin)
                        record.detail += std::format(", applied at {} cycles: the instruction in progress at {} ends first", *record.applied, begin);
                    break;
                case Step::Kind::WaitFor: {
                    auto&      pin      = device.pins()[step.pin];
                    const auto deadline = begin + step.duration;
                    const auto describe = [&] {
                        switch (step.condition) {
                            case Step::Condition::Pin:
                                return std::format("{} {}", pin.label().empty() ? pin.name() : pin.label(), stateName(step.pinState));
                            case Step::Condition::Serial:
                                return std::format("Serial text \"{}\"", step.text);
                            case Step::Condition::Component:
                                break;
                        }
                        return std::format("{}.{}{}{}",
                                           step.component,
                                           step.stateName,
                                           step.above ? std::format(" > {}", formatNumber(*step.above)) : "",
                                           step.below ? std::format(" < {}", formatNumber(*step.below)) : "");
                    };
                    // Serial text emitted since the step began: from the first byte logged at or after `begin`. The
                    // match ends at the cycle of its last byte.
                    const auto serialMatch = [&]() -> std::optional<Cycles> {
                        const auto& log   = s.transmitted[step.port];
                        const auto  first = static_cast<std::size_t>(std::ranges::lower_bound(log, begin) - log.begin());
                        const auto  found = device.serial(step.port)->output().find(step.text, first);
                        if (found == std::string::npos)
                            return std::nullopt;
                        return log[found + step.text.size() - 1];
                    };
                    const auto componentHolds = [&] {
                        const auto value = model.state(step.component, step.stateName).value_or(0);
                        return (!step.above || value > *step.above) && (!step.below || value < *step.below);
                    };
                    // First what happened between `begin` and the boundary where the core stands, from the logs; then
                    // the observers watch every change until the deadline.
                    std::optional<Cycles> satisfied;
                    switch (step.condition) {
                        case Step::Condition::Pin: {
                            const auto& changes = s.history[step.pin];
                            auto        level   = initial[step.pin];
                            auto        cursor  = changes.begin();
                            for (; cursor != changes.end() && cursor->at <= begin; ++cursor)
                                level = cursor->state;
                            if (level == step.pinState)
                                satisfied = begin;
                            for (; !satisfied && cursor != changes.end(); ++cursor)
                                if (cursor->state == step.pinState)
                                    satisfied = cursor->at;
                            s.watch = [&] {
                                return pin.state() == step.pinState;
                            };
                            break;
                        }
                        case Step::Condition::Serial:
                            satisfied = serialMatch();
                            s.watch   = [&] {
                                return serialMatch().has_value();
                            };
                            break;
                        case Step::Condition::Component: {
                            const auto& values = s.stateHistory[step.watched];
                            const auto  holds  = [&](double value) {
                                return (!step.above || value > *step.above) && (!step.below || value < *step.below);
                            };
                            auto cursor = values.begin();
                            auto value  = cursor->second;  // Recorded at reset.
                            for (; cursor != values.end() && cursor->first <= begin; ++cursor)
                                value = cursor->second;
                            if (holds(value))
                                satisfied = begin;
                            for (; !satisfied && cursor != values.end(); ++cursor)
                                if (holds(cursor->second))
                                    satisfied = cursor->first;
                            s.watch = componentHolds;
                            break;
                        }
                    }
                    s.satisfiedAt = satisfied;
                    while (!s.satisfiedAt && device.cycles() < deadline) {
                        const auto result = device.run(deadline - device.cycles());
                        if (result.reason != StopReason::BudgetReached && result.reason != StopReason::Requested) {
                            failure = stopped(result.reason);
                            break;
                        }
                    }
                    s.watch = {};
                    (void)device.debugger().takeStopRequest();  // A condition met by the last event of a run left one
                    if (!failure) {
                        if (s.satisfiedAt && *s.satisfiedAt <= deadline) {
                            record.end    = std::max(*s.satisfiedAt, begin);
                            record.detail = std::format("Satisfied at {} cycles, {} cycles after the step began", record.end, record.end - begin);
                        } else {
                            record.end = deadline;
                            failure =
                                std::format("{} not reached within {} cycles ({} s)", describe(), step.duration, seconds(step.duration, device.clockHz()));
                        }
                    }
                    s.satisfiedAt.reset();
                    break;
                }
                case Step::Kind::Measure: {
                    Measurement measurement;
                    measurement.id       = step.id;
                    measurement.label    = step.label;
                    measurement.pin      = device.pins()[step.pin].label().empty() ? device.pins()[step.pin].name() : device.pins()[step.pin].label();
                    measurement.quantity = step.quantity;
                    measurement.unit     = std::string{unitOf(step.quantity)};
                    measurement.minimum  = step.minimum;
                    measurement.maximum  = step.maximum;
                    measurement.from     = begin;
                    measurement.to       = begin + step.duration;
                    record.end           = measurement.to;
                    failure              = advanceTo(measurement.to);
                    if (failure)
                        break;
                    measure(measurement, s.history[step.pin], initial[step.pin], device.clockHz());
                    if (measurement.value && !std::isfinite(*measurement.value)) {  // JSON has no infinity: never report one
                        measurement.detail = std::format("The measurement gives {}: it is not a value", *measurement.value);
                        measurement.value.reset();
                    }
                    if (step.minimum) {
                        measurement.passed = measurement.value && *measurement.value >= *step.minimum && *measurement.value <= *step.maximum;
                        if (!measurement.passed && measurement.value)
                            measurement.detail = std::format("{:.6g} {} is outside [{}, {}] {}",
                                                             *measurement.value,
                                                             measurement.unit,
                                                             formatNumber(*step.minimum),
                                                             formatNumber(*step.maximum),
                                                             measurement.unit);
                    }
                    record.detail = measurement.value ? std::format("{} = {:.6g} {}", step.id, *measurement.value, measurement.unit)
                                                      : std::format("{}: {}", step.id, measurement.detail);
                    if (!measurement.passed) {
                        record.outcome = StepRecord::Outcome::Failed;
                        report.verdict = Verdict::Fail;
                    }
                    report.measurements.push_back(std::move(measurement));
                    break;
                }
            }
        }
        if (failure) {
            // A step ends where the run ended: no later than the cycle the MCU stopped at, which a wait or a window
            // planned past it never reached. The skipped steps then start there.
            record.start   = std::min(record.start, device.cycles());
            record.end     = std::max(std::min(record.end, device.cycles()), record.start);
            record.outcome = StepRecord::Outcome::Failed;
            record.detail  = *failure;
            report.verdict = Verdict::Fail;
            failedStep     = i;  // Later steps would start from an unexpected state.
        } else if (record.outcome != StepRecord::Outcome::Failed)
            record.outcome = StepRecord::Outcome::Done;
        timeline = std::max(timeline, record.end);
        report.steps.push_back(std::move(record));
    }

    report.duration = device.cycles();
    for (const auto& port : device.serialPorts())
        report.serial.push_back(port.output());
    report.trace = vcd(device, initial, s.history, report.duration);
    return report;
}

Report run(const std::filesystem::path& file) {
    auto prepared = Scenario::prepare(file);
    if (prepared)
        return (*prepared)->run();
    Report report;
    report.verdict = Verdict::Incompatible;
    report.problem = prepared.error();
    report.inputs  = prepared.error().inputs;
    return report;
}

Json toJson(const Report& report, std::string_view traceFile) {
    const auto clock = report.clockHz;
    Json       out{
              {"formatVersion",                                                                                           1},
              {      "verdict",                                                                    toString(report.verdict)},
              {     "scenario",                                                                                 report.name},
              {       "engine", {{"name", "SiliconForge"}, {"version", engineVersion()}, {"scenarioFormat", formatVersion}}}
    };
    auto& inputs = out["inputs"] = Json::array();
    for (const auto& input : report.inputs)
        inputs.push_back({
            {  "role",   input.role},
            {  "path",   input.path},
            {"sha256", input.sha256}
        });
    if (report.problem)
        out["problem"] = {
            {  "cause", toString(report.problem->cause)},
            {   "file",            report.problem->file},
            {   "path",            report.problem->path},
            {"message",         report.problem->message}
        };
    if (clock == 0)
        return out;
    out["mcu"] = {
        {   "name", report.mcu},
        {"clockHz",      clock}
    };
    out["duration"] = timeJson(report.duration, clock);
    auto& steps = out["steps"] = Json::array();
    for (const auto& step : report.steps) {
        Json entry{
            {"index", step.index},
            {"kind", step.kind},
            {"outcome", toString(step.outcome)},
            {"start", timeJson(step.start, clock)},
            {"end", timeJson(step.end, clock)}
        };
        if (step.applied)
            entry["applied"] = timeJson(*step.applied, clock);
        if (!step.label.empty())
            entry["label"] = step.label;
        if (!step.detail.empty())
            entry["detail"] = step.detail;
        steps.push_back(std::move(entry));
    }
    auto& measurements = out["measurements"] = Json::array();
    for (const auto& measurement : report.measurements) {
        Json entry{
            {         "id",                                                                         measurement.id},
            {        "pin",                                                                        measurement.pin},
            {   "quantity",                                                                   measurement.quantity},
            {       "unit",                                                                       measurement.unit},
            {      "value",                           measurement.value ? Json(*measurement.value) : Json(nullptr)},
            {    "verdict",                                                   measurement.passed ? "pass" : "fail"},
            {     "window", {{"from", timeJson(measurement.from, clock)}, {"to", timeJson(measurement.to, clock)}}},
            {"risingEdges",                                                                measurement.risingEdges}
        };
        if (!measurement.label.empty())
            entry["label"] = measurement.label;
        if (measurement.minimum)
            entry["expected"] = {
                {"min", *measurement.minimum},
                {"max", *measurement.maximum}
            };
        if (!measurement.detail.empty())
            entry["detail"] = measurement.detail;
        measurements.push_back(std::move(entry));
    }
    // Serial output is bytes, not text: base64 keeps them all, `text` shows them, invalid UTF-8 replaced by U+FFFD.
    auto& serial = out["serial"] = Json::array();
    for (std::size_t port = 0; port < report.serial.size(); ++port)
        serial.push_back({
            {  "port",                              port},
            { "bytes",        report.serial[port].size()},
            {"base64",       base64(report.serial[port])},
            {  "text", readableText(report.serial[port])}
        });
    if (!traceFile.empty())
        out["trace"] = {
            {   "format",                               "vcd"},
            {     "file",                           traceFile},
            {"timescale", detail::VcdClock{clock}.timescale()},
            {   "sha256",                      hash(report.trace)}
        };
    return out;
}

std::filesystem::path defaultEvidenceDirectory(const std::filesystem::path& file) {
    return std::filesystem::path{".siliconforge"} / "runs" / file.stem();
}

namespace {

/// Evidence is read and written as plain files, so that a run directory cannot make a reader return, wait on or
/// overwrite something else. Reading: a link is refused by the open itself (O_NOFOLLOW) and the descriptor is checked
/// to be a regular file; the open does not wait (O_NONBLOCK), so a FIFO without a writer cannot hold the reader. Writing:
/// the text goes to a new file that replaces the old one by a rename. The old inode is never opened, so a hard link
/// to a file elsewhere keeps its content, and the new file is created exclusively, without following a link. Outside
/// POSIX the checks come first, which is best effort.
#if defined(__unix__) || defined(__APPLE__)
/// Writes `text` to path.new, ready to replace `path` by publish().
bool stageFile(const std::filesystem::path& path, std::string_view text) {
    auto fresh = path;
    fresh += ".new";
    std::error_code error;
    std::filesystem::remove(fresh, error);  // A leftover of an interrupted run; a link is removed, not followed.
    const int descriptor = ::open(fresh.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (descriptor < 0)
        return false;
    bool ok = true;
    for (std::size_t done = 0; ok && done < text.size();) {
        const auto written = ::write(descriptor, text.data() + done, text.size() - done);
        if (written < 0 && errno == EINTR)
            continue;
        ok = written > 0;
        done += ok ? static_cast<std::size_t>(written) : 0;
    }
    ok = ::close(descriptor) == 0 && ok;
    if (!ok)
        std::filesystem::remove(fresh, error);
    return ok;
}

std::optional<std::string> readEvidenceFile(const std::filesystem::path& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0)
        return std::nullopt;
    struct stat info {};
    std::optional<std::string> text;
    if (::fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode)) {
        text.emplace();
        char buffer[1 << 16];
        for (;;) {
            const auto count = ::read(descriptor, buffer, sizeof buffer);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                if (count < 0)
                    text.reset();
                break;
            }
            text->append(buffer, static_cast<std::size_t>(count));
        }
    }
    ::close(descriptor);
    return text;
}
#else
/// Writes `text` to path.new, ready to replace `path` by publish().
bool stageFile(const std::filesystem::path& path, std::string_view text) {
    auto fresh = path;
    fresh += ".new";
    std::error_code error;
    std::filesystem::remove(fresh, error);
    std::ofstream stream{fresh, std::ios::binary};
    stream << text;
    if (stream.flush())
        return true;
    stream.close();
    std::filesystem::remove(fresh, error);
    return false;
}

std::optional<std::string> readEvidenceFile(const std::filesystem::path& path) {
    std::error_code error;
    const auto      status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status))
        return std::nullopt;
    return readFile(path);
}
#endif

/// Makes the staged file the one at `path`: a rename, so a reader sees the old file or the new one, whole.
bool publish(const std::filesystem::path& path) {
    auto fresh = path;
    fresh += ".new";
    std::error_code error;
    std::filesystem::rename(fresh, path, error);
    if (error)
        std::filesystem::remove(fresh, error);
    return !error;
}

void discard(const std::filesystem::path& path) {
    auto fresh = path;
    fresh += ".new";
    std::error_code error;
    std::filesystem::remove(fresh, error);
}

}  // namespace

std::expected<std::string, std::string> keepEvidence(const Report& report, const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return std::unexpected(std::format("Cannot create {}: {}", directory.generic_string(), error.message()));
    const auto reportPath = directory / "report.json";
    const auto tracePath  = directory / "trace.vcd";
    const auto hasTrace   = !report.trace.empty();
    // The report is valid UTF-8; replacing what would not be keeps the evidence even if a string slipped through.
    auto text = toJson(report, hasTrace ? "trace.vcd" : "").dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    // Both files are written whole before either replaces its predecessor, so a failure to write leaves the earlier
    // evidence as it was. The report names the SHA-256 of its trace, and readers check it: the pair is consistent
    // whenever they match, and the few moments between the two renames show as a mismatch, never as a mixed state.
    if (!stageFile(reportPath, text) || (hasTrace && !stageFile(tracePath, report.trace))) {
        discard(reportPath);
        discard(tracePath);
        return std::unexpected(std::format("Cannot write the evidence of the run in {}", directory.generic_string()));
    }
    if (hasTrace) {
        // The trace first: the old report then sees a trace it does not match, until the new report replaces it.
        if (!publish(tracePath)) {
            discard(reportPath);
            return std::unexpected(std::format("Cannot write the evidence of the run in {}", directory.generic_string()));
        }
        if (!publish(reportPath))
            return std::unexpected(std::format("Cannot write the evidence of the run in {}", directory.generic_string()));
    } else {
        // The report first: readers take a trace only when their report names one, so a trace left by an earlier
        // run is ignored from this moment, and removed.
        if (!publish(reportPath))
            return std::unexpected(std::format("Cannot write the evidence of the run in {}", directory.generic_string()));
        std::filesystem::remove(tracePath, error);
        if (error)
            return std::unexpected(std::format("Cannot remove the trace of an earlier run, {}: {}", tracePath.generic_string(), error.message()));
    }
    return text;
}

std::expected<std::string, std::string> readReport(const std::filesystem::path& directory) {
    auto report = readEvidenceFile(directory / "report.json");
    if (!report)
        return std::unexpected(std::format("No report in {}: report.json is missing or is not a plain file", directory.generic_string()));
    const auto parsed = Json::parse(*report, nullptr, false);
    if (!parsed.is_object() || !parsed.contains("verdict") || !parsed.contains("inputs"))
        return std::unexpected(std::format("{} is not the report of a bench run", (directory / "report.json").generic_string()));
    return std::move(*report);
}

std::expected<Evidence, std::string> readEvidence(const std::filesystem::path& directory) {
    // A run being replaced shows for a moment a report and a trace that do not match: read again, a few times.
    constexpr int attempts = 20;
    for (int attempt = 1;; ++attempt) {
        auto report = readReport(directory);
        if (!report)
            return std::unexpected(std::move(report.error()));
        const auto parsed  = Json::parse(*report, nullptr, false);
        const auto wanted  = parsed.contains("trace") ? parsed.at("trace").value("sha256", std::string{}) : std::string{};
        if (wanted.empty())
            return Evidence{std::move(*report), std::nullopt};  // The run kept no trace: a file left there is not its own.
        auto trace = readEvidenceFile(directory / "trace.vcd");
        if (trace && hash(*trace) == wanted)
            return Evidence{std::move(*report), std::move(trace)};
        if (attempt == attempts)
            return std::unexpected(std::format("The trace of {} does not match its report: the run is being replaced, or was interrupted while it was", directory.generic_string()));
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

}  // namespace siliconforge::scenario
