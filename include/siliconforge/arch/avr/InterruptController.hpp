#pragma once

#include <siliconforge/core/Types.hpp>

#include <array>
#include <bit>
#include <functional>

namespace siliconforge::avr {

/// Interrupt lines of an AVR device. Vector 0 is reset; a lower vector number has a higher priority.
///
/// Peripherals keep each line up to date with `set(vector, flag && enable)` whenever a flag or an enable bit changes.
/// When the core enters a vector it calls acknowledge(), which runs the peripheral's hook: this is where flags that
/// the hardware clears on vector execution (TOV0, OCF0A, TXC0...) are cleared.
class InterruptController {
public:
    static constexpr unsigned maxVectors = 64;

    void set(unsigned vector, bool active) noexcept {
        const auto bit = u64{1} << vector;
        pending        = active ? (pending | bit) : (pending & ~bit);
    }

    [[nodiscard]] bool     any() const noexcept { return pending != 0; }
    [[nodiscard]] bool     isPending(unsigned vector) const noexcept { return (pending >> vector) & 1u; }
    [[nodiscard]] unsigned highestPriority() const noexcept { return static_cast<unsigned>(std::countr_zero(pending)); }

    void onAcknowledge(unsigned vector, std::function<void()> hook) { hooks[vector] = std::move(hook); }

    void acknowledge(unsigned vector) {
        if (hooks[vector])
            hooks[vector]();
    }

    void clear() noexcept { pending = 0; }

private:
    u64                                            pending{0};
    std::array<std::function<void()>, maxVectors> hooks;
};

}  // namespace siliconforge::avr
