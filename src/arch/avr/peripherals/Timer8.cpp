#include <siliconforge/arch/avr/peripherals/Timer8.hpp>

#include <algorithm>

namespace siliconforge::avr {

namespace {
constexpr u8 tov    = 1 << 0;
constexpr u8 ocfA   = 1 << 1;
constexpr u8 ocfB   = 1 << 2;
constexpr u8 ocf[2] = {ocfA, ocfB};

constexpr u8 focA   = 1 << 7;
constexpr u8 focB   = 1 << 6;
constexpr u8 wgm2   = 1 << 3;
constexpr u8 csMask = 0x07;
}  // namespace

Timer8::Timer8(DataSpace&           data,
               Scheduler&           scheduler,
               InterruptController& interrupts,
               const Cycles&        clock,
               Registers            registers,
               Vectors              vectors,
               std::array<u16, 8>   prescalers,
               OutputPin            outputA,
               OutputPin            outputB)
    : data(data),
      scheduler(scheduler),
      interrupts(interrupts),
      clock(clock),
      regs(registers),
      vectors(vectors),
      prescalers(prescalers),
      outputs{outputA, outputB} {
    const auto store = [this](Address address, u8 value, u8 mask) {
        auto& stored = this->data.raw(address);
        stored       = static_cast<u8>((stored & ~mask) | (value & mask));
    };

    data.map(regs.tccrA, {nullptr, [this, store](u8 value, u8 mask) {
                              sync(this->clock);
                              store(regs.tccrA, value, mask);
                              updateOutputs();
                              reschedule();
                          }});
    data.map(regs.tccrB, {nullptr, [this, store](u8 value, u8 mask) {
                              sync(this->clock);
                              const auto forced = static_cast<u8>(value & mask & (focA | focB));
                              store(regs.tccrB, static_cast<u8>(value & ~(focA | focB)), mask);
                              // Force Output Compare: the output action of a match, without flag nor interrupt.
                              if (!pwm()) {
                                  if (forced & focA)
                                      compareMatch(2);
                                  if (forced & focB)
                                      compareMatch(3);
                              }
                              reschedule();
                          }});
    data.map(regs.tcnt,
             {[this] {
                  return counter();
              },
              [this](u8 value, u8 mask) {
                  sync(this->clock);
                  tcnt = static_cast<u8>((tcnt & ~mask) | (value & mask));
                  reschedule();
              }});
    for (unsigned unit = 0; unit < 2; ++unit) {
        const auto address = unit == 0 ? regs.ocrA : regs.ocrB;
        data.map(address,
                 {[this, unit] {
                      return ocrBuffer[unit];
                  },
                  [this, unit](u8 value, u8 mask) {
                      sync(this->clock);
                      ocrBuffer[unit] = static_cast<u8>((ocrBuffer[unit] & ~mask) | (value & mask));
                      if (!pwm())
                          ocr[unit] = ocrBuffer[unit];
                      reschedule();
                  }});
    }
    data.map(regs.timsk, {nullptr, [this, store](u8 value, u8 mask) {
                              store(regs.timsk, value, mask);
                              updateInterrupts();
                          }});
    data.map(regs.tifr,
             {[this] {
                  sync(this->clock);
                  return this->data.raw(regs.tifr);
              },
              [this](u8 value, u8 mask) {
                  sync(this->clock);
                  // Flags are cleared by writing a logic one to them.
                  this->data.raw(regs.tifr) &= static_cast<u8>(~(value & mask));
                  updateInterrupts();
              }});

    interrupts.onAcknowledge(vectors.overflow, [this] {
        this->data.raw(regs.tifr) &= static_cast<u8>(~tov);
        updateInterrupts();
    });
    interrupts.onAcknowledge(vectors.compareA, [this] {
        this->data.raw(regs.tifr) &= static_cast<u8>(~ocfA);
        updateInterrupts();
    });
    interrupts.onAcknowledge(vectors.compareB, [this] {
        this->data.raw(regs.tifr) &= static_cast<u8>(~ocfB);
        updateInterrupts();
    });
}

void Timer8::reset(bool preserveRegisters) {
    if (event)
        scheduler.cancel(std::exchange(event, 0));
    if (!preserveRegisters)
        for (const auto address : {regs.tccrA, regs.tccrB, regs.timsk, regs.tifr})
            data.raw(address) = 0;
    tcnt         = preserveRegisters ? data.raw(regs.tcnt) : 0;
    countingDown = false;
    ocr          = preserveRegisters ? std::array<u8, 2>{data.raw(regs.ocrA), data.raw(regs.ocrB)} : std::array<u8, 2>{};
    ocrBuffer    = ocr;
    outputLevel  = {};
    lastSync     = clock;
    updateOutputs();
    updateInterrupts();
    reschedule();
}

Timer8::Mode Timer8::mode() const noexcept {
    switch ((data.raw(regs.tccrA) & 0x03) | ((data.raw(regs.tccrB) & wgm2) >> 1)) {
        case 1:
        case 5:
            return Mode::PhaseCorrect;
        case 2:
            return Mode::Ctc;
        case 3:
        case 7:
            return Mode::FastPwm;
        default:
            return Mode::Normal;
    }
}

u8 Timer8::top() const noexcept {
    const auto wgm = (data.raw(regs.tccrA) & 0x03) | ((data.raw(regs.tccrB) & wgm2) >> 1);
    return (wgm == 2 || wgm == 5 || wgm == 7) ? ocr[0] : u8{0xFF};
}

unsigned Timer8::prescaler() const noexcept {
    return prescalers[data.raw(regs.tccrB) & csMask];
}

void Timer8::sync(Cycles now) {
    if (now <= lastSync)
        return;
    const auto divider = prescaler();
    if (divider != 0)
        advance(now / divider - lastSync / divider);
    lastSync = now;
}

void Timer8::advance(Cycles ticks) {
    if (mode() == Mode::PhaseCorrect) {
        for (; ticks > 0; --ticks)
            tick();
        return;
    }
    while (ticks > 0) {
        // Count without side effects up to the tick that reaches a compare value or wraps, then process that tick.
        const auto distance = ticksToNextEvent();
        if (ticks < distance) {
            tcnt = static_cast<u8>(tcnt + ticks);
            return;
        }
        tcnt = static_cast<u8>(tcnt + distance - 1);
        tick();
        ticks -= distance;
    }
}

void Timer8::tick() {
    // A compare match takes effect on the timer clock that follows TCNTn == OCRnx, hence the comparison with the
    // value before this tick. In Fast PWM this gives a duty cycle of (OCRnx + 1) / (TOP + 1), as in the datasheet.
    const auto previous = tcnt;
    const auto t        = top();
    switch (mode()) {
        case Mode::Normal:
            if (tcnt == 0xFF)
                setFlag(tov);
            ++tcnt;
            break;
        case Mode::Ctc:
            if (tcnt == t) {
                tcnt = 0;
            } else {
                if (tcnt == 0xFF)
                    setFlag(tov);
                ++tcnt;
            }
            break;
        case Mode::FastPwm:
            if (tcnt >= t) {
                tcnt = 0;
                setFlag(tov);
                atBottom();
            } else {
                ++tcnt;
            }
            break;
        case Mode::PhaseCorrect:
            if (!countingDown) {
                ++tcnt;
                if (tcnt >= t) {
                    tcnt         = t;
                    countingDown = true;
                    ocr          = ocrBuffer;
                }
            } else {
                --tcnt;
                if (tcnt == 0) {
                    countingDown = false;
                    setFlag(tov);
                }
            }
            break;
    }
    for (unsigned unit = 0; unit < 2; ++unit)
        if (previous == ocr[unit])
            compareMatch(unit);
}

Cycles Timer8::ticksToNextEvent() const {
    const auto t = top();
    if (mode() == Mode::PhaseCorrect) {
        auto copy = tcnt;
        auto down = countingDown;
        for (Cycles n = 1; n <= 1024; ++n) {
            const auto previous = copy;
            if (!down) {
                ++copy;
                if (copy >= t) {
                    copy = t;
                    down = true;
                }
            } else if (--copy == 0) {
                down = false;
            }
            if (previous == ocr[0] || previous == ocr[1] || copy == t || copy == 0)
                return n;
        }
        return 1024;
    }
    // Up-counting modes: the tick processed by tick() is the one that wraps, or the one that reaches a compare value.
    Cycles limit = 0xFF;
    if (mode() == Mode::Ctc && tcnt <= t)
        limit = t;
    else if (mode() == Mode::FastPwm)
        limit = std::max(t, tcnt);
    auto distance = limit - tcnt + 1;
    for (const auto value : ocr)
        if (value >= tcnt)
            distance = std::min<Cycles>(distance, Cycles{value} - tcnt + 1u);
    return distance;
}

void Timer8::compareMatch(unsigned unit) {
    // Units 2 and 3 are the forced compares of A and B (FOCnx): same output action, no flag.
    const bool forced = unit >= 2;
    unit             &= 1;
    if (!forced)
        setFlag(ocf[unit]);

    const auto com    = (data.raw(regs.tccrA) >> (unit == 0 ? 6 : 4)) & 0x03;
    const bool toggle = unit == 0 && (data.raw(regs.tccrB) & wgm2);
    auto&      level  = outputLevel[unit];
    switch (mode()) {
        case Mode::Normal:
        case Mode::Ctc:
            if (com == 1)
                level = !level;
            else if (com == 2)
                level = false;
            else if (com == 3)
                level = true;
            break;
        case Mode::FastPwm:
            if (com == 1 && toggle)
                level = !level;
            else if (com == 2)
                level = false;
            else if (com == 3)
                level = true;
            break;
        case Mode::PhaseCorrect:
            if (com == 1 && toggle)
                level = !level;
            else if (com == 2)
                level = countingDown;
            else if (com == 3)
                level = !countingDown;
            break;
    }
    updateOutputs();
}

void Timer8::atBottom() {
    ocr = ocrBuffer;
    for (unsigned unit = 0; unit < 2; ++unit) {
        const auto com = (data.raw(regs.tccrA) >> (unit == 0 ? 6 : 4)) & 0x03;
        if (com == 2)
            outputLevel[unit] = true;
        else if (com == 3)
            outputLevel[unit] = false;
    }
    updateOutputs();
}

void Timer8::setFlag(u8 flag) {
    data.raw(regs.tifr) |= flag;
    updateInterrupts();
}

void Timer8::updateInterrupts() {
    const auto pending = static_cast<u8>(data.raw(regs.tifr) & data.raw(regs.timsk));
    interrupts.set(vectors.overflow, pending & tov);
    interrupts.set(vectors.compareA, pending & ocfA);
    interrupts.set(vectors.compareB, pending & ocfB);
}

void Timer8::updateOutputs() {
    for (unsigned unit = 0; unit < 2; ++unit) {
        const auto& output = outputs[unit];
        if (!output.port)
            continue;
        const auto com       = (data.raw(regs.tccrA) >> (unit == 0 ? 6 : 4)) & 0x03;
        const bool toggle    = unit == 0 && (data.raw(regs.tccrB) & wgm2);
        const bool connected = com != 0 && (com != 1 || !pwm() || toggle);
        output.port->overrideLevel(output.bit, connected ? std::optional<bool>{outputLevel[unit]} : std::nullopt);
    }
}

void Timer8::reschedule() {
    if (event)
        scheduler.cancel(std::exchange(event, 0));
    const auto divider = prescaler();
    if (divider == 0)
        return;
    const auto firstTick = (lastSync / divider + 1) * divider;
    const auto at        = firstTick + (ticksToNextEvent() - 1) * divider;
    event                = scheduler.schedule(at, [this](Cycles now) {
        event = 0;
        sync(now);
        reschedule();
    });
}

}  // namespace siliconforge::avr
