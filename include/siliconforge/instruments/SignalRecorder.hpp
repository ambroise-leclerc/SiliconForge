#pragma once

#include <siliconforge/core/Pin.hpp>
#include <siliconforge/core/Types.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <vector>

namespace siliconforge::instruments {

/// One change of a signal: the instant, the channel (index of the pin in Mcu::pins()), its new logical state and the
/// voltage a probe would read.
struct Sample {
    Cycles   at;
    float    volts;
    u16      channel;
    PinState state;
};

/// Capture memory of the logic analyser and the oscilloscope: the changes of every pin, time-stamped in clock cycles,
/// in a ring that keeps the most recent ones. Samples are numbered from 0 in the order they were recorded; a reader
/// keeps the number of the next sample it wants and learns from read() whether the ring overtook it.
///
/// Recording is one store per pin change, so it is always on. Not thread-safe: the owner serialises recording and
/// reading (the application does both under the simulation lock).
class SignalRecorder {
public:
    explicit SignalRecorder(std::size_t capacity = std::size_t{1} << 18) : ring(std::bit_ceil(std::max<std::size_t>(capacity, 2))) {}

    [[nodiscard]] std::size_t capacity() const noexcept { return ring.size(); }
    /// Changes each time the capture restarts: sample numbers of another epoch mean nothing.
    [[nodiscard]] u64 epoch() const noexcept { return currentEpoch; }
    /// Number of the oldest sample still held.
    [[nodiscard]] u64 first() const noexcept { return written > ring.size() ? written - ring.size() : 0; }
    /// Number the next recorded sample will get.
    [[nodiscard]] u64 next() const noexcept { return written; }

    void record(u16 channel, Cycles at, PinState state, double volts) noexcept {
        ring[written++ & (ring.size() - 1)] = Sample{at, static_cast<float>(volts), channel, state};
    }

    /// Forgets everything and starts a new epoch (reset, new firmware, new MCU).
    void restart() noexcept {
        written = 0;
        ++currentEpoch;
    }

    /// Calls visit(sample) for at most `limit` samples starting at number `from` (or at first() when the ring already
    /// overwrote it) and returns the number of the sample after the last one visited. Any limit is valid, the largest
    /// included: it is compared with what is held, never added to a sample number.
    template <typename Visit>
    u64 read(u64 from, std::size_t limit, Visit&& visit) const {
        auto       position = std::clamp(from, first(), written);
        const auto end      = position + std::min<u64>(limit, written - position);
        for (; position < end; ++position)
            visit(ring[position & (ring.size() - 1)]);
        return position;
    }

private:
    std::vector<Sample> ring;
    u64                 written{0};
    u64                 currentEpoch{0};
};

}  // namespace siliconforge::instruments
