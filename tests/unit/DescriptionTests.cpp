#include "../support/AvrAssembler.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

using namespace siliconforge;
using Json = nlohmann::json;
namespace {
std::size_t peripheralIndex(const Json& d, std::string_view type) {
    const auto& list = d.at("peripherals");
    const auto  it   = std::ranges::find_if(list, [&](const Json& p) {
        return p.at("type") == type;
    });
    REQUIRE(it != list.end());
    return static_cast<std::size_t>(std::distance(list.begin(), it));
}
Json& peripheral(Json& d, std::string_view type) {
    return d.at("peripherals").at(peripheralIndex(d, type));
}
std::string peripheralPath(const Json& d, std::string_view type) {
    return "/peripherals/" + std::to_string(peripheralIndex(d, type));
}
void expectError(const Json& d, std::string_view path, std::string_view message) {
    auto result = mcu::validate(d);
    REQUIRE_FALSE(result);
    CHECK(result.error().path == path);
    CHECK(result.error().message.find(message) != std::string::npos);
}
// Give a second controller its own registers and IRQ so the singleton check is reached.
void duplicateController(Json& d, std::string_view type) {
    auto p    = peripheral(d, type);
    p["name"] = p.at("name").get<std::string>() + "_second";
    std::set<unsigned> used;
    for (const auto& r : d.at("registers"))
        used.insert(r.at("address").get<unsigned>());
    unsigned address = 32;
    for (auto& ref : p["registers"]) {
        const std::string oldName = ref;
        const std::string newName = oldName + "_second";
        while (used.contains(address))
            ++address;
        used.insert(address);
        d["registers"][newName]            = d.at("registers").at(oldName);
        d["registers"][newName]["address"] = address++;
        ref                                = newName;
    }
    std::set<unsigned> vectors;
    for (const auto& v : d.at("vectors"))
        vectors.insert(v.get<unsigned>());
    unsigned irq = 1;
    while (vectors.contains(irq))
        ++irq;
    const std::string vectorName = p.at("vector").get<std::string>() + "_second";
    d["vectors"][vectorName]     = irq;
    p["vector"]                  = vectorName;
    d["peripherals"].push_back(p);
}
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
        peripheral(d, "avr.adc")["vector"] = "unknown";
        auto r                             = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == peripheralPath(d, "avr.adc") + "/vector");
    }
    SECTION("unknown peripheral") {
        const auto path                   = peripheralPath(d, "avr.gpio") + "/type";
        peripheral(d, "avr.gpio")["type"] = "avr.magic";
        auto r                            = mcu::loadJson(d.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().path == path);
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
        peripheral(d, "avr.adc")["prescalers"][0] = 0;
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
    auto d                                           = mcu::builtinDescription("atmega328p");
    peripheral(d, "avr.adc")["modelFirstConversion"] = true;
    peripheral(d, "avr.adc")["conversionCycles"]     = 10;
    peripheral(d, "avr.eeprom")["writeTimeNs"]       = 1000;
    auto m                                           = device(d);
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

TEST_CASE("Disabling ADEN cancels a conversion without updating the result or IRQ", "[mcu][adc]") {
    auto  d                     = mcu::builtinDescription("atmega328p");
    auto& adc                   = peripheral(d, "avr.adc");
    adc["modelFirstConversion"] = true;
    const auto irq              = d.at("vectors").at(adc.at("vector").get<std::string>()).get<unsigned>();
    auto       m                = device(d);
    m->load(test::AvrAssembler{}.org(16 * 1024).image());
    m->pin("A0")->setVoltage(2.5, 5.0);
    auto& cpu  = dynamic_cast<avr::Device&>(*m).avrCore();
    auto& data = cpu.data;
    data.write(0x7A, 0xC9);  // ADEN, ADSC, ADIE, /2: first conversion takes 50 CPU clocks.
    m->run(10);
    REQUIRE(data.read(0x7A) & 0x40);
    data.write(0x7A, 0x08);  // Disable while keeping ADIE set.
    CHECK_FALSE(data.read(0x7A) & 0x40);
    m->run(100);             // Past the cancelled event's deadline.
    CHECK_FALSE(data.read(0x7A) & 0x10);
    CHECK(data.read(0x78) == 0);
    CHECK(data.read(0x79) == 0);
    CHECK_FALSE(cpu.interrupts.isPending(irq));
    data.write(0x7A, 0xC9);  // Re-enabling starts a fresh first conversion.
    m->run(49);
    CHECK(data.read(0x7A) & 0x40);
    m->run(1);
    CHECK_FALSE(data.read(0x7A) & 0x40);
    CHECK(data.read(0x7A) & 0x10);
    CHECK(cpu.interrupts.isPending(irq));
    CHECK((data.read(0x78) | (data.read(0x79) << 8)) == 512);
}

TEST_CASE("An oversized ATmega168 HEX is rejected before changing the loaded firmware", "[mcu][firmware]") {
    auto d                          = mcu::builtinDescription("atmega328p");
    d["name"]                       = "ATmega168";
    d["memories"]["flash"]["size"]  = 16384;
    d["memories"]["sram"]["size"]   = 1024;
    d["memories"]["eeprom"]["size"] = 512;
    d["registers"]["SPH"]["reset"]  = 4;
    auto m                          = device(d);
    m->load(test::AvrAssembler{}.org(8192).image());
    m->step();
    const auto before = m->readMemory(MemorySpace::Program, 0, 4);
    const auto cycles = m->cycles();
    // Two bytes straddle the 16 KiB limit; the HEX checksum is valid.
    auto image = parseIntelHex(":023FFF0011228D\n:00000001FF\n");
    REQUIRE(image);
    CHECK_THROWS_AS(m->load(*image), std::out_of_range);
    CHECK(m->readMemory(MemorySpace::Program, 0, 4) == before);
    CHECK(m->cycles() == cycles);
    SECTION("EEPROM overflow") {
        image->segments = {
            {MemorySpace::Eeprom, 511, {0x11, 0x22}}
        };
        CHECK_THROWS_AS(m->load(*image), std::out_of_range);
    }
    SECTION("boundary fits exactly") {
        image->segments = {
            {MemorySpace::Program, 16383, {0x42}}
        };
        CHECK_NOTHROW(m->load(*image));
        CHECK(m->readMemory(MemorySpace::Program, 16383, 1)[0] == 0x42);
    }
}

TEST_CASE("SPM and sleep control registers may be absent from a description", "[mcu][avr]") {
    auto d = mcu::builtinDescription("atmega328p");
    SECTION("no SPM") {
        d["cpu"].erase("spmcsr");
        d["registers"].erase("SPMCSR");
    }
    SECTION("no sleep controller") {
        d["cpu"].erase("smcr");
        d["registers"].erase("SMCR");
    }
    SECTION("neither controller") {
        d["cpu"].erase("spmcsr");
        d["cpu"].erase("smcr");
        d["registers"].erase("SPMCSR");
        d["registers"].erase("SMCR");
    }
    SECTION("present references must still resolve") {
        d["cpu"]["spmcsr"] = "missing";
        expectError(d, "/cpu/spmcsr", "Unknown register");
        return;
    }
    auto  m   = device(d);
    auto& cpu = dynamic_cast<avr::Device&>(*m).avrCore();
    CHECK(cpu.config().spmcsr == (d["cpu"].contains("spmcsr") ? 0x57 : 0));
    CHECK(cpu.config().smcr == (d["cpu"].contains("smcr") ? 0x53 : 0));
    ProgramImage image;
    image.segments = {
        {MemorySpace::Program, 0, {0xE8, 0x95, 0x88, 0x95}}
    };  // SPM; SLEEP
    m->load(image);
    CHECK_NOTHROW(m->step());
    CHECK_NOTHROW(m->step());
    CHECK_FALSE(cpu.sleeping);
}

TEST_CASE("Description cross references reject ambiguous or unwired devices", "[mcu][validation]") {
    auto d = mcu::builtinDescription("atmega328p");
    SECTION("duplicate register address") {
        d["registers"]["DDRB"]["address"] = d["registers"]["PINB"]["address"];
        expectError(d, "/registers/PINB/address", "Duplicate register");
    }
    SECTION("duplicate vector number") {
        d["vectors"]["ADC"] = d["vectors"]["RESET"];
        expectError(d, "/vectors/RESET", "Duplicate interrupt vector");
    }
    SECTION("duplicate physical pin") {
        d["package"]["pins"][1]["n"] = 1;
        expectError(d, "/package/pins", "Duplicate physical pin");
    }
    SECTION("duplicate port pin") {
        d["package"]["pins"][1]["port"] = "PC6";
        expectError(d, "/package/pins", "Duplicate port pin");
    }
    SECTION("duplicate label") {
        d["package"]["pins"][2]["label"] = "D0";
        expectError(d, "/package/pins", "Duplicate board label");
    }
    SECTION("nonconsecutive package numbering") {
        d["package"]["pins"].back()["n"] = 29;
        expectError(d, "/package/pins", "consecutively numbered");
    }
    SECTION("odd pin count") {
        d["package"]["pins"].erase(d["package"]["pins"].end() - 1);
        expectError(d, "/package/pins", "even pin count");
    }
    SECTION("GPIO wired twice") {
        peripheral(d, "avr.gpio")["pins"][1] = "PB0";
        expectError(d, peripheralPath(d, "avr.gpio") + "/pins", "multiple GPIO bits");
    }
    SECTION("GPIO missing from package") {
        d["package"]["pins"][1]["port"] = nullptr;
        expectError(d, "/package/pins", "Missing GPIO pin");
    }
    SECTION("unknown USART port") {
        peripheral(d, "avr.usart")["port"] = "missing";
        expectError(d, peripheralPath(d, "avr.usart") + "/port", "Unknown GPIO port");
    }
    SECTION("unknown timer output port") {
        peripheral(d, "avr.timer8")["outputs"][0]["port"] = "missing";
        expectError(d, peripheralPath(d, "avr.timer8") + "/outputs/0/port", "Unknown GPIO port");
    }
    SECTION("second ADC") {
        duplicateController(d, "avr.adc");
        expectError(d, "/peripherals/" + std::to_string(d["peripherals"].size() - 1) + "/type", "Only one ADC");
    }
    SECTION("second EEPROM") {
        duplicateController(d, "avr.eeprom");
        expectError(d, "/peripherals/" + std::to_string(d["peripherals"].size() - 1) + "/type", "Only one EEPROM");
    }
    SECTION("unwired ADC channel") {
        peripheral(d, "avr.adc")["channels"][0] = "PC6";
        expectError(d, peripheralPath(d, "avr.adc") + "/channels", "not wired to GPIO");
    }
    SECTION("duplicate peripheral name") {
        peripheral(d, "avr.adc")["name"] = "EEPROM";
        expectError(d, peripheralPath(d, "avr.adc") + "/name", "Duplicate peripheral name");
    }
    SECTION("register assigned twice") {
        peripheral(d, "avr.adc")["registers"]["low"] = "EECR";
        expectError(d, peripheralPath(d, "avr.adc") + "/registers/low", "multiple peripherals");
    }
    SECTION("vector assigned twice") {
        peripheral(d, "avr.adc")["vector"] = "EE_READY";
        expectError(d, peripheralPath(d, "avr.adc") + "/vector", "multiple peripheral sources");
    }
    SECTION("peripherals can be reordered") {
        std::ranges::reverse(d["peripherals"]);
        CHECK(mcu::validate(d));
        CHECK(device(d)->pin("D13"));
    }
}

TEST_CASE("Legacy ATmega328P constants agree with the embedded description", "[mcu]") {
    const auto d = mcu::builtinDescription("atmega328p");
    CHECK(mcu::Atmega328p::defaultClockHz == d.at("clockHz").get<u64>());
    CHECK(mcu::Atmega328p::supplyVoltage == d.at("supplyVoltage").get<double>());
    using namespace mcu::atmega328p_vectors;
    for (const auto& [name, value] : std::vector<std::pair<std::string, unsigned>>{
             {"TIMER0_COMPA", timer0CompareA},
             {"TIMER0_COMPB", timer0CompareB},
             {  "TIMER0_OVF", timer0Overflow},
             {    "USART_RX",        usartRx},
             {  "USART_UDRE",      usartUdre},
             {    "USART_TX",        usartTx},
             {         "ADC",            adc},
             {    "EE_READY",    eepromReady}
    })
        CHECK(d.at("vectors").at(name).get<unsigned>() == value);
}

TEST_CASE("Catalog discovery reports duplicates, invalid files and missing directories", "[mcu][registry]") {
    struct Directory {
        std::filesystem::path path = std::filesystem::temp_directory_path()
                                     / ("siliconforge-registry-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Directory() {
            std::filesystem::create_directory(path);
        }
        ~Directory() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } directory;
    SECTION("missing directory") {
        const auto missing = directory.path / "missing";
        auto       errors  = mcu::addPath(missing);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].first == missing);
        CHECK(errors[0].second.path.empty());
        CHECK_FALSE(errors[0].second.message.empty());
    }
    SECTION("validated catalog content is stable") {
        auto       d    = mcu::builtinDescription("atmega328p");
        const auto name = directory.path.filename().string();
        d["name"]       = name;
        const auto good = directory.path / "a-valid.json";
        {
            std::ofstream out{good};
            out << d.dump();
        }
        d["name"]  = std::string(name);  // Case-insensitive duplicate of the newly discovered entry.
        auto upper = name;
        std::ranges::transform(upper, upper.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        d["name"] = upper;
        {
            std::ofstream out{directory.path / "b-duplicate.json"};
            out << d.dump();
        }
        {
            std::ofstream out{directory.path / "c-invalid.json"};
            out << "{";
        }
        d["name"]    = name + "-invalid";
        d["clockHz"] = 0;
        {
            std::ofstream out{directory.path / "d-invalid.json"};
            out << d.dump();
        }
        {
            std::ofstream out{directory.path / "ignored.txt"};
            out << "not JSON";
        }
        const auto count  = mcu::available().size();
        auto       errors = mcu::addPath(directory.path);
        REQUIRE(errors.size() == 3);
        CHECK(errors[0].first.filename() == "b-duplicate.json");
        CHECK(errors[0].second.path == "/name");
        CHECK(errors[0].second.message.find("Duplicate MCU name") != std::string::npos);
        CHECK(errors[1].first.filename() == "c-invalid.json");
        CHECK_FALSE(errors[1].second.message.empty());
        CHECK(errors[2].first.filename() == "d-invalid.json");
        CHECK(errors[2].second.path == "/clockHz");
        CHECK(mcu::available().size() == count + 1);
        REQUIRE(mcu::create(upper));
        {
            std::ofstream out{good};
            out << "{";
        }
        auto stored = mcu::create(name);
        REQUIRE(stored);
        CHECK(stored->name() == name);  // Stored JSON, not a later reread of the file.
    }
}
