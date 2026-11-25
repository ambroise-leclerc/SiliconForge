#include <siliconforge/arch/avr/peripherals/Twi.hpp>

#include <algorithm>
#include <memory>
#include <utility>

namespace siliconforge::avr {

namespace {
// TWCR
constexpr u8 twint = 1 << 7;
constexpr u8 twea  = 1 << 6;
constexpr u8 twsta = 1 << 5;
constexpr u8 twsto = 1 << 4;
constexpr u8 twwc  = 1 << 3;
constexpr u8 twen  = 1 << 2;
constexpr u8 twie  = 1;
// TWSR
constexpr u8 twps = 0x03;
// Status codes of the master modes
constexpr u8 started       = 0x08;
constexpr u8 restarted     = 0x10;
constexpr u8 writeAcked    = 0x18;
constexpr u8 writeNacked   = 0x20;
constexpr u8 dataAcked     = 0x28;
constexpr u8 dataNacked    = 0x30;
constexpr u8 arbitration   = 0x38;
constexpr u8 readAcked     = 0x40;
constexpr u8 readNacked    = 0x48;
constexpr u8 receivedAck   = 0x50;
constexpr u8 receivedNack  = 0x58;
constexpr u8 noInformation = 0xF8;
}  // namespace

Twi::Twi(DataSpace& data, Scheduler& scheduler, InterruptController& interrupts, const Cycles& clock, Registers registers, unsigned vector, Pins pins)
    : data(data), scheduler(scheduler), interrupts(interrupts), clock(clock), regs(registers), vector(vector), pins(pins) {
    data.map(regs.twcr, {nullptr, [this](u8 value, u8 mask) {
                             auto&      stored = this->data.raw(regs.twcr);
                             const auto merged = static_cast<u8>((stored & ~mask) | (value & mask));
                             // TWINT is cleared by writing a one; TWWC is read-only.
                             const bool clearing = (value & mask & twint) != 0;
                             stored = static_cast<u8>((merged & ~(twint | twwc)) | (stored & (twint | twwc)));
                             if (clearing)
                                 stored &= static_cast<u8>(~twint);
                             controlChanged(clearing ? twint : 0);
                         }});
    data.map(regs.twsr, {nullptr, [this](u8 value, u8 mask) {
                             auto& stored = this->data.raw(regs.twsr);
                             stored       = static_cast<u8>((stored & ~(mask & twps)) | (value & mask & twps));  // The status is read-only
                         }});
    data.map(regs.twdr, {nullptr, [this](u8 value, u8 mask) {
                             auto& control = this->data.raw(regs.twcr);
                             if (!(control & twint)) {
                                 control |= twwc;  // Written while the peripheral works: ignored
                                 return;
                             }
                             control &= static_cast<u8>(~twwc);
                             auto& stored = this->data.raw(regs.twdr);
                             stored       = static_cast<u8>((stored & ~mask) | (value & mask));
                         }});
}

void Twi::reset(bool preserveRegisters) {
    for (const auto event : std::exchange(events, {}))
        scheduler.cancel(event);
    busy = owner = reading = lost = false;
    status = noInformation;
    if (!preserveRegisters) {
        data.raw(regs.twcr) = 0;
        data.raw(regs.twbr) = 0;
        data.raw(regs.twdr) = 0xFF;
        data.raw(regs.twar) = 0xFE;
    }
    data.raw(regs.twsr) = static_cast<u8>(noInformation | (data.raw(regs.twsr) & twps));
    if (pins.port) {
        pins.port->openDrain(pins.sda, std::nullopt);
        pins.port->openDrain(pins.scl, std::nullopt);
    }
    controlChanged(0);
}

Cycles Twi::bitCycles() const noexcept {
    static constexpr Cycles prescalers[] = {1, 4, 16, 64};
    return 16 + 2 * static_cast<Cycles>(data.raw(regs.twbr)) * prescalers[data.raw(regs.twsr) & twps];
}

void Twi::line(unsigned bit, bool released) {
    if (pins.port)
        pins.port->openDrain(bit, released);
}

bool Twi::sda() const {
    const auto* pin = pins.port ? pins.port->pin(pins.sda) : nullptr;
    return pin && pin->level();
}

void Twi::at(Cycles when, std::function<void()> action) {
    events.push_back(scheduler.schedule(when, [action = std::move(action)](Cycles) { action(); }));
}

void Twi::abort() {
    for (const auto event : std::exchange(events, {}))
        scheduler.cancel(event);
    busy = owner = lost = false;
}

void Twi::controlChanged(u8 written) {
    const auto control = data.raw(regs.twcr);
    if (!(control & twen)) {
        // Disabled: whatever was under way stops and the pins go back to PORTx and DDRx.
        abort();
        status = noInformation;
        data.raw(regs.twsr) = static_cast<u8>(noInformation | (data.raw(regs.twsr) & twps));
        if (pins.port) {
            pins.port->openDrain(pins.sda, std::nullopt);
            pins.port->openDrain(pins.scl, std::nullopt);
        }
        updateInterrupt();
        return;
    }
    if (!busy && !owner) {  // Enabled and idle: both lines released
        line(pins.sda, true);
        line(pins.scl, true);
    }
    if (written & twint)
        act();
    updateInterrupt();
}

void Twi::act() {
    if (busy)
        return;
    const auto control = data.raw(regs.twcr);
    if ((control & twsto) && (control & twsta))
        stop(true);
    else if (control & twsta)
        start(owner);
    else if (control & twsto)
        stop(false);
    else
        switch (status) {
            case started:
            case restarted: transmit(data.raw(regs.twdr), true); break;
            case writeAcked:
            case writeNacked:
            case dataAcked:
            case dataNacked: transmit(data.raw(regs.twdr), false); break;
            case readAcked:
            case receivedAck: receive((control & twea) != 0); break;
            default: break;  // Nothing to do before a START or a STOP
        }
}

void Twi::start(bool repeated) {
    busy        = true;
    const auto period = bitCycles(), half = std::max<Cycles>(period / 2, 1);
    auto       t      = clock;
    if (repeated) {  // SCL is low: SDA, then SCL, go high first
        at(t, [this] { line(pins.sda, true); });
        at(t + half, [this] { line(pins.scl, true); });
        t += period;
    }
    at(t, [this] { line(pins.sda, false); });  // SDA falls while SCL is high
    at(t + half, [this, repeated] {
        line(pins.scl, false);
        owner = true;
        complete(repeated ? restarted : started);
    });
}

void Twi::stop(bool thenStart) {
    busy        = true;
    const auto period = bitCycles(), half = std::max<Cycles>(period / 2, 1);
    const auto t      = clock;
    const auto done   = [this, thenStart] {
        events.clear();
        busy  = false;
        owner = false;
        data.raw(regs.twcr) &= static_cast<u8>(~twsto);
        status              = noInformation;
        data.raw(regs.twsr) = static_cast<u8>(noInformation | (data.raw(regs.twsr) & twps));
        if (thenStart)
            start(false);
    };
    if (!owner) {  // Not holding the bus: nothing to release
        at(t, done);
        return;
    }
    at(t, [this] { line(pins.sda, false); });
    at(t + half, [this] { line(pins.scl, true); });
    at(t + period, [this, done] {
        line(pins.sda, true);  // SDA rises while SCL is high
        done();
    });
}

void Twi::transmit(u8 value, bool address) {
    busy = true;
    lost = false;
    if (address)
        reading = (value & 1) != 0;
    const auto period = bitCycles(), half = std::max<Cycles>(period / 2, 1);
    const auto t      = clock;
    for (unsigned i = 0; i < 8; ++i) {
        const bool one   = ((value >> (7 - i)) & 1u) != 0;
        const auto begin = t + i * period;
        at(begin, [this, one] {
            if (!lost)
                line(pins.sda, one);
        });
        at(begin + half, [this, one] {
            if (lost)
                return;
            line(pins.scl, true);
            if (one && !sda()) {  // Another device holds SDA low: the arbitration is lost
                lost = true;
                line(pins.sda, true);
            }
        });
        at(begin + period, [this] {
            if (!lost)
                line(pins.scl, false);
        });
    }
    const auto acknowledge = t + 8 * period;
    auto       acked       = std::make_shared<bool>(false);
    at(acknowledge, [this] {
        if (!lost)
            line(pins.sda, true);  // The device answers on SDA
    });
    at(acknowledge + half, [this, acked] {
        if (lost)
            return;
        line(pins.scl, true);
        *acked = !sda();
    });
    at(acknowledge + period, [this, acked, address] {
        if (lost) {
            owner = false;
            line(pins.sda, true);
            line(pins.scl, true);
            complete(arbitration);
            return;
        }
        line(pins.scl, false);
        complete(address ? (reading ? (*acked ? readAcked : readNacked) : (*acked ? writeAcked : writeNacked)) : (*acked ? dataAcked : dataNacked));
    });
}

void Twi::receive(bool acknowledge) {
    busy  = true;
    shift = 0;
    const auto period = bitCycles(), half = std::max<Cycles>(period / 2, 1);
    const auto t      = clock;
    at(t, [this] { line(pins.sda, true); });  // The device drives SDA
    for (unsigned i = 0; i < 8; ++i) {
        const auto begin = t + i * period;
        at(begin + half, [this] {
            line(pins.scl, true);
            shift = static_cast<u8>(shift << 1 | (sda() ? 1u : 0u));
        });
        at(begin + period, [this] { line(pins.scl, false); });
    }
    const auto answer = t + 8 * period;
    at(answer, [this, acknowledge] { line(pins.sda, !acknowledge); });
    at(answer + half, [this] { line(pins.scl, true); });
    at(answer + period, [this, acknowledge] {
        line(pins.scl, false);
        line(pins.sda, true);
        data.raw(regs.twdr) = shift;
        complete(acknowledge ? receivedAck : receivedNack);
    });
}

void Twi::complete(u8 code) {
    events.clear();
    busy                = false;
    status              = code;
    data.raw(regs.twsr) = static_cast<u8>(code | (data.raw(regs.twsr) & twps));
    data.raw(regs.twcr) |= twint;
    updateInterrupt();
}

void Twi::updateInterrupt() {
    const auto control = data.raw(regs.twcr);
    interrupts.set(vector, (control & twint) && (control & twie) && (control & twen));
}

}  // namespace siliconforge::avr
