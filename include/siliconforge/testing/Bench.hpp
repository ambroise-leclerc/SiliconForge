#pragma once

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/core/Mcu.hpp>
#include <siliconforge/core/loader/ProgramImage.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace siliconforge::testing {

/// One change of a pin, recorded by the bench.
struct Transition {
    Cycles   at;
    PinState state;
};

/// Test bench around an emulated MCU: loads firmware, advances simulated time, stimulates inputs and observes
/// outputs. Every duration is simulated time, so tests are deterministic and usually much faster than real time.
///
///     Bench bench{mcu::create("atmega328p")};
///     bench.load("blink.hex");
///     REQUIRE(bench.waitForSerial("ready\n", 100ms));
///     REQUIRE(bench.period("D13") == Approx(1.0s));
///
/// With a circuit (ADR 0005) the bench sees what the user sees, through the same electrical model as the interface:
///
///     bench.attach(circuit::builtinDescription("arduino-demo"));
///     bench.press("button-d2");
///     REQUIRE(bench.component("led-d13").brightness() == 1.0);
///
/// The methods that take a circuit need the SiliconForge::circuit library.
class Bench {
public:
    explicit Bench(std::unique_ptr<Mcu> device) : device(std::move(device)) {
        if (!this->device)
            throw std::invalid_argument("Bench needs an MCU");
        for (auto& pin : this->device->pins()) {
            pin.subscribe([this](const Pin& changed, Cycles at) { history[changed.name()].push_back({at, changed.state()}); });
            pin.subscribeDrive([this](const Pin&, Cycles) {
                if (model)
                    model->update();
            });
        }
    }

    [[nodiscard]] Mcu& mcu() noexcept { return *device; }

    void load(const ProgramImage& image) {
        device->load(image);
        history.clear();
        if (model)
            model->restart();
    }

    void load(const std::filesystem::path& firmware) {
        auto image = loadProgram(firmware);
        if (!image)
            throw std::runtime_error(std::format("{}: {} (line {})", firmware.string(), image.error().message, image.error().line));
        load(*image);
    }

    // Simulated time ------------------------------------------------------------------------------------------------

    template <typename Rep, typename Period>
    RunResult runFor(std::chrono::duration<Rep, Period> duration) {
        return device->run(device->cyclesFor(duration));
    }

    /// Runs until `condition` holds (checked every `granularity` cycles) or `timeout` elapses. Returns whether it holds.
    template <typename Rep, typename Period>
    bool runUntil(const std::function<bool()>& condition, std::chrono::duration<Rep, Period> timeout, Cycles granularity = 64) {
        const auto deadline = device->cycles() + device->cyclesFor(timeout);
        while (!condition()) {
            if (device->cycles() >= deadline)
                return false;
            const auto result = device->run(std::min(granularity, deadline - device->cycles()));
            if (result.reason != StopReason::BudgetReached)
                return condition();
        }
        return true;
    }

    [[nodiscard]] double seconds() const noexcept { return static_cast<double>(device->cycles()) / static_cast<double>(device->clockHz()); }

    // Serial ports --------------------------------------------------------------------------------------------------

    [[nodiscard]] SerialPort& serial(std::size_t index = 0) {
        auto* port = device->serial(index);
        if (!port)
            throw std::out_of_range(std::format("{} has no serial port {}", device->name(), index));
        return *port;
    }

    template <typename Rep, typename Period>
    bool waitForSerial(std::string_view expected, std::chrono::duration<Rep, Period> timeout, std::size_t port = 0) {
        auto& output = serial(port);
        return runUntil([&] { return output.output().find(expected) != std::string::npos; }, timeout);
    }

    // Pins ----------------------------------------------------------------------------------------------------------

    [[nodiscard]] Pin& pin(std::string_view nameOrLabel) {
        auto* found = device->pin(nameOrLabel);
        if (!found)
            throw std::out_of_range(std::format("{} has no pin {}", device->name(), nameOrLabel));
        return *found;
    }

    template <typename Rep, typename Period>
    bool waitForPin(std::string_view nameOrLabel, PinState state, std::chrono::duration<Rep, Period> timeout) {
        auto& watched = pin(nameOrLabel);
        return runUntil([&] { return watched.state() == state; }, timeout, 1);
    }

    /// Every change of a pin since the firmware was loaded.
    [[nodiscard]] const std::vector<Transition>& transitions(std::string_view nameOrLabel) { return history[pin(nameOrLabel).name()]; }

    /// Average time between rising edges, in seconds, over the recorded history.
    [[nodiscard]] std::optional<double> period(std::string_view nameOrLabel) {
        std::vector<Cycles> rising;
        for (const auto& transition : transitions(nameOrLabel))
            if (transition.state == PinState::High)
                rising.push_back(transition.at);
        if (rising.size() < 2)
            return std::nullopt;
        return static_cast<double>(rising.back() - rising.front()) / static_cast<double>(rising.size() - 1) / static_cast<double>(device->clockHz());
    }

    /// Fraction of time a pin spent high between its first and last rising edges (PWM duty cycle).
    [[nodiscard]] std::optional<double> dutyCycle(std::string_view nameOrLabel) {
        const auto& changes = transitions(nameOrLabel);
        const auto  isRising = [](const Transition& transition) { return transition.state == PinState::High; };
        const auto  first    = std::ranges::find_if(changes, isRising);
        const auto  last     = std::ranges::find_if(changes.rbegin(), changes.rend(), isRising).base();
        if (first == changes.end() || std::distance(first, last) < 2)
            return std::nullopt;
        Cycles high = 0;
        for (auto it = first; std::next(it) != last; ++it)
            if (it->state == PinState::High)
                high += std::next(it)->at - it->at;
        return static_cast<double>(high) / static_cast<double>(std::prev(last)->at - first->at);
    }

    // Circuit ---------------------------------------------------------------------------------------------------------

    /// One component of the attached circuit, by its id.
    class Component {
    public:
        /// A state entry of the component type ("brightness", "pressed", "position", "a.brightness"...).
        [[nodiscard]] double state(std::string_view name) const {
            const auto value = model->state(id, name);
            if (!value)
                throw std::out_of_range(std::format("Component {} has no state {}", id, name));
            return *value;
        }
        /// 0 (dark) to 1 (the nominal current or more), at this instant.
        [[nodiscard]] double brightness() const { return state("brightness"); }
        [[nodiscard]] bool   lit() const { return brightness() > 0.05; }

    private:
        friend class Bench;
        Component(circuit::Circuit& model, std::string id) : model(&model), id(std::move(id)) {}
        circuit::Circuit* model;
        std::string       id;
    };

    /// Plugs the MCU in a circuit: its outputs drive the components and the components drive its inputs. Throws
    /// std::invalid_argument, with the JSON Pointer of the fault, for an invalid description.
    void attach(nlohmann::json description, const circuit::Library& library = circuit::Library::builtin()) {
        auto created = circuit::Circuit::create(std::move(description), library);
        if (created) {
            if (auto attached = (*created)->attach(*device); !attached)
                created = std::unexpected(attached.error());
        }
        if (!created)
            throw std::invalid_argument(std::format("{}: {}", created.error().path, created.error().message));
        model = std::move(*created);
    }

    [[nodiscard]] circuit::Circuit& circuit() {
        if (!model)
            throw std::logic_error("The bench has no circuit: call attach() first");
        return *model;
    }

    [[nodiscard]] Component component(std::string_view id) {
        if (!circuit().has(id))
            throw std::out_of_range(std::format("The circuit has no component {}", id));
        return Component{*model, std::string{id}};
    }

    /// Changes an interactive state of a component, like the user does on the breadboard.
    void set(std::string_view id, std::string_view name, double value) {
        if (!circuit().set(id, name, value))
            throw std::invalid_argument(std::format("Cannot set {} of component {} to {}", name, id, value));
    }
    void press(std::string_view button) { set(button, "pressed", 1); }
    void release(std::string_view button) { set(button, "pressed", 0); }
    /// Turns a potentiometer: 0 puts the wiper at terminal a, 1 at terminal b.
    void turn(std::string_view potentiometer, double position) { set(potentiometer, "position", position); }

private:
    std::unique_ptr<Mcu>                          device;
    std::unique_ptr<circuit::Circuit>             model;
    std::map<std::string, std::vector<Transition>, std::less<>> history;
};

}  // namespace siliconforge::testing
