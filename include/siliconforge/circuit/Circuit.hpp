#pragma once

#include <nlohmann/json_fwd.hpp>
#include <siliconforge/core/DescriptionError.hpp>
#include <siliconforge/core/Mcu.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace siliconforge::circuit {

class Library;
using LoadError = DescriptionError;
namespace detail {
struct Model;
}

/// An electrical fault found while resolving the circuit.
struct Fault {
    enum class Kind : u8 {
        ShortCircuit,  ///< Two strong sources at different voltages on the same net.
    };
    Kind                     kind;
    Cycles                   at;           ///< Cycle at which the fault appeared.
    std::string              description;  ///< "Short circuit between PB5 (5 V) and PB4 (0 V)".
    std::vector<std::string> sources;      ///< The conflicting sources: pin names ("PB5") and rails ("T+").
};

/// One component of a circuit, as seen by tests and by the interface. A cheap handle on its Circuit.
class Component {
public:
    [[nodiscard]] const std::string& id() const;
    [[nodiscard]] const std::string& type() const;

    /// Value of a state entry of the component description: "brightness", "pressed", "position", or
    /// "<element>.brightness" for a composite. Booleans read as 0 or 1. Throws std::out_of_range when unknown.
    [[nodiscard]] double state(std::string_view name) const;
    /// Changes an interactive state ("pressed", "position"), then resolves the circuit again.
    void set(std::string_view name, double value);

    /// Brightness of an LED right now, from 0 (off) to 1 (its nominal current or more).
    [[nodiscard]] double brightness() const {
        return state("brightness");
    }
    /// Average brightness since the previous call (or since the circuit was attached): what the eye sees of a PWM.
    [[nodiscard]] double meanBrightness(std::string_view name = "brightness");
    /// Current through an LED right now, in amperes.
    [[nodiscard]] double current(std::string_view name = "brightness") const;

    [[nodiscard]] bool pressed() const {
        return state("pressed") != 0;
    }
    [[nodiscard]] double position() const {
        return state("position");
    }
    void press() {
        set("pressed", 1);
    }
    void release() {
        set("pressed", 0);
    }

private:
    friend class Circuit;
    Component(std::shared_ptr<detail::Model> model, std::size_t index) : model(std::move(model)), index(index) { }

    std::shared_ptr<detail::Model> model;
    std::size_t                    index;
};

/// A circuit on a breadboard, resolved in C++ so that the test bench sees what the user sees (ADR 0005).
///
/// The model is logical and resistive, not a SPICE simulation. Holes of a strip, wires and pressed buttons form nets.
/// Power rails and MCU outputs fix the voltage of their net; a potentiometer between two fixed nets divides it; an LED
/// in series with resistors between two fixed nets carries I = (V+ - V- - Vf) / R; resistors to fixed nets pull the
/// others. Meshes and parallel LEDs sharing a resistor are not resolved: such LEDs stay off.
class Circuit {
public:
    /// Loads and validates a circuit description.
    [[nodiscard]] static std::expected<Circuit, LoadError> load(const std::filesystem::path& path);
    [[nodiscard]] static std::expected<Circuit, LoadError> load(const std::filesystem::path& path, const Library& library);
    [[nodiscard]] static std::expected<Circuit, LoadError> fromJson(const nlohmann::json& description);
    [[nodiscard]] static std::expected<Circuit, LoadError> fromJson(const nlohmann::json& description, const Library& library);
    /// A circuit embedded in the binary ("arduino-demo"). Throws std::invalid_argument for an unknown name.
    [[nodiscard]] static Circuit builtin(std::string_view name);

    [[nodiscard]] const std::string& name() const;

    /// Plugs the chip in the board: its pins join the nets, its outputs drive them and its inputs read them from now
    /// on. Fails when the package does not fit the board or covers a hole used by the circuit. The MCU must outlive
    /// every use of the circuit; a circuit is attached once.
    std::expected<void, LoadError> attach(Mcu& mcu);

    [[nodiscard]] std::vector<std::string> componentIds() const;
    /// Throws std::out_of_range for an unknown id.
    [[nodiscard]] Component component(std::string_view id);

    /// Whether two holes are on the same net ("f15", "T+3"). Throws std::invalid_argument for a hole off the board.
    [[nodiscard]] bool connected(std::string_view first, std::string_view second) const;
    /// Voltage of the net of a hole, nullopt when nothing fixes or pulls it.
    [[nodiscard]] std::optional<double> voltage(std::string_view hole) const;

    /// Every fault since the circuit was attached, oldest first.
    [[nodiscard]] const std::vector<Fault>& faults() const;
    void                                    clearFaults();

private:
    explicit Circuit(std::shared_ptr<detail::Model> model) : model(std::move(model)) { }

    std::shared_ptr<detail::Model> model;
};

}  // namespace siliconforge::circuit
