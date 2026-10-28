#include "../support/AvrAssembler.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <fstream>

using namespace siliconforge;
using Json = nlohmann::json;
namespace {
std::unique_ptr<Mcu> device(const Json& d) {
    auto result = mcu::loadJson(d.dump());
    INFO((result ? std::string{} : result.error().path + ": " + result.error().message));
    REQUIRE(result.has_value());
    return std::move(*result);
}
}  // namespace
TEST_CASE("The shipped MCU file validates against the versioned JSON Schema", "[mcu][schema]") {
    auto result = mcu::load(std::filesystem::path{SILICONFORGE_MCU_DIR} / "atmega328p.json");
    INFO((result ? std::string{} : result.error().path + ": " + result.error().message));
    REQUIRE(result.has_value());
    CHECK((*result)->name() == "ATmega328P");
    CHECK((*result)->pins().size() == 22);
    CHECK(Json::parse((*result)->chipJson()).at("pins").size() == 28);
    std::ifstream input{std::filesystem::path{SILICONFORGE_MCU_DIR} / "atmega328p.json"};
    CHECK(Json::parse(input) == mcu::builtinDescription("atmega328p"));
}
TEST_CASE("Description failures carry the precise JSON Pointer", "[mcu]") {
    auto d = mcu::builtinDescription("atmega328p");
    SECTION("missing instruction") {
        d["timing"].erase("nop");
        auto r = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == "/timing/nop");
    }
    SECTION("missing branch case") {
        d["timing"]["brbc"].erase("taken");
        auto r = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == "/timing/brbc/taken");
    }
    SECTION("bad vector reference") {
        d["peripherals"][6]["vector"] = "unknown";
        auto r                        = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == "/peripherals/6/vector");
    }
    SECTION("unknown peripheral") {
        d["peripherals"][2]["type"] = "avr.magic";
        auto r                      = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == "/peripherals/2/type");
        CHECK(r.error().message.find("avr.magic") != std::string::npos);
    }
    SECTION("out of bounds register") {
        d["registers"]["EECR"]["address"] = 4096;
        auto r                            = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == "/registers/EECR/address");
    }
    SECTION("wrong scalar type") {
        d["clockHz"] = "fast";
        auto r       = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == "/clockHz");
    }
    SECTION("zero ADC divider") {
        d["peripherals"][6]["prescalers"][0] = 0;
        CHECK_FALSE(mcu::loadJson(d.dump()));
    }
    SECTION("unknown format") {
        d["formatVersion"] = 2;
        auto r             = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == "/formatVersion");
    }
    SECTION("malformed JSON") {
        CHECK_FALSE(mcu::loadJson("{"));
    }
    SECTION("missing file") {
        CHECK_FALSE(mcu::load("/nonexistent/siliconforge.json"));
    }
}
TEST_CASE("Instruction timings are compiled per device, including branches and skips", "[mcu][avr]") {
    auto d                              = mcu::builtinDescription("atmega328p");
    d["timing"]["nop"]["cycles"]        = 7;
    d["timing"]["brbs"]["taken"]        = 9;
    d["timing"]["cpse"]["skipOneWord"]  = 5;
    d["timing"]["cpse"]["skipTwoWords"] = 6;
    auto m                              = device(d);
    // NOP; BRBS C,+0; CPSE R0,R0; NOP; CPSE R0,R0; JMP 0.
    ProgramImage image;
    image.segments.push_back({
        MemorySpace::Program,
        0,
        {0, 0, 0, 0xF0, 0, 0x10, 0, 0, 0, 0x10, 0x0C, 0x94, 0, 0}
    });
    m->load(image);
    CHECK(m->step().elapsed == 7);
    auto& avr = dynamic_cast<avr::Device&>(*m).avrCore();
    avr.sreg  = avr::sreg::C;
    CHECK(m->step().elapsed == 9);
    CHECK(m->step().elapsed == 5);
    CHECK(m->step().elapsed == 6);
    auto original = mcu::create("atmega328p");
    original->load(image);
    CHECK(original->step().elapsed == 1);
}
TEST_CASE("ADC first and subsequent conversions and EEPROM writes use JSON timings", "[mcu][adc][eeprom]") {
    auto d                                      = mcu::builtinDescription("atmega328p");
    d["peripherals"][6]["modelFirstConversion"] = true;
    d["peripherals"][6]["conversionCycles"]     = 10;
    d["peripherals"][5]["writeTimeNs"]          = 1000;
    auto m                                      = device(d);
    m->load(test::AvrAssembler{}.org(16 * 1024).image());
    auto& data = dynamic_cast<avr::Device&>(*m).dataSpace();
    data.write(0x7A, 0xC1);  // first conversion, /2
    m->run(49);
    CHECK(data.read(0x7A) & 0x40);
    m->run(1);
    CHECK_FALSE(data.read(0x7A) & 0x40);
    data.write(0x7A, 0xC1);
    m->run(19);
    CHECK(data.read(0x7A) & 0x40);
    m->run(1);
    CHECK_FALSE(data.read(0x7A) & 0x40);
    data.write(0x7A, 0);
    data.write(0x7A, 0xC1);
    m->run(49);
    CHECK(data.read(0x7A) & 0x40);
    m->run(1);
    CHECK_FALSE(data.read(0x7A) & 0x40);
    data.write(0x3F, 4);
    data.write(0x3F, 2);
    m->run(15);
    CHECK(data.read(0x3F) & 2);
    m->run(1);
    CHECK_FALSE(data.read(0x3F) & 2);
}
TEST_CASE("A JSON-only ATmega168 runs blink with smaller memories", "[mcu][firmware]") {
    auto d                          = mcu::builtinDescription("atmega328p");
    d["name"]                       = "ATmega168";
    d["memories"]["flash"]["size"]  = 16384;
    d["memories"]["sram"]["size"]   = 1024;
    d["memories"]["eeprom"]["size"] = 512;
    d["registers"]["SPH"]["reset"]  = 4;
    testing::Bench bench{device(d)};
    bench.load(std::filesystem::path{SILICONFORGE_FIRMWARE_DIR} / "blink.hex");
    bench.runFor(std::chrono::seconds{3});
    REQUIRE(bench.period("D13"));
    CHECK(*bench.period("D13") == Catch::Approx(1.0).epsilon(0.001));
}
TEST_CASE("Register reset values and remapped peripherals come from JSON", "[mcu]") {
    auto d                             = mcu::builtinDescription("atmega328p");
    d["registers"]["PORTB"]["address"] = 0x30;
    d["registers"]["PORTB"]["reset"]   = 0x20;
    d["registers"]["DDRB"]["reset"]    = 0x20;
    auto m                             = device(d);
    CHECK(m->pin("D13")->state() == PinState::High);
    auto& data = dynamic_cast<avr::Device&>(*m).dataSpace();
    data.write(0x30, 0);
    CHECK(m->pin("D13")->state() == PinState::Low);
    m->reset();
    CHECK(m->pin("D13")->state() == PinState::High);
}

TEST_CASE("SRAM accesses and three-byte-PC calls have independent JSON timings", "[mcu][avr]") {
    auto d = mcu::builtinDescription("atmega328p");
    SECTION("I/O versus SRAM") {
        d["timing"]["lds"]["sram"] = 9;
        auto         m             = device(d);
        ProgramImage image;
        image.segments.push_back({
            MemorySpace::Program,
            0,
            {0, 0x90, 0x30, 0, 0, 0x90, 0, 1}
        });
        m->load(image);
        CHECK(m->step().elapsed == 2);
        CHECK(m->step().elapsed == 9);
    }
    SECTION("three-byte PC") {
        d["cpu"]["pcBytes"]             = 3;
        d["timing"]["call"]["pc3Bytes"] = 9;
        d["timing"]["ret"]["pc3Bytes"]  = 10;
        auto         m                  = device(d);
        ProgramImage image;
        image.segments.push_back({
            MemorySpace::Program,
            0,
            {0x0E, 0x94, 3, 0, 0, 0, 8, 0x95}
        });
        m->load(image);
        auto&      cpu       = dynamic_cast<avr::Device&>(*m).avrCore();
        const auto initialSp = cpu.sp;
        CHECK(m->step().elapsed == 9);
        CHECK(cpu.sp == initialSp - 3);
        CHECK(m->step().elapsed == 10);
        CHECK(cpu.sp == initialSp);
        CHECK(cpu.pc == 2);
    }
}

TEST_CASE("Peripheral internal state uses described reset values", "[mcu]") {
    auto d                           = mcu::builtinDescription("atmega328p");
    d["registers"]["TCNT0"]["reset"] = 37;
    d["registers"]["OCR0A"]["reset"] = 91;
    auto  m                          = device(d);
    auto& data                       = dynamic_cast<avr::Device&>(*m).dataSpace();
    CHECK(data.read(0x46) == 37);
    CHECK(data.read(0x47) == 91);
    data.write(0x46, 2);
    data.write(0x47, 3);
    m->reset();
    CHECK(data.read(0x46) == 37);
    CHECK(data.read(0x47) == 91);
}
