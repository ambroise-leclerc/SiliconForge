#include <siliconforge/circuit/Pcf8574.hpp>

namespace siliconforge::circuit {

Pcf8574::Pcf8574(std::uint8_t address) : own(address) {}

void Pcf8574::power(bool on) {
    if (on == supplied)
        return;
    supplied      = on;
    state         = State::Idle;
    acknowledging = false;
    pulling       = false;
    lastScl = lastSda = true;
    latch = 0xFF;  // Power-on reset: every pin a weak high
}

void Pcf8574::bus(bool scl, bool sda, const std::function<std::uint8_t()>& pins, const std::function<void(std::uint8_t)>& written) {
    if (!supplied)
        return;
    const auto send = [&](std::uint8_t bit) { pulling = ((outgoing >> bit) & 1u) == 0; };  // A one is SDA released
    if (scl && lastScl && sda != lastSda) {
        // SDA moving while SCL is high: START (falling) or STOP (rising), whatever was under way.
        acknowledging = false;
        pulling       = false;
        bits          = 0;
        shift         = 0;
        state         = sda ? State::Idle : State::Address;
    } else if (scl && !lastScl) {
        if ((state == State::Address || state == State::Write) && !acknowledging && bits < 8) {
            shift = static_cast<std::uint8_t>(shift << 1 | (sda ? 1u : 0u));
            ++bits;
        } else if (state == State::Read && bits == 8) {
            masterAcked = !sda;
        }
    } else if (!scl && lastScl) {
        if (acknowledging) {  // The end of this chip's acknowledge
            acknowledging = false;
            pulling       = false;
            bits          = 0;
            shift         = 0;
            if (state == State::Read) {
                outgoing = pins();
                ++readCount;
                send(7);
            }
        } else if (state == State::Address && bits == 8) {
            if ((shift >> 1) == own) {
                acknowledging = pulling = true;
                state                   = (shift & 1) ? State::Read : State::Write;
            } else {
                state = State::Idle;  // Another device: wait for the next START
            }
        } else if (state == State::Write && bits == 8) {
            latch = shift;
            ++writeCount;
            written(latch);
            acknowledging = pulling = true;
        } else if (state == State::Read) {
            if (bits < 8) {
                ++bits;
                if (bits < 8)
                    send(static_cast<std::uint8_t>(7 - bits));
                else
                    pulling = false;  // The master acknowledges, or not
            } else if (masterAcked) {
                outgoing = pins();
                ++readCount;
                bits = 0;
                send(7);
            } else {
                state   = State::Idle;
                pulling = false;
            }
        }
    }
    lastScl = scl;
    lastSda = sda;
}

}  // namespace siliconforge::circuit
