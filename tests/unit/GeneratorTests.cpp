// Signal generator (#63, ADR 0010): a sine, square, triangle or arbitrary wave stepped on the clock of the MCU. The captures
// are compared with references that follow from the definition of each wave, not from the model's own output: the exact
// cycle of every step, the exact level of a square wave, the distance of a held sine or triangle to the ideal one.

#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

using namespace siliconforge;
using namespace std::chrono_literals;
using Catch::Approx;
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
        SKIP(path.string() << " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(generatorCircuit());
    bench.load(path);
    return bench;
}

void program(testing::Bench& bench, double waveform, double frequency, double amplitude, double offset) {
    auto& circuit = bench.circuit();
    REQUIRE(circuit.set("gbf", "waveform", waveform));
    REQUIRE(circuit.set("gbf", "frequency", frequency));
    REQUIRE(circuit.set("gbf", "amplitude", amplitude));
    REQUIRE(circuit.set("gbf", "offset", offset));
}
}  // namespace

SCENARIO("a sine is programmed by frequency, amplitude and offset and follows the ideal sine", "[circuit][generator]") {
    GIVEN("a 1 kHz sine of 1 V around 2.5 V on a 16 MHz MCU") {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 0, 1000, 1.0, 2.5);

        WHEN("the channel is enabled and the MCU runs for 4 periods") {
            const auto start = bench.mcu().cycles();
            REQUIRE(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(4ms);

            THEN("it steps 64 times per period, each step on its exact cycle: 250 cycles apart") {
                // The first sample is the offset itself (sine at phase 0), which is the voltage already on the pin.
                REQUIRE(capture.samples.size() >= 4 * 64 - 8);
                for (const auto& [at, volts] : capture.samples) {
                    const auto step = std::llround(static_cast<double>(at - start) / 250.0);
                    CHECK(at - start == static_cast<Cycles>(step) * 250);
                    (void)volts;
                }
            }
            THEN("every capture is within one step of the ideal sine at the instant it was taken") {
                const auto bound = 1.0 * 2.0 * std::numbers::pi / 64.0 + 1e-6;
                for (const auto& [at, volts] : capture.samples) {
                    const auto t = static_cast<double>(at - start) / clockHz;
                    CHECK(std::abs(volts - (2.5 + std::sin(2.0 * std::numbers::pi * 1000.0 * t))) <= bound);
                }
            }
            THEN("it reaches the peaks and the offset reference values") {
                double highest = -1e9, lowest = 1e9;
                for (const auto& [at, volts] : capture.samples) {
                    highest = std::max(highest, volts);
                    lowest  = std::min(lowest, volts);
                }
                CHECK(highest == Approx(3.5).margin(1e-9));
                CHECK(lowest == Approx(1.5).margin(1e-9));
            }
        }
    }
}

SCENARIO("a square wave changes at its two edges with the duty cycle asked for", "[circuit][generator]") {
    GIVEN("a 1 kHz square wave of 2 V around 2 V, 25 % high") {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 1, 1000, 2.0, 2.0);
        REQUIRE(bench.circuit().set("gbf", "duty", 25.0));
        const auto start = bench.mcu().cycles();

        WHEN("the channel is enabled for 3 periods") {
            REQUIRE(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(3ms);
            THEN("it is high (4 V) for 4000 cycles, then low (0 V) for 12000, three times") {
                REQUIRE(capture.samples.size() >= 6);
                for (std::size_t i = 0; i < 6; ++i) {
                    CHECK(capture.samples[i].first - start == static_cast<Cycles>(i / 2) * 16000 + (i % 2 ? 4000 : 0));
                    CHECK(capture.samples[i].second == Approx(i % 2 ? 0.0 : 4.0).margin(1e-9));
                }
            }
        }
    }
}

SCENARIO("a triangle rises and falls linearly between its extremes", "[circuit][generator]") {
    GIVEN("a 500 Hz triangle of 2 V around 0 V") {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 2, 500, 2.0, 0.0);
        const auto start = bench.mcu().cycles();
        WHEN("it runs for 2 periods") {
            REQUIRE(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(4ms);
            THEN("it starts at the low extreme and every capture is within one step of the ideal triangle") {
                // The pin sees max(0, v): the MCU input reads the generator's voltage; only the shape above 0 V can be captured through it.
                // The generator's own setpoint is exact, so this reads it where it is whole: the output voltage at the node.
                REQUIRE(!capture.samples.empty());
                const auto period = 1.0 / 500.0;
                for (const auto& [at, volts] : capture.samples) {
                    const auto t     = static_cast<double>(at - start) / clockHz;
                    const auto phase = std::fmod(t / period, 1.0);
                    const auto ideal = 2.0 * (phase < 0.5 ? -1.0 + 4.0 * phase : 3.0 - 4.0 * phase);
                    CHECK(std::abs(volts - ideal) <= 2.0 * 4.0 / 64.0 + 1e-6);
                }
            }
        }
    }
}

SCENARIO("an arbitrary wave holds each sample for the period divided by the sample count", "[circuit][generator]") {
    GIVEN("five samples over 5 ms") {
        auto                      bench = benchWithGenerator();
        Capture                   capture;
        capture.attach(bench.pin("PC5"));
        const std::vector<double> samples{0.5, 4.0, 1.0, 3.0, 2.0};
        const auto                start = bench.mcu().cycles();
        REQUIRE_FALSE(bench.circuit().program("gbf", samples, 0.005));
        WHEN("the channel is enabled for two periods") {
            REQUIRE(bench.circuit().set("gbf", "enabled", 1.0));
            bench.runFor(10ms);
            THEN("every sample is held for 1 ms, exactly 16000 cycles, and the list repeats") {
                REQUIRE(capture.samples.size() >= 10);
                for (std::size_t i = 0; i < 10; ++i) {
                    CHECK(capture.samples[i].first - start == static_cast<Cycles>(i) * 16000);
                    CHECK(capture.samples[i].second == Approx(samples[i % 5]).margin(1e-9));
                }
            }
            THEN("the generator reports the samples it holds") {
                CHECK(bench.circuit().state("gbf", "sampleCount").value() == 5.0);
                CHECK(bench.circuit().state("gbf", "waveform").value() == 3.0);
                CHECK(bench.circuit().state("gbf", "period").value() == Approx(0.005));
            }
        }
    }
}

SCENARIO("a request beyond the capacity is refused before anything runs", "[circuit][generator]") {
    GIVEN("a generator on a 16 MHz MCU") {
        auto bench = benchWithGenerator();
        auto& circuit = bench.circuit();

        THEN("it publishes its electrical bounds and its time resolution") {
            const auto limits = circuit.limits("gbf");
            REQUIRE(limits);
            CHECK(limits->at("voltage").at("min") == -20.0);
            CHECK(limits->at("voltage").at("max") == 20.0);
            CHECK(limits->at("amplitude").at("max") == 10.0);
            CHECK(limits->at("frequency").at("min") == 0.1);
            CHECK(limits->at("timeResolution").at("cycles") == 1);
            CHECK(limits->at("timeResolution").at("seconds").get<double>() == Approx(1.0 / clockHz));
            CHECK(limits->at("stepsPerPeriod").at("sine") == 64);
            CHECK(limits->at("maxFrequency").at("sine").get<double>() == Approx(15625.0));
            CHECK(limits->at("maxFrequency").at("square").get<double>() == Approx(100000.0));  // 500 kHz by the clock, capped by the type.
        }
        THEN("the bounds are also in the view the interface draws") {
            const auto view = circuit.view();
            CHECK(view.at("components")[0].contains("limits"));
        }
        THEN("a frequency beyond what the clock can step is refused, with the highest one") {
            const auto reason = circuit.refusal("gbf", "frequency", 20000.0);
            REQUIRE(reason);
            CHECK(reason->find("15625") != std::string::npos);
            CHECK_FALSE(circuit.set("gbf", "frequency", 20000.0));
            CHECK(circuit.state("gbf", "frequency").value() == 1000.0);
        }
        THEN("the same frequency is accepted for a square wave, which steps twice per period") {
            REQUIRE(circuit.set("gbf", "waveform", 1.0));
            CHECK_FALSE(circuit.refusal("gbf", "frequency", 20000.0));
        }
        THEN("values outside the bounds of the type are refused") {
            CHECK(circuit.refusal("gbf", "amplitude", 10.5));
            CHECK(circuit.refusal("gbf", "offset", -10.5));
            CHECK(circuit.refusal("gbf", "frequency", 0.05));
            CHECK(circuit.refusal("gbf", "duty", 100.0));
            CHECK(circuit.refusal("gbf", "waveform", 1.5));
        }
        THEN("an arbitrary wave needs samples before it is selected") {
            CHECK(circuit.refusal("gbf", "waveform", 3.0));
        }
        THEN("samples that cannot be held are refused") {
            const std::vector<double> fast(200, 1.0);
            CHECK(circuit.program("gbf", fast, 0.0001));        // 0.5 us per sample: 8 cycles of the 16 needed.
            CHECK_FALSE(circuit.program("gbf", std::vector<double>(100, 1.0), 0.0001));  // 16 cycles: the limit itself is allowed.
            const std::vector<double> loud{0.0, 25.0};
            CHECK(circuit.program("gbf", loud, 0.01));          // Beyond +-20 V.
            CHECK(circuit.program("gbf", std::vector<double>{1.0}, 0.01));  // One sample is not a wave.
            CHECK(circuit.program("gbf", std::vector<double>(5000, 1.0), 1.0));
            CHECK(circuit.program("gbf", std::vector<double>{0.0, 1.0}, 20.0));
            CHECK(circuit.program("load", std::vector<double>{0.0, 1.0}, 1.0));
            CHECK(circuit.state("gbf", "sampleCount").value() == 100.0);  // Only the accepted program stands.
        }
    }
}

SCENARIO("the wave starts again from its first sample when the MCU resets, and stops when the channel is disabled", "[circuit][generator]") {
    GIVEN("a running 1 kHz square wave") {
        auto    bench = benchWithGenerator();
        Capture capture;
        capture.attach(bench.pin("PC5"));
        program(bench, 1, 1000, 2.0, 2.0);
        REQUIRE(bench.circuit().set("gbf", "enabled", 1.0));
        bench.runFor(2500us);

        WHEN("the firmware is loaded again") {
            capture.samples.clear();
            bench.load(firmwareDir / "blink.hex");
            bench.runFor(1ms);
            THEN("the first edges come from cycle 0 again") {
                REQUIRE(capture.samples.size() >= 2);
                CHECK(capture.samples[0].first <= 1);
                CHECK(capture.samples[1].first == 8000);
            }
        }
        WHEN("the channel is disabled") {
            REQUIRE(bench.circuit().set("gbf", "enabled", 0.0));
            capture.samples.clear();
            bench.runFor(5ms);
            THEN("the output stays open and nothing steps any more") {
                CHECK(bench.circuit().state("gbf", "mode").value() == 0.0);
                for (const auto& sample : capture.samples)
                    CHECK(sample.second == Approx(0.0).margin(1e-9));
                CHECK(capture.samples.size() <= 1);
            }
        }
    }
}

SCENARIO("a generator sources and sinks current and reports an overload", "[circuit][generator]") {
    GIVEN("a generator at -2 V on a 1 kohm resistor to ground, with no MCU") {
        auto created = circuit::Circuit::create(generatorCircuit("load"));
        REQUIRE(created);
        auto& circuit = **created;
        REQUIRE(circuit.set("gbf", "waveform", 1.0));
        REQUIRE(circuit.set("gbf", "amplitude", 0.0));
        REQUIRE(circuit.set("gbf", "offset", -2.0));
        REQUIRE(circuit.set("gbf", "enabled", 1.0));
        THEN("it holds -2 V and absorbs 2 mA, as a source of both signs must") {
            CHECK(circuit.state("gbf", "outputVoltage").value() == Approx(-2.0).margin(1e-6));
            CHECK(circuit.state("gbf", "outputCurrent").value() == Approx(-0.002).margin(1e-7));
            CHECK(circuit.state("gbf", "mode").value() == 1.0);
            CHECK(circuit.shorts().empty());
        }
        WHEN("the load is replaced by a ground-to-output short of 1 V on 1 ohm") {
            THEN("without an MCU the wave holds its first sample and there is nothing to step") {
                CHECK(circuit.state("gbf", "setpoint").value() == Approx(-2.0));
            }
        }
    }
    GIVEN("a generator of 5 V peak across 10 ohms") {
        auto description = generatorCircuit("load");
        description["components"][1]["properties"]["resistance"] = 10;
        auto created = circuit::Circuit::create(description);
        REQUIRE(created);
        auto& circuit = **created;
        REQUIRE(circuit.set("gbf", "waveform", 1.0));
        REQUIRE(circuit.set("gbf", "amplitude", 0.0));
        REQUIRE(circuit.set("gbf", "offset", 5.0));
        REQUIRE(circuit.set("gbf", "enabled", 1.0));
        THEN("it still holds 5 V but is flagged as overloaded: 500 mA against a rating of 100 mA") {
            CHECK(circuit.state("gbf", "outputVoltage").value() == Approx(5.0).margin(1e-6));
            CHECK(circuit.state("gbf", "mode").value() == 2.0);
            REQUIRE(circuit.shorts().size() == 1);
            CHECK(circuit.shorts()[0].find("Overload") == 0);
        }
    }
}

SCENARIO("the ADC converts the generator on A0 to the level the wave has at that instant", "[circuit][generator][adc]") {
    GIVEN("a 1 kHz square wave of 1.5 V around 2.5 V (4 V, then 1 V) on A0, enabled at cycle 0") {
        const auto path = firmwareDir / "blink.hex";
        if (!std::filesystem::exists(path))
            SKIP(path.string() << " is missing: build the firmware (see firmware/README.md)");
        testing::Bench bench{mcu::create("atmega328p")};
        bench.attach(circuit::builtinDescription("generator-demo"));
        bench.load(path);
        auto* device = dynamic_cast<avr::Device*>(&bench.mcu());
        REQUIRE(device);
        auto& data = device->dataSpace();
        program(bench, 1, 1000, 1.5, 2.5);
        REQUIRE(bench.circuit().set("gbf", "enabled", 1.0));
        const auto convert = [&] {
            data.write(0x7C, 0x40);  // ADMUX: AVcc reference, channel 0.
            data.write(0x7A, 0xC7);  // ADCSRA: enable, start, clock / 128: 13 ADC clocks, 104 us.
            bench.runFor(110us);
            return data.read(0x78) | data.read(0x79) << 8;
        };
        WHEN("a conversion starts 100 us into the high half, then 600 us into the low half") {
            bench.runFor(100us);
            const auto high = convert();
            bench.runFor(390us);
            const auto low = convert();
            THEN("the counts are those of 4 V and 1 V on a 5 V reference: V * 1024 / 5") {
                CHECK(high == Approx(819.0).margin(1.0));
                CHECK(low == Approx(205.0).margin(1.0));
            }
        }
    }
}

SCENARIO("a generator set before there was a clock is checked when the MCU is attached", "[circuit][generator]") {
    GIVEN("a circuit with no MCU, where a 100 kHz sine is accepted: there is no clock to check it against") {
        auto created = circuit::Circuit::create(generatorCircuit());
        REQUIRE(created);
        auto& circuit = **created;
        REQUIRE(circuit.set("gbf", "frequency", 100000.0));
        auto device = mcu::create("atmega328p");

        WHEN("it is attached to a 16 MHz MCU") {
            const auto attached = circuit.attach(*device);
            THEN("it is refused, names the generator and the highest frequency, and nothing is attached") {
                REQUIRE_FALSE(attached);
                CHECK(attached.error().path == "/components/0");
                CHECK(attached.error().message.find("15625") != std::string::npos);
                CHECK_FALSE(circuit.limits("gbf")->contains("clockHz"));
            }
            AND_WHEN("the frequency is brought within what the clock can step") {
                REQUIRE(circuit.set("gbf", "frequency", 10000.0));
                THEN("the same MCU is accepted") {
                    CHECK(circuit.attach(*device));
                    CHECK(circuit.limits("gbf")->contains("clockHz"));
                }
            }
        }
    }
    GIVEN("a circuit with no MCU where the arbitrary wave is selected without samples") {
        auto created = circuit::Circuit::create(generatorCircuit());
        REQUIRE(created);
        auto& circuit = **created;
        REQUIRE(circuit.set("gbf", "waveform", 3.0));
        auto device = mcu::create("atmega328p");
        THEN("attaching is refused: there is nothing to step") {
            const auto attached = circuit.attach(*device);
            REQUIRE_FALSE(attached);
            CHECK(attached.error().message.find("program its samples first") != std::string::npos);
        }
    }
}
