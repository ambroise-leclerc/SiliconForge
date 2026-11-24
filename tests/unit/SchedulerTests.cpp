#include <siliconforge/core/Scheduler.hpp>

#include <webfront/testing/Bdd.hpp>

#include <vector>

using namespace webfront::bdd;

using namespace siliconforge;

const Scenario theSchedulerFiresEventsInTimeOrder{"The scheduler fires events in time order", {"core", "scheduler"}, [] {
    Scheduler           scheduler;
    std::vector<Cycles> fired;
    const auto          record = [&](Cycles at) { fired.push_back(at); };
    scheduler.schedule(30, record);
    scheduler.schedule(10, record);
    const auto cancelled = scheduler.schedule(20, record);
    scheduler.schedule(10, record);
    check(scheduler.nextEventAt() == 10);

    when("one event is cancelled and time reaches 25", [&] {
        scheduler.cancel(cancelled);
        scheduler.runDue(25);
        then("only the due events fired, with their own time", [&] {
            check(fired == std::vector<Cycles>{10, 10});
            check(scheduler.nextEventAt() == 30);
        });
    });
    when("an event schedules another one already due", [&] {
        scheduler.clear();
        scheduler.schedule(5, [&](Cycles at) { scheduler.schedule(at + 1, record); });
        scheduler.runDue(100);
        check(fired == std::vector<Cycles>{6});
        check(scheduler.nextEventAt() == Scheduler::never);
    });
}};

const Scenario theDeviceClockFollowsEachOverdueEvent{"The device clock follows each overdue event and returns to the instruction boundary",
                                                     {"core", "scheduler"}, [] {
    Scheduler scheduler;
    Cycles clock = 10;
    std::vector<Cycles> observed;
    scheduler.schedule(8, [&](Cycles at) {
        observed.push_back(clock);
        scheduler.schedule(at + 1, [&](Cycles) { observed.push_back(clock); });
    });
    scheduler.runDueWithClock(clock);
    check(observed == std::vector<Cycles>{8, 9});
    check(clock == 10);
    scheduler.schedule(11, [](Cycles) { throw 1; });
    scheduler.runDueWithClock(clock);
    check(clock == 10);
    check(scheduler.nextEventAt() == 11);
    clock = 12;
    checkThrows([&] { scheduler.runDueWithClock(clock); });
    check(clock == 12);
}};
