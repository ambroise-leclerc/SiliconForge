#include <siliconforge/core/Scheduler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace siliconforge;

SCENARIO("The scheduler fires events in time order", "[core][scheduler]") {
    Scheduler           scheduler;
    std::vector<Cycles> fired;
    const auto          record = [&](Cycles at) { fired.push_back(at); };
    scheduler.schedule(30, record);
    scheduler.schedule(10, record);
    const auto cancelled = scheduler.schedule(20, record);
    scheduler.schedule(10, record);
    CHECK(scheduler.nextEventAt() == 10);

    WHEN("one event is cancelled and time reaches 25") {
        scheduler.cancel(cancelled);
        scheduler.runDue(25);
        THEN("only the due events fired, with their own time") {
            CHECK(fired == std::vector<Cycles>{10, 10});
            CHECK(scheduler.nextEventAt() == 30);
        }
    }
    WHEN("an event schedules another one already due") {
        scheduler.clear();
        scheduler.schedule(5, [&](Cycles at) { scheduler.schedule(at + 1, record); });
        scheduler.runDue(100);
        CHECK(fired == std::vector<Cycles>{6});
        CHECK(scheduler.nextEventAt() == Scheduler::never);
    }
}
