#pragma once

#include <siliconforge/arch/avr/Opcodes.hpp>

#include <cstdlib>
#include <filesystem>

namespace siliconforge::testing {

/// Records the opcodes executed by the process when SILICONFORGE_OPCODE_COVERAGE names a directory, and saves them
/// there on destruction. CTest sets the variable; the qualification report merges the files (ADR 0006).
class OpcodeCoverageSession {
public:
    OpcodeCoverageSession() {
        if (const auto* directory = std::getenv("SILICONFORGE_OPCODE_COVERAGE"); directory && *directory) {
            target = directory;
            avr::OpcodeCoverage::start();
        }
    }
    ~OpcodeCoverageSession() {
        if (!target.empty())
            avr::OpcodeCoverage::save(target);
    }
    OpcodeCoverageSession(const OpcodeCoverageSession&)            = delete;
    OpcodeCoverageSession& operator=(const OpcodeCoverageSession&) = delete;

private:
    std::filesystem::path target;
};

}  // namespace siliconforge::testing
