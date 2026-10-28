#pragma once

#include <siliconforge/core/Debugger.hpp>
#include <siliconforge/core/Pin.hpp>
#include <siliconforge/core/SerialPort.hpp>
#include <siliconforge/core/Types.hpp>
#include <siliconforge/core/loader/ProgramImage.hpp>

#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace siliconforge {

/// A named CPU or peripheral register, for user interfaces and traces.
struct RegisterValue {
    std::string name;
    u32         value;
    u8          bits;
};

/// Architecture-independent view of an emulated microcontroller.
///
/// This is the only interface the application, the web UI and the test bench rely on. Each concrete MCU composes a
/// core from arch/<family> with the peripherals and memory map of one chip (mcu/<chip>), and implements it.
class Mcu {
public:
    virtual ~Mcu() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept         = 0;
    [[nodiscard]] virtual std::string_view architecture() const noexcept = 0;
    [[nodiscard]] virtual u64              clockHz() const noexcept      = 0;

    /// Board view metadata serialized as JSON, from the loaded chip description.
    [[nodiscard]] virtual std::string_view chipJson() const noexcept {
        return "null";
    }

    /// Loads a firmware in program memory (and EEPROM if present), then resets.
    virtual void load(const ProgramImage& image) = 0;
    /// Power-on reset: clears the volatile state, keeps the program memory and the external pin levels.
    virtual void reset() = 0;

    /// Executes instructions until `budget` cycles have elapsed or a stop condition is met.
    virtual RunResult run(Cycles budget) = 0;
    /// Executes a single instruction (or services one pending interrupt).
    virtual RunResult step() = 0;

    [[nodiscard]] virtual Cycles  cycles() const noexcept         = 0;
    [[nodiscard]] virtual Address programCounter() const noexcept = 0;  ///< Byte address.

    [[nodiscard]] virtual std::span<Pin>       pins() noexcept               = 0;
    [[nodiscard]] virtual std::span<SerialPort> serialPorts() noexcept       = 0;
    [[nodiscard]] virtual std::vector<RegisterValue> registers() const = 0;
    [[nodiscard]] virtual std::vector<u8> readMemory(MemorySpace space, Address address, std::size_t size) const = 0;
    [[nodiscard]] virtual Debugger&       debugger() noexcept = 0;
    /// Symbol of the function containing a program address, when the firmware carried symbols.
    [[nodiscard]] virtual std::string symbolAt(Address address) const = 0;

    // Conveniences --------------------------------------------------------------------------------------------------

    /// Finds a pin by datasheet name ("PB5") or board label ("D13").
    [[nodiscard]] Pin* pin(std::string_view nameOrLabel) noexcept {
        for (auto& candidate : pins())
            if (candidate.name() == nameOrLabel || (!candidate.label().empty() && candidate.label() == nameOrLabel))
                return &candidate;
        return nullptr;
    }

    [[nodiscard]] SerialPort* serial(std::size_t index = 0) noexcept {
        auto ports = serialPorts();
        return index < ports.size() ? &ports[index] : nullptr;
    }

    [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept {
        return std::chrono::nanoseconds{static_cast<std::int64_t>(static_cast<long double>(cycles()) * 1e9L / static_cast<long double>(clockHz()))};
    }

    template <typename Rep, typename Period>
    [[nodiscard]] Cycles cyclesFor(std::chrono::duration<Rep, Period> duration) const noexcept {
        const auto seconds = std::chrono::duration<long double>(duration).count();
        return static_cast<Cycles>(seconds * static_cast<long double>(clockHz()) + 0.5L);
    }

    template <typename Rep, typename Period>
    RunResult runFor(std::chrono::duration<Rep, Period> duration) {
        return run(cyclesFor(duration));
    }
};

}  // namespace siliconforge
