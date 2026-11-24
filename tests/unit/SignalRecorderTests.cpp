// Capture memory of the instruments, the pin observers it relies on, and the UART waveform a logic analyser decodes.

#include <siliconforge/instruments/SignalRecorder.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <webfront/testing/Bdd.hpp>

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace webfront::bdd;

using namespace siliconforge;
using namespace std::chrono_literals;

const Scenario theSignalRecorderKeepsTheMostRecent{"The signal recorder keeps the most recent samples and tells a reader what it missed", {"instruments"}, [] {
    instruments::SignalRecorder recorder{8};
    require(recorder.capacity() == 8);
    std::vector<Cycles> seen;
    const auto          collect = [&](const instruments::Sample& sample) { seen.push_back(sample.at); };

    for (Cycles at = 0; at < 5; ++at)
        recorder.record(3, at, at % 2 ? PinState::High : PinState::Low, at % 2 ? 5.0 : 0.0);
    check(recorder.first() == 0);
    check(recorder.next() == 5);
    check(recorder.read(0, 3, collect) == 3);  // Limited: the reader comes back for the rest
    check(recorder.read(3, 100, collect) == 5);
    check(seen == std::vector<Cycles>{0, 1, 2, 3, 4});
    check(recorder.read(5, 100, collect) == 5);  // Nothing new

    for (Cycles at = 5; at < 20; ++at)
        recorder.record(3, at, PinState::High, 5.0);
    check(recorder.first() == 12);  // The ring overtook the reader, who can see it: first() is past its cursor
    seen.clear();
    check(recorder.read(5, 100, collect) == 20);
    check(seen.front() == 12);
    check(seen.size() == 8);

    const auto epoch = recorder.epoch();
    recorder.restart();
    check(recorder.epoch() == epoch + 1);
    check(recorder.next() == 0);
    check(recorder.read(20, 100, collect) == 0);  // A cursor of the previous epoch restarts from the beginning
}};

const Scenario aReaderMayAskForEverySample{"A reader may ask for every sample held, with the largest limit, once the ring has wrapped", {"instruments"}, [] {
    given("a ring of 8 samples that recorded 13", [&] {
        instruments::SignalRecorder recorder{8};
        for (Cycles at = 0; at < 13; ++at)
            recorder.record(0, at, PinState::Low, 0.0);
        require(recorder.first() == 5);
        when("it is read from its first sample with SIZE_MAX as the limit", [&] {
            std::vector<Cycles> seen;
            const auto next = recorder.read(recorder.first(), std::numeric_limits<std::size_t>::max(), [&](const instruments::Sample& sample) { seen.push_back(sample.at); });
            then("the eight samples held are visited, oldest first", [&] {
                check(seen == std::vector<Cycles>{5, 6, 7, 8, 9, 10, 11, 12});
                check(next == 13);
            });
        });
    });
}};

const Scenario aPinTellsItsDriveAndSignal{"A pin tells its drive and signal observers what the state observers cannot see", {"instruments", "pin"}, [] {
    Cycles clock = 0;
    Pin    pin{"PB1", "D9", &clock};
    int    states = 0, drives = 0, signals = 0;
    pin.subscribe([&](const Pin&, Cycles) { ++states; });
    pin.subscribeDrive([&](const Pin&, Cycles) { ++drives; });
    pin.subscribeSignal([&](const Pin&, Cycles) { ++signals; });

    pin.setExternal(false);        // Floating -> low
    pin.configure(true, false, false);  // Becomes an output at the same level: no state change, but the drive changed
    check(states == 1);
    check(drives == 1);
    pin.configure(true, false, false);  // Nothing changed
    check(drives == 1);
    pin.configure(false, false, false);
    check(drives == 2);

    const auto before = signals;
    pin.setVoltage(1.0, 5.0);  // Still low, but an oscilloscope sees 1 V instead of 0 V
    pin.setVoltage(1.2, 5.0);
    check(states == 1);
    check(signals == before + 2);
    pin.setVoltage(1.2, 5.0);
    check(signals == before + 2);
    pin.setVoltage(4.0, 5.0);  // Crosses the threshold: both kinds of observers
    check(states == 2);
    check(signals == before + 3);
}};

const Scenario uartEchoTheTxdPinCarriesThe{"uart_echo: the TXD pin carries the frames bit by bit, as a logic analyser would capture them",
                                           {"instruments", "firmware"}, [] {
    const auto path = std::filesystem::path{SILICONFORGE_FIRMWARE_DIR} / "uart_echo.hex";
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.load(path);
    require(bench.waitForSerial("ready\r\n", 100ms));
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
        check(levelAt(static_cast<double>(edge.at) + 9.5 * bitTime));  // Stop bit
        decoded += static_cast<char>(byte);
        busyUntil = static_cast<double>(edge.at) + 9.5 * bitTime;
    }
    check(decoded == "ready\r\n");
    check(bench.pin("D1").state() == PinState::High);  // Idle
}};
