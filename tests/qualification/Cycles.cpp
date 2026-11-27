// Cycles of every instruction, measured on the emulated device and compared with the `timing` table of its description
// and with the AVR Instruction Set Manual (ADR 0006, #26).
//
// The list of what is measured comes from the description: each entry of `timing`, and each of its cases (taken,
// skipOneWord, skipTwoWords, sram, pc3Bytes), has its scenario, or the tool fails. Two checks are made:
//   - every scenario: measured == description == manual (exceptions below);
//   - every opcode of every instruction executed once from a reset state: the cycles are one of those of its entry.

#include "Qualification.hpp"
#include "support/AvrAssembler.hpp"

#include <siliconforge/mcu/Description.hpp>
#include <siliconforge/mcu/Registry.hpp>

#include <siliconforge/arch/avr/Device.hpp>

#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>

namespace siliconforge::qualification {

namespace {

using test::AvrAssembler;

/// Cycles of the "AVR Instruction Set Manual" (Microchip DS40002198B), column AVRe+ of each instruction, for devices
/// with a 16-bit program counter and internal SRAM. A missing case uses `cycles`.
struct Manual {
    unsigned cycles;
    int      taken{-1}, skipOneWord{-1}, skipTwoWords{-1}, sram{-1}, pc3Bytes{-1};
};
const std::map<std::string, Manual, std::less<>> manual{
    {"adc", {1}},   {"add", {1}},   {"adiw", {2}},  {"and", {1}},  {"andi", {1}}, {"asr", {1}},
    {"bclr", {1}},  {"bld", {1}},   {"brbc", {1, 2}}, {"brbs", {1, 2}}, {"break", {1}}, {"bset", {1}},
    {"bst", {1}},   {"call", {4, -1, -1, -1, -1, 5}}, {"cbi", {2}}, {"com", {1}}, {"cp", {1}}, {"cpc", {1}},
    {"cpi", {1}},   {"cpse", {1, -1, 2, 3}}, {"dec", {1}}, {"eicall", {4}}, {"eijmp", {2}}, {"elpm", {3}}, {"eor", {1}}, {"fmul", {2}}, {"fmuls", {2}},
    {"fmulsu", {2}}, {"icall", {3, -1, -1, -1, -1, 4}}, {"ijmp", {2}}, {"in", {1}}, {"inc", {1}}, {"jmp", {3}},
    {"ld -x", {2, -1, -1, -1, 2}}, {"ld -y", {2, -1, -1, -1, 2}}, {"ld -z", {2, -1, -1, -1, 2}},
    {"ld x", {2, -1, -1, -1, 2}}, {"ld x+", {2, -1, -1, -1, 2}}, {"ld y+", {2, -1, -1, -1, 2}},
    {"ld z+", {2, -1, -1, -1, 2}}, {"ldd y", {2, -1, -1, -1, 2}}, {"ldd z", {2, -1, -1, -1, 2}}, {"ldi", {1}},
    {"lds", {2, -1, -1, -1, 2}}, {"lpm", {3}}, {"lpm z+", {3}}, {"lsr", {1}}, {"mov", {1}}, {"movw", {1}},
    {"mul", {2}}, {"muls", {2}}, {"mulsu", {2}}, {"neg", {1}}, {"nop", {1}}, {"or", {1}}, {"ori", {1}},
    {"out", {1}}, {"pop", {2, -1, -1, -1, 2}}, {"push", {2, -1, -1, -1, 2}}, {"rcall", {3, -1, -1, -1, -1, 4}},
    {"ret", {4, -1, -1, -1, -1, 5}}, {"reti", {4, -1, -1, -1, -1, 5}}, {"rjmp", {2}}, {"ror", {1}}, {"sbc", {1}},
    {"sbci", {1}}, {"sbi", {2}}, {"sbic", {1, -1, 2, 3}}, {"sbis", {1, -1, 2, 3}}, {"sbiw", {2}},
    {"sbrc", {1, -1, 2, 3}}, {"sbrs", {1, -1, 2, 3}}, {"sleep", {1}}, {"spm", {1}},
    {"st -x", {2, -1, -1, -1, 2}}, {"st -y", {2, -1, -1, -1, 2}}, {"st -z", {2, -1, -1, -1, 2}},
    {"st x", {2, -1, -1, -1, 2}}, {"st x+", {2, -1, -1, -1, 2}}, {"st y+", {2, -1, -1, -1, 2}},
    {"st z+", {2, -1, -1, -1, 2}}, {"std y", {2, -1, -1, -1, 2}}, {"std z", {2, -1, -1, -1, 2}},
    {"sts", {2, -1, -1, -1, 2}}, {"sub", {1}}, {"subi", {1}}, {"swap", {1}}, {"wdr", {1}},
};

/// Where the manual gives no single number, and what the description uses instead.
const std::map<std::string, std::string, std::less<>> exceptions{
    {"spm", "le manuel ne donne pas de nombre de cycles : le CPU est arrêté pendant l'effacement ou l'écriture de la "
            "page, quelques ms que l'émulateur ne modélise pas. La description compte l'instruction elle-même, 1 cycle."},
};

/// Opcodes whose cycles include a halt of the CPU decided by a peripheral, executed from the reset state.
struct Stall {
    unsigned    extra;
    std::string reason;
};
std::map<u16, Stall> eepromStalls(const nlohmann::json& description, Address ioStart) {
    for (const auto& peripheral : description.at("peripherals")) {
        if (peripheral.at("type") != "avr.eeprom")
            continue;
        const auto reg = peripheral.at("registers").at("control").get<std::string>();
        const auto address = description.at("registers").at(reg).at("address").get<Address>();
        if (address >= ioStart && address - ioStart < 32) {
            const auto opcode = static_cast<u16>(0x9A00 | ((address - ioStart) << 3));  // SBI control, EERE (bit 0).
            return {{opcode, {peripheral.at("readCycles").get<unsigned>(),
                              "SBI active EERE et ajoute les cycles de lecture du périphérique `avr.eeprom`."}}};
        }
    }
    return {};
}

/// A program, the number of its instructions that prepare the state, then the instruction measured.
struct Scenario {
    std::string  name;  ///< Case of the timing entry: "cycles", "taken", "skipOneWord", "skipTwoWords", "sram".
    std::string  description;
    AvrAssembler program;
    int          setup;
    std::optional<Address> dataAddress{};  ///< Address the measured instruction must access.
};

struct MemoryLayout {
    Address scratch;  ///< Data address of the general-purpose I/O register used by the fixtures.
    Address sram;
    Address spl;
    Address sph;
    unsigned bitIo;   ///< Instruction operand for SBI, CBI, SBIC and SBIS on the scratch register.
};

MemoryLayout memoryLayout(const nlohmann::json& description, const avr::AvrCoreConfig& config) {
    const auto& registers = description.at("registers");
    const auto scratch = registers.at(registers.contains("GPIOR0") ? "GPIOR0" : "GPIO_GPIOR0")
                             .at("address").get<Address>();
    if (scratch < config.ioStart || scratch - config.ioStart >= 32)
        throw std::logic_error("Qualification scratch register must support bit I/O instructions");
    return {scratch, description.at("memories").at("sram").at("start").get<Address>(),
            config.spl, config.sph, scratch - config.ioStart};
}

/// The pointer register (26, 28, 30) of an indirect access, set to `address`.
AvrAssembler pointer(std::string_view mnemonic, unsigned address) {
    const unsigned reg = mnemonic.contains('x') ? 26 : mnemonic.contains('y') ? 28 : 30;
    return AvrAssembler{}.ldi(reg, address & 0xFF).ldi(reg + 1, address >> 8);
}

/// Scenarios of an entry, one per case that the device can reach.
std::vector<Scenario> scenarios(const std::string& mnemonic, const nlohmann::json& entry, unsigned pcBytes, const MemoryLayout& layout) {
    std::vector<Scenario> result;
    const auto add = [&](std::string name, std::string description, AvrAssembler program, int setup,
                         std::optional<Address> address = std::nullopt) {
        result.push_back({std::move(name), std::move(description), std::move(program), setup, address});
    };
    const auto first = opcodesByMnemonic().find(mnemonic)->second.front();
    const bool memory = mnemonic == "lds" || mnemonic == "sts" || mnemonic.starts_with("ld ") ||
        mnemonic.starts_with("st ") || mnemonic.starts_with("ldd ") || mnemonic.starts_with("std ");

    if (mnemonic == "brbs" || mnemonic == "brbc") {
        const bool set = mnemonic == "brbs";
        auto       taken = AvrAssembler{};
        (set ? taken.sec() : taken.clc()).raw(static_cast<u16>(first | 1 << 3));  // BRxS/BRxC C, .+2
        auto notTaken = AvrAssembler{};
        (set ? notTaken.clc() : notTaken.sec()).raw(static_cast<u16>(first | 1 << 3));
        add("cycles", "condition false: falls through", notTaken, 1);
        add("taken", "condition true: branches", taken, 1);
    } else if (mnemonic == "cpse" || mnemonic == "sbrc" || mnemonic == "sbrs" || mnemonic == "sbic" || mnemonic == "sbis") {
        // `skip(a)` prepares a state where the instruction skips (a = true) or not.
        const auto make = [&](bool skips) {
            AvrAssembler a;
            if (mnemonic == "cpse")
                a.ldi(16, 1).ldi(17, skips ? 1 : 2).cpse(16, 17);
            else if (mnemonic == "sbrc")
                a.ldi(16, skips ? 0 : 1).nop().sbrc(16, 0);
            else if (mnemonic == "sbrs")
                a.ldi(16, skips ? 1 : 0).nop().sbrs(16, 0);
            else if (mnemonic == "sbic")
                (skips ? a.cbi(layout.bitIo, 0) : a.sbi(layout.bitIo, 0)).nop().sbic(layout.bitIo, 0);
            else
                (skips ? a.sbi(layout.bitIo, 0) : a.cbi(layout.bitIo, 0)).nop().sbis(layout.bitIo, 0);
            return a;
        };
        add("cycles", "no skip", make(false).nop(), 2);
        add("skipOneWord", "skips a one-word instruction (NOP)", make(true).nop(), 2);
        add("skipTwoWords", "skips a two-word instruction (LDS)", make(true).lds(0, layout.sram), 2);
    } else if (memory || mnemonic == "push" || mnemonic == "pop") {
        const auto access = [&](Address address, std::string name, std::string_view region) {
            AvrAssembler program;
            int setup = 0;
            if (mnemonic == "lds" || mnemonic == "sts") {
                program.raw(first).raw(static_cast<u16>(address));
            } else if (mnemonic == "push" || mnemonic == "pop") {
                // PUSH writes at SP before decrementing, while POP increments SP before reading.
                const auto sp = address - (mnemonic == "pop" ? 1u : 0u);
                program.ldi(16, sp >> 8).sts(layout.sph, 16)
                       .ldi(16, sp & 0xff).sts(layout.spl, 16).raw(first);
                setup = 4;
            } else {
                // A pre-decrement instruction needs a pointer one byte past the intended address.
                const auto initial = address + (mnemonic.contains('-') ? 1u : 0u);
                program = pointer(mnemonic, initial);
                program.raw(first);
                setup = 2;
            }
            add(std::move(name), std::format("{} access in {} ({:#06x})", mnemonic, region, address), std::move(program), setup, address);
        };
        access(layout.scratch, "cycles", "I/O");
        access(layout.sram, "sram", "SRAM");
    } else if (mnemonic == "jmp" || mnemonic == "call") {
        add("cycles", "to 0x0010", AvrAssembler{}.raw(first).raw(0x0008), 0);
    } else if (mnemonic == "sleep") {
        add("cycles", "sleep disabled (SMCR.SE = 0): no sleep", AvrAssembler{}.raw(first), 0);
    } else {
        add("cycles", "from the reset state", AvrAssembler{}.raw(first).nop(), 0);
    }
    if (pcBytes == 3 && entry.contains("pc3Bytes"))
        for (auto& scenario : result)
            if (scenario.name == "cycles")
                scenario.name = "pc3Bytes";
    return result;
}

Cycles measure(avr::Device& device, const Scenario& scenario) {
    device.load(scenario.program.image());
    device.reset();
    for (int i = 0; i < scenario.setup; ++i)
        device.step();
    auto& debugger = device.debugger();
    if (scenario.dataAddress)
        debugger.addWatchpoint(*scenario.dataAddress, Access::ReadWrite);
    const auto before = device.cycles();
    const auto result = device.step();
    if (scenario.dataAddress) {
        debugger.clearWatchpoints();
        if (result.reason != StopReason::Watchpoint)
            throw std::logic_error(std::format("{} did not reach data address {:#06x}", scenario.description, *scenario.dataAddress));
    }
    return device.cycles() - before;
}

int expectedCase(const nlohmann::json& entry, std::string_view name) {
    return entry.contains(std::string(name)) ? entry.at(std::string(name)).get<int>() : entry.at("cycles").get<int>();
}

int manualCase(const Manual& m, std::string_view name) {
    const auto value = name == "taken" ? m.taken : name == "skipOneWord" ? m.skipOneWord : name == "skipTwoWords" ? m.skipTwoWords
                     : name == "sram" ? m.sram : name == "pc3Bytes" ? m.pc3Bytes : static_cast<int>(m.cycles);
    return value < 0 ? static_cast<int>(m.cycles) : value;
}

}  // namespace

int cycles(const Options& options) {
    // No opcode coverage: the sweep below executes every opcode from the reset state, which says nothing of how well
    // an instruction is exercised.
    const auto                           name        = options.get("mcu", "atmega328p");
    const auto                           description = mcu::builtinDescription(name);
    const auto&                          timing      = description.at("timing");
    const auto                           pcBytes     = description.at("cpu").value("pcBytes", 2u);
    avr::Device                          device(description);
    const bool consistencyOnly = description.at("core") == "avr:AVRxt";
    const auto& config = device.avrCore().config();
    const auto capabilities = config.instructions();
    const auto layout = memoryLayout(description, config);
    const auto peripheralStalls = eepromStalls(description, config.ioStart);

    nlohmann::json instructions = nlohmann::json::object();
    unsigned       failures = 0, measured = 0;
    for (const auto& [mnemonic, entry] : timing.items()) {
        if (!avr::supportsInstruction(mnemonic, capabilities))
            continue;
        nlohmann::json row{{"cases", nlohmann::json::array()}};
        std::set<std::string> covered;
        const auto            m = manual.find(mnemonic);
        if (!consistencyOnly && m == manual.end()) {
            row["status"] = "fail";
            row["error"]  = "no reference in the manual table of the tool";
            ++failures;
            instructions[mnemonic] = row;
            continue;
        }
        bool ok = true;
        for (const auto& scenario : scenarios(mnemonic, entry, pcBytes, layout)) {
            const auto value      = static_cast<int>(measure(device, scenario));
            const auto expected   = expectedCase(entry, scenario.name);
            const auto reference  = consistencyOnly ? nlohmann::json() : nlohmann::json(manualCase(m->second, scenario.name));
            const bool exception  = !consistencyOnly && exceptions.contains(mnemonic);
            const bool agrees     = value == expected && (consistencyOnly || reference == expected || exception);
            ok                    = ok && agrees;
            covered.insert(scenario.name);
            ++measured;
            row["cases"].push_back({{"case", scenario.name}, {"description", scenario.description}, {"measured", value},
                                    {"description_cycles", expected}, {"manual", reference}, {"agrees", agrees}});
            if (scenario.dataAddress)
                row["cases"].back()["dataAddress"] = *scenario.dataAddress;
            if (!agrees)
                std::cerr << std::format("{} ({}): measured {}, description {}, manual {}\n", mnemonic, scenario.name, value, expected, reference.dump());
        }
        // Every case of the entry must have been measured, except those this device cannot reach.
        for (const auto& [field, value] : entry.items()) {
            if (covered.contains(field))
                continue;
            if ((field == "pc3Bytes" && pcBytes != 3) || (field == "cycles" && pcBytes == 3 && entry.contains("pc3Bytes"))) {
                const auto note = pcBytes == 3 ? "not applicable: three-byte program counter" : "not applicable: two-byte program counter";
                const auto reference = consistencyOnly ? nlohmann::json() : nlohmann::json(manualCase(m->second, field));
                const bool agrees = consistencyOnly || value == reference;
                row["cases"].push_back({{"case", field}, {"description", note},
                                        {"description_cycles", value}, {"manual", reference}, {"agrees", agrees}});
                ok = ok && agrees;
                continue;
            }
            ok = false;
            std::cerr << std::format("{}: no scenario for the case {}\n", mnemonic, field);
            row["cases"].push_back({{"case", field}, {"description", "no scenario"}, {"agrees", false}});
        }
        if (const auto e = exceptions.find(mnemonic); !consistencyOnly && e != exceptions.end())
            row["exception"] = e->second;
        row["status"] = ok ? (row.contains("exception") ? "exception" : "pass") : "fail";
        failures += !ok;
        instructions[mnemonic] = row;
    }

    // Every opcode once, from the reset state: its cycles are one of the values of its entry.
    unsigned outside = 0, opcodes = 0;
    nlohmann::json examples = nlohmann::json::array();
    for (u32 op = 0; op < 65536; ++op) {
        const auto c = avr::classify(static_cast<u16>(op), capabilities);
        if (c.kind != avr::OpcodeKind::Instruction)
            continue;
        ++opcodes;
        const auto& entry = timing.at(std::string(c.mnemonic));
        std::set<int> allowed;
        for (const auto& [field, value] : entry.items()) {
            if ((field == "pc3Bytes" && pcBytes != 3) || (field == "cycles" && pcBytes == 3 && entry.contains("pc3Bytes")))
                continue;
            allowed.insert(value.get<int>());
        }
        auto value = static_cast<int>(measure(device, {"", "", AvrAssembler{}.raw(static_cast<u16>(op)).nop().nop(), 0}));
        if (const auto stall = peripheralStalls.find(static_cast<u16>(op)); stall != peripheralStalls.end())
            value -= static_cast<int>(stall->second.extra);
        if (!allowed.contains(value)) {
            ++outside;
            if (examples.size() < 20)
                examples.push_back(std::format("{} took {} cycles", disassemble(static_cast<u16>(op), 0), value));
        }
    }
    for (const auto& example : examples)
        std::cerr << "  " << example.get<std::string>() << '\n';

    writeJson(options.get("out"), {{"mcu", description.at("name")},
                                   {"mode", consistencyOnly ? "consistency" : "qualification"},
                                   {"reference", consistencyOnly ? nlohmann::json() :
                                       nlohmann::json("AVR Instruction Set Manual (DS40002198B), colonne AVRe+")},
                                   {"instructions", instructions},
                                   {"scenarios", measured},
                                   {"opcodes", opcodes},
                                   {"opcodesOutsideTheirEntry", outside},
                                   {"peripheralStalls", [&] {
                                        auto list = nlohmann::json::array();
                                        for (const auto& [opcode, stall] : peripheralStalls)
                                            list.push_back({{"instruction", disassemble(opcode, 0)}, {"extra", stall.extra}, {"reason", stall.reason}});
                                        return list;
                                    }()},
                                   {"examples", examples}});
    std::cout << std::format("{}: {} timing entries, {} scenarios, {} opcodes; {} entry(ies) in error, {} opcode(s) outside their entry\n",
                             description.at("name").get<std::string>(), instructions.size(), measured, opcodes, failures, outside);
    if (consistencyOnly)
        std::cout << "Consistency check against the profile; AVRxt timings are not qualified against the manual.\n";
    return failures == 0 && outside == 0 ? 0 : 1;
}

}  // namespace siliconforge::qualification
