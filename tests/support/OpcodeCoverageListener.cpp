// Saves the opcodes executed by a test process for the qualification report (ADR 0006).
#include "OpcodeCoverageSession.hpp"

#include <webfront/testing/Bdd.hpp>

#include <optional>

namespace {
// webfront::bdd calls no listener when a run ends: the session saves the opcodes when the process exits.
std::optional<siliconforge::testing::OpcodeCoverageSession> session;

const webfront::bdd::Listener opcodeCoverage{[] { session.emplace(); }, nullptr};
}  // namespace
