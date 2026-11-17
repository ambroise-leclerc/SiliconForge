#include "Qualification.hpp"

#include <siliconforge/arch/avr/AvrCore.hpp>

#include <format>
#include <fstream>
#include <stdexcept>

namespace siliconforge::qualification {

Options::Options(int argc, char** argv, int first) {
    for (int i = first; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.starts_with("--")) {
            if (i + 1 >= argc)
                throw std::invalid_argument(std::format("{} needs a value", argument));
            values[std::string(argument.substr(2))] = argv[++i];
        } else {
            positional.emplace_back(argument);
        }
    }
}

std::string Options::get(std::string_view name, std::string_view fallback) const {
    const auto it = values.find(std::string(name));
    return it == values.end() ? std::string(fallback) : it->second;
}

u64 Options::number(std::string_view name, u64 fallback) const {
    const auto it = values.find(std::string(name));
    return it == values.end() ? fallback : std::stoull(it->second, nullptr, 0);
}

void writeJson(const std::filesystem::path& path, const nlohmann::json& document) {
    if (path.empty())
        return;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << document.dump(2) << '\n';
    if (!file)
        throw std::runtime_error(std::format("cannot write {}", path.string()));
}

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        return nullptr;
    return nlohmann::json::parse(file, nullptr, false);
}

const std::map<std::string, std::vector<u16>, std::less<>>& opcodesByMnemonic() {
    static const auto all = [] {
        std::map<std::string, std::vector<u16>, std::less<>> result;
        for (u32 op = 0; op < 65536; ++op)
            if (const auto c = avr::classify(static_cast<u16>(op)); c.kind == avr::OpcodeKind::Instruction)
                result[std::string(c.mnemonic)].push_back(static_cast<u16>(op));
        return result;
    }();
    return all;
}

std::string disassemble(u16 op, u16 next) {
    const auto c = avr::classify(op);
    if (c.kind == avr::OpcodeKind::Reserved)
        return std::format(".word 0x{:04x}", op);
    const auto name = c.mnemonic;
    const auto d5   = (op >> 4) & 0x1F;
    const auto r5   = (op & 0x0F) | ((op >> 5) & 0x10);
    const auto d4   = 16 + ((op >> 4) & 0x0F);
    const auto k8   = (op & 0x0F) | ((op >> 4) & 0xF0);
    const auto bit  = op & 7;
    const auto is   = [&](std::initializer_list<std::string_view> names) {
        for (const auto candidate : names)
            if (candidate == name)
                return true;
        return false;
    };
    if (is({"add", "adc", "sub", "sbc", "and", "or", "eor", "mov", "cp", "cpc", "cpse", "mul"}))
        return std::format("{} r{}, r{}", name, d5, r5);
    if (is({"subi", "sbci", "andi", "ori", "cpi", "ldi"}))
        return std::format("{} r{}, 0x{:02x}", name, d4, k8);
    if (is({"com", "neg", "swap", "inc", "dec", "asr", "lsr", "ror", "push", "pop"}))
        return std::format("{} r{}", name, d5);
    if (is({"adiw", "sbiw"}))
        return std::format("{} r{}, {}", name, 24 + ((op >> 3) & 6), (op & 0x0F) | ((op >> 2) & 0x30));
    if (name == "movw")
        return std::format("movw r{}, r{}", ((op >> 4) & 0x0F) * 2, (op & 0x0F) * 2);
    if (name == "muls")
        return std::format("muls r{}, r{}", d4, 16 + (op & 0x0F));
    if (is({"mulsu", "fmul", "fmuls", "fmulsu"}))
        return std::format("{} r{}, r{}", name, 16 + ((op >> 4) & 7), 16 + (op & 7));
    if (is({"lds"}))
        return std::format("lds r{}, 0x{:04x}", d5, next);
    if (is({"sts"}))
        return std::format("sts 0x{:04x}, r{}", next, d5);
    if (name.starts_with("ldd ") || name.starts_with("std ")) {
        const auto q       = (op & 7) | ((op >> 7) & 0x18) | ((op >> 8) & 0x20);
        const auto pointer = name.back() == 'z' ? 'Z' : 'Y';
        return name.starts_with("ldd") ? std::format("ldd r{}, {}+{}", d5, pointer, q) : std::format("std {}+{}, r{}", pointer, q, d5);
    }
    if (name.starts_with("ld "))
        return std::format("ld r{}, {}", d5, name.substr(3));
    if (name.starts_with("st "))
        return std::format("st {}, r{}", name.substr(3), d5);
    if (name == "lpm")
        return op == 0x95C8 ? "lpm" : std::format("lpm r{}, z", d5);
    if (name == "lpm z+")
        return std::format("lpm r{}, z+", d5);
    if (name == "in")
        return std::format("in r{}, 0x{:02x}", d5, (op & 0x0F) | ((op >> 5) & 0x30));
    if (name == "out")
        return std::format("out 0x{:02x}, r{}", (op & 0x0F) | ((op >> 5) & 0x30), d5);
    if (is({"sbi", "cbi", "sbic", "sbis"}))
        return std::format("{} 0x{:02x}, {}", name, (op >> 3) & 0x1F, bit);
    if (is({"sbrc", "sbrs", "bst", "bld"}))
        return std::format("{} r{}, {}", name, d5, bit);
    if (is({"bset", "bclr"}))
        return std::string(c.alias);
    if (is({"brbs", "brbc"}))
        return std::format("{} .{:+}", c.alias, (static_cast<int>((op >> 3) & 0x7F) - ((op & 0x0200) ? 0x80 : 0)) * 2);
    if (is({"rjmp", "rcall"}))
        return std::format("{} .{:+}", name, (static_cast<int>(op & 0x0FFF) - ((op & 0x0800) ? 0x1000 : 0)) * 2);
    if (is({"jmp", "call"}))
        return std::format("{} 0x{:04x}", name, next * 2);
    return std::string(name);
}

}  // namespace siliconforge::qualification
