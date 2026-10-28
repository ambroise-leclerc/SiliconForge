#pragma once

#include <siliconforge/core/Mcu.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace siliconforge::app {

/// Runs an MCU on its own thread, paced against the wall clock (real time or a multiple of it) or as fast as possible.
///
/// Every access to the MCU from other threads goes through access(), which holds the simulation lock: the emulation
/// advances by slices of one simulated millisecond, so the UI and the controls never wait long.
class Simulation {
public:
    explicit Simulation(std::unique_ptr<Mcu> device) : device(std::move(device)), worker([this](std::stop_token stop) { loop(stop); }) {}

    ~Simulation() { shutdown(); }

    /// Stops the emulation thread for good; the MCU stays accessible.
    void shutdown() {
        worker.request_stop();
        wakeUp.notify_all();
        if (worker.joinable())
            worker.join();
    }

    /// Runs `function(Mcu&)` under the simulation lock and returns its result.
    template <typename Function>
    decltype(auto) access(Function&& function) {
        std::scoped_lock lock{mutex};
        return std::forward<Function>(function)(*device);
    }

    /// Runs function(Mcu&, optional<StopReason>) with both values protected by the simulation lock.
    template <typename Function>
    decltype(auto) snapshot(Function&& function) {
        std::scoped_lock lock{mutex};
        return std::forward<Function>(function)(*device, lastStop);
    }

    /// Prepares next under the lock, then commits it. A failed initializer keeps the current MCU and run state.
    /// The initializer must prepare any external state before publishing it.
    template <typename Initialize>
    void replace(std::unique_ptr<Mcu> next, Initialize&& initialize) {
        std::scoped_lock lock{mutex};
        initialize(*next);
        running = false;
        device  = std::move(next);
        lastStop.reset();
        paceFrom(std::chrono::steady_clock::now());
    }

    void run() {
        {
            std::scoped_lock lock{mutex};
            running = true;
            lastStop.reset();
            paceFrom(std::chrono::steady_clock::now());
        }
        wakeUp.notify_all();
    }

    void pause() {
        std::scoped_lock lock{mutex};
        running = false;
    }

    /// Executes one instruction while paused.
    void step() {
        std::scoped_lock lock{mutex};
        running  = false;
        lastStop = device->step().reason;
    }

    void reset() {
        std::scoped_lock lock{mutex};
        device->reset();
        lastStop.reset();
        paceFrom(std::chrono::steady_clock::now());
    }

    /// 1.0 is real time, 0 means as fast as possible.
    void setSpeed(double factor) {
        std::scoped_lock lock{mutex};
        speed = factor;
        paceFrom(std::chrono::steady_clock::now());
    }

    [[nodiscard]] bool isRunning() const noexcept { return running; }
    [[nodiscard]] double speedFactor() const noexcept { return speed; }
    [[nodiscard]] std::optional<StopReason> stopReason() const {
        std::scoped_lock lock{mutex};
        return lastStop;
    }

private:
    void paceFrom(std::chrono::steady_clock::time_point now) {
        paceWall   = now;
        paceCycles = device->cycles();
    }

    void loop(std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::unique_lock lock{mutex};
            wakeUp.wait(lock, stop, [this] { return running.load(); });
            if (stop.stop_requested())
                break;

            const auto slice  = std::max<Cycles>(1, device->clockHz() / 1000);
            const auto result = device->run(slice);
            if (result.reason != StopReason::BudgetReached) {
                running  = false;
                lastStop = result.reason;
                continue;
            }
            if (speed <= 0) {
                lock.unlock();
                std::this_thread::yield();  // Let the UI and the controls take the lock between slices.
                continue;
            }
            // Real-time pacing: sleep until the wall clock catches up with the simulated time.
            const auto simulated = static_cast<double>(device->cycles() - paceCycles) / static_cast<double>(device->clockHz()) / speed;
            const auto target    = paceWall + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(simulated));
            lock.unlock();
            std::this_thread::sleep_until(target);
        }
    }

    std::unique_ptr<Mcu>                  device;
    mutable std::mutex                    mutex;
    std::condition_variable_any           wakeUp;
    std::atomic<bool>                     running{false};
    std::atomic<double>                   speed{1.0};
    std::optional<StopReason>             lastStop;
    std::chrono::steady_clock::time_point paceWall{std::chrono::steady_clock::now()};
    Cycles                                paceCycles{0};
    std::jthread                          worker;  // Last: started once everything else is initialised.
};

[[nodiscard]] constexpr std::string_view toString(StopReason reason) {
    switch (reason) {
        case StopReason::BudgetReached: return "budget";
        case StopReason::Breakpoint: return "breakpoint";
        case StopReason::Watchpoint: return "watchpoint";
        case StopReason::BreakInstruction: return "break instruction";
        case StopReason::InvalidOpcode: return "invalid opcode";
        case StopReason::Requested: return "stop requested";
    }
    return "unknown";
}

}  // namespace siliconforge::app
