#include <siliconforge/arch/avr/peripherals/Gpio.hpp>

namespace siliconforge::avr {

namespace {
constexpr u8 pullUpDisable = 1 << 4;
}

GpioPort::GpioPort(DataSpace& data, Registers registers, std::array<Pin*, 8> pins, Address mcucr)
    : data(data), regs(registers), pins(pins), mcucr(mcucr) {
    data.map(regs.pin, {[this] { return input(); },
                        [this](u8 value, u8 mask) {
                            // Writing a logic one to PINxn toggles PORTxn.
                            this->data.raw(regs.port) ^= static_cast<u8>(value & mask);
                            refresh();
                        }});
    data.map(regs.ddr, {nullptr, [this](u8 value, u8 mask) {
                            auto& ddrValue = this->data.raw(regs.ddr);
                            ddrValue       = static_cast<u8>((ddrValue & ~mask) | (value & mask));
                            refresh();
                        }});
    data.map(regs.port, {nullptr, [this](u8 value, u8 mask) {
                             auto& portValue = this->data.raw(regs.port);
                             portValue       = static_cast<u8>((portValue & ~mask) | (value & mask));
                             refresh();
                         }});
}

void GpioPort::reset() {
    data.raw(regs.ddr)  = 0;
    data.raw(regs.port) = 0;
    levelOverrides.fill(std::nullopt);
    outputOverrides.fill(std::nullopt);
    refresh();
}

void GpioPort::overrideLevel(unsigned bit, std::optional<bool> level) {
    levelOverrides[bit] = level;
    update(bit);
}

void GpioPort::forceOutput(unsigned bit, std::optional<bool> level) {
    outputOverrides[bit] = level;
    update(bit);
}

void GpioPort::refresh() {
    for (unsigned bit = 0; bit < 8; ++bit)
        update(bit);
}

u8 GpioPort::input() const noexcept {
    u8 value = 0;
    for (unsigned bit = 0; bit < 8; ++bit)
        if (pins[bit] && pins[bit]->level())
            value |= static_cast<u8>(1 << bit);
    return value;
}

void GpioPort::update(unsigned bit) {
    auto* pin = pins[bit];
    if (!pin)
        return;
    const auto mask = static_cast<u8>(1 << bit);
    if (outputOverrides[bit]) {
        pin->configure(true, *outputOverrides[bit], false);
        return;
    }
    const bool output    = data.raw(regs.ddr) & mask;
    const bool portValue = data.raw(regs.port) & mask;
    const bool level     = levelOverrides[bit].value_or(portValue);
    const bool pullUp    = !output && portValue && !(data.raw(mcucr) & pullUpDisable);
    pin->configure(output, level, pullUp);
}

}  // namespace siliconforge::avr
