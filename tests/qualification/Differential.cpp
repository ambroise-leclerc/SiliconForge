// Differential tests of the AVR core against simavr (ADR 0006, #24).
//
// Random programs, reproducible from their seed, run on SiliconForge and on simavr. After every instruction the
// registers, SREG, SP, PC, the SRAM and the cycle counter are compared. A divergence is reduced to the smallest program
// that still shows it. The divergences that are errors of the reference are listed in knownReferenceDivergences below,
// with their justification: they are counted, simavr is realigned on SiliconForge and the comparison goes on.
//
//   siliconforge_differential [--seed 1] [--programs 2000] [--length 60] [--out differential.json]
//
// The exit status is 1 when an unexplained divergence was found.

#include "Qualification.hpp"
#include "support/OpcodeCoverageSession.hpp"

#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <format>
#include <iostream>
#include <optional>
#include <random>

extern "C" {
#include "sim_avr.h"
}

namespace {

using namespace siliconforge;
using namespace siliconforge::qualification;

constexpr Address sramStart = 0x0100;
constexpr Address gpior0 = 0x3E, gpior1 = 0x4A, gpior2 = 0x4B;  // Data addresses: plain storage on both simulators.
constexpr std::array<Address, 3> plainIo{gpior0, gpior1, gpior2};

/// Instructions left out of the random programs, and why. The matrix shows them as excluded.
const std::map<std::string, std::string, std::less<>> excluded{
    {"sleep", "arrête le cœur jusqu'à une interruption : le réveil relève des périphériques, couvert par les tests "
              "d'interruptions"},
    {"break", "arrête l'émulation pour le débogueur ; simavr l'exécute comme un NOP en l'absence de débogueur"},
    {"wdr", "n'agit que sur le chien de garde, qui n'est pas émulé"},
    {"spm", "écrit la flash par SPMCSR, une séquence de périphérique couverte par les tests unitaires"},
};

struct Divergence {
    unsigned                 step{0};
    unsigned                 pc{0};  ///< Word address of the instruction.
    u16                      opcode{0}, next{0};
    u16                      pointer{0};  ///< Value of X, Y or Z before an indirect store.
    std::string              mnemonic;
    std::vector<std::string> kinds;    ///< "semantics", "flags", "cycles".
    std::vector<std::string> details;  ///< "r16: 0x12 here, 0x13 on simavr".
};

/// Register (26, 28 or 30) of the pointer of an indirect access ("st -y"...), 0 for another instruction.
unsigned pointerRegister(std::string_view mnemonic) {
    if (!mnemonic.starts_with("st ") && !mnemonic.starts_with("ld "))
        return 0;
    return mnemonic.contains('x') ? 26 : mnemonic.contains('y') ? 28 : 30;
}

/// The indirect access reaches its own pointer register, through the register file mapped at 0x00-0x1F.
bool accessesItsPointer(const Divergence& d) {
    const auto pointer = pointerRegister(d.mnemonic);
    const auto address = d.mnemonic.contains('-') ? d.pointer - 1u : d.pointer;
    return pointer != 0 && (address == pointer || address == pointer + 1);
}

/// Divergences that are errors of simavr, not of SiliconForge. `applies` narrows the case when needed.
struct KnownDivergence {
    std::string_view mnemonic;
    std::string_view kind;  ///< "semantics", "flags" or "cycles".
    std::string_view reason;
    bool (*applies)(const Divergence&){nullptr};
};
constexpr std::string_view pointerWriteBack =
    "simavr met X, Y ou Z à jour après l'accès, et réécrit X même pour ST X, qui ne le modifie pas. Quand l'adresse est "
    "le registre pointeur lui-même (fichier de registres en 0x00-0x1F), LD -X lit l'ancien pointeur et un stockage dans "
    "le pointeur est perdu. Le manuel décrémente avant l'accès et laisse X intact pour ST X, comme SiliconForge.";
const std::vector<KnownDivergence> knownReferenceDivergences{
    {"st x", "semantics", pointerWriteBack, accessesItsPointer},  {"st -x", "semantics", pointerWriteBack, accessesItsPointer},
    {"st -y", "semantics", pointerWriteBack, accessesItsPointer}, {"st -z", "semantics", pointerWriteBack, accessesItsPointer},
    {"ld -x", "semantics", pointerWriteBack, accessesItsPointer},  {"ld -y", "semantics", pointerWriteBack, accessesItsPointer},
    {"ld -z", "semantics", pointerWriteBack, accessesItsPointer},
};

// Random programs ------------------------------------------------------------------------------------------------------

enum class Patch : u8 { None, Branch, Relative, Absolute, PointerZ, Return };

/// One or several instructions that only make sense together (a pointer and the access through it, a skip and the
/// instruction it skips...). Jumps go forward, to the beginning of another item: the program always ends.
struct Item {
    std::vector<u16> words{};
    Patch            patch{Patch::None};
    int              target{-1};  ///< Identifier of the destination item.
    int              id{0};
};

struct Program {
    std::vector<Item> items{};
    u64               stateSeed{0};
};

using Random = std::mt19937_64;

unsigned below(Random& random, unsigned bound) { return static_cast<unsigned>(random() % bound); }

u16 pick(Random& random, std::string_view mnemonic) {
    const auto& opcodes = opcodesByMnemonic().find(mnemonic)->second;
    return opcodes[below(random, static_cast<unsigned>(opcodes.size()))];
}

template <std::size_t N>
std::string_view oneOf(Random& random, const std::array<std::string_view, N>& names) { return names[below(random, N)]; }

constexpr std::array<std::string_view, 38> plain{"adc",  "add",  "adiw",  "and",   "andi", "asr", "bclr", "bld", "bset", "bst",
                                                 "com",  "cp",   "cpc",   "cpi",   "dec",  "eor", "fmul", "fmuls", "fmulsu", "inc",
                                                 "ldi",  "lsr",  "mov",   "movw",  "mul",  "muls", "mulsu", "neg", "nop", "or",
                                                 "ori",  "ror",  "sbc",   "sbci",  "sbiw", "sub", "subi", "swap"};
constexpr std::array<std::string_view, 18> indirect{"ld x", "ld x+", "ld -x", "ld y+", "ld -y", "ld z+", "ld -z", "ldd y", "ldd z",
                                                    "st x", "st x+", "st -x", "st y+", "st -y", "st z+", "st -z", "std y", "std z"};

u16 ldi(unsigned d, unsigned k) { return static_cast<u16>(0xE000 | (k & 0xF0) << 4 | (d - 16) << 4 | (k & 0x0F)); }
unsigned destination(u16 op) { return (op >> 4) & 0x1F; }

Address sramAddress(Random& random) { return sramStart + 0x40 + below(random, 0x6C0); }

/// LDS or STS at an SRAM address or, now and then, in the register file (the access that is not in SRAM).
std::vector<u16> direct(Random& random) {
    const auto address = below(random, 6) == 0 ? below(random, 0x20) : sramStart + below(random, 0x800);
    return {pick(random, below(random, 2) ? "lds" : "sts"), static_cast<u16>(address)};
}

Item makeItem(Random& random, int id) {
    Item       item;
    item.id = id;
    const auto jumpTo = [&](Patch patch) {
        item.patch  = patch;
        item.target = id + 1 + static_cast<int>(below(random, 5));
    };
    const auto kind = below(random, 100);
    if (kind < 52) {
        item.words = {pick(random, oneOf(random, plain))};
    } else if (kind < 64) {  // Access through X, Y or Z, loaded just before.
        const auto name    = oneOf(random, indirect);
        const auto pointer = name.back() == 'x' || name.ends_with("x+") ? 26u : name.back() == 'y' || name.ends_with("y+") ? 28u : 30u;
        const bool moves   = name.contains('+') || name.contains('-');
        u16        op;
        do  // "LD r26, X+" and the like are undefined in the manual.
            op = pick(random, name);
        while (moves && destination(op) >= pointer && destination(op) <= pointer + 1);
        const bool displaced = name.starts_with("ldd") || name.starts_with("std");
        // Now and then an address of the register file, up to the pointer itself, Z included (0x1F-0x20 for -Z).
        const auto address   = !displaced && below(random, 6) == 0 ? 2 + below(random, 0x1F) : sramAddress(random);
        item.words           = {ldi(pointer, address), ldi(pointer + 1, address >> 8), op};
    } else if (kind < 69) {
        item.words = direct(random);
    } else if (kind < 74) {  // A POP always follows its own PUSH: the stack never rises above its start.
        item.words = {pick(random, "push")};
        if (below(random, 2))
            item.words.push_back(pick(random, "pop"));
    } else if (kind < 80) {
        item.words = {static_cast<u16>(pick(random, below(random, 2) ? "brbs" : "brbc") & 0xFC07)};
        jumpTo(Patch::Branch);
    } else if (kind < 87) {  // A skip and the instruction it may skip, of one or two words.
        switch (below(random, 5)) {
            case 0: item.words = {pick(random, "cpse")}; break;
            case 1: item.words = {pick(random, "sbrc")}; break;
            case 2: item.words = {pick(random, "sbrs")}; break;
            case 3: item.words = {static_cast<u16>(0x9900 | 0x1E << 3 | below(random, 8))}; break;  // SBIC GPIOR0, b
            default: item.words = {static_cast<u16>(0x9B00 | 0x1E << 3 | below(random, 8))}; break;  // SBIS GPIOR0, b
        }
        if (below(random, 3) == 0) {
            const auto skipped = direct(random);
            item.words.insert(item.words.end(), skipped.begin(), skipped.end());
        } else {
            item.words.push_back(pick(random, oneOf(random, plain)));
        }
    } else if (kind < 93) {
        switch (below(random, 8)) {
            case 0: item.words = {0xC000}; jumpTo(Patch::Relative); break;          // RJMP
            case 1: item.words = {0xD000}; jumpTo(Patch::Relative); break;          // RCALL
            case 2: item.words = {0x940C, 0}; jumpTo(Patch::Absolute); break;       // JMP
            case 3: item.words = {0x940E, 0}; jumpTo(Patch::Absolute); break;       // CALL
            case 4: item.words = {0, 0, 0x9409}; jumpTo(Patch::PointerZ); break;    // IJMP
            case 5: item.words = {0, 0, 0x9509}; jumpTo(Patch::PointerZ); break;    // ICALL
            case 6: item.words = {0, 0, 0, 0, 0x9508}; jumpTo(Patch::Return); break;  // RET
            default: item.words = {0, 0, 0, 0, 0x9518}; jumpTo(Patch::Return); break;  // RETI
        }
        if (item.patch == Patch::Return)  // The register that carries the return address to the stack.
            item.words[1] = static_cast<u16>(16 + below(random, 16));
    } else if (kind < 97) {  // I/O instructions on SREG, SP (read only) and the general purpose I/O registers.
        constexpr std::array<unsigned, 6> readable{0x3F, 0x3D, 0x3E, 0x1E, 0x2A, 0x2B};
        constexpr std::array<unsigned, 4> writable{0x3F, 0x1E, 0x2A, 0x2B};
        const auto                        d = below(random, 32);
        switch (below(random, 4)) {
            case 0: {
                const auto a = readable[below(random, 6)];
                item.words   = {static_cast<u16>(0xB000 | (a & 0x30) << 5 | d << 4 | (a & 0x0F))};
                break;
            }
            case 1: {
                const auto a = writable[below(random, 4)];
                item.words   = {static_cast<u16>(0xB800 | (a & 0x30) << 5 | d << 4 | (a & 0x0F))};
                break;
            }
            case 2: item.words = {static_cast<u16>(0x9A00 | 0x1E << 3 | below(random, 8))}; break;  // SBI GPIOR0, b
            default: item.words = {static_cast<u16>(0x9800 | 0x1E << 3 | below(random, 8))}; break;  // CBI GPIOR0, b
        }
    } else {  // LPM with Z inside the program.
        u16 op;
        switch (below(random, 3)) {
            case 0: op = 0x95C8; break;
            case 1: op = pick(random, "lpm"); break;
            default:
                do
                    op = pick(random, "lpm z+");
                while (destination(op) >= 30);
        }
        const auto z = below(random, 64);
        item.words   = {ldi(30, z), ldi(31, z >> 8), op};
    }
    return item;
}

Program makeProgram(u64 seed, unsigned length) {
    Random  random(seed);
    Program program;
    program.stateSeed = random();
    for (unsigned i = 0; i < length; ++i)
        program.items.push_back(makeItem(random, static_cast<int>(i)));
    return program;
}

/// Lays the items out and resolves the jumps. A jump to an item that is no longer there goes to the next one.
std::vector<u16> assemble(const Program& program) {
    std::vector<unsigned> addresses;
    unsigned              at = 0;
    for (const auto& item : program.items) {
        addresses.push_back(at);
        at += static_cast<unsigned>(item.words.size());
    }
    const auto end       = at;
    const auto addressOf = [&](int id) {
        for (std::size_t i = 0; i < program.items.size(); ++i)
            if (program.items[i].id >= id)
                return addresses[i];
        return end;
    };
    std::vector<u16> words;
    for (std::size_t i = 0; i < program.items.size(); ++i) {
        auto       item   = program.items[i];
        const auto target = item.patch == Patch::None ? 0u : addressOf(item.target);
        const auto offset = target - addresses[i] - 1;  // From a one-word jump at the beginning of the item.
        switch (item.patch) {
            case Patch::None: break;
            case Patch::Branch: item.words[0] = static_cast<u16>(item.words[0] | (offset & 0x7F) << 3); break;
            case Patch::Relative: item.words[0] = static_cast<u16>(item.words[0] | (offset & 0x0FFF)); break;
            case Patch::Absolute: item.words[1] = static_cast<u16>(target); break;
            case Patch::PointerZ:
                item.words[0] = ldi(30, target);
                item.words[1] = ldi(31, target >> 8);
                break;
            case Patch::Return: {  // The low byte is pushed first, as CALL does.
                const auto carrier = item.words[1];
                item.words[0]      = ldi(carrier, target);
                item.words[1]      = static_cast<u16>(0x920F | carrier << 4);
                item.words[2]      = ldi(carrier, target >> 8);
                item.words[3]      = static_cast<u16>(0x920F | carrier << 4);
                break;
            }
        }
        words.insert(words.end(), item.words.begin(), item.words.end());
    }
    return words;
}

// The two simulators ---------------------------------------------------------------------------------------------------

void silent(avr_t*, const int, const char*, va_list) {}

struct Statistics {
    struct Count {
        u64 executions{0}, semantics{0}, flags{0}, cycles{0}, reference{0};
    };
    std::map<std::string, Count, std::less<>> mnemonics;
    std::map<std::string, u64>                knownCounts;  ///< "mnemonic/kind" -> occurrences.
    u64                                       instructions{0};
};

class Pair {
public:
    Pair() {
        avr_global_logger_set(silent);
        reference = avr_make_mcu_by_name("atmega328p");
        if (!reference)
            throw std::runtime_error("simavr has no atmega328p");
        avr_init(reference);
        reference->log = 0;
    }

    /// Runs the program on both simulators. Returns the first divergence that is not a known error of the reference.
    std::optional<Divergence> run(const Program& program, Statistics* statistics) {
        const auto words = assemble(program);
        load(words, program.stateSeed);
        auto&      core  = device.avrCore();
        const auto limit = words.size() * 2 + 16;  // Forward jumps only: each word executes at most once.
        for (unsigned step = 0; step < limit && core.pc < words.size(); ++step) {
            Divergence divergence;
            divergence.step     = step;
            divergence.pc       = core.pc;
            divergence.opcode   = words[core.pc];
            divergence.next     = core.pc + 1 < words.size() ? words[core.pc + 1] : u16{0};
            divergence.mnemonic = std::string(avr::classify(divergence.opcode).mnemonic);
            if (const auto pointer = pointerRegister(divergence.mnemonic))
                divergence.pointer = core.pair(pointer);
            const auto result   = device.step();
            avr_run(reference);
            if (result.reason != StopReason::BudgetReached)
                divergence.details.push_back("SiliconForge stopped"), divergence.kinds.push_back("semantics");
            if (reference->state != cpu_Running)
                divergence.details.push_back("simavr stopped"), divergence.kinds.push_back("semantics");
            compare(divergence);

            if (statistics) {
                auto& count = statistics->mnemonics[divergence.mnemonic];
                ++count.executions;
                ++statistics->instructions;
            }
            if (divergence.kinds.empty())
                continue;
            if (const auto known = knownKinds(divergence); known == divergence.kinds.size()) {
                if (statistics) {
                    ++statistics->mnemonics[divergence.mnemonic].reference;
                    for (const auto& kind : divergence.kinds)
                        ++statistics->knownCounts[divergence.mnemonic + "/" + kind];
                }
                if (reference->state != cpu_Running)
                    return std::nullopt;
                realign();
                continue;
            }
            return divergence;
        }
        return std::nullopt;
    }

private:
    void load(const std::vector<u16>& words, u64 stateSeed) {
        ProgramImage image{{Segment{MemorySpace::Program, 0, {}}}, {}};
        for (const auto word : words) {
            image.segments[0].bytes.push_back(static_cast<u8>(word));
            image.segments[0].bytes.push_back(static_cast<u8>(word >> 8));
        }
        device.load(image);
        device.reset();
        avr_reset(reference);
        std::memset(reference->flash, 0xFF, reference->flashend + 1);
        std::memcpy(reference->flash, image.segments[0].bytes.data(), image.segments[0].bytes.size());

        // The same arbitrary registers, flags and memory on both sides.
        Random random(stateSeed);
        auto&  core = device.avrCore();
        auto&  data = device.dataSpace();
        for (unsigned i = 0; i < 32; ++i)
            core.r(i) = reference->data[i] = static_cast<u8>(random());
        core.sreg = static_cast<u8>(random() & 0x7F);  // Interrupts stay disabled until the program enables them.
        for (unsigned i = 0; i < 8; ++i)
            reference->sreg[i] = (core.sreg >> i) & 1;
        for (Address address = sramStart; address <= data.ramEndAddress(); ++address)
            data.raw(address) = reference->data[address] = static_cast<u8>(random());
        for (const auto address : plainIo)
            data.raw(address) = reference->data[address] = static_cast<u8>(random());
    }

    [[nodiscard]] u8 referenceSreg() const {
        u8 value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= static_cast<u8>((reference->sreg[i] ? 1 : 0) << i);
        return value;
    }
    [[nodiscard]] u16 referenceSp() const { return static_cast<u16>(reference->data[R_SPL] | reference->data[R_SPH] << 8); }

    void compare(Divergence& divergence) {
        auto&      core = device.avrCore();
        auto&      data = device.dataSpace();
        const auto note = [&](std::string_view kind, std::string detail) {
            if (std::ranges::find(divergence.kinds, kind) == divergence.kinds.end())
                divergence.kinds.emplace_back(kind);
            if (divergence.details.size() < 8)
                divergence.details.push_back(std::move(detail));
        };
        for (unsigned i = 0; i < 32; ++i)
            if (core.r(i) != reference->data[i])
                note("semantics", std::format("r{}: 0x{:02x} here, 0x{:02x} on simavr", i, core.r(i), reference->data[i]));
        if (core.pc * 2 != reference->pc)
            note("semantics", std::format("PC: 0x{:04x} here, 0x{:04x} on simavr", core.pc * 2, reference->pc));
        if (core.sp != referenceSp())
            note("semantics", std::format("SP: 0x{:04x} here, 0x{:04x} on simavr", core.sp, referenceSp()));
        if (std::memcmp(&data.raw(sramStart), reference->data + sramStart, data.ramEndAddress() + 1 - sramStart) != 0)
            for (Address address = sramStart; address <= data.ramEndAddress(); ++address)
                if (data.raw(address) != reference->data[address])
                    note("semantics", std::format("[0x{:04x}]: 0x{:02x} here, 0x{:02x} on simavr", address, data.raw(address), reference->data[address]));
        for (const auto address : plainIo)
            if (data.raw(address) != reference->data[address])
                note("semantics", std::format("[0x{:04x}]: 0x{:02x} here, 0x{:02x} on simavr", address, data.raw(address), reference->data[address]));
        if (core.sreg != referenceSreg())
            note("flags", std::format("SREG: 0x{:02x} here, 0x{:02x} on simavr", core.sreg, referenceSreg()));
        if (core.cycles != reference->cycle)
            note("cycles", std::format("cycles: {} here, {} on simavr", core.cycles, reference->cycle));
    }

    [[nodiscard]] static std::size_t knownKinds(const Divergence& divergence) {
        std::size_t known = 0;
        for (const auto& kind : divergence.kinds)
            known += std::ranges::any_of(knownReferenceDivergences, [&](const KnownDivergence& k) {
                return k.mnemonic == divergence.mnemonic && k.kind == kind && (!k.applies || k.applies(divergence));
            });
        return known;
    }

    /// Copies the state of SiliconForge to simavr after a known divergence of the reference.
    void realign() {
        auto& core = device.avrCore();
        auto& data = device.dataSpace();
        std::memcpy(reference->data, data.registers(), 32);
        for (unsigned i = 0; i < 8; ++i)
            reference->sreg[i] = (core.sreg >> i) & 1;
        reference->data[R_SPL] = static_cast<u8>(core.sp);
        reference->data[R_SPH] = static_cast<u8>(core.sp >> 8);
        reference->pc          = core.pc * 2;
        reference->cycle       = core.cycles;
        std::memcpy(reference->data + sramStart, &data.raw(sramStart), data.ramEndAddress() + 1 - sramStart);
        for (const auto address : plainIo)
            reference->data[address] = data.raw(address);
    }

    mcu::Atmega328p device;
    avr_t*          reference{nullptr};
};

// Reduction -------------------------------------------------------------------------------------------------------------

bool same(const std::optional<Divergence>& found, const Divergence& wanted) {
    return found && found->mnemonic == wanted.mnemonic && found->kinds == wanted.kinds;
}

/// Delta debugging: removes items as long as the same instruction still diverges in the same way.
Program reduce(Pair& pair, Program program, const Divergence& wanted) {
    std::size_t parts = 2;
    while (program.items.size() >= 2) {
        const auto chunk   = std::max<std::size_t>(1, program.items.size() / parts);
        bool       removed = false;
        for (std::size_t start = 0; start < program.items.size(); start += chunk) {
            auto candidate = program;
            candidate.items.erase(candidate.items.begin() + static_cast<std::ptrdiff_t>(start),
                                  candidate.items.begin() + static_cast<std::ptrdiff_t>(std::min(start + chunk, candidate.items.size())));
            if (!candidate.items.empty() && same(pair.run(candidate, nullptr), wanted)) {
                program = std::move(candidate);
                parts   = std::max<std::size_t>(parts - 1, 2);
                removed = true;
                break;
            }
        }
        if (!removed) {
            if (chunk == 1)
                break;
            parts = std::min(program.items.size(), parts * 2);
        }
    }
    return program;
}

nlohmann::json listing(const Program& program) {
    const auto words = assemble(program);
    auto       lines = nlohmann::json::array();
    for (std::size_t at = 0; at < words.size(); ++at) {
        const auto next = at + 1 < words.size() ? words[at + 1] : u16{0};
        lines.push_back(std::format("{:04x}: {:04x}  {}", at * 2, words[at], disassemble(words[at], next)));
        if (avr::isTwoWord(words[at]))
            ++at;
    }
    return lines;
}

}  // namespace

int main(int argc, char** argv) try {
    const testing::OpcodeCoverageSession coverage;
    const Options                        options(argc, argv, 1);
    const auto                           seed     = options.number("seed", 1);
    const auto                           programs = options.number("programs", 2000);
    const auto                           length   = static_cast<unsigned>(options.number("length", 60));

    Pair           pair;
    Statistics     statistics;
    nlohmann::json divergences = nlohmann::json::array();
    std::map<std::string, bool> reported;  // One reduced example per instruction and kind of divergence.
    u64            unexplained = 0;
    for (u64 index = 0; index < programs; ++index) {
        const auto program    = makeProgram(seed * 0x9E3779B97F4A7C15ull + index, length);
        const auto divergence = pair.run(program, &statistics);
        if (!divergence)
            continue;
        ++unexplained;
        auto& count = statistics.mnemonics[divergence->mnemonic];
        for (const auto& kind : divergence->kinds)
            ++(kind == "semantics" ? count.semantics : kind == "flags" ? count.flags : count.cycles);
        std::string signature = divergence->mnemonic;
        for (const auto& kind : divergence->kinds)
            signature += "/" + kind;
        if (std::exchange(reported[signature], true))
            continue;
        const auto reduced = reduce(pair, program, *divergence);
        const auto again   = pair.run(reduced, nullptr).value_or(*divergence);
        divergences.push_back({{"program", index},
                               {"instruction", disassemble(again.opcode, again.next)},
                               {"opcode", std::format("0x{:04x}", again.opcode)},
                               {"mnemonic", again.mnemonic},
                               {"kinds", again.kinds},
                               {"details", again.details},
                               {"stateSeed", reduced.stateSeed},
                               {"reduced", listing(reduced)}});
        std::cerr << std::format("Divergence on {} ({}), program {}:\n", disassemble(again.opcode, again.next), signature, index);
        for (const auto& detail : again.details)
            std::cerr << "  " << detail << '\n';
        for (const auto& line : listing(reduced))
            std::cerr << "    " << line.get<std::string>() << '\n';
    }

    nlohmann::json mnemonics = nlohmann::json::object();
    for (const auto& [name, count] : statistics.mnemonics)
        mnemonics[name] = {{"executions", count.executions}, {"semantics", count.semantics}, {"flags", count.flags},
                           {"cycles", count.cycles},         {"reference", count.reference}};
    auto known = nlohmann::json::array();
    for (const auto& k : knownReferenceDivergences) {
        const auto it = statistics.knownCounts.find(std::string(k.mnemonic) + "/" + std::string(k.kind));
        known.push_back({{"mnemonic", k.mnemonic}, {"kind", k.kind}, {"reason", k.reason},
                         {"occurrences", it == statistics.knownCounts.end() ? 0 : it->second}});
    }
    writeJson(options.get("out"), {{"reference", "simavr " SILICONFORGE_SIMAVR_REVISION},
                                   {"mcu", "atmega328p"},
                                   {"seed", seed},
                                   {"programs", programs},
                                   {"length", length},
                                   {"instructions", statistics.instructions},
                                   {"mnemonics", mnemonics},
                                   {"excluded", excluded},
                                   {"knownReferenceDivergences", known},
                                   {"unexplained", unexplained},
                                   {"divergences", divergences}});
    std::cout << std::format("{} programs, {} instructions compared with simavr, {} unexplained divergence(s)\n", programs,
                             statistics.instructions, unexplained);
    return unexplained == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << "siliconforge_differential: " << error.what() << '\n';
    return 2;
}
