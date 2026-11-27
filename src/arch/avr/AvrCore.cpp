#include <siliconforge/arch/avr/AvrCore.hpp>
#include <siliconforge/arch/avr/Opcodes.hpp>

#include <algorithm>
#include <stdexcept>

namespace siliconforge::avr {

void recordOpcode(u16 opcode, u8* coverage) noexcept;  // Opcodes.cpp

AvrCore::AvrCore(const AvrCoreConfig& config, DataSpace& data, Scheduler& scheduler, InterruptController& interrupts, Debugger& debugger)
    : AvrCore(config, data, scheduler, interrupts, debugger, {}) {}

AvrCore::AvrCore(const AvrCoreConfig& config, DataSpace& data, Scheduler& scheduler, InterruptController& interrupts, Debugger& debugger,
                 std::string_view builtinTarget)
    : data(data), scheduler(scheduler), interrupts(interrupts), debugger(debugger), cfg(config), regs(data.registers()), coverageTarget(builtinTarget),
      flashWords(config.flashBytes / 2, 0xFFFF), pageBuffer(config.flashPageBytes / 2, 0xFFFF) {
    data.map(cfg.spl, {[this] { return static_cast<u8>(sp); }, [this](u8 value, u8 mask) { sp = static_cast<u16>((sp & ~mask) | (value & mask)); }});
    data.map(cfg.sph, {[this] { return static_cast<u8>(sp >> 8); },
                   [this](u8 value, u8 mask) { sp = static_cast<u16>((sp & ~(mask << 8)) | ((value & mask) << 8)); }});
    data.map(cfg.sreg, {[this] { return sreg; }, [this](u8 value, u8 mask) { sreg = static_cast<u8>((sreg & ~mask) | (value & mask)); }});
    const auto mapExtension = [&](std::optional<Address> reg, u8 highBits) {
        if (!reg)
            return;
        data.map(*reg, {nullptr, [this, address = *reg, highBits](u8 value, u8 mask) {
            auto& stored = this->data.raw(address);
            stored = static_cast<u8>(((stored & ~mask) | (value & mask)) & highBits);
        }});
    };
    mapExtension(cfg.rampz, static_cast<u8>((cfg.flashBytes - 1) >> 16));
    mapExtension(cfg.eind, static_cast<u8>(pcMask() >> 16));
    data.attach(&debugger);
}

void AvrCore::reset() {
    pc             = 0;
    sp             = static_cast<u16>(cfg.ramEnd);
    sreg           = 0;
    cycles         = 0;
    sleeping       = false;
    interruptDelay = false;
    interruptActive = false;
    stopRequest.reset();
    resumeAt.reset();
    std::ranges::fill(pageBuffer, u16{0xFFFF});
}

void AvrCore::loadFlash(Address address, std::span<const u8> bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto byteAddress = address + i;
        auto&      word        = flashWords[(byteAddress >> 1) & pcMask()];
        word = byteAddress & 1 ? static_cast<u16>((word & 0x00FF) | bytes[i] << 8) : static_cast<u16>((word & 0xFF00) | bytes[i]);
    }
    if (bytes.size() >= flashWords.size() * 2) {
        markWritten(0, flashWords.size());
        decodeFlash(0, flashWords.size());
    } else if (!bytes.empty()) {
        const auto first = (address >> 1) & pcMask();
        const auto last  = ((address + bytes.size() - 1) >> 1) & pcMask();
        if (first <= last) {
            markWritten(first, last - first + 1);
            decodeFlash(first, last - first + 1);
        } else {  // The segment wraps around the end of the flash.
            markWritten(first, flashWords.size() - first);
            markWritten(0, last + 1);
            decodeFlash(first, flashWords.size() - first);
            decodeFlash(0, last + 1);
        }
    }
}

void AvrCore::eraseFlash() {
    if (writtenFirst >= writtenEnd)
        return;  // Nothing was written since the last erase: every word already holds 0xFFFF, decoded.
    std::fill(flashWords.begin() + static_cast<std::ptrdiff_t>(writtenFirst), flashWords.begin() + static_cast<std::ptrdiff_t>(writtenEnd), u16{0xFFFF});
    decodeFlash(writtenFirst, writtenEnd - writtenFirst);
    writtenFirst = flashWords.size();
    writtenEnd   = 0;
}

void AvrCore::decodeFlash(std::size_t first, std::size_t count) {
    if (decoded.empty())
        return;  // Decoded as a whole by setTiming().
    const auto* indices = instructionIndices().data();
    for (auto at = first; at < first + count; ++at) {
        const auto  opcode = flashWords[at];
        const auto& entry  = decoded[indices[opcode]];
        program[at]        = {entry.handler, entry.timing, opcode};
    }
}

RunResult AvrCore::run(Cycles budget, bool honourBreakpoints) {
    if (decoded.empty())
        throw std::logic_error("AVR instruction timings are not configured");
    // Two instances of the same loop: the usual one pays nothing for the coverage of the qualification reports.
    if (OpcodeCoverage::recording())
        return execute<true>(budget, honourBreakpoints, OpcodeCoverage::targetBuffer(coverageTarget));
    return execute<false>(budget, honourBreakpoints, nullptr);
}

template <bool Recorded>
RunResult AvrCore::execute(Cycles budget, bool honourBreakpoints, u8* coverage) {
    const auto* slots = program.data();
    const auto  mask  = pcMask();
    const auto  start = cycles;
    const auto  end   = cycles + budget;

    while (cycles < end) {
        if (cycles >= scheduler.nextEventAt())
            scheduler.runDueWithClock(cycles);

        if (canServiceInterrupt()) {
            serviceInterrupt();
            continue;
        }

        if (debugger.takeStopRequest())
            return {StopReason::Requested, cycles - start};

        if (sleeping) {
            // Nothing executes until an event raises an interrupt: jump straight to the next event.
            cycles = std::max(cycles + 1, std::min(scheduler.nextEventAt(), end));
            continue;
        }

        if (honourBreakpoints && debugger.hasBreakpoints() && debugger.isBreakpoint(pc * 2) && resumeAt != pc) {
            resumeAt = pc;
            return {StopReason::Breakpoint, cycles - start};
        }
        resumeAt.reset();

        interruptDelay    = false;
        const auto& slot = slots[pc++ & mask];
        if constexpr (Recorded)
            recordOpcode(slot.opcode, coverage);
        cycles          += slot.handler(*this, slot.opcode, slot.timing);

        if (stopRequest) [[unlikely]]
            return {*std::exchange(stopRequest, std::nullopt), cycles - start};
        if (debugger.hasWatchpoints() && debugger.takeWatchHit())
            return {StopReason::Watchpoint, cycles - start};
    }
    // Events due exactly at the end of the run belong to it: observers must see the state at that instant.
    if (cycles >= scheduler.nextEventAt())
        scheduler.runDueWithClock(cycles);
    return {StopReason::BudgetReached, cycles - start};
}

RunResult AvrCore::step() {
    if (sleeping && !canServiceInterrupt()) {
        // A step while sleeping waits for the next event (or for one cycle when none is scheduled).
        const auto next = scheduler.nextEventAt();
        return run(next == Scheduler::never || next <= cycles ? 1 : next - cycles, false);
    }
    // One instruction or one interrupt entry: a budget of one cycle stops after exactly one of them.
    return run(1, false);
}

void AvrCore::serviceInterrupt() {
    const auto vector = interrupts.highestPriority();
    interrupts.acknowledge(vector);
    if (sleeping) {
        sleeping = false;
        cycles += cfg.wakeUpCycles;
    }
    pushPc(pc);
    if (cfg.preserveInterruptEnable)
        interruptActive = true;
    else
        sreg &= static_cast<u8>(~sreg::I);
    pc = (vector * cfg.vectorWords) & pcMask();
    cycles += cfg.interruptCycles;
}

void AvrCore::returnFromInterrupt() {
    pc = popPc();
    if (cfg.preserveInterruptEnable)
        interruptActive = false;
    else
        sreg |= sreg::I;
    interruptDelay = true;
}

void AvrCore::executeSpm() {
    if (cfg.spmcsr == 0)
        return;
    auto&      spmcsr     = data.raw(cfg.spmcsr);
    const auto z          = extendedZ(cfg.rampz);
    const auto pageWords  = static_cast<u32>(pageBuffer.size());
    const auto pageStart  = (static_cast<u32>(z >> 1) & pcMask()) & ~(pageWords - 1);
    switch (spmcsr & 0x1F) {
        case 0x01: pageBuffer[(z >> 1) & (pageWords - 1)] = pair(0); break;  // Fill the temporary page buffer
        case 0x03:  // Page erase
            std::fill_n(flashWords.begin() + pageStart, pageWords, u16{0xFFFF});
            decodeFlash(pageStart, pageWords);
            break;
        case 0x05:  // Page write
            std::ranges::copy(pageBuffer, flashWords.begin() + pageStart);
            markWritten(pageStart, pageWords);
            decodeFlash(pageStart, pageWords);
            std::ranges::fill(pageBuffer, u16{0xFFFF});
            break;
        default: break;  // RWW re-enable, lock and fuse bits: nothing to emulate.
    }
    spmcsr &= static_cast<u8>(~0x1F);
}

}  // namespace siliconforge::avr
