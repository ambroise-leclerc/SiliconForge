#pragma once

#include <siliconforge/arch/avr/Opcodes.hpp>
#include <siliconforge/core/Types.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Tools of the qualification of the CPU core (ADR 0006), shared by siliconforge_qualification and
/// siliconforge_differential.
namespace siliconforge::qualification {

/// Command line "--name value" options after the sub-command.
class Options {
public:
    Options(int argc, char** argv, int first);
    [[nodiscard]] std::string get(std::string_view name, std::string_view fallback = {}) const;
    [[nodiscard]] u64         number(std::string_view name, u64 fallback) const;
    [[nodiscard]] bool        has(std::string_view name) const { return values.contains(std::string(name)); }
    std::vector<std::string>  positional;

private:
    std::map<std::string, std::string> values;
};

void                         writeJson(const std::filesystem::path& path, const nlohmann::json& document);
[[nodiscard]] nlohmann::json readJson(const std::filesystem::path& path);

/// Every opcode of each mnemonic of the emulated instruction set, in increasing order.
[[nodiscard]] const std::map<std::string, std::vector<u16>, std::less<>>& opcodesByMnemonic();

/// "add r0, r1"-like text of one instruction, for the reports; `next` is its second word when it has one.
[[nodiscard]] std::string disassemble(u16 opcode, u16 next);

int cycles(const Options& options);
int program(const Options& options);
int matrix(const Options& options);

}  // namespace siliconforge::qualification
