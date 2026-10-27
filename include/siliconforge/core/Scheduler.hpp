#pragma once

#include <siliconforge/core/Types.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

namespace siliconforge {

/// Time-ordered queue of peripheral events, expressed in core cycles.
///
/// Peripherals do not tick on every cycle: they compute when their next observable change happens (a timer compare
/// match, the end of a serial frame...) and schedule it here. The core only checks nextEventAt() between
/// instructions, which keeps the emulation fast and fully deterministic.
class Scheduler {
public:
    using EventId  = u64;
    using Callback = std::function<void(Cycles now)>;

    static constexpr Cycles never = std::numeric_limits<Cycles>::max();

    EventId schedule(Cycles at, Callback callback) {
        const auto id       = nextId++;
        const auto position = std::ranges::upper_bound(events, at, {}, &Event::at);
        events.insert(position, Event{at, id, std::move(callback)});
        return id;
    }

    void cancel(EventId id) {
        std::erase_if(events, [id](const Event& event) { return event.id == id; });
    }

    [[nodiscard]] Cycles nextEventAt() const noexcept { return events.empty() ? never : events.front().at; }

    /// Fires, in time order, every event due at or before `now`. Callbacks may schedule or cancel events.
    void runDue(Cycles now) {
        while (!events.empty() && events.front().at <= now) {
            auto event = std::move(events.front());
            events.erase(events.begin());
            event.callback(event.at);
        }
    }

    void clear() noexcept { events.clear(); }

    [[nodiscard]] std::size_t pending() const noexcept { return events.size(); }

private:
    struct Event {
        Cycles   at;
        EventId  id;
        Callback callback;
    };

    std::vector<Event> events;
    EventId            nextId{1};
};

}  // namespace siliconforge
