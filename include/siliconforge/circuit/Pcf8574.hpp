#pragma once

#include <cstdint>
#include <functional>

namespace siliconforge::circuit {

/// A PCF8574 (or PCF8574A) I/O expander on an I2C bus, seen from SDA and SCL: the chip of the I2C backpacks of character
/// LCDs.
///
/// It answers its 7-bit address (0x20-0x27, 0x38-0x3F for the A version) by pulling SDA low during the acknowledge clock.
/// Written, each data byte becomes the state of P0-P7 at its acknowledge; read, it sends the levels of P0-P7, sampled at the
/// acknowledge of the address and of each byte the master acknowledged, until the master does not acknowledge. The port is
/// quasi-bidirectional: a bit at one is a weak high that the outside can pull low, a bit at zero a strong low. START, repeated
/// START and STOP are recognised anywhere; a byte for another address is ignored until the next START.
class Pcf8574 {
public:
    explicit Pcf8574(std::uint8_t address = 0x27);

    [[nodiscard]] std::uint8_t address() const noexcept { return own; }
    /// Supply on or off: at power on the port is all ones (inputs), the bus logic idle.
    void power(bool on);
    /// The levels of SCL and SDA now (true: high); an edge since the previous call is acted on. `pins` gives the levels of
    /// P0-P7 when the chip has to send them; `written` is called with the new port after a write.
    void bus(bool scl, bool sda, const std::function<std::uint8_t()>& pins, const std::function<void(std::uint8_t)>& written);

    /// Whether the chip pulls SDA low now (an acknowledge, or a zero it sends).
    [[nodiscard]] bool pullsSda() const noexcept { return pulling; }
    /// The state written to P0-P7 (ones are weak highs).
    [[nodiscard]] std::uint8_t port() const noexcept { return latch; }

    [[nodiscard]] std::size_t writes() const noexcept { return writeCount; }  ///< Bytes written to the port.
    [[nodiscard]] std::size_t reads() const noexcept { return readCount; }    ///< Bytes sent to the master.

private:
    enum class State { Idle, Address, Write, Read };

    std::uint8_t own;
    bool         supplied{false};
    bool         lastScl{true}, lastSda{true};
    State        state{State::Idle};
    unsigned     bits{0};
    std::uint8_t shift{0};
    bool         acknowledging{false};  ///< This chip holds the acknowledge clock.
    bool         masterAcked{false};
    std::uint8_t outgoing{0};
    bool         pulling{false};
    std::uint8_t latch{0xFF};
    std::size_t  writeCount{0}, readCount{0};
};

}  // namespace siliconforge::circuit
