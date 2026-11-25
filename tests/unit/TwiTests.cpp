// TWI master (I2C): the lines are open drain and every edge lands on its own cycle, at the rate TWBR and TWPS select; each
// action the firmware starts by clearing TWINT ends with the status code of the datasheet. The device on the bus is the
// PCF8574 model of the circuits, wired to the pins by a bus with pull-ups.

#include "../support/AvrFixture.hpp"

#include <siliconforge/circuit/Pcf8574.hpp>

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

namespace {
constexpr Address twbr = 0xB8, twsr = 0xB9, twdr = 0xBB, twcr = 0xBC;
constexpr u8      twint = 0x80, twea = 0x40, twsta = 0x20, twsto = 0x10, twwc = 0x08, twen = 0x04, twie = 0x01;

/// SDA (PC4) and SCL (PC5) with pull-ups, and a device that answers on them when there is one.
struct Bus {
    AvrFixture                      avr{AvrAssembler{}.rjmp(-1)};
    Pin&                            sda{*avr.mcu.pin("PC4")};
    Pin&                            scl{*avr.mcu.pin("PC5")};
    std::optional<circuit::Pcf8574> device;
    std::uint8_t                    pins{0xFF};  ///< What P0-P7 read.
    std::vector<std::uint8_t>       written;
    bool                            heldLow{false};  ///< Another device holds SDA low.
    std::vector<std::pair<Cycles, bool>> clock, data;  ///< The levels of SCL and SDA, change by change.
    bool                            settling{false};

    explicit Bus(std::optional<std::uint8_t> address = 0x27) {
        if (address) {
            device.emplace(*address);
            device->power(true);
        }
        const auto settle = [this](const Pin&, Cycles) { update(); };
        sda.subscribeDrive(settle);
        scl.subscribeDrive(settle);
        scl.subscribe([this](const Pin& pin, Cycles at) { clock.emplace_back(at, pin.level()); });
        sda.subscribe([this](const Pin& pin, Cycles at) { data.emplace_back(at, pin.level()); });
        update();
        avr.mcu.dataSpace().write(twbr, 72);  // 100 kHz at 16 MHz: 160 cycles per bit
    }
    /// The wired AND of the bus: low if the MCU or a device pulls the line low, high through the pull-ups otherwise.
    void update() {
        if (settling)
            return;
        settling            = true;
        const bool sclLevel = !(scl.isOutput() && scl.state() == PinState::Low);
        const bool masterSda = !(sda.isOutput() && sda.state() == PinState::Low);
        if (device)
            device->bus(sclLevel, masterSda && !heldLow && !device->pullsSda(), [this] { return pins; }, [this](std::uint8_t port) { written.push_back(port); });
        const bool sdaLevel = masterSda && !heldLow && !(device && device->pullsSda());
        if (!scl.isOutput())
            scl.setExternal(sclLevel);
        if (!sda.isOutput())
            sda.setExternal(sdaLevel);
        settling = false;
    }
    void control(u8 value) { avr.mcu.dataSpace().write(twcr, value); }
    void run(Cycles cycles) { avr.mcu.run(cycles); }
    /// Runs until TWINT is set; returns the status.
    u8 await(Cycles limit = 4000) {
        for (Cycles spent = 0; !(avr.io(twcr) & twint) && spent < limit; spent += 8)
            run(8);
        require((avr.io(twcr) & twint) != 0);
        return static_cast<u8>(avr.io(twsr) & 0xF8);
    }
    u8 start() {
        control(twint | twsta | twen);
        return await();
    }
    u8 send(u8 value, u8 extra = 0) {
        avr.mcu.dataSpace().write(twdr, value);
        control(static_cast<u8>(twint | twen | extra));
        return await();
    }
    u8 receive(bool acknowledge) {
        control(static_cast<u8>(twint | twen | (acknowledge ? twea : 0)));
        return await();
    }
    void stop() {
        control(twint | twsto | twen);
        for (Cycles spent = 0; (avr.io(twcr) & twsto) && spent < 4000; spent += 8)
            run(8);
    }
};
}  // namespace

const Scenario aTwiMasterWritesToADevice{"a TWI master writes bytes to a device that acknowledges them", {"twi"}, [] {
    given("a master at 100 kHz and a PCF8574 at 0x27", [&] {
        Bus bus;

        then("the rate is F_CPU / (16 + 2 * TWBR * 4^TWPS)", [&] {
            check(bus.avr.io(twsr) == 0xF8);  // Nothing under way after reset
            bus.control(twen);
            check(bus.sda.level());
            check(bus.scl.level());
            bus.avr.mcu.dataSpace().write(twsr, 0x01);  // Prescaler 4
            bus.avr.mcu.dataSpace().write(twbr, 2);
            bus.clock.clear();
            check(bus.start() == 0x08);
            bus.clock.clear();
            bus.send(0x4E);
            std::vector<Cycles> rising;
            for (const auto& [at, level] : bus.clock)
                if (level)
                    rising.push_back(at);
            require(rising.size() == 9);
            check(rising[1] - rising[0] == 32);  // 16 + 2 * 2 * 4
        });
        when("it sends START, the address for writing and two bytes, then STOP", [&] {
            bus.control(twen);
            bus.clock.clear();
            bus.data.clear();
            const auto started  = bus.start();
            const auto sdaFell  = bus.data.empty() ? Cycles{0} : bus.data.front().first;
            const auto sclFell  = bus.clock.empty() ? Cycles{0} : bus.clock.front().first;
            const auto address  = bus.send(0x27 << 1);
            const auto first    = bus.send(0xA5);
            const auto second   = bus.send(0x3C);
            bus.stop();

            then("each step ends with its status", [&] {
                check(started == 0x08);
                check(address == 0x18);
                check(first == 0x28);
                check(second == 0x28);
            });
            then("START is SDA falling while SCL is high, half a bit before SCL falls", [&] {
                check(!bus.data.front().second);
                check(sclFell - sdaFell == 80);
            });
            then("the device took both bytes, and the STOP left both lines high with TWSTO clear", [&] {
                check(bus.written == std::vector<std::uint8_t>{0xA5, 0x3C});
                check(bus.device->port() == 0x3C);
                check((bus.avr.io(twcr) & twsto) == 0);
                check(bus.sda.level());
                check(bus.scl.level());
                check((bus.avr.io(twsr) & 0xF8) == 0xF8);
            });
            then("SDA changed only while SCL was low, except for START and STOP", [&] {
                std::size_t changesWhileHigh = 0;
                std::string seen;
                for (const auto& [at, level] : bus.data) {
                    bool clockHigh = true;
                    for (const auto& [when, value] : bus.clock)
                        if (when <= at)  // SCL falls before SDA moves within a cycle
                            clockHigh = value;
                    changesWhileHigh += clockHigh ? 1 : 0;
                    if (clockHigh)
                        seen += std::format("SDA {} at {}; ", level ? "rose" : "fell", at);
                }
                const auto note = info(seen);
                check(changesWhileHigh == 2);
            });
        });
        when("the address is another one", [&] {
            bus.control(twen);
            bus.start();
            const auto status = bus.send(0x20 << 1);
            then("nobody acknowledges: 0x20", [&] { check(status == 0x20); });
        });
        when("TWDR is written while a byte is being sent", [&] {
            bus.control(twen);
            bus.start();
            bus.avr.mcu.dataSpace().write(twdr, 0x27 << 1);
            bus.control(twint | twen);
            bus.run(100);
            bus.avr.mcu.dataSpace().write(twdr, 0x55);
            then("TWWC is set and the byte under way is kept", [&] {
                check((bus.avr.io(twcr) & twwc) != 0);
                check(bus.await() == 0x18);
                check(bus.avr.io(twdr) == (0x27 << 1));
            });
        });
    });
}};

const Scenario aTwiMasterReadsFromADevice{"a TWI master reads bytes after a repeated START", {"twi"}, [] {
    given("a PCF8574 whose pins read 0x5A", [&] {
        Bus bus;
        bus.pins = 0x5A;
        bus.control(twen);

        when("the master writes, then reads two bytes after a repeated START, the last one not acknowledged", [&] {
            check(bus.start() == 0x08);
            check(bus.send(0x27 << 1) == 0x18);
            check(bus.send(0xFF) == 0x28);
            const auto restarted = bus.start();
            const auto address   = bus.send(0x27 << 1 | 1);
            bus.pins             = 0xC3;  // Sampled for the second byte, at the acknowledge of the first: the first was sampled already
            const auto first     = bus.receive(true);
            const auto value1    = bus.avr.io(twdr);
            const auto second    = bus.receive(false);
            const auto value2    = bus.avr.io(twdr);
            bus.stop();

            then("the statuses are those of the master receiver", [&] {
                check(restarted == 0x10);
                check(address == 0x40);
                check(first == 0x50);
                check(second == 0x58);
            });
            then("TWDR holds what the pins read at the acknowledge before each byte", [&] {
                check(value1 == 0x5A);
                check(value2 == 0xC3);
                check(bus.device->reads() == 2);
            });
        });
    });
}};

const Scenario aTwiMasterLosesTheBus{"a TWI master loses the arbitration and raises its interrupt", {"twi"}, [] {
    given("a bus whose SDA another device holds low", [&] {
        Bus bus{std::nullopt};
        bus.control(twen);
        bus.start();
        bus.heldLow = true;
        bus.update();

        when("the master sends a one", [&] {
            const auto status = bus.send(0x80);
            then("it lost the arbitration and released both lines", [&] {
                check(status == 0x38);
                check(!bus.scl.isOutput());
                check(!bus.sda.isOutput());
            });
        });
    });
    given("TWIE set", [&] {
        Bus bus;
        bus.control(twen | twie);
        bus.control(twint | twsta | twen | twie);
        bus.await();
        then("TWINT requests the TWI vector", [&] {
            check(bus.avr.core.interrupts.isPending(24));
            check((bus.avr.io(twcr) & twint) != 0);
        });
        then("clearing TWEN releases the pins to PORTx and DDRx", [&] {
            bus.control(0);
            check(!bus.scl.isOutput());
            check((bus.avr.io(twsr) & 0xF8) == 0xF8);
        });
    });
}};
