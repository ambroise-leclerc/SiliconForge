#pragma once

#include <siliconforge/core/Debugger.hpp>
#include <siliconforge/core/Types.hpp>

#include <algorithm>
#include <functional>
#include <vector>

namespace siliconforge::avr {

/// Read and write behaviour of a memory-mapped I/O register.
///
/// `write` receives a `mask` of the bits actually written: 0xFF for OUT/ST/STS, a single bit for SBI/CBI. On the
/// ATmega328P, SBI/CBI only operate on the specified bit, which matters for registers where writing 1 has a side
/// effect (PINx toggles the output, TIFRx clears a flag).
struct IoHandler {
    std::function<u8()>                   read;
    std::function<void(u8 value, u8 mask)> write;
};

/// AVR data address space: 32 general-purpose registers, I/O registers, extended I/O registers, then SRAM.
///
/// Registers without a handler behave as plain storage, so that firmware touching peripherals that are not emulated
/// yet keeps running and reads back what it wrote.
class DataSpace {
public:
    static constexpr Address registerCount = 0x20;

    DataSpace(Address ioEnd, Address ramEnd) : ioEnd(ioEnd), ramEnd(ramEnd), memory(ramEnd + 1, 0), handlers(ioEnd + 1) {}

    void map(Address address, IoHandler handler) { handlers[address] = std::move(handler); }

    [[nodiscard]] u8 read(Address address) {
        if (debugger && debugger->hasWatchpoints())
            debugger->checkAccess(address, Access::Read);
        if (address < registerCount)
            return memory[address];
        if (address <= ioEnd) {
            const auto& handler = handlers[address];
            return handler.read ? handler.read() : memory[address];
        }
        return address <= ramEnd ? memory[address] : 0;
    }

    void write(Address address, u8 value, u8 mask = 0xFF) {
        if (debugger && debugger->hasWatchpoints())
            debugger->checkAccess(address, Access::Write);
        if (address < registerCount) {
            memory[address] = value;
        } else if (address <= ioEnd) {
            const auto& handler = handlers[address];
            if (handler.write)
                handler.write(value, mask);
            else
                memory[address] = static_cast<u8>((memory[address] & ~mask) | (value & mask));
        } else if (address <= ramEnd) {
            memory[address] = value;
        }
    }

    /// Direct access to the storage, bypassing handlers (used by peripherals for their own registers, and by the UI).
    [[nodiscard]] u8& raw(Address address) noexcept { return memory[address]; }
    [[nodiscard]] u8  raw(Address address) const noexcept { return address <= ramEnd ? memory[address] : 0; }
    [[nodiscard]] u8* registers() noexcept { return memory.data(); }

    /// Peek used by debuggers and UIs: goes through read handlers but never triggers watchpoints.
    [[nodiscard]] u8 peek(Address address) const {
        if (address >= registerCount && address <= ioEnd && handlers[address].read)
            return handlers[address].read();
        return raw(address);
    }

    [[nodiscard]] Address ramEndAddress() const noexcept { return ramEnd; }
    [[nodiscard]] Address ioEndAddress() const noexcept { return ioEnd; }

    void clear() { std::ranges::fill(memory, u8{0}); }
    void attach(Debugger* attachedDebugger) noexcept { debugger = attachedDebugger; }

private:
    Address                ioEnd;
    Address                ramEnd;
    std::vector<u8>        memory;
    std::vector<IoHandler> handlers;
    Debugger*              debugger{nullptr};
};

}  // namespace siliconforge::avr
