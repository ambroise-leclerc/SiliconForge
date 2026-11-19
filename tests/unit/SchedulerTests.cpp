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

TEST_CASE("The device clock follows each overdue event and returns to the instruction boundary", "[core][scheduler]") {
    Scheduler scheduler;
    Cycles clock = 10;
    std::vector<Cycles> observed;
    scheduler.schedule(8, [&](Cycles at) {
        observed.push_back(clock);
        scheduler.schedule(at + 1, [&](Cycles) { observed.push_back(clock); });
    });
    scheduler.runDueWithClock(clock);
    CHECK(observed == std::vector<Cycles>{8, 9});
    CHECK(clock == 10);
    scheduler.schedule(11, [](Cycles) { throw 1; });
    clock = 12;
    CHECK_THROWS(scheduler.runDueWithClock(clock));
    CHECK(clock == 12);
}
