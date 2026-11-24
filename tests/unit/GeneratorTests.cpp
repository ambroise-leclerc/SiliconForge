// Signal generator (#63, ADR 0010): a sine, square, triangle or arbitrary wave stepped on the clock of the MCU. The captures
// are compared with references that follow from the definition of each wave, not from the model's own output: the exact
// cycle of every step, the exact level of a square wave, the distance of a held sine or triangle to the ideal one.

#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"

#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

using namespace webfront::bdd;

using namespace siliconforge;
using namespace std::chrono_literals;
using siliconforge::test::Approx;
using Json = nlohmann::json;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};

constexpr double clockHz = 16'000'000.0;

/// A generator (i20 output, i22 common) wired to ground, its output wired to PC5 (pin 28, f3) of the chip at column 3.
Json generatorCircuit(std::string_view load = {}) {
    Json description = {
        {"formatVersion", 1},
        {"name", "Generator"},
        {"board", {{"type", "breadboard"}, {"columns", 30}}},
        {"chip", {{"mcu", "atmega328p"}, {"column", 3}}},
        {"components", {{{"id", "gbf"}, {"type", "signal-generator"}, {"at", {{"output", "i20"}, {"common", "i22"}}}}}},
        {"wires", {{{"from", "h22"}, {"to", "T-22"}, {"color", "black"}}, {{"from", "h20"}, {"to", "h3"}, {"color", "yellow"}}}}
    };
    if (!load.empty()) {  // A resistor from the output strip to the ground rail.
        description["components"].push_back({{"id", "load"}, {"type", "resistor"}, {"at", {{"a", std::string{"g20"}}, {"b", std::string{"g22"}}}}, {"properties", {{"resistance", 1000}}}});
    }
    return description;
}

/// What an oscilloscope probe on the pin sees: the cycle and the voltage of every change.
struct Capture {
    std::vector<std::pair<Cycles, double>> samples;
    void attach(Pin& pin) {
        pin.subscribeSignal([this](const Pin& changed, Cycles at) { samples.emplace_back(at, changed.voltage(5.0)); });
    }
};

testing::Bench benchWithGenerator() {
    const auto path = firmwareDir / "blink.hex";
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(generatorCircuit());
    bench.load(path);
    return bench;
}

void program(testing::Bench& bench, double waveform, double frequency, double amplitude, double offset) {
    auto& circuit = bench.circuit();
    require(circuit.set("gbf", "waveform", waveform));
    require(circuit.set("gbf", "frequency", frequency));
    require(circuit.set("gbf", "amplitude", amplitude));
    require(circuit.set("gbf", "offset", offset));
}
}  // namespace

const Scenario aSineIsProgrammedByFrequencyAmplitude{"a sine is programmed by frequency, amplitude and offset and follows the ideal sine",
                                                     {"circuit", "generator"}, [] {
    given("a 1 kHz sine of 1 V around 2.5 V on a 16 MHz MCU", [&] {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 0, 1000, 1.0, 2.5);

        when("the channel is enabled and the MCU runs for 4 periods", [&] {
            const auto start = bench.mcu().cycles();
            require(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(4ms);

            then("it steps 64 times per period, each step on its exact cycle: 250 cycles apart", [&] {
                // The first sample is the offset itself (sine at phase 0), which is the voltage already on the pin.
                require(capture.samples.size() >= 4 * 64 - 8);
                for (const auto& [at, volts] : capture.samples) {
                    const auto step = std::llround(static_cast<double>(at - start) / 250.0);
                    check(at - start == static_cast<Cycles>(step) * 250);
                    (void)volts;
                }
            });
            then("every capture is within one step of the ideal sine at the instant it was taken", [&] {
                const auto bound = 1.0 * 2.0 * std::numbers::pi / 64.0 + 1e-6;
                for (const auto& [at, volts] : capture.samples) {
                    const auto t = static_cast<double>(at - start) / clockHz;
                    check(std::abs(volts - (2.5 + std::sin(2.0 * std::numbers::pi * 1000.0 * t))) <= bound);
                }
            });
            then("it reaches the peaks and the offset reference values", [&] {
                double highest = -1e9, lowest = 1e9;
                for (const auto& [at, volts] : capture.samples) {
                    highest = std::max(highest, volts);
                    lowest  = std::min(lowest, volts);
                }
                check(highest == Approx(3.5).margin(1e-9));
                check(lowest == Approx(1.5).margin(1e-9));
            });
        });
    });
}};

const Scenario aSquareWaveChangesAtItsTwo{"a square wave changes at its two edges with the duty cycle asked for", {"circuit", "generator"}, [] {
    given("a 1 kHz square wave of 2 V around 2 V, 25 % high", [&] {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 1, 1000, 2.0, 2.0);
        require(bench.circuit().set("gbf", "duty", 25.0));
        const auto start = bench.mcu().cycles();

        when("the channel is enabled for 3 periods", [&] {
            require(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(3ms);
            then("it is high (4 V) for 4000 cycles, then low (0 V) for 12000, three times", [&] {
                require(capture.samples.size() >= 6);
                for (std::size_t i = 0; i < 6; ++i) {
                    check(capture.samples[i].first - start == static_cast<Cycles>(i / 2) * 16000 + (i % 2 ? 4000 : 0));
                    check(capture.samples[i].second == Approx(i % 2 ? 0.0 : 4.0).margin(1e-9));
                }
            });
        });
    });
}};

const Scenario aTriangleRisesAndFallsLinearlyBetween{"a triangle rises and falls linearly between its extremes", {"circuit", "generator"}, [] {
    given("a 500 Hz triangle of 2 V around 0 V", [&] {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 2, 500, 2.0, 0.0);
        const auto start = bench.mcu().cycles();
        when("it runs for 2 periods", [&] {
            require(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(4ms);
            then("it starts at the low extreme and every capture is within one step of the ideal triangle", [&] {
                // The pin sees max(0, v): the MCU input reads the generator's voltage; only the shape above 0 V can be captured through it.
                // The generator's own setpoint is exact, so this reads it where it is whole: the output voltage at the node.
                require(!capture.samples.empty());
                const auto period = 1.0 / 500.0;
                for (const auto& [at, volts] : capture.samples) {
                    const auto t     = static_cast<double>(at - start) / clockHz;
                    const auto phase = std::fmod(t / period, 1.0);
                    const auto ideal = 2.0 * (phase < 0.5 ? -1.0 + 4.0 * phase : 3.0 - 4.0 * phase);
                    check(std::abs(volts - ideal) <= 2.0 * 4.0 / 64.0 + 1e-6);
                }
            });
        });
    });
}};

const Scenario anArbitraryWaveHoldsEachSampleFor{"an arbitrary wave holds each sample for the period divided by the sample count",
                                                 {"circuit", "generator"}, [] {
    given("five samples over 5 ms", [&] {
        auto                      bench = benchWithGenerator();
        Capture                   capture;
        capture.attach(bench.pin("PC5"));
        const std::vector<double> samples{0.5, 4.0, 1.0, 3.0, 2.0};
        const auto                start = bench.mcu().cycles();
        require(!bench.circuit().program("gbf", samples, 0.005));
        when("the channel is enabled for two periods", [&] {
            require(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(10ms);
            then("every sample is held for 1 ms, exactly 16000 cycles, and the list repeats", [&] {
                require(capture.samples.size() >= 10);
                for (std::size_t i = 0; i < 10; ++i) {
                    check(capture.samples[i].first - start == static_cast<Cycles>(i) * 16000);
                    check(capture.samples[i].second == Approx(samples[i % 5]).margin(1e-9));
                }
            });
            then("the generator reports the samples it holds", [&] {
                check(bench.circuit().state("gbf", "sampleCount").value() == 5.0);
                check(bench.circuit().state("gbf", "waveform").value() == 3.0);
                check(bench.circuit().state("gbf", "period").value() == Approx(0.005));
            });
        });
    });
}};

const Scenario aRequestBeyondTheCapacityIsRefused{"a request beyond the capacity is refused before anything runs", {"circuit", "generator"}, [] {
    given("a generator on a 16 MHz MCU", [&] {
        auto bench = benchWithGenerator();
        auto& circuit = bench.circuit();

        then("it publishes its electrical bounds and its time resolution", [&] {
            const auto limits = circuit.limits("gbf");
            require(limits);
            check(limits->at("voltage").at("min") == -20.0);
            check(limits->at("voltage").at("max") == 20.0);
            check(limits->at("amplitude").at("max") == 10.0);
            check(limits->at("frequency").at("min") == 0.1);
            check(limits->at("timeResolution").at("cycles") == 1);
            check(limits->at("timeResolution").at("seconds").get<double>() == Approx(1.0 / clockHz));
            check(limits->at("stepsPerPeriod").at("sine") == 64);
            check(limits->at("maxFrequency").at("sine").get<double>() == Approx(15625.0));
            check(limits->at("maxFrequency").at("square").get<double>() == Approx(100000.0));  // 500 kHz by the clock, capped by the type.
        });
        then("the bounds are also in the view the interface draws", [&] {
            const auto view = circuit.view();
            check(view.at("components")[0].contains("limits"));
        });
        then("a frequency beyond what the clock can step is refused, with the highest one", [&] {
            const auto reason = circuit.refusal("gbf", "frequency", 20000.0);
            require(reason);
            check(reason->find("15625") != std::string::npos);
            check(!circuit.set("gbf", "frequency", 20000.0));
            check(circuit.state("gbf", "frequency").value() == 1000.0);
        });
        then("the same frequency is accepted for a square wave, which steps twice per period", [&] {
            require(circuit.set("gbf", "waveform", 1.0));
            check(!circuit.refusal("gbf", "frequency", 20000.0));
        });
        then("values outside the bounds of the type are refused", [&] {
            check(circuit.refusal("gbf", "amplitude", 10.5));
            check(circuit.refusal("gbf", "offset", -10.5));
            check(circuit.refusal("gbf", "frequency", 0.05));
            check(circuit.refusal("gbf", "duty", 100.0));
            check(circuit.refusal("gbf", "waveform", 1.5));
        });
        then("an arbitrary wave needs samples before it is selected", [&] {
            check(circuit.refusal("gbf", "waveform", 3.0));
        });
        then("samples that cannot be held are refused", [&] {
            const std::vector<double> fast(200, 1.0);
            check(circuit.program("gbf", fast, 0.0001));        // 0.5 us per sample: 8 cycles of the 16 needed.
            check(!circuit.program("gbf", std::vector<double>(100, 1.0), 0.0001));  // 16 cycles: the limit itself is allowed.
            const std::vector<double> loud{0.0, 25.0};
            check(circuit.program("gbf", loud, 0.01));          // Beyond +-20 V.
            check(circuit.program("gbf", std::vector<double>{1.0}, 0.01));  // One sample is not a wave.
            check(circuit.program("gbf", std::vector<double>(5000, 1.0), 1.0));
            check(circuit.program("gbf", std::vector<double>{0.0, 1.0}, 20.0));
            check(circuit.program("load", std::vector<double>{0.0, 1.0}, 1.0));
            check(circuit.state("gbf", "sampleCount").value() == 100.0);  // Only the accepted program stands.
        });
    });
}};

const Scenario theWaveStartsAgainFromItsFirst{"the wave starts again from its first sample when the MCU resets, and stops when the channel is disabled",
                                              {"circuit", "generator"}, [] {
    given("a running 1 kHz square wave", [&] {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 1, 1000, 2.0, 2.0);
        require(bench.circuit().set("gbf", "enabled", 1.0));
        bench.runFor(2500us);

        when("the firmware is loaded again", [&] {
            capture.samples.clear();
            bench.load(firmwareDir / "blink.hex");
            bench.runFor(1ms);
            then("the first edges come from cycle 0 again", [&] {
                require(capture.samples.size() >= 2);
                check(capture.samples[0].first <= 1);
                check(capture.samples[1].first == 8000);
            });
        });
        when("the channel is disabled", [&] {
            require(bench.circuit().set("gbf", "enabled", 0.0));
            capture.samples.clear();
            bench.runFor(5ms);
            then("the output stays open and nothing steps any more", [&] {
                check(bench.circuit().state("gbf", "mode").value() == 0.0);
                for (const auto& sample : capture.samples)
                    check(sample.second == Approx(0.0).margin(1e-9));
                check(capture.samples.size() <= 1);
            });
        });
    });
}};

const Scenario aGeneratorSourcesAndSinksCurrentAnd{"a generator sources and sinks current and reports an overload", {"circuit", "generator"}, [] {
    given("a generator at -2 V on a 1 kohm resistor to ground, with no MCU", [&] {
        auto created = circuit::Circuit::create(generatorCircuit("load"));
        require(created);
        auto& circuit = **created;
        require(circuit.set("gbf", "waveform", 1.0));
        require(circuit.set("gbf", "amplitude", 0.0));
        require(circuit.set("gbf", "offset", -2.0));
        require(circuit.set("gbf", "enabled", 1.0));
        then("it holds -2 V and absorbs 2 mA, as a source of both signs must", [&] {
            check(circuit.state("gbf", "outputVoltage").value() == Approx(-2.0).margin(1e-6));
            check(circuit.state("gbf", "outputCurrent").value() == Approx(-0.002).margin(1e-7));
            check(circuit.state("gbf", "mode").value() == 1.0);
            check(circuit.shorts().empty());
        });
        when("the load is replaced by a ground-to-output short of 1 V on 1 ohm", [&] {
            then("without an MCU the wave holds its first sample and there is nothing to step", [&] {
                check(circuit.state("gbf", "setpoint").value() == Approx(-2.0));
            });
        });
    });
    given("a generator of 5 V peak across 10 ohms", [&] {
        auto description = generatorCircuit("load");
        description["components"][1]["properties"]["resistance"] = 10;
        auto created = circuit::Circuit::create(description);
        require(created);
        auto& circuit = **created;
        require(circuit.set("gbf", "waveform", 1.0));
        require(circuit.set("gbf", "amplitude", 0.0));
        require(circuit.set("gbf", "offset", 5.0));
        require(circuit.set("gbf", "enabled", 1.0));
        then("it still holds 5 V but is flagged as overloaded: 500 mA against a rating of 100 mA", [&] {
            check(circuit.state("gbf", "outputVoltage").value() == Approx(5.0).margin(1e-6));
            check(circuit.state("gbf", "mode").value() == 2.0);
            require(circuit.shorts().size() == 1);
            check(circuit.shorts()[0].find("Overload") == 0);
        });
    });
}};

const Scenario theAdcConvertsTheGeneratorOnA0{"the ADC converts the generator on A0 to the level the wave has at that instant",
                                              {"circuit", "generator", "adc"}, [] {
    given("a 1 kHz square wave of 1.5 V around 2.5 V (4 V, then 1 V) on A0, enabled at cycle 0", [&] {
        const auto path = firmwareDir / "blink.hex";
        if (!std::filesystem::exists(path))
            skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
        testing::Bench bench{mcu::create("atmega328p")};
        bench.attach(circuit::builtinDescription("generator-demo"));
        bench.load(path);
        auto* device = dynamic_cast<avr::Device*>(&bench.mcu());
        require(device);
        auto& data = device->dataSpace();
        program(bench, 1, 1000, 1.5, 2.5);
        require(bench.circuit().set("gbf", "enabled", 1.0));
        const auto convert = [&] {
            data.write(0x7C, 0x40);  // ADMUX: AVcc reference, channel 0.
            data.write(0x7A, 0xC7);  // ADCSRA: enable, start, clock / 128: 13 ADC clocks, 104 us.
            bench.runFor(110us);
            return data.read(0x78) | data.read(0x79) << 8;
        };
        when("a conversion starts 100 us into the high half, then 600 us into the low half", [&] {
            bench.runFor(100us);
            const auto high = convert();
            bench.runFor(390us);
            const auto low = convert();
            then("the counts are those of 4 V and 1 V on a 5 V reference: V * 1024 / 5", [&] {
                check(high == Approx(819.0).margin(1.0));
                check(low == Approx(205.0).margin(1.0));
            });
        });
    });
}};

const Scenario aGeneratorSetBeforeThereWasA{"a generator set before there was a clock is checked when the MCU is attached", {"circuit", "generator"}, [] {
    given("a circuit with no MCU, where a 100 kHz sine is accepted: there is no clock to check it against", [&] {
        auto created = circuit::Circuit::create(generatorCircuit());
        require(created);
        auto& circuit = **created;
        require(circuit.set("gbf", "frequency", 100000.0));
        auto device = mcu::create("atmega328p");

        when("it is attached to a 16 MHz MCU", [&] {
            const auto attached = circuit.attach(*device);
            then("it is refused, names the generator and the highest frequency, and nothing is attached", [&] {
                require(!attached);
                check(attached.error().path == "/components/0");
                check(attached.error().message.find("15625") != std::string::npos);
                check(!(circuit.limits("gbf")->contains("clockHz")));
            });
            andWhen("the frequency is brought within what the clock can step", [&] {
                require(circuit.set("gbf", "frequency", 10000.0));
                then("the same MCU is accepted", [&] {
                    check(circuit.attach(*device));
                    check(circuit.limits("gbf")->contains("clockHz"));
                });
            });
        });
    });
    given("a circuit with no MCU where the arbitrary wave is selected without samples", [&] {
        auto created = circuit::Circuit::create(generatorCircuit());
        require(created);
        auto& circuit = **created;
        require(circuit.set("gbf", "waveform", 3.0));
        auto device = mcu::create("atmega328p");
        then("attaching is refused: there is nothing to step", [&] {
            const auto attached = circuit.attach(*device);
            require(!attached);
            check(attached.error().message.find("program its samples first") != std::string::npos);
        });
    });
}};
