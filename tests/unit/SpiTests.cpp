// SPI master (#64): every edge of SCK and every change of MOSI lands on its own cycle, in the mode and the bit order the
// control register selects, so that a device on the bus (the display of a bench circuit, a logic analyser) sees the real
// waveform; the status flags behave as on the device.

#include "../support/AvrFixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

namespace {
constexpr Address ddrb = 0x24, portb = 0x25, spcr = 0x4C, spsr = 0x4D, spdr = 0x4E;
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

SCENARIO("a byte is shifted out in SPI mode 0, most significant bit first", "[spi]") {
    GIVEN("a master at fck/4 in mode 0") {
        Spi spi{0x50};  // SPE | MSTR
        const auto start = spi.avr.mcu.cycles();

        WHEN("0xA5 is written") {
            spi.send(0xA5);
            spi.run(64);

            THEN("SCK rises in the middle of each bit and falls at its end, four cycles per bit") {
                // The first change, to the idle level low, is not an edge: it was already low.
                std::vector<Cycles> rising;
                for (const auto& [at, level] : spi.wires.clock)
                    if (level)
                        rising.push_back(at);
                REQUIRE(rising.size() == 8);
                for (std::size_t i = 0; i < 8; ++i)
                    CHECK(rising[i] == start + 4 * i + 2);
            }
            THEN("MOSI carries the bits of the byte, from the most significant, at each rising edge") {
                std::vector<bool> seen;
                for (const auto& [at, level] : spi.wires.clock)
                    if (level)
                        seen.push_back(spi.wires.dataAt(at));
                CHECK(seen == std::vector<bool>{true, false, true, false, false, true, false, true});
            }
            THEN("SPIF is set 32 cycles after the write, not before") {
                CHECK((spi.status() & spif) != 0);
            }
        }
        WHEN("SPSR is read and then SPDR") {
            spi.send(0x00);
            spi.run(8);
            CHECK((spi.status() & spif) == 0);
            spi.run(40);
            CHECK((spi.status() & spif) != 0);
            THEN("SPIF stays set: the flag is cleared by SPSR then SPDR in that order") {
                CHECK((spi.avr.io(spsr) & spif) != 0);
                (void)spi.avr.io(spdr);
                CHECK((spi.status() & spif) == 0);
            }
        }
        WHEN("a second byte is written while the first is being shifted") {
            spi.send(0xFF);
            spi.run(8);
            spi.send(0x00);
            spi.run(64);
            THEN("the collision is flagged and the first byte goes on") {
                CHECK((spi.status() & wcol) != 0);
                std::vector<bool> seen;
                for (const auto& [at, level] : spi.wires.clock)
                    if (level)
                        seen.push_back(spi.wires.dataAt(at));
                CHECK(seen == std::vector<bool>(8, true));
            }
        }
        WHEN("the collision flag is read and SPDR accessed while the first byte is still being shifted") {
            spi.send(0xFF);
            spi.run(8);
            spi.send(0x00);
            REQUIRE((spi.status() & wcol) != 0);
            (void)spi.avr.io(spdr);
            THEN("WCOL is cleared at once, and the transfer under way still completes with SPIF") {
                CHECK((spi.status() & wcol) == 0);
                spi.run(64);
                CHECK((spi.status() & spif) != 0);
            }
        }
        WHEN("the firmware clears SPE while a byte is being shifted") {
            spi.send(0xFF);
            spi.run(10);  // Two bits are out
            const auto edges = spi.wires.clock.size();
            const auto bits  = spi.wires.data.size();
            spi.avr.mcu.dataSpace().write(spcr, 0x10);  // MSTR without SPE
            spi.run(100);
            THEN("the byte is aborted: no more edge, no SPIF, the pins are released") {
                CHECK(spi.wires.clock.size() <= edges + 1);  // At most the release of SCK
                CHECK(spi.wires.data.size() <= bits + 1);
                CHECK((spi.status() & spif) == 0);
                CHECK_FALSE(spi.avr.mcu.pin("PB5")->level());
                CHECK_FALSE(spi.avr.mcu.pin("PB3")->level());
            }
            AND_THEN("a new byte can be sent once the peripheral is enabled again") {
                spi.avr.mcu.dataSpace().write(spcr, 0x50);
                spi.send(0x00);
                spi.run(40);
                CHECK((spi.status() & spif) != 0);
            }
        }
        WHEN("MISO is held high by the device and a byte is exchanged") {
            spi.avr.mcu.pin("PB4")->setExternal(true);
            spi.send(0x00);
            spi.run(64);
            (void)spi.status();
            THEN("SPDR holds what was received") {
                CHECK(spi.avr.io(spdr) == 0xFF);
            }
        }
    }
}

SCENARIO("the mode and the bit order follow the control register", "[spi]") {
    GIVEN("a master at fck/2 (SPI2X) in mode 3, least significant bit first") {
        Spi spi{0x50 | 0x20 | 0x08 | 0x04, 0x01};  // SPE | MSTR | DORD | CPOL | CPHA, SPI2X
        const auto start = spi.avr.mcu.cycles();
        spi.send(0x01);
        spi.run(40);

        THEN("SCK idles high and each bit is clocked in two cycles: falling edge first") {
            REQUIRE(spi.wires.clock.size() >= 2);
            CHECK(spi.wires.clock.front().second == true);  // The idle level, applied when SPI is enabled
            std::vector<Cycles> falling;
            for (const auto& [at, level] : spi.wires.clock)
                if (!level)
                    falling.push_back(at);
            REQUIRE(falling.size() == 8);
            for (std::size_t i = 0; i < 8; ++i)
                CHECK(falling[i] == start + 2 * i);
        }
        THEN("the least significant bit goes first, and in mode 3 the data change on the falling edge") {
            // The bit is valid when SCK rises again (the sampling edge in mode 3).
            std::vector<bool> seen;
            bool              last = true;
            for (const auto& [at, level] : spi.wires.clock) {
                if (level && !last)
                    seen.push_back(spi.wires.dataAt(at));
                last = level;
            }
            REQUIRE(seen.size() == 8);
            CHECK(seen == std::vector<bool>{true, false, false, false, false, false, false, false});
        }
    }
}
