#pragma once

#include <nlohmann/json.hpp>
#include <siliconforge/circuit/Description.hpp>
#include <siliconforge/circuit/Tft.hpp>
#include <siliconforge/core/Mcu.hpp>

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace siliconforge::circuit {

/// Why Circuit::set refused a value.
enum class SetError { UnknownComponent, UnknownState, NotInteractive, OutOfRange };

/// Electrical model of a circuit on a breadboard (ADR 0005): the same circuit for the interface and for the tests.
///
/// The holes and the wires make nets. Power rails, MCU outputs and probes (force) are ideal voltage sources; resistors,
/// potentiometers, LEDs (a threshold and a small series resistance) and the pull-ups of the MCU inputs link the nets.
/// The node voltages are solved each time a source or a component changes, and each solution is remembered, so a PWM
/// output that alternates between two states costs two solves, not one per edge. The model is resistive and logical,
/// not a SPICE simulation.
///
/// The circuit reads the outputs of the MCU and applies to its inputs the voltage of their net. Call update() whenever
/// a pin drive changes (Pin::subscribeDrive) and after every change made from outside (set, force).
class Circuit {
public:
    /// Builds the model of a description, which is validated first. The error carries the JSON Pointer of the fault.
    [[nodiscard]] static std::expected<std::unique_ptr<Circuit>, LoadError> create(nlohmann::json description,
                                                                                   const Library& library = Library::builtin());
    ~Circuit();
    Circuit(const Circuit&)            = delete;
    Circuit& operator=(const Circuit&) = delete;

    /// Plugs the chip of `mcu` at `chip.column`: its package pins are connected to their holes. The MCU must outlive
    /// the circuit or be detached. Fails ("/chip/column") when the package does not fit on the board.
    ///
    /// A signal generator set while there was no clock (set() cannot check its steps then) is checked here: one that
    /// this MCU cannot step (steps closer than 16 cycles, an arbitrary wave without samples) fails with
    /// "/components/<index>", and nothing is attached.
    std::expected<void, LoadError> attach(Mcu& mcu);
    /// Releases the inputs the circuit was driving and forgets the MCU.
    void detach();

    /// Solves the circuit for the current pin drives and applies the result to the MCU inputs.
    void update();
    /// Called with the MCU cycle after every change the circuit makes by itself, once it is solved: a signal generator
    /// stepping. Pin changes and what a caller does through set() are not reported here, the caller knows them. What
    /// watches the states of the components (a bounded wait of a scenario) listens here, or it would not see a voltage
    /// that crosses a threshold between two pin changes.
    void onChange(std::function<void(Cycles at)> observer) { observed = std::move(observer); }
    /// The MCU was reset: its clock restarted, so the averages start again from now.
    void restart();

    [[nodiscard]] const nlohmann::json& description() const noexcept { return source; }
    [[nodiscard]] double                supplyVoltage() const noexcept { return supply; }
    [[nodiscard]] std::vector<std::string> componentIds() const;
    [[nodiscard]] bool                     has(std::string_view id) const noexcept;

    /// Present value of a state entry of a component ("brightness", "pressed", "position", "a.brightness"...);
    /// booleans read 0 or 1. nullopt for an unknown component or entry.
    [[nodiscard]] std::optional<double> state(std::string_view id, std::string_view name) const;
    /// Whether frame() reports a state entry as its time average (the brightness of an LED the MCU may modulate), its
    /// present value following every pulse.
    [[nodiscard]] bool averaged(std::string_view id, std::string_view name) const;
    /// Changes an interactive state entry (a button press, a potentiometer position) and solves the circuit again.
    std::expected<void, SetError> set(std::string_view id, std::string_view name, double value);

    /// Why a state entry cannot take `value` now, or nullopt when it can: the reasons Circuit::set would refuse, with the
    /// words to tell the person. Besides the bounds of the type, a signal generator also needs its steps to stay at
    /// least 16 clock cycles apart, which depends on the clock of the MCU the circuit is attached to. The bench engine
    /// calls this before a run, so that a request beyond the capacity fails before anything executes.
    [[nodiscard]] std::optional<std::string> refusal(std::string_view id, std::string_view name, double value) const;

    /// Programs the arbitrary wave of a signal generator and selects it: `samples` in volts, each held for
    /// `period / samples.size()` seconds, then repeated. Returns why it cannot (unknown component, not a generator, too
    /// many samples, a sample or a period out of range, steps closer than 16 cycles), or nullopt.
    std::optional<std::string> program(std::string_view id, std::span<const double> samples, double period);

    /// What a signal generator can do, as numbers: electrical bounds, frequency and period ranges, steps per period,
    /// the time resolution of one clock cycle, and, with an MCU attached, the highest frequency of each wave at its
    /// clock. nullopt for a component that is not a generator.
    [[nodiscard]] std::optional<nlohmann::json> limits(std::string_view id) const;

    /// The controller of a TFT module (`tft-spi` or `tft-parallel` component): its pixels, its state and what its bus
    /// carried. nullptr when the component is not a display.
    [[nodiscard]] const Tft* display(std::string_view id) const;

    /// A probe applies `volts` to the net of an MCU pin, like an ideal source; nullopt removes it. Returns false for a
    /// pin that is not on the board.
    bool force(std::string_view pin, std::optional<double> volts);
    /// Applies a voltage the way a user would: by turning the potentiometer whose wiper feeds this pin when there is
    /// one between the rails, otherwise with a probe.
    bool drive(std::string_view pin, double volts);
    [[nodiscard]] std::optional<double> forced(std::string_view pin) const;

    /// Voltage of the net of a hole ("e6", "T+3"), for tests and diagnostics.
    [[nodiscard]] std::optional<double> voltageAt(std::string_view hole) const;
    /// Nets where two sources impose different voltages, described for a human.
    [[nodiscard]] const std::vector<std::string>& shorts() const noexcept { return shortCircuits; }

    /// What the interface draws: the description with the properties of every component resolved, the MCU pin each
    /// component is wired to, and the descriptions of the types in use.
    [[nodiscard]] nlohmann::json view() const;
    /// The state of every component since the previous call: time averages for what the MCU modulates (the brightness
    /// of a PWM-driven LED), present values for the rest.
    [[nodiscard]] nlohmann::json frame();

private:
    Circuit() = default;
    /// The wave of a generator starts again at the present cycle, from its first sample, and its steps are scheduled.
    void reprogram(std::size_t element);
    void reprogramAll();
    void scheduleStep(std::size_t element, std::uint64_t step, Cycles now);
    void stepGenerator(std::size_t element, std::uint64_t step);
    struct Model;
    std::unique_ptr<Model>   model;
    nlohmann::json           source;
    double                   supply{5.0};
    std::vector<std::string> shortCircuits;
    std::function<void(Cycles)> observed;
};

}  // namespace siliconforge::circuit
