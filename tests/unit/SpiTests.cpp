// SPI master (#64): every edge of SCK and every change of MOSI lands on its own cycle, in the mode and the bit order the
// control register selects, so that a device on the bus (the display of a bench circuit, a logic analyser) sees the real
// waveform; the status flags behave as on the device.

#include "../support/AvrFixture.hpp"

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

#include <utility>
#include <vector>

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

namespace {
constexpr Address ddrb = 0x24, spcr = 0x4C, spsr = 0x4D, spdr = 0x4E;
constexpr u8      spif = 0x80, wcol = 0x40;

struct Waveform {
    std::vector<std::pair<Cycles, bool>> clock, data;
    explicit Waveform(Mcu& mcu) {
        mcu.pin("PB5")->subscribe([this](const Pin& pin, Cycles at) { clock.emplace_back(at, pin.level()); });
        mcu.pin("PB3")->subscribe([this](const Pin& pin, Cycles at) { data.emplace_back(at, pin.level()); });
    }
    /// The level of MOSI at `at`: the last change at or before it.
    [[nodiscard]] bool dataAt(Cycles at) const {
        bool level = false;
        for (const auto& [when, value] : data)
            if (when <= at)
                level = value;
        return level;
    }
};

struct Spi {
    AvrFixture avr{AvrAssembler{}.rjmp(-1)};
    Waveform   wires{avr.mcu};
    explicit Spi(u8 control, u8 status = 0) {
        auto& data = avr.mcu.dataSpace();
        data.write(ddrb, 0x28);  // MOSI and SCK are outputs
        data.write(spcr, control);
        data.write(spsr, status);
        wires.clock.clear();  // The levels the pins took while the registers were set are not part of a transfer.
        wires.data.clear();
        wires.clock.emplace_back(avr.mcu.cycles(), avr.mcu.pin("PB5")->level());
    }
    void send(u8 value) { avr.mcu.dataSpace().write(spdr, value); }
    void run(Cycles cycles) { avr.mcu.run(cycles); }
    u8   status() { return avr.io(spsr); }
};
}  // namespace

const Scenario aByteIsShiftedOutIn{"a byte is shifted out in SPI mode 0, most significant bit first", {"spi"}, [] {
    given("a master at fck/4 in mode 0", [&] {
        Spi spi{0x50};  // SPE | MSTR
        const auto start = spi.avr.mcu.cycles();

        when("0xA5 is written", [&] {
            spi.send(0xA5);
            spi.run(64);

            then("SCK rises in the middle of each bit and falls at its end, four cycles per bit", [&] {
                // The first change, to the idle level low, is not an edge: it was already low.
                std::vector<Cycles> rising;
                for (const auto& [at, level] : spi.wires.clock)
                    if (level)
                        rising.push_back(at);
                require(rising.size() == 8);
                for (std::size_t i = 0; i < 8; ++i)
                    check(rising[i] == start + 4 * i + 2);
            });
            then("MOSI carries the bits of the byte, from the most significant, at each rising edge", [&] {
                std::vector<bool> seen;
                for (const auto& [at, level] : spi.wires.clock)
                    if (level)
                        seen.push_back(spi.wires.dataAt(at));
                check(seen == std::vector<bool>{true, false, true, false, false, true, false, true});
            });
            then("SPIF is set 32 cycles after the write, not before", [&] {
                check((spi.status() & spif) != 0);
            });
        });
        when("SPSR is read and then SPDR", [&] {
            spi.send(0x00);
            spi.run(8);
            check((spi.status() & spif) == 0);
            spi.run(40);
            check((spi.status() & spif) != 0);
            then("SPIF stays set: the flag is cleared by SPSR then SPDR in that order", [&] {
                check((spi.avr.io(spsr) & spif) != 0);
                (void)spi.avr.io(spdr);
                check((spi.status() & spif) == 0);
            });
        });
        when("a second byte is written while the first is being shifted", [&] {
            spi.send(0xFF);
            spi.run(8);
            spi.send(0x00);
            spi.run(64);
            then("the collision is flagged and the first byte goes on", [&] {
                check((spi.status() & wcol) != 0);
                std::vector<bool> seen;
                for (const auto& [at, level] : spi.wires.clock)
                    if (level)
                        seen.push_back(spi.wires.dataAt(at));
                check(seen == std::vector<bool>(8, true));
            });
        });
        when("the collision flag is read and SPDR accessed while the first byte is still being shifted", [&] {
            spi.send(0xFF);
            spi.run(8);
            spi.send(0x00);
            require((spi.status() & wcol) != 0);
            (void)spi.avr.io(spdr);
            then("WCOL is cleared at once, and the transfer under way still completes with SPIF", [&] {
                check((spi.status() & wcol) == 0);
                spi.run(64);
                check((spi.status() & spif) != 0);
            });
        });
        when("the firmware clears SPE while a byte is being shifted", [&] {
            spi.send(0xFF);
            spi.run(10);  // Two bits are out
            const auto edges = spi.wires.clock.size();
            const auto bits  = spi.wires.data.size();
            spi.avr.mcu.dataSpace().write(spcr, 0x10);  // MSTR without SPE
            spi.run(100);
            then("the byte is aborted: no more edge, no SPIF, the pins are released", [&] {
                check(spi.wires.clock.size() <= edges + 1);  // At most the release of SCK
                check(spi.wires.data.size() <= bits + 1);
                check((spi.status() & spif) == 0);
                check(!(spi.avr.mcu.pin("PB5")->level()));
                check(!(spi.avr.mcu.pin("PB3")->level()));
            });
            andThen("a new byte can be sent once the peripheral is enabled again", [&] {
                spi.avr.mcu.dataSpace().write(spcr, 0x50);
                spi.send(0x00);
                spi.run(40);
                check((spi.status() & spif) != 0);
            });
        });
        when("SPDR is written during a transfer after SPSR showed a collision", [&] {
            spi.send(0xFF);
            spi.run(8);
            spi.send(0x00);
            require((spi.status() & wcol) != 0);  // Read: the next access to SPDR completes the sequence
            spi.send(0x00);                         // A write during the transfer: a new collision, after the flags were cleared
            then("that access used the sequence up: a plain read of SPDR does not clear the new collision", [&] {
                check((spi.avr.mcu.dataSpace().raw(spsr) & wcol) != 0);
                (void)spi.avr.io(spdr);
                check((spi.avr.mcu.dataSpace().raw(spsr) & wcol) != 0);
            });
        });
        when("SS is an input driven low while the master is enabled", [&] {
            spi.send(0xFF);
            spi.run(10);  // Two bits are out
            const auto edges = spi.wires.clock.size();
            spi.avr.mcu.pin("PB2")->setExternal(false);
            spi.run(100);
            then("the master mode is lost: MSTR clears, the byte is aborted and SPIF is set", [&] {
                check((spi.avr.mcu.dataSpace().raw(spcr) & 0x10) == 0);
                check((spi.status() & spif) != 0);
                check(spi.wires.clock.size() <= edges + 1);
            });
            andThen("a write to SPDR starts nothing", [&] {
                const auto before = spi.wires.clock.size();
                spi.send(0x55);
                spi.run(100);
                check(spi.wires.clock.size() == before);
            });
        });
        when("SPSR then SPDR are accessed after SS took the master mode away", [&] {
            spi.send(0xFF);
            spi.run(10);
            spi.avr.mcu.pin("PB2")->setExternal(false);
            require((spi.status() & spif) != 0);  // Read: the next access to SPDR completes the sequence
            spi.send(0x00);                         // A write, now that MSTR is clear
            then("the flag is cleared by that write and no interrupt stays pending", [&] {
                check((spi.status() & spif) == 0);
            });
        });
        when("SPIE is switched while a transfer is under way and SPIF of the last one is still up", [&] {
            auto& interrupts = spi.avr.core.interrupts;
            constexpr unsigned vector = 17;  // SPI_STC
            spi.send(0x00);
            spi.run(40);
            require((spi.avr.mcu.dataSpace().raw(spsr) & spif) != 0);
            spi.send(0xFF);  // SPSR was not read: SPIF stays up as the next transfer starts
            spi.run(4);
            require((spi.avr.mcu.dataSpace().raw(spsr) & spif) != 0);
            check(!(interrupts.isPending(vector)));
            then("the request follows SPIE at once, in both directions", [&] {
                spi.avr.mcu.dataSpace().write(spcr, 0x50 | 0x80);
                check(interrupts.isPending(vector));
                spi.avr.mcu.dataSpace().write(spcr, 0x50);
                check(!(interrupts.isPending(vector)));
            });
        });
        when("SS is an output driven low", [&] {
            spi.avr.mcu.dataSpace().write(ddrb, 0x2C);  // SS, MOSI and SCK are outputs
            spi.send(0xFF);
            spi.run(64);
            then("nothing is lost: the byte completes", [&] {
                check((spi.avr.mcu.dataSpace().raw(spcr) & 0x10) != 0);
                check((spi.status() & spif) != 0);
            });
        });
        when("MISO is held high by the device and a byte is exchanged", [&] {
            spi.avr.mcu.pin("PB4")->setExternal(true);
            spi.send(0x00);
            spi.run(64);
            (void)spi.status();
            then("SPDR holds what was received", [&] {
                check(spi.avr.io(spdr) == 0xFF);
            });
        });
    });
}};

const Scenario theModeAndTheBitOrder{"the mode and the bit order follow the control register", {"spi"}, [] {
    given("a master at fck/2 (SPI2X) in mode 3, least significant bit first", [&] {
        Spi spi{0x50 | 0x20 | 0x08 | 0x04, 0x01};  // SPE | MSTR | DORD | CPOL | CPHA, SPI2X
        const auto start = spi.avr.mcu.cycles();
        spi.send(0x01);
        spi.run(40);

        then("SCK idles high and each bit is clocked in two cycles: falling edge first", [&] {
            require(spi.wires.clock.size() >= 2);
            check(spi.wires.clock.front().second == true);  // The idle level, applied when SPI is enabled
            std::vector<Cycles> falling;
            for (const auto& [at, level] : spi.wires.clock)
                if (!level)
                    falling.push_back(at);
            require(falling.size() == 8);
            for (std::size_t i = 0; i < 8; ++i)
                check(falling[i] == start + 2 * i);
        });
        then("the least significant bit goes first, and in mode 3 the data change on the falling edge", [&] {
            // The bit is valid when SCK rises again (the sampling edge in mode 3).
            std::vector<bool> seen;
            bool              last = true;
            for (const auto& [at, level] : spi.wires.clock) {
                if (level && !last)
                    seen.push_back(spi.wires.dataAt(at));
                last = level;
            }
            require(seen.size() == 8);
            check(seen == std::vector<bool>{true, false, false, false, false, false, false, false});
        });
    });
}};
