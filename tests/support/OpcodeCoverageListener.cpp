// Saves the opcodes executed by a Catch2 test process for the qualification report (ADR 0006).
#include "OpcodeCoverageSession.hpp"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <optional>

namespace {
class OpcodeCoverageListener final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;
    void testRunStarting(const Catch::TestRunInfo&) override { session.emplace(); }
    void testRunEnded(const Catch::TestRunStats&) override { session.reset(); }

private:
    std::optional<siliconforge::testing::OpcodeCoverageSession> session;
};
}  // namespace

CATCH_REGISTER_LISTENER(OpcodeCoverageListener)
