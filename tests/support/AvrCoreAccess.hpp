#pragma once
#include <siliconforge/arch/avr/AvrCore.hpp>
#include <cstddef>

namespace siliconforge::testing {
// Inspection for tests and the manual benchmark; not part of the installed MCU API.
struct AvrCoreAccess {
    [[nodiscard]] static std::size_t timingTableBytes(const avr::AvrCore& core) noexcept {
        return core.decoded.capacity() * sizeof(avr::InstructionEntry);
    }
};
}
