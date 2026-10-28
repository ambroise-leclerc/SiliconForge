#pragma once

#include <siliconforge/arch/avr/DataSpace.hpp>
#include <siliconforge/arch/avr/InterruptController.hpp>
#include <siliconforge/arch/avr/peripherals/Gpio.hpp>
#include <siliconforge/core/Scheduler.hpp>

#include <array>

namespace siliconforge::avr {

/// 8-bit Timer/Counter with two output compare units (Timer/Counter0 of the megaAVR family).
///
/// Supports the Normal, CTC, Fast PWM and Phase Correct PWM modes, the double-buffered OCRnx of the PWM modes, the
/// output compare pins and the three interrupts. The counter is not ticked cycle by cycle: it is brought up to date
/// lazily when firmware reads it, and an event is scheduled at the next compare match or overflow.
class Timer8 {
public:
    struct Registers {
        Address tccrA, tccrB, tcnt, ocrA, ocrB, timsk, tifr;
    };
    struct Vectors {
        unsigned compareA, compareB, overflow;
    };
    struct OutputPin {
        GpioPort* port;
        unsigned  bit;
    };

    /// `prescalers[cs]` gives the clock division for each CSn2:0 value, 0 meaning stopped (or external clock, which
    /// is not supported yet).
    Timer8(DataSpace&           data,
           Scheduler&           scheduler,
           InterruptController& interrupts,
           const Cycles&        clock,
           Registers            registers,
           Vectors              vectors,
           std::array<u16, 8>   prescalers,
           OutputPin            outputA,
           OutputPin            outputB);

    /// Keep JSON-provided register reset values when assembling a described device.
    void reset(bool preserveRegisters = false);

    /// Brings the counter up to `now`.
    void sync(Cycles now);

    [[nodiscard]] u8 counter() noexcept {
        sync(clock);
        return tcnt;
    }

private:
    enum class Mode { Normal, PhaseCorrect, Ctc, FastPwm };

    [[nodiscard]] Mode     mode() const noexcept;
    [[nodiscard]] u8       top() const noexcept;
    [[nodiscard]] unsigned prescaler() const noexcept;
    [[nodiscard]] bool     pwm() const noexcept {
        return mode() == Mode::FastPwm || mode() == Mode::PhaseCorrect;
    }

    void                 tick();
    void                 advance(Cycles ticks);
    void                 compareMatch(unsigned unit);
    void                 atBottom();
    void                 setFlag(u8 flag);
    void                 updateInterrupts();
    void                 updateOutputs();
    void                 reschedule();
    [[nodiscard]] Cycles ticksToNextEvent() const;

    DataSpace&               data;
    Scheduler&               scheduler;
    InterruptController&     interrupts;
    const Cycles&            clock;
    Registers                regs;
    Vectors                  vectors;
    std::array<u16, 8>       prescalers;
    std::array<OutputPin, 2> outputs;

    u8                  tcnt{0};
    bool                countingDown{false};
    std::array<u8, 2>   ocr{};        ///< Active compare values.
    std::array<u8, 2>   ocrBuffer{};  ///< Values written by firmware, copied to `ocr` at TOP/BOTTOM in PWM modes.
    std::array<bool, 2> outputLevel{};
    Cycles              lastSync{0};
    Scheduler::EventId  event{0};
};

}  // namespace siliconforge::avr
