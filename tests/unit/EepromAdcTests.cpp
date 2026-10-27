#include "../support/AvrFixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

namespace {
constexpr Address eecr = 0x3F, eedr = 0x40, eearl = 0x41;
constexpr Address adcl = 0x78, adch = 0x79, adcsra = 0x7A, admux = 0x7C;
}  // namespace

SCENARIO("EEPROM writes take 3.4 ms and read back", "[avr][eeprom]") {
    AvrFixture avr{AvrAssembler{}.org(16 * 1024)};  // NOPs over the whole flash
    auto&      data = avr.mcu.dataSpace();
    data.write(eearl, 10);
    data.write(eedr, 0x5A);
    data.write(eecr, 0x04);  // EEMPE
    data.write(eecr, 0x02);  // EEPE
    CHECK((data.read(eecr) & 0x02) != 0);
    CHECK(avr.mcu.readMemory(MemorySpace::Eeprom, 10, 1)[0] == 0x5A);
    avr.mcu.runFor(std::chrono::microseconds{3400});
    CHECK((data.read(eecr) & 0x02) == 0);

    data.write(eedr, 0);
    data.write(eecr, 0x01);  // EERE
    CHECK(data.read(eedr) == 0x5A);
}

SCENARIO("A single ADC conversion samples the pin voltage", "[avr][adc]") {
    AvrFixture avr{AvrAssembler{}.org(16 * 1024)};  // NOPs over the whole flash
    auto&      data = avr.mcu.dataSpace();
    avr.mcu.pin("A0")->setVoltage(2.5, 5.0);
    data.write(admux, 0x40);   // AVcc reference, channel 0
    data.write(adcsra, 0xC7);  // ADEN, ADSC, clk/128
    CHECK((data.read(adcsra) & 0x40) != 0);
    avr.mcu.run(13 * 128);
    THEN("after 13 ADC clocks ADSC is cleared and ADC holds 512") {
        CHECK((data.read(adcsra) & 0x40) == 0);
        CHECK((data.read(adcsra) & 0x10) != 0);  // ADIF
        const auto low  = data.read(adcl);
        const auto high = data.read(adch);
        CHECK((low | high << 8) == 512);
    }
}
