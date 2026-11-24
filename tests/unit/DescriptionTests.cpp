#include <nlohmann/json.hpp>
#include "../support/AvrAssembler.hpp"
#include "../support/AvrCoreAccess.hpp"
#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"
#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <map>
#include <stdexcept>

using namespace webfront::bdd;

using namespace siliconforge;
using Json = nlohmann::json;
namespace {
std::size_t peripheralIndex(const Json& d, std::string_view type) {
    const auto& list = d.at("peripherals");
    const auto  it   = std::ranges::find_if(list, [&](const Json& p) {
        return p.at("type") == std::string(type);
    });
    require(it != list.end());
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
    require(!result);
    check(result.error().path == path);
    check(result.error().message.find(message) != std::string::npos);
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
    const auto note1 = info((result ? std::string{} : result.error().path + ": " + result.error().message));
    require(result.has_value());
    return std::move(*result);
}
}  // namespace
const Scenario theShippedMcuFileValidatesAgainstThe{"The shipped MCU file validates against the versioned JSON Schema", {"mcu", "schema"}, [] {
    auto result = mcu::load(std::filesystem::path{SILICONFORGE_MCU_DIR} / "atmega328p.json");
    const auto note2 = info((result ? std::string{} : result.error().path + ": " + result.error().message));
    require(result.has_value());
    check((*result)->name() == "ATmega328P");
    check((*result)->pins().size() == 22);
    const auto chip = Json::parse((*result)->chipJson());
    check(chip.at("pins").size() == 28);
    const auto clockPin = std::ranges::find_if(chip.at("pins"), [](const Json& pin) { return pin.at("port") == "PB0"; });
    require(clockPin != chip.at("pins").end());
    const auto functions = clockPin->at("functions").get<std::vector<std::string>>();
    check(std::ranges::find(functions, std::string{"CLKO"}) != functions.end());
    check(std::ranges::find(functions, std::string{"CLK0"}) == functions.end());
    std::ifstream input{std::filesystem::path{SILICONFORGE_MCU_DIR} / "atmega328p.json"};
    check(Json::parse(input) == mcu::builtinDescription("atmega328p"));
}};
const Scenario descriptionFailuresCarryThePreciseJsonPointer{"Description failures carry the precise JSON Pointer", {"mcu"}, [] {
    auto d = mcu::builtinDescription("atmega328p");
    section("missing instruction", [&] {
        d["timing"].erase("nop");
        auto r = mcu::loadJson(d.dump());
        require(!r);
        check(r.error().path == "/timing/nop");
    });
    section("missing branch case", [&] {
        d["timing"]["brbc"].erase("taken");
        auto r = mcu::loadJson(d.dump());
        require(!r);
        check(r.error().path == "/timing/brbc/taken");
    });
    section("bad vector reference", [&] {
        peripheral(d, "avr.adc")["vector"] = "unknown";
        auto r                             = mcu::loadJson(d.dump());
        require(!r);
        check(r.error().path == peripheralPath(d, "avr.adc") + "/vector");
    });
    section("unknown peripheral", [&] {
        const auto path                   = peripheralPath(d, "avr.gpio") + "/type";
        peripheral(d, "avr.gpio")["type"] = "avr.magic";
        auto r                            = mcu::loadJson(d.dump());
        require(!r);
        check(r.error().path == path);
        check(r.error().message.find("avr.magic") != std::string::npos);
    });
    section("out of bounds register", [&] {
        d["registers"]["EECR"]["address"] = 4096;
        auto r                            = mcu::loadJson(d.dump());
        require(!r);
        check(r.error().path == "/registers/EECR/address");
    });
    section("wrong scalar type", [&] {
        d["clockHz"] = "fast";
        auto r       = mcu::loadJson(d.dump());
        require(!r);
        check(r.error().path == "/clockHz");
    });
    section("zero ADC divider", [&] {
        peripheral(d, "avr.adc")["prescalers"][0] = 0;
        check(!mcu::loadJson(d.dump()));
    });
    section("unknown format", [&] {
        d["formatVersion"] = 2;
        auto r             = mcu::loadJson(d.dump());
        require(!r);
        check(r.error().path == "/formatVersion");
    });
    section("malformed JSON", [&] {
        check(!mcu::loadJson("{"));
    });
    section("missing file", [&] {
        check(!mcu::load("/nonexistent/siliconforge.json"));
    });
}};
const Scenario instructionTimingsAreCompiledPerDeviceIncluding{"Instruction timings are compiled per device, including branches and skips", {"mcu", "avr"}, [] {
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
    check(m->step().elapsed == 7);
    auto& avr = dynamic_cast<avr::Device&>(*m).avrCore();
    avr.sreg  = avr::sreg::C;
    check(m->step().elapsed == 9);
    check(m->step().elapsed == 5);
    check(m->step().elapsed == 6);
    auto original = mcu::create("atmega328p");
    original->load(image);
    check(original->step().elapsed == 1);
}};
const Scenario adcFirstAndSubsequentConversionsAndEeprom{"ADC first and subsequent conversions and EEPROM writes use JSON timings",
                                                         {"mcu", "adc", "eeprom"}, [] {
    auto d                                           = mcu::builtinDescription("atmega328p");
    peripheral(d, "avr.adc")["modelFirstConversion"] = true;
    peripheral(d, "avr.adc")["conversionCycles"]     = 10;
    peripheral(d, "avr.eeprom")["writeTimeNs"]       = 1000;
    auto m                                           = device(d);
    m->load(test::AvrAssembler{}.org(16 * 1024).image());
    auto& data = dynamic_cast<avr::Device&>(*m).dataSpace();
    data.write(0x7A, 0xC1);  // first conversion, /2
    m->run(49);
    check(data.read(0x7A) & 0x40);
    m->run(1);
    check(!(data.read(0x7A) & 0x40));
    data.write(0x7A, 0xC1);
    m->run(19);
    check(data.read(0x7A) & 0x40);
    m->run(1);
    check(!(data.read(0x7A) & 0x40));
    data.write(0x7A, 0);
    data.write(0x7A, 0xC1);
    m->run(49);
    check(data.read(0x7A) & 0x40);
    m->run(1);
    check(!(data.read(0x7A) & 0x40));
    data.write(0x3F, 4);
    data.write(0x3F, 2);
    m->run(15);
    check(data.read(0x3F) & 2);
    m->run(1);
    check(!(data.read(0x3F) & 2));
}};
const Scenario aJsonOnlyAtmega168RunsBlinkWith{"A JSON-only ATmega168 runs blink with smaller memories", {"mcu", "firmware"}, [] {
    auto d                          = mcu::builtinDescription("atmega328p");
    d["name"]                       = "ATmega168";
    d["memories"]["flash"]["size"]  = 16384;
    d["memories"]["sram"]["size"]   = 1024;
    d["memories"]["eeprom"]["size"] = 512;
    d["registers"]["SPH"]["reset"]  = 4;
    testing::Bench bench{device(d)};
    bench.load(std::filesystem::path{SILICONFORGE_FIRMWARE_DIR} / "blink.hex");
    bench.runFor(std::chrono::seconds{3});
    require(bench.period("D13"));
    check(*bench.period("D13") == siliconforge::test::Approx(1.0).epsilon(0.001));
}};
const Scenario registerResetValuesAndRemappedPeripheralsCome{"Register reset values and remapped peripherals come from JSON", {"mcu"}, [] {
    auto d                             = mcu::builtinDescription("atmega328p");
    d["registers"]["PORTB"]["address"] = 0x30;
    d["registers"]["PORTB"]["reset"]   = 0x20;
    d["registers"]["DDRB"]["reset"]    = 0x20;
    auto m                             = device(d);
    check(m->pin("D13")->state() == PinState::High);
    auto& data = dynamic_cast<avr::Device&>(*m).dataSpace();
    data.write(0x30, 0);
    check(m->pin("D13")->state() == PinState::Low);
    m->reset();
    check(m->pin("D13")->state() == PinState::High);
}};

const Scenario sramAccessesAndThreeBytePcCalls{"SRAM accesses and three-byte-PC calls have independent JSON timings", {"mcu", "avr"}, [] {
    auto d = mcu::builtinDescription("atmega328p");
    section("I/O versus SRAM", [&] {
        d["timing"]["lds"]["sram"] = 9;
        auto         m             = device(d);
        ProgramImage image;
        image.segments.push_back({
            MemorySpace::Program,
            0,
            {0, 0x90, 0x30, 0, 0, 0x90, 0, 1}
        });
        m->load(image);
        check(m->step().elapsed == 2);
        check(m->step().elapsed == 9);
    });
    section("three-byte PC", [&] {
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
        check(m->step().elapsed == 9);
        check(cpu.sp == initialSp - 3);
        check(m->step().elapsed == 10);
        check(cpu.sp == initialSp);
        check(cpu.pc == 2);
    });
}};

const Scenario peripheralInternalStateUsesDescribedResetValues{"Peripheral internal state uses described reset values", {"mcu"}, [] {
    auto d                           = mcu::builtinDescription("atmega328p");
    d["registers"]["TCNT0"]["reset"] = 37;
    d["registers"]["OCR0A"]["reset"] = 91;
    auto  m                          = device(d);
    auto& data                       = dynamic_cast<avr::Device&>(*m).dataSpace();
    check(data.read(0x46) == 37);
    check(data.read(0x47) == 91);
    data.write(0x46, 2);
    data.write(0x47, 3);
    m->reset();
    check(data.read(0x46) == 37);
    check(data.read(0x47) == 91);
}};

const Scenario disablingAdenCancelsAConversionWithoutUpdating{"Disabling ADEN cancels a conversion without updating the result or IRQ", {"mcu", "adc"}, [] {
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
    require(data.read(0x7A) & 0x40);
    data.write(0x7A, 0x08);  // Disable while keeping ADIE set.
    check(!(data.read(0x7A) & 0x40));
    m->run(100);             // Past the cancelled event's deadline.
    check(!(data.read(0x7A) & 0x10));
    check(data.read(0x78) == 0);
    check(data.read(0x79) == 0);
    check(!cpu.interrupts.isPending(irq));
    data.write(0x7A, 0xC9);  // Re-enabling starts a fresh first conversion.
    m->run(49);
    check(data.read(0x7A) & 0x40);
    m->run(1);
    check(!(data.read(0x7A) & 0x40));
    check(data.read(0x7A) & 0x10);
    check(cpu.interrupts.isPending(irq));
    check((data.read(0x78) | (data.read(0x79) << 8)) == 512);
}};

const Scenario anOversizedAtmega168HexIsRejectedBefore{"An oversized ATmega168 HEX is rejected before changing the loaded firmware", {"mcu", "firmware"}, [] {
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
    require(image);
    checkThrows<std::out_of_range>([&] { m->load(*image); });
    check(m->readMemory(MemorySpace::Program, 0, 4) == before);
    check(m->cycles() == cycles);
    section("EEPROM overflow", [&] {
        image->segments = {
            {MemorySpace::Eeprom, 511, {0x11, 0x22}}
        };
        checkThrows<std::out_of_range>([&] { m->load(*image); });
    });
    section("boundary fits exactly", [&] {
        image->segments = {
            {MemorySpace::Program, 16383, {0x42}}
        };
        checkNothrow([&] { m->load(*image); });
        check(m->readMemory(MemorySpace::Program, 16383, 1)[0] == 0x42);
    });
}};

const Scenario spmAndSleepControlRegistersMayBe{"SPM and sleep control registers may be absent from a description", {"mcu", "avr"}, [] {
    auto d        = mcu::builtinDescription("atmega328p");
    bool rejected = false;
    section("no SPM", [&] {
        d["cpu"].erase("spmcsr");
        d["registers"].erase("SPMCSR");
    });
    section("no sleep controller", [&] {
        d["cpu"].erase("smcr");
        d["registers"].erase("SMCR");
    });
    section("neither controller", [&] {
        d["cpu"].erase("spmcsr");
        d["cpu"].erase("smcr");
        d["registers"].erase("SPMCSR");
        d["registers"].erase("SMCR");
    });
    section("present references must still resolve", [&] {
        d["cpu"]["spmcsr"] = "missing";
        expectError(d, "/cpu/spmcsr", "Unknown register");
        rejected = true;
    });
    if (rejected)
        return;
    auto  m   = device(d);
    auto& cpu = dynamic_cast<avr::Device&>(*m).avrCore();
    check(cpu.config().spmcsr == (d["cpu"].contains("spmcsr") ? 0x57u : 0u));
    check(cpu.config().smcr == (d["cpu"].contains("smcr") ? 0x53u : 0u));
    ProgramImage image;
    image.segments = {
        {MemorySpace::Program, 0, {0xE8, 0x95, 0x88, 0x95}}
    };  // SPM; SLEEP
    m->load(image);
    checkNothrow([&] { m->step(); });
    checkNothrow([&] { m->step(); });
    check(!cpu.sleeping);
}};

const Scenario descriptionCrossReferencesRejectAmbiguousOrUnwired{"Description cross references reject ambiguous or unwired devices",
                                                                  {"mcu", "validation"}, [] {
    auto d = mcu::builtinDescription("atmega328p");
    section("duplicate register address", [&] {
        d["registers"]["DDRB"]["address"] = d["registers"]["PINB"]["address"];
        expectError(d, "/registers/PINB/address", "Duplicate register");
    });
    section("duplicate vector number", [&] {
        d["vectors"]["ADC"] = d["vectors"]["RESET"];
        expectError(d, "/vectors/RESET", "Duplicate interrupt vector");
    });
    section("duplicate physical pin", [&] {
        d["package"]["pins"][1]["n"] = 1;
        expectError(d, "/package/pins", "Duplicate physical pin");
    });
    section("duplicate port pin", [&] {
        d["package"]["pins"][1]["port"] = "PC6";
        expectError(d, "/package/pins", "Duplicate port pin");
    });
    section("duplicate label", [&] {
        d["package"]["pins"][2]["label"] = "D0";
        expectError(d, "/package/pins", "Duplicate board label");
    });
    section("nonconsecutive package numbering", [&] {
        d["package"]["pins"].back()["n"] = 29;
        expectError(d, "/package/pins", "consecutively numbered");
    });
    section("odd pin count", [&] {
        d["package"]["pins"].erase(d["package"]["pins"].end() - 1);
        expectError(d, "/package/pins", "even pin count");
    });
    section("GPIO wired twice", [&] {
        peripheral(d, "avr.gpio")["pins"][1] = "PB0";
        expectError(d, peripheralPath(d, "avr.gpio") + "/pins", "multiple GPIO bits");
    });
    section("GPIO missing from package", [&] {
        d["package"]["pins"][1]["port"] = nullptr;
        expectError(d, "/package/pins", "Missing GPIO pin");
    });
    section("unknown USART port", [&] {
        peripheral(d, "avr.usart")["port"] = "missing";
        expectError(d, peripheralPath(d, "avr.usart") + "/port", "Unknown GPIO port");
    });
    section("unknown timer output port", [&] {
        peripheral(d, "avr.timer8")["outputs"][0]["port"] = "missing";
        expectError(d, peripheralPath(d, "avr.timer8") + "/outputs/0/port", "Unknown GPIO port");
    });
    section("second ADC", [&] {
        duplicateController(d, "avr.adc");
        expectError(d, "/peripherals/" + std::to_string(d["peripherals"].size() - 1) + "/type", "Only one ADC");
    });
    section("second EEPROM", [&] {
        duplicateController(d, "avr.eeprom");
        expectError(d, "/peripherals/" + std::to_string(d["peripherals"].size() - 1) + "/type", "Only one EEPROM");
    });
    section("unwired ADC channel", [&] {
        peripheral(d, "avr.adc")["channels"][0] = "PC6";
        expectError(d, peripheralPath(d, "avr.adc") + "/channels", "not wired to GPIO");
    });
    section("duplicate peripheral name", [&] {
        peripheral(d, "avr.adc")["name"] = "EEPROM";
        expectError(d, peripheralPath(d, "avr.adc") + "/name", "Duplicate peripheral name");
    });
    section("register assigned twice", [&] {
        peripheral(d, "avr.adc")["registers"]["low"] = "EECR";
        expectError(d, peripheralPath(d, "avr.adc") + "/registers/low", "multiple peripherals");
    });
    section("vector assigned twice", [&] {
        peripheral(d, "avr.adc")["vector"] = "EE_READY";
        expectError(d, peripheralPath(d, "avr.adc") + "/vector", "multiple peripheral sources");
    });
    section("peripherals can be reordered", [&] {
        std::ranges::reverse(d["peripherals"]);
        check(mcu::validate(d));
        check(device(d)->pin("D13"));
    });
}};

const Scenario legacyAtmega328pConstantsAgreeWithTheEmbedded{"Legacy ATmega328P constants agree with the embedded description", {"mcu"}, [] {
    const auto d = mcu::builtinDescription("atmega328p");
    check(mcu::Atmega328p::defaultClockHz == d.at("clockHz").get<u64>());
    check(mcu::Atmega328p::supplyVoltage == d.at("supplyVoltage").get<double>());
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
        check(d.at("vectors").at(name).get<unsigned>() == value);
}};

const Scenario catalogDiscoveryReportsDuplicatesInvalidFilesAnd{"Catalog discovery reports duplicates, invalid files and missing directories",
                                                                {"mcu", "registry"}, [] {
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
    section("missing directory", [&] {
        const auto missing = directory.path / "missing";
        auto       errors  = mcu::addPath(missing);
        require(errors.size() == 1);
        check(errors[0].first == missing);
        check(errors[0].second.path.empty());
        check(!errors[0].second.message.empty());
    });
    section("validated catalog content is stable", [&] {
        auto       d    = mcu::builtinDescription("atmega328p");
        const auto name = "catalog-" + directory.path.filename().string();
        auto displayName = name;
        displayName[0] = 'C';
        d["name"] = displayName;
        const auto good = directory.path / "a-valid.json";
        {
            std::ofstream out{good};
            out << d.dump();
        }
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
        require(errors.size() == 3);
        check(errors[0].first.filename() == "b-duplicate.json");
        check(errors[0].second.path == "/name");
        check(errors[0].second.message.find("Duplicate MCU name") != std::string::npos);
        check(errors[1].first.filename() == "c-invalid.json");
        check(!errors[1].second.message.empty());
        check(errors[2].first.filename() == "d-invalid.json");
        check(errors[2].second.path == "/clockHz");
        const auto names = mcu::available();
        check(names.size() == count + 1);
        check(std::ranges::find(names, name) != names.end());
        require(mcu::create(upper));
        {
            std::ofstream out{good};
            out << "{";
        }
        auto stored = mcu::create(name);
        require(stored);
        check(stored->name() == displayName);  // Stored JSON, not a later reread of the file.
    });
}};

const Scenario cpuControlRegistersCannotBeMappedBy{"CPU control registers cannot be mapped by peripherals", {"mcu", "validation"}, [] {
    auto d = mcu::builtinDescription("atmega328p");
    section("SPMCSR as an ADC register", [&] {
        peripheral(d, "avr.adc")["registers"]["low"] = "SPMCSR";
        expectError(d, peripheralPath(d, "avr.adc") + "/registers/low", "multiple peripherals");
    });
    section("SMCR as an ADC register", [&] {
        peripheral(d, "avr.adc")["registers"]["low"] = "SMCR";
        expectError(d, peripheralPath(d, "avr.adc") + "/registers/low", "multiple peripherals");
    });
    section("CPU control as GPIO pull-up control", [&] {
        peripheral(d, "avr.gpio")["mcucr"] = "SPMCSR";
        expectError(d, peripheralPath(d, "avr.gpio") + "/mcucr", "multiple peripherals");
    });
    section("absent optional CPU controls reserve no registers", [&] {
        d["cpu"].erase("spmcsr");
        d["cpu"].erase("smcr");
        peripheral(d, "avr.adc")["registers"]["low"] = "SPMCSR";
        check(mcu::validate(d));
    });
}};

const Scenario dataFirmwareSegmentsAreRejectedWithoutChanging{"Data firmware segments are rejected without changing MCU state", {"mcu", "firmware"}, [] {
    auto m = mcu::create("atmega328p");
    require(m);
    m->load(test::AvrAssembler{}.org(16 * 1024).image());
    m->step();
    ProgramImage image;
    image.segments = {{MemorySpace::Program, 0, {0xAB, 0xCD}}, {MemorySpace::Data, 256, {0x42}}};
    const auto flash = m->readMemory(MemorySpace::Program, 0, 4);
    const auto ram = m->readMemory(MemorySpace::Data, 256, 1);
    const auto cycles = m->cycles();
    checkThrows<std::invalid_argument>([&] { m->load(image); });
    check(m->readMemory(MemorySpace::Program, 0, 4) == flash);
    check(m->readMemory(MemorySpace::Data, 256, 1) == ram);
    check(m->cycles() == cycles);
}};

const Scenario theAdcBandgapMustNotOverlapAn{"The ADC bandgap must not overlap an external channel", {"mcu", "adc", "validation"}, [] {
    auto d = mcu::builtinDescription("atmega328p");
    auto& adc = peripheral(d, "avr.adc");
    section("first external channel", [&] {
        adc["bandgapChannel"] = 0;
        expectError(d, peripheralPath(d, "avr.adc") + "/bandgapChannel", "overlaps an external ADC channel");
    });
    section("last external channel", [&] {
        adc["bandgapChannel"] = adc["channels"].size() - 1;
        expectError(d, peripheralPath(d, "avr.adc") + "/bandgapChannel", "overlaps an external ADC channel");
    });
    section("first unused mux value selects bandgap", [&] {
        const auto channel = adc["channels"].size();
        adc["bandgapChannel"] = channel;
        auto m = device(d);
        m->load(test::AvrAssembler{}.org(16 * 1024).image());
        auto& data = dynamic_cast<avr::Device&>(*m).dataSpace();
        data.write(0x7C, static_cast<u8>(0x40 | channel));
        data.write(0x7A, 0xC1);
        m->run(26); // 13 ADC clocks at /2.
        check((data.read(0x78) | data.read(0x79) << 8) == 225); // 1.1 V at 5 V reference.
    });
}};

const Scenario compactOpcodeIndicesPreserveDispatchAndKeep{"Compact opcode indices preserve dispatch and keep per-core timings in L1",
                                                           {"mcu", "avr", "decoder"}, [] {
    const auto&                                                         indices  = avr::instructionIndices();
    const auto&                                                         handlers = avr::instructionTable();
    std::map<u16, std::pair<avr::InstructionHandler, std::string_view>> kinds;
    for (u32 op = 0; op < indices.size(); ++op) {
        const auto expected       = std::pair{handlers[op], avr::mnemonic(static_cast<u16>(op))};
        const auto [it, inserted] = kinds.emplace(indices[op], expected);
        check(it->second == expected);
    }
    check(sizeof(indices) == 128 * 1024);
    auto m = mcu::create("atmega328p");
    require(m);
    const auto& cpu = static_cast<avr::Device&>(*m).avrCore();
    check(testing::AvrCoreAccess::timingTableBytes(cpu) <= 4096);
    check(testing::AvrCoreAccess::timingTableBytes(cpu) >= kinds.size() * sizeof(avr::InstructionEntry));
}};

const Scenario sharedLpmTimingsRetainTheDistinctImplicit{"Shared LPM timings retain the distinct implicit and explicit destination handlers",
                                                         {"mcu", "avr", "decoder"}, [] {
    auto d                       = mcu::builtinDescription("atmega328p");
    d["timing"]["lpm"]["cycles"] = 5;
    auto         m               = device(d);
    ProgramImage image;
    image.segments = {
        {MemorySpace::Program,  0, {0xC8, 0x95, 0x14, 0x91}},
        {MemorySpace::Program, 16,                   {0x42}}
    };
    m->load(image);  // LPM (R0); LPM R17,Z.
    auto& cpu = static_cast<avr::Device&>(*m).avrCore();
    cpu.setPair(30, 16);
    check(avr::instructionIndices()[0x95C8] != avr::instructionIndices()[0x9114]);
    check(m->step().elapsed == 5);
    check(cpu.r(0) == 0x42);
    check(cpu.r(17) == 0);
    auto invalidTiming = d.at("timing");
    invalidTiming["lpm"]["cycles"] = 9;
    invalidTiming.erase("wdr");
    checkThrows([&] { cpu.setTiming(invalidTiming); });  // A failed reconfiguration leaves the previous timings intact.
    check(m->step().elapsed == 5);
    check(cpu.r(17) == 0x42);
}};
