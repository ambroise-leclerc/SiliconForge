#include "../support/AvrFixture.hpp"

#include <siliconforge/arch/avr/Opcodes.hpp>
#include <siliconforge/mcu/Registry.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

using namespace siliconforge;
using namespace siliconforge::avr;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

TEST_CASE("Each of the 65 536 opcodes is an instruction of the description or a reserved opcode", "[avr][qualification]") {
    const auto            timing = mcu::builtinDescription("atmega328p").at("timing");
    std::set<std::string> used;
    unsigned              reserved = 0;
    for (u32 op = 0; op < 65536; ++op) {
        const auto c = classify(static_cast<u16>(op));
        if (c.kind == OpcodeKind::Reserved) {
            ++reserved;
            CHECK(c.mnemonic.empty());
            continue;
        }
        INFO("opcode " << op);
        REQUIRE(timing.contains(std::string(c.mnemonic)));
        REQUIRE((c.variant == "AVR" || c.variant == "AVRe" || c.variant == "AVRe+"));
        used.insert(std::string(c.mnemonic));
    }
    // The table of the description and the decoder name the same instructions, no more, no fewer.
    CHECK(used.size() == timing.size());
    CHECK(reserved == 1766);  // 1 554 unknown to every AVR core, and 212 instructions of the cores not emulated
}

TEST_CASE("A reserved opcode stops the execution, whichever it is", "[avr][qualification]") {
    AvrFixture avr{AvrAssembler{}.nop().nop()};
    unsigned   stopped = 0, reserved = 0;
    for (u32 op = 0; op < 65536; ++op) {
        if (classify(static_cast<u16>(op)).kind != OpcodeKind::Reserved)
            continue;
        ++reserved;
        avr.mcu.load(AvrAssembler{}.raw(static_cast<u16>(op)).nop().image());
        avr.mcu.reset();
        const auto result = avr.mcu.run(4);
        stopped += result.reason == StopReason::InvalidOpcode && avr.core.pc == 0;
    }
    CHECK(stopped == reserved);
}

TEST_CASE("Aliases and instructions of other cores are named", "[avr][qualification]") {
    CHECK(classify(0x0C00).alias == "lsl");  // ADD r0, r0
    CHECK(classify(0x0C01).alias.empty());   // ADD r0, r1
    CHECK(classify(0x1FFF).alias == "rol");
    CHECK(classify(0x2411).alias == "clr");
    CHECK(classify(0x2000).alias == "tst");
    CHECK(classify(0xEF0F).alias == "ser");
    CHECK(classify(0x9478).alias == "sei");
    CHECK(classify(0x94F8).alias == "cli");
    CHECK(classify(0xF009).alias == "breq");
    CHECK(classify(0xF409).alias == "brne");
    CHECK(classify(0x8000).alias == "ld z");
    CHECK(classify(0x8001).alias.empty());  // LDD r0, Z+1
    CHECK(classify(0x8208).alias == "st y");

    CHECK(classify(0x9C00).variant == "AVRe+");  // MUL
    CHECK(classify(0x0100).variant == "AVRe");   // MOVW
    CHECK(classify(0x95C8).variant == "AVR");    // LPM (R0 implied)
    CHECK(classify(0x9004).variant == "AVRe");   // LPM Rd, Z

    for (const auto& [opcode, name] : std::map<u16, std::string_view>{
             {0x9419, "eijmp"}, {0x9519, "eicall"}, {0x95D8, "elpm"}, {0x9006, "elpm"}, {0x9007, "elpm"}, {0x95F8, "spm z+"},
             {0x940B, "des"},   {0x9204, "xch"},    {0x9205, "las"},  {0x9206, "lac"},  {0x9207, "lat"}}) {
        INFO(name);
        CHECK(classify(opcode).kind == OpcodeKind::Reserved);
        CHECK(classify(opcode).elsewhere == name);
    }
    CHECK(classify(0x0001).elsewhere.empty());
}

#ifdef SILICONFORGE_AVR_OBJDUMP
// An independent reading of the instruction set: the disassembler of binutils. It knows every AVR core at once, so the
// opcodes it rejects must be reserved here, and those it alone accepts must be the instructions of the other cores.
TEST_CASE("The classification agrees with the disassembler of binutils", "[avr][qualification]") {
    const auto path = std::filesystem::temp_directory_path() / "siliconforge-all-opcodes.bin";
    {
        std::ofstream file(path, std::ios::binary);
        for (u32 op = 0; op < 65536; ++op)  // Each opcode is followed by a NOP, the second word of the long ones.
            file.put(static_cast<char>(op)).put(static_cast<char>(op >> 8)).put(0).put(0);
    }
    const auto command = std::string(SILICONFORGE_AVR_OBJDUMP) + " -D -b binary -m avr:5 " + path.string();
    auto*      listing = popen(command.c_str(), "r");
    REQUIRE(listing != nullptr);
    std::array<std::string, 65536> names;
    char                           line[256];
    unsigned                       read = 0;
    while (std::fgets(line, sizeof line, listing)) {
        unsigned address = 0;
        char     name[32]{};
        // "   2401c:\t07 90       \telpm\tr0, Z+"
        if (std::sscanf(line, " %x: %*x %*x %*x %*x %31s", &address, name) != 2 && std::sscanf(line, " %x: %*x %*x %31s", &address, name) != 2)
            continue;
        if (address % 4 == 0 && address / 4 < names.size()) {
            names[address / 4] = name;
            ++read;
        }
    }
    pclose(listing);
    std::filesystem::remove(path);
    REQUIRE(read == 65536);

    unsigned disagreements = 0;
    for (u32 op = 0; op < 65536; ++op) {
        const auto  c    = classify(static_cast<u16>(op));
        const auto& name = names[op];
        const auto  word = [](std::string_view text) { return text.substr(0, text.find(' ')); };
        bool        same;
        if (name == ".word")
            same = c.kind == OpcodeKind::Reserved && c.elsewhere.empty();
        else if (c.kind == OpcodeKind::Reserved)
            same = word(c.elsewhere) == name;
        else
            same = word(c.mnemonic) == name || word(c.alias) == name;
        if (!same) {
            ++disagreements;
            UNSCOPED_INFO("opcode " << op << ": binutils says " << name << ", classified " << c.mnemonic << " / " << c.alias << " / " << c.elsewhere);
        }
    }
    CHECK(disagreements == 0);
}
#endif
