// Capture memory of the instruments, the pin observers it relies on, and the UART waveform a logic analyser decodes.

#include <siliconforge/instruments/SignalRecorder.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace siliconforge;
using namespace std::chrono_literals;

TEST_CASE("The signal recorder keeps the most recent samples and tells a reader what it missed", "[instruments]") {
    instruments::SignalRecorder recorder{8};
    REQUIRE(recorder.capacity() == 8);
    std::vector<Cycles> seen;
    const auto          collect = [&](const instruments::Sample& sample) { seen.push_back(sample.at); };

    for (Cycles at = 0; at < 5; ++at)
        recorder.record(3, at, at % 2 ? PinState::High : PinState::Low, at % 2 ? 5.0 : 0.0);
    CHECK(recorder.first() == 0);
    CHECK(recorder.next() == 5);
    CHECK(recorder.read(0, 3, collect) == 3);  // Limited: the reader comes back for the rest
    CHECK(recorder.read(3, 100, collect) == 5);
    CHECK(seen == std::vector<Cycles>{0, 1, 2, 3, 4});
    CHECK(recorder.read(5, 100, collect) == 5);  // Nothing new

    for (Cycles at = 5; at < 20; ++at)
        recorder.record(3, at, PinState::High, 5.0);
    CHECK(recorder.first() == 12);  // The ring overtook the reader, who can see it: first() is past its cursor
    seen.clear();
    CHECK(recorder.read(5, 100, collect) == 20);
    CHECK(seen.front() == 12);
    CHECK(seen.size() == 8);

    const auto epoch = recorder.epoch();
    recorder.restart();
    CHECK(recorder.epoch() == epoch + 1);
    CHECK(recorder.next() == 0);
    CHECK(recorder.read(20, 100, collect) == 0);  // A cursor of the previous epoch restarts from the beginning
}

TEST_CASE("A pin tells its drive and signal observers what the state observers cannot see", "[instruments][pin]") {
    Cycles clock = 0;
    Pin    pin{"PB1", "D9", &clock};
    int    states = 0, drives = 0, signals = 0;
    pin.subscribe([&](const Pin&, Cycles) { ++states; });
    pin.subscribeDrive([&](const Pin&, Cycles) { ++drives; });
    pin.subscribeSignal([&](const Pin&, Cycles) { ++signals; });

    pin.setExternal(false);        // Floating -> low
    pin.configure(true, false, false);  // Becomes an output at the same level: no state change, but the drive changed
    CHECK(states == 1);
    CHECK(drives == 1);
    pin.configure(true, false, false);  // Nothing changed
    CHECK(drives == 1);
    pin.configure(false, false, false);
    CHECK(drives == 2);

    const auto before = signals;
    pin.setVoltage(1.0, 5.0);  // Still low, but an oscilloscope sees 1 V instead of 0 V
    pin.setVoltage(1.2, 5.0);
    CHECK(states == 1);
    CHECK(signals == before + 2);
    pin.setVoltage(1.2, 5.0);
    CHECK(signals == before + 2);
    pin.setVoltage(4.0, 5.0);  // Crosses the threshold: both kinds of observers
    CHECK(states == 2);
    CHECK(signals == before + 3);
}

SCENARIO("uart_echo: the TXD pin carries the frames bit by bit, as a logic analyser would capture them", "[instruments][firmware]") {
    const auto path = std::filesystem::path{SILICONFORGE_FIRMWARE_DIR} / "uart_echo.hex";
    if (!std::filesystem::exists(path))
        SKIP(path.string() << " is missing");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.load(path);
    REQUIRE(bench.waitForSerial("ready\r\n", 100ms));
    bench.runFor(5ms);

    // Decode 8N1 at 9600 baud from the recorded edges of D1, sampling each bit at its middle.
    const auto& edges    = bench.transitions("D1");
    const auto  bitTime  = static_cast<double>(bench.mcu().clockHz()) / 9600.0;
    const auto  levelAt  = [&](double cycle) {
        bool high = true;
        for (const auto& edge : edges) {
            if (static_cast<double>(edge.at) > cycle)
                break;
            high = edge.state == PinState::High;
        }
        return high;
    };
    std::string decoded;
    double      busyUntil = 0;
    for (const auto& edge : edges) {
        if (edge.state != PinState::Low || static_cast<double>(edge.at) < busyUntil)
            continue;  // Not a start bit
        unsigned byte = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            if (levelAt(static_cast<double>(edge.at) + (1.5 + bit) * bitTime))
                byte |= 1u << bit;
        CHECK(levelAt(static_cast<double>(edge.at) + 9.5 * bitTime));  // Stop bit
        decoded += static_cast<char>(byte);
        busyUntil = static_cast<double>(edge.at) + 9.5 * bitTime;
    }
    CHECK(decoded == "ready\r\n");
    CHECK(bench.pin("D1").state() == PinState::High);  // Idle
}
