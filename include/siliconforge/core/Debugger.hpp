#pragma once

#include <siliconforge/core/Types.hpp>

#include <atomic>
#include <set>
#include <utility>
#include <vector>

namespace siliconforge {

enum class Access : u8 { Read = 1, Write = 2, ReadWrite = 3 };

/// Breakpoints, watchpoints and stop requests, shared by every architecture.
///
/// Breakpoints use program-space byte addresses, watchpoints data-space byte addresses. The cores query
/// hasBreakpoints()/hasWatchpoints() first so that an emulation without any of them pays nothing.
class Debugger {
public:
    void addBreakpoint(Address address) {
        breakpointSet.insert(address);
        if (address >= breakpointMap.size())
            breakpointMap.resize(address + 1);
        breakpointMap[address] = true;
    }
    void removeBreakpoint(Address address) {
        breakpointSet.erase(address);
        if (address < breakpointMap.size())
            breakpointMap[address] = false;
    }
    void clearBreakpoints() {
        breakpointSet.clear();
        breakpointMap.clear();
    }
    [[nodiscard]] bool hasBreakpoints() const noexcept { return !breakpointSet.empty(); }
    [[nodiscard]] bool isBreakpoint(Address address) const noexcept { return address < breakpointMap.size() && breakpointMap[address]; }
    [[nodiscard]] const std::set<Address>& breakpoints() const noexcept { return breakpointSet; }

    void addWatchpoint(Address address, Access access = Access::Write) { watchpoints.push_back({address, access}); }
    void clearWatchpoints() { watchpoints.clear(); }
    [[nodiscard]] bool hasWatchpoints() const noexcept { return !watchpoints.empty(); }

    /// Called by the cores on each data access when hasWatchpoints() is true.
    void checkAccess(Address address, Access access) noexcept {
        for (const auto& watch : watchpoints)
            if (watch.address == address && (static_cast<u8>(watch.access) & static_cast<u8>(access))) {
                watchHit = true;
                hitAddress = address;
            }
    }
    /// Consumes a pending watchpoint hit.
    bool takeWatchHit() noexcept { return std::exchange(watchHit, false); }
    [[nodiscard]] Address lastWatchHit() const noexcept { return hitAddress; }

    /// Thread-safe: asks a running emulation to stop at the next instruction boundary.
    void requestStop() noexcept { stopFlag.store(true, std::memory_order_relaxed); }
    bool takeStopRequest() noexcept { return stopFlag.exchange(false, std::memory_order_relaxed); }
    [[nodiscard]] bool stopRequested() const noexcept { return stopFlag.load(std::memory_order_relaxed); }

private:
    struct Watchpoint {
        Address address;
        Access  access;
    };

    std::set<Address>       breakpointSet;
    std::vector<bool>       breakpointMap;
    std::vector<Watchpoint> watchpoints;
    bool                    watchHit{false};
    Address                 hitAddress{0};
    std::atomic<bool>       stopFlag{false};
};

}  // namespace siliconforge
