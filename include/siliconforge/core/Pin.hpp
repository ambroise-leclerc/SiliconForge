#pragma once

#include <siliconforge/core/Types.hpp>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace siliconforge {

/// Electrical state of a pin as seen from the outside world.
enum class PinState : u8 { Low, High, Floating };

/// One package pin, shared between the MCU (which drives it as an output or reads it as an input) and the outside
/// world (UI, test bench, simulated circuits) which can apply an external level when the MCU does not drive it.
class Pin {
public:
    using Observer = std::function<void(const Pin&, Cycles at)>;

    Pin(std::string name, std::string label, const Cycles* clock) : pinName(std::move(name)), pinLabel(std::move(label)), clock(clock) {}

    /// Port name used by the datasheet ("PB5").
    [[nodiscard]] const std::string& name() const noexcept { return pinName; }
    /// Board name, when the MCU is mounted on a known board ("D13"); empty otherwise.
    [[nodiscard]] const std::string& label() const noexcept { return pinLabel; }

    [[nodiscard]] bool isOutput() const noexcept { return output; }
    [[nodiscard]] bool hasPullUp() const noexcept { return pullUp; }
    [[nodiscard]] std::optional<bool> external() const noexcept { return externalLevel; }

    [[nodiscard]] PinState state() const noexcept {
        if (output)
            return driven ? PinState::High : PinState::Low;
        if (externalLevel)
            return *externalLevel ? PinState::High : PinState::Low;
        return pullUp ? PinState::High : PinState::Floating;
    }

    /// Logical level read by the MCU input buffer. A floating input reads low.
    [[nodiscard]] bool level() const noexcept { return state() == PinState::High; }

    /// Cycle of the last change of state().
    [[nodiscard]] Cycles lastChange() const noexcept { return changedAt; }

    /// MCU side: configure the output driver and the pull-up.
    void configure(bool isOutput, bool drivenLevel, bool pullUpEnabled) {
        const auto before       = state();
        const auto driveChanged = output != isOutput || driven != drivenLevel || pullUp != pullUpEnabled;
        output                  = isOutput;
        driven                  = drivenLevel;
        pullUp                  = pullUpEnabled;
        notifyIfChanged(before, false);
        if (driveChanged)
            for (auto& observer : driveObservers)
                observer(*this, clock ? *clock : 0);
    }

    /// Outside world side: apply a level (true/false) or release the pin (nullopt).
    void setExternal(std::optional<bool> newLevel) {
        const auto before        = state();
        const auto voltageBefore = externalVoltage;
        externalLevel            = newLevel;
        externalVoltage.reset();
        notifyIfChanged(before, voltageBefore != externalVoltage);
    }

    /// Outside world side: apply an analog voltage. Digital inputs see it as high above half the supply voltage.
    void setVoltage(double volts, double supply) {
        const auto before        = state();
        const auto voltageBefore = externalVoltage;
        externalLevel            = volts > supply / 2;
        externalVoltage          = volts;
        notifyIfChanged(before, voltageBefore != externalVoltage);
    }

    /// Voltage seen by an analog input (ADC, comparator).
    [[nodiscard]] double voltage(double supply) const noexcept {
        if (!output && externalVoltage)
            return *externalVoltage;
        return state() == PinState::High ? supply : 0.0;
    }

    /// Called when state() changes.
    void subscribe(Observer observer) { observers.push_back(std::move(observer)); }

    /// Called when the MCU side changes: direction, driven level or pull-up, even when state() stays the same (an
    /// output that becomes an input held at the same level by the outside world). Simulated circuits listen here.
    void subscribeDrive(Observer observer) { driveObservers.push_back(std::move(observer)); }

    /// Called when state() or the voltage applied from outside changes: what an oscilloscope probe would see.
    void subscribeSignal(Observer observer) { signalObservers.push_back(std::move(observer)); }

private:
    void notifyIfChanged(PinState before, bool voltageChanged) {
        const auto stateChanged = state() != before;
        if (!stateChanged && !voltageChanged)
            return;
        const auto now = clock ? *clock : 0;
        if (stateChanged) {
            changedAt = now;
            for (auto& observer : observers)
                observer(*this, changedAt);
        }
        for (auto& observer : signalObservers)
            observer(*this, now);
    }

    std::string         pinName;
    std::string         pinLabel;
    const Cycles*       clock;
    bool                output{false};
    bool                driven{false};
    bool                pullUp{false};
    std::optional<bool> externalLevel;
    std::optional<double> externalVoltage;
    Cycles              changedAt{0};
    std::vector<Observer> observers;
    std::vector<Observer> driveObservers;
    std::vector<Observer> signalObservers;
};

}  // namespace siliconforge
