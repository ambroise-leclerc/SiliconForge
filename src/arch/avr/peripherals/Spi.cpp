#include <siliconforge/arch/avr/peripherals/Spi.hpp>

#include <utility>

namespace siliconforge::avr {

namespace {
// SPCR
constexpr u8 spie = 1 << 7;
constexpr u8 spe  = 1 << 6;
constexpr u8 dord = 1 << 5;
constexpr u8 mstr = 1 << 4;
constexpr u8 cpol = 1 << 3;
constexpr u8 cpha = 1 << 2;
// SPSR
constexpr u8 spif = 1 << 7;
constexpr u8 wcol = 1 << 6;
constexpr u8 spi2x = 1;
}  // namespace

Spi::Spi(DataSpace& data, Scheduler& scheduler, InterruptController& interrupts, const Cycles& clock, Registers registers, unsigned vector, Pins pins)
    : data(data), scheduler(scheduler), interrupts(interrupts), clock(clock), regs(registers), vector(vector), pins(pins) {
    data.map(regs.spcr, {nullptr, [this](u8 value, u8 mask) {
                             auto& stored = this->data.raw(regs.spcr);
                             stored       = static_cast<u8>((stored & ~mask) | (value & mask));
                             controlChanged();
                         }});
    data.map(regs.spsr, {[this] {
                             auto status = this->data.raw(regs.spsr);
                             statusSeen  = static_cast<u8>(statusSeen | (status & (spif | wcol)));  // Cleared by the next access to SPDR
                             return status;
                         },
                         [this](u8 value, u8 mask) {
                             auto& stored = this->data.raw(regs.spsr);
                             stored       = static_cast<u8>((stored & ~(mask & spi2x)) | (value & mask & spi2x));
                         }});
    data.map(regs.spdr, {[this] {
                             clearSeenFlags();
                             return received;
                         },
                         [this](u8 value, u8) {
                             clearSeenFlags();  // The access that follows a read of SPSR clears what it showed, whatever the mode
                             const auto control = this->data.raw(regs.spcr);
                             if (!(control & spe) || !(control & mstr))
                                 return;
                             if (busy) {
                                 this->data.raw(regs.spsr) |= wcol;
                                 return;
                             }
                             start(value, this->clock);
                         }});
    // SS: an input driven low takes the master mode away, whenever it happens (the level, the direction or the register).
    if (pins.port && pins.ss)
        if (auto* select = pins.port->pin(*pins.ss)) {
            select->subscribe([this](const Pin&, Cycles) { checkSelect(); });
            select->subscribeDrive([this](const Pin&, Cycles) { checkSelect(); });
        }
    interrupts.onAcknowledge(vector, [this] {
        this->data.raw(regs.spsr) &= static_cast<u8>(~spif);
        updateInterrupt();
    });
}

void Spi::reset(bool preserveRegisters) {
    for (const auto event : std::exchange(events, {}))
        scheduler.cancel(event);
    busy       = false;
    statusSeen = 0;
    received   = 0;
    if (!preserveRegisters) {
        data.raw(regs.spcr) = 0;
        data.raw(regs.spsr) = 0;
    }
    releasePins();
    controlChanged();
}

Cycles Spi::bitCycles() const noexcept {
    static constexpr Cycles dividers[] = {4, 16, 64, 128};
    const auto              divider    = dividers[data.raw(regs.spcr) & 0x03];
    return (data.raw(regs.spsr) & spi2x) ? divider / 2 : divider;
}

void Spi::releasePins() {
    if (!pins.port)
        return;
    pins.port->overrideLevel(pins.sck, std::nullopt);
    pins.port->overrideLevel(pins.mosi, std::nullopt);
}

void Spi::clearSeenFlags() {
    if (!statusSeen)
        return;
    data.raw(regs.spsr) &= static_cast<u8>(~statusSeen);
    statusSeen = 0;
    updateInterrupt();
}

void Spi::abortTransfer() {
    for (const auto event : std::exchange(events, {}))
        scheduler.cancel(event);
    busy = false;
    releasePins();
}

void Spi::checkSelect() {
    const auto control = data.raw(regs.spcr);
    if (!(control & spe) || !(control & mstr) || !pins.port || !pins.ss)
        return;
    const auto* select = pins.port->pin(*pins.ss);
    if (!select || select->isOutput() || select->state() != PinState::Low)
        return;
    // Mode fault: the master becomes a slave, which this model does not drive: a byte under way is lost, SPIF tells the firmware.
    abortTransfer();
    data.raw(regs.spcr) = static_cast<u8>(control & ~mstr);
    data.raw(regs.spsr) |= spif;
    updateInterrupt();
}

void Spi::controlChanged() {
    const auto control = data.raw(regs.spcr);
    if (busy) {
        if ((control & spe) && (control & mstr)) {
            updateInterrupt();  // SPIE may have changed while the flag of the last transfer is still up
            return;             // The mode of a transfer under way does not change
        }
        // Disabling the peripheral (or leaving master mode) aborts the byte: no more edge, no SPIF, the pins are released.
        abortTransfer();
        updateInterrupt();
        return;
    }
    if (!pins.port)
        return;
    // A master that is enabled holds SCK at its idle level; the direction stays with DDRx.
    if ((control & spe) && (control & mstr))
        pins.port->overrideLevel(pins.sck, (control & cpol) != 0);
    else
        releasePins();
    updateInterrupt();
    checkSelect();  // Enabled as a master while SS is held low
}

void Spi::start(u8 value, Cycles now) {
    const auto control = data.raw(regs.spcr);
    const auto period  = bitCycles();
    const auto half    = period / 2;
    const bool idle    = (control & cpol) != 0;
    const bool phase   = (control & cpha) != 0;
    busy               = true;
    shiftIn            = 0;
    // The edges in time order, each at its own cycle; edges that fall on the same cycle keep this order, which is the
    // order the real hardware gives them: the trailing edge of a bit, then the data of the next.
    const auto at = [&](Cycles when, auto action) { events.push_back(scheduler.schedule(when, [action](Cycles) { action(); })); };
    const bool lsbFirst = (control & dord) != 0;
    const auto leading  = [this, idle] { pins.port->overrideLevel(pins.sck, !idle); };
    const auto trailing = [this, idle] { pins.port->overrideLevel(pins.sck, idle); };
    // The received bits enter on the side the data leave from.
    const auto sample = [this, lsbFirst] {
        const auto line = static_cast<u8>((pins.port->input() >> pins.miso) & 1u);
        shiftIn         = lsbFirst ? static_cast<u8>((shiftIn >> 1) | (line << 7)) : static_cast<u8>((shiftIn << 1) | line);
    };
    for (unsigned i = 0; i < 8; ++i) {
        const auto bit   = lsbFirst ? ((value >> i) & 1u) != 0 : ((value >> (7 - i)) & 1u) != 0;
        const auto begin = now + i * period;
        const auto drive = [this, bit] { pins.port->overrideLevel(pins.mosi, bit); };
        if (!phase) {  // The data are there before the leading edge, which samples; the trailing edge shifts.
            at(begin, drive);
            at(begin + half, [leading, sample] { leading(); sample(); });
            at(begin + period, trailing);
        } else {  // The leading edge shifts, the trailing edge samples.
            at(begin, [leading, drive] { leading(); drive(); });
            at(begin + half, [trailing, sample] { trailing(); sample(); });
        }
    }
    at(now + 8 * period, [this] { finish(); });
}

void Spi::finish() {
    events.clear();
    busy     = false;
    received = shiftIn;
    // SPCR may have changed during the transfer, which kept its own mode: SCK now takes the idle level of the new one.
    if (const auto control = data.raw(regs.spcr); pins.port && (control & spe) && (control & mstr))
        pins.port->overrideLevel(pins.sck, (control & cpol) != 0);
    data.raw(regs.spsr) |= spif;
    updateInterrupt();
}

void Spi::updateInterrupt() {
    interrupts.set(vector, (data.raw(regs.spsr) & spif) && (data.raw(regs.spcr) & spie));
}

}  // namespace siliconforge::avr
