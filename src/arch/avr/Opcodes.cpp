// Classification of the 65 536 opcodes and record of the executed ones (ADR 0006). The classification is written
// independently of the decoder of Instructions.cpp, which it only asks for the mnemonic: the tests check that both agree.

#include <siliconforge/arch/avr/AvrCore.hpp>
#include <siliconforge/arch/avr/Opcodes.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <string>

namespace siliconforge::avr {

namespace {

constexpr std::array<std::string_view, 8> flagSet{"sec", "sez", "sen", "sev", "ses", "seh", "set", "sei"};
constexpr std::array<std::string_view, 8> flagClear{"clc", "clz", "cln", "clv", "cls", "clh", "clt", "cli"};
constexpr std::array<std::string_view, 8> branchSet{"brcs", "breq", "brmi", "brvs", "brlt", "brhs", "brts", "brie"};
constexpr std::array<std::string_view, 8> branchClear{"brcc", "brne", "brpl", "brvc", "brge", "brhc", "brtc", "brid"};

std::string_view variantOf(std::string_view mnemonic) {
    for (const auto name : {"mul", "muls", "mulsu", "fmul", "fmuls", "fmulsu"})
        if (mnemonic == name)
            return "AVRe+";
    for (const auto name : {"movw", "lpm z+", "spm", "break"})
        if (mnemonic == name)
            return "AVRe";
    return "AVR";
}

std::string_view aliasOf(u16 op, std::string_view mnemonic) {
    const bool sameRegisters = ((op >> 4) & 0x1F) == ((op & 0x0F) | ((op >> 5) & 0x10));
    if (mnemonic == "add" && sameRegisters) return "lsl";
    if (mnemonic == "adc" && sameRegisters) return "rol";
    if (mnemonic == "and" && sameRegisters) return "tst";
    if (mnemonic == "eor" && sameRegisters) return "clr";
    if (mnemonic == "ldi" && (op & 0x0F0F) == 0x0F0F) return "ser";
    if (mnemonic == "ori") return "sbr";
    if (mnemonic == "andi") return "cbr";
    if (mnemonic == "bset") return flagSet[(op >> 4) & 7];
    if (mnemonic == "bclr") return flagClear[(op >> 4) & 7];
    if (mnemonic == "brbs") return branchSet[op & 7];
    if (mnemonic == "brbc") return branchClear[op & 7];
    const bool noDisplacement = (op & 0x2C07) == 0;
    if (mnemonic == "ldd z" && noDisplacement) return "ld z";
    if (mnemonic == "ldd y" && noDisplacement) return "ld y";
    if (mnemonic == "std z" && noDisplacement) return "st z";
    if (mnemonic == "std y" && noDisplacement) return "st y";
    return {};
}

/// Instructions of the cores that are not emulated: 22-bit program counters, more than 64 KiB of flash, XMEGA.
std::string_view elsewhereOf(u16 op) {
    if (op == 0x9419) return "eijmp";
    if (op == 0x9519) return "eicall";
    if (op == 0x95D8 || (op & 0xFE0E) == 0x9006) return "elpm";
    if (op == 0x95F8) return "spm z+";
    if ((op & 0xFF0F) == 0x940B) return "des";
    if ((op & 0xFE0F) == 0x9204) return "xch";
    if ((op & 0xFE0F) == 0x9205) return "las";
    if ((op & 0xFE0F) == 0x9206) return "lac";
    if ((op & 0xFE0F) == 0x9207) return "lat";
    return {};
}

std::atomic<bool>     active{false};
std::array<u8, 65536> seen{};

}  // namespace

OpcodeClass classify(u16 opcode) {
    const auto name = mnemonic(opcode);
    if (name == "(invalid)")
        return {OpcodeKind::Reserved, {}, {}, {}, elsewhereOf(opcode)};
    // The enhanced LPM is the one with a destination register; LPM alone (R0 implied) is as old as the classic core.
    const auto variant = name == "lpm" && opcode != 0x95C8 ? std::string_view{"AVRe"} : variantOf(name);
    return {OpcodeKind::Instruction, name, variant, aliasOf(opcode, name), {}};
}

void OpcodeCoverage::start() noexcept { active.store(true, std::memory_order_relaxed); }
bool OpcodeCoverage::recording() noexcept { return active.load(std::memory_order_relaxed); }
const std::array<u8, 65536>& OpcodeCoverage::executed() noexcept { return seen; }

void OpcodeCoverage::save(const std::filesystem::path& directory) {
    if (std::ranges::none_of(seen, [](u8 hit) { return hit != 0; }))
        return;
    std::filesystem::create_directories(directory);
    // One file per process: the tests run in parallel, each in its own process.
    std::random_device random;
    const auto         stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto         path  = directory / (std::to_string(stamp) + "-" + std::to_string(random()) + ".opcodes");
    std::ofstream      file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(seen.data()), static_cast<std::streamsize>(seen.size()));
}

std::array<u8, 65536> OpcodeCoverage::merge(const std::filesystem::path& directory) {
    std::array<u8, 65536> all{};
    std::error_code       error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.path().extension() != ".opcodes")
            continue;
        std::ifstream         file(entry.path(), std::ios::binary);
        std::array<u8, 65536> one{};
        file.read(reinterpret_cast<char*>(one.data()), static_cast<std::streamsize>(one.size()));
        if (file.gcount() != static_cast<std::streamsize>(one.size()))
            continue;
        for (std::size_t i = 0; i < all.size(); ++i)
            all[i] |= one[i];
    }
    return all;
}

/// Called by AvrCore::run() when the coverage is recorded.
void recordOpcode(u16 opcode) noexcept { seen[opcode] = 1; }

}  // namespace siliconforge::avr
