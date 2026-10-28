#pragma once

#include <nlohmann/json.hpp>
#include <siliconforge/arch/avr/AvrCore.hpp>
#include <siliconforge/arch/avr/peripherals/Gpio.hpp>
#include <siliconforge/arch/avr/peripherals/Timer8.hpp>
#include <siliconforge/arch/avr/peripherals/Usart.hpp>
#include <siliconforge/core/Mcu.hpp>

#include <functional>
#include <map>
#include <memory>

namespace siliconforge::avr {
/// An AVR device assembled from a validated MCU description. Semantics belong to the architecture, parameters to JSON.
class Device : public Mcu {
public:
    explicit Device(nlohmann::json description, u64 clockOverride = 0);
    [[nodiscard]] std::string_view name() const noexcept override {
        return deviceName;
    }
    [[nodiscard]] std::string_view architecture() const noexcept override {
        return "avr";
    }
    [[nodiscard]] std::string_view chipJson() const noexcept override {
        return chip;
    }
    [[nodiscard]] u64 clockHz() const noexcept override {
        return clock;
    }
    void      load(const ProgramImage& image) override;
    void      reset() override;
    RunResult run(Cycles budget) override {
        return core.run(budget);
    }
    RunResult step() override {
        return core.step();
    }
    [[nodiscard]] Cycles cycles() const noexcept override {
        return core.cycles;
    }
    [[nodiscard]] Address programCounter() const noexcept override {
        return core.pc * 2;
    }
    [[nodiscard]] std::span<Pin> pins() noexcept override {
        return pinList;
    }
    [[nodiscard]] std::span<SerialPort> serialPorts() noexcept override {
        return serialList;
    }
    [[nodiscard]] std::vector<RegisterValue> registers() const override;
    [[nodiscard]] std::vector<u8>            readMemory(MemorySpace space, Address address, std::size_t size) const override;
    [[nodiscard]] Debugger&                  debugger() noexcept override {
        return debug;
    }
    [[nodiscard]] std::string symbolAt(Address address) const override;
    [[nodiscard]] AvrCore&    avrCore() noexcept {
        return core;
    }
    [[nodiscard]] DataSpace& dataSpace() noexcept {
        return data;
    }
    [[nodiscard]] Usart& usart0() {
        return *usarts.at(0);
    }

private:
    Address                                          reg(const nlohmann::json& p, std::string_view key) const;
    unsigned                                         vector(const nlohmann::json& p, std::string_view key) const;
    void                                             makeGpio(const nlohmann::json& p);
    void                                             makeTimer(const nlohmann::json& p);
    void                                             makeUsart(const nlohmann::json& p);
    void                                             makeEeprom(const nlohmann::json& p);
    void                                             makeAdc(const nlohmann::json& p);
    nlohmann::json                                   description;
    std::string                                      deviceName;
    std::string                                      chip;
    u64                                              clock;
    double                                           supplyVoltage;
    Scheduler                                        scheduler;
    Debugger                                         debug;
    InterruptController                              interrupts;
    DataSpace                                        data;
    AvrCore                                          core;
    std::vector<Pin>                                 pinList;
    std::vector<SerialPort>                          serialList;
    std::map<std::string, std::unique_ptr<GpioPort>> ports;
    std::vector<std::unique_ptr<Timer8>>             timers;
    std::vector<std::unique_ptr<Usart>>              usarts;
    std::vector<std::function<void()>>               resetters;
    std::vector<u8>                                  eeprom;
    std::map<Address, std::string>                   symbols;
};
}  // namespace siliconforge::avr
