// Bench scenarios (ADR 0010, #57, #58, #59): a versioned file checked before it runs, executed on the emulated
// ATmega328P with stimuli on the scheduled timeline and bounded waits, measured on the captured edges, and reported the
// same way every time it runs on the same inputs.

#include "../support/AvrAssembler.hpp"
#include "scenario/Sha256.hpp"
#include "scenario/VcdClock.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/scenario/Scenario.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>

using namespace siliconforge;
using Catch::Approx;
using Json = nlohmann::json;
using siliconforge::test::AvrAssembler;
using Outcome = scenario::StepRecord::Outcome;

namespace {
const std::filesystem::path sourceDir{SILICONFORGE_SOURCE_DIR};
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};
const auto                  scenarioDir = sourceDir / "scenarios";

Json example() {
    std::ifstream stream{scenarioDir / "pwm-setpoint.json"};
    return Json::parse(stream);
}

/// The committed example with the firmware the tests were built with (compiled from firmware/src when possible).
Json exampleWith(std::string_view firmware) {
    const auto path = firmwareDir / firmware;
    if (!std::filesystem::exists(path))
        SKIP(path.string() << " is missing: build the firmware (see firmware/README.md)");
    auto scenario        = example();
    scenario["firmware"] = path.generic_string();
    return scenario;
}

/// The committed example with other steps.
Json exampleWith(std::string_view firmware, std::string_view steps) {
    auto document     = exampleWith(firmware);
    document["steps"] = Json::parse(steps);
    return document;
}

/// Writes an assembled program as an Intel HEX file and returns its path.
std::filesystem::path hexFile(const AvrAssembler& program, std::string_view name) {
    const auto  bytes = program.image().segments.front().bytes;
    std::string text;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 16) {
        const auto count = std::min<std::size_t>(16, bytes.size() - offset);
        unsigned   sum   = static_cast<unsigned>(count + (offset >> 8) + (offset & 0xFF));
        text            += std::format(":{:02X}{:04X}00", count, offset);
        for (std::size_t i = 0; i < count; ++i) {
            text += std::format("{:02X}", bytes[offset + i]);
            sum  += bytes[offset + i];
        }
        text += std::format("{:02X}\n", (0x100 - (sum & 0xFF)) & 0xFF);
    }
    text           += ":00000001FF\n";
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream{path, std::ios::binary} << text;
    return path;
}

/// The example circuit with an assembled program and other steps.
Json programWith(const AvrAssembler& program, std::string_view name, std::string_view steps) {
    auto document        = example();
    document["firmware"] = hexFile(program, name).generic_string();
    document["initial"]  = Json::object();
    document["steps"]    = Json::parse(steps);
    return document;
}

std::unique_ptr<scenario::Scenario> prepared(const Json& document) {
    auto result = scenario::Scenario::prepare(document.dump(), scenarioDir, "test.json");
    if (!result)
        FAIL(scenario::toString(result.error().cause) << " " << result.error().file << result.error().path << ": " << result.error().message);
    return std::move(*result);
}

scenario::Report runOf(const Json& document) {
    auto report = prepared(document)->run();
    UNSCOPED_INFO(scenario::toJson(report).dump(2));
    return report;
}

scenario::Problem problemOf(const Json& document) {
    auto result = scenario::Scenario::prepare(document.dump(), scenarioDir, "test.json");
    if (result)
        FAIL("The scenario was accepted: " << document.dump());
    return result.error();
}

/// Checks the cause, the place and the message of the refusal of a scenario.
void expectProblem(const Json& document, scenario::Problem::Cause cause, std::string_view path, std::string_view message, std::string_view file = "") {
    const auto problem = problemOf(document);
    INFO(problem.file << problem.path << ": " << problem.message);
    CHECK(problem.cause == cause);
    CHECK(problem.path == path);
    CHECK(problem.file == file);
    CHECK(problem.message.contains(message));
}

/// A copy of the example circuit with one change, written to a temporary file.
std::filesystem::path circuitWith(std::string_view name, const std::function<void(Json&)>& change) {
    auto circuit = Json::parse(std::ifstream{sourceDir / "circuits" / "arduino-demo.json"});
    change(circuit);
    const auto file = std::filesystem::temp_directory_path() / name;
    std::ofstream{file} << circuit.dump();
    return file;
}

const scenario::Measurement& measurement(const scenario::Report& report, std::string_view id) {
    const auto found = std::ranges::find(report.measurements, id, &scenario::Measurement::id);
    INFO("Measurement " << id);
    REQUIRE(found != report.measurements.end());
    return *found;
}
}  // namespace

// Inputs and reports -------------------------------------------------------------------------------------------------

SCENARIO("SHA-256 identifies the inputs of a run", "[scenario]") {
    const auto digest = [](std::string_view text) {
        return scenario::detail::sha256({reinterpret_cast<const unsigned char*>(text.data()), text.size()});
    };
    GIVEN("the test vectors of FIPS 180-2") {
        THEN("the digests are the published ones, across one and two padding blocks") {
            CHECK(digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
            CHECK(digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
            CHECK(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
            CHECK(digest(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        }
    }
}

SCENARIO("The committed scenario example runs on the committed firmware", "[scenario]") {
    GIVEN("scenarios/pwm-setpoint.json") {
        WHEN("it runs from its file") {
            const auto report = scenario::run(scenarioDir / "pwm-setpoint.json");
            INFO(scenario::toJson(report).dump(2));
            THEN("it passes on the ATmega328P of its circuit") {
                CHECK(report.verdict == scenario::Verdict::Pass);
                CHECK(report.mcu == "ATmega328P");
            }
            THEN("the report identifies the scenario, the circuit and the firmware by their SHA-256") {
                REQUIRE(report.inputs.size() == 3);
                CHECK(report.inputs[0].role == "scenario");
                CHECK(report.inputs[1].path == "../circuits/arduino-demo.json");
                CHECK(report.inputs[2].path == "../firmware/prebuilt/pwm_setpoint.hex");
                for (const auto& input : report.inputs)
                    CHECK(input.sha256.size() == 64);
            }
        }
    }
}

SCENARIO("A scenario switches an input on its cycle and measures the PWM on the captured edges", "[scenario]") {
    GIVEN("the example on the pwm_setpoint firmware") {
        auto scenario = prepared(exampleWith("pwm_setpoint.hex"));
        WHEN("it runs") {
            const auto report = scenario->run();
            INFO(scenario::toJson(report).dump(2));
            REQUIRE(report.verdict == scenario::Verdict::Pass);
            THEN("the button is pressed at 5 ms and the firmware acknowledges it well before the timeout") {
                const auto& press = report.steps.at(3);
                CHECK(press.kind == "set");
                CHECK(press.start == 80000);
                const auto& acknowledged = report.steps.at(4);
                CHECK(acknowledged.outcome == Outcome::Done);
                CHECK(acknowledged.end > press.start);
                CHECK(acknowledged.end - press.start < 16000);
            }
            THEN("the duty cycle follows the setpoint, at 62.5 kHz, with its unit") {
                CHECK(measurement(report, "duty-released").value == Approx(25.0));
                CHECK(measurement(report, "duty-released").unit == "%");
                CHECK(measurement(report, "frequency").value == Approx(62500.0));
                CHECK(measurement(report, "frequency").unit == "Hz");
                CHECK(measurement(report, "duty-held").value.value_or(0) == Approx(75.0).margin(0.1));
                CHECK(measurement(report, "duty-held").risingEdges >= 60);
            }
            THEN("the trace holds every pin from reset on a valid VCD timescale: 100 ps, 625 units per cycle") {
                CHECK(report.trace.starts_with("$comment SiliconForge bench trace"));
                CHECK(report.trace.contains("$timescale 100 ps $end"));
                CHECK(report.trace.contains(" D6_PD6 $end"));
                std::istringstream lines{report.trace};
                std::size_t        times = 0;
                for (std::string line; std::getline(lines, line);)
                    if (line.starts_with('#')) {
                        ++times;
                        CHECK(std::stoull(line.substr(1)) % 625 == 0);
                    }
                CHECK(times > 100);
            }
        }
    }
}

SCENARIO("Two runs on the same inputs give the same measurements, verdict and trace", "[scenario]") {
    GIVEN("a scenario that passes") {
        auto scenario = prepared(exampleWith("pwm_setpoint.hex"));
        WHEN("it runs twice") {
            THEN("both reports are identical") {
                CHECK(scenario::toJson(scenario->run()) == scenario::toJson(scenario->run()));
            }
        }
    }
    GIVEN("a firmware that never acknowledges: pwm.hex keeps 25 % and never lights D13") {
        auto scenario = prepared(exampleWith("pwm.hex"));
        WHEN("it runs twice") {
            const auto first  = scenario->run();
            const auto second = scenario->run();
            THEN("both reports and traces are identical") {
                CHECK(scenario::toJson(first) == scenario::toJson(second));
                CHECK(first.trace == second.trace);
            }
            THEN("the acknowledgement reaches its timeout and the later steps are skipped") {
                REQUIRE(first.verdict == scenario::Verdict::Fail);
                CHECK(first.steps.at(4).outcome == Outcome::Failed);
                CHECK(first.steps.at(4).detail.contains("D13 high not reached within 16000 cycles"));
                CHECK(first.steps.at(4).end - first.steps.at(4).start == 16000);
                CHECK(first.steps.at(5).outcome == Outcome::Skipped);
            }
            THEN("the measurements made before the timeout and the trace are kept") {
                CHECK(first.measurements.size() == 2);
                CHECK(first.duration > 0);
                CHECK_FALSE(first.trace.empty());
            }
        }
    }
}

SCENARIO("A missed expectation fails the run but the later steps still run and measure", "[scenario]") {
    GIVEN("the example expecting 60 % while the button is held") {
        auto document                                      = exampleWith("pwm_setpoint.hex");
        document["steps"][5]["measure"]["expect"]["value"] = 60;
        WHEN("it runs") {
            const auto report = runOf(document);
            THEN("the run fails on that measurement, which keeps its value and says why") {
                CHECK(report.verdict == scenario::Verdict::Fail);
                const auto& held = measurement(report, "duty-held");
                CHECK_FALSE(held.passed);
                CHECK(held.value.value_or(0) == Approx(75.0).margin(0.1));
                CHECK(held.detail.contains("is outside [59.5, 60.5] %"));
                CHECK(report.steps.at(5).outcome == Outcome::Failed);
            }
            THEN("the steps after it still run") {
                CHECK(report.steps.at(8).outcome == Outcome::Done);
                CHECK(measurement(report, "duty-released-again").passed);
            }
            THEN("the JSON report carries the verdict, the engine, the trace and the measurement") {
                const auto json = scenario::toJson(report, "trace.vcd");
                CHECK(json["verdict"] == "fail");
                CHECK(json["engine"]["version"] == std::string{scenario::engineVersion()});
                CHECK(json["trace"]["file"] == "trace.vcd");
                CHECK(json["trace"]["timescale"] == "100 ps");
                const auto& entry = json["measurements"][2];
                CHECK(entry["id"] == "duty-held");
                CHECK(entry["verdict"] == "fail");
                CHECK(entry["unit"] == "%");
                CHECK(entry["expected"]["min"] == 59.5);
            }
        }
    }
}

SCENARIO("Binary serial output is kept in the report, reversibly", "[scenario]") {
    GIVEN("a firmware transmitting 0xFF, 'o', 'k', then 0xC3 0xA9 (\"é\" in UTF-8) at 1 Mbaud") {
        AvrAssembler program;
        program.ldi(16, 0x08).sts(0xC1, 16);             // UCSR0B: TXEN0
        for (const unsigned byte : {0xFFu, unsigned{'o'}, unsigned{'k'}, 0xC3u, 0xA9u}) {
            program.lds(17, 0xC0).sbrs(17, 5).rjmp(-4);  // Wait for UDRE0
            program.ldi(18, byte).sts(0xC6, 18);         // UDR0
        }
        program.rjmp(-1);
        const auto document = programWith(program, "siliconforge-scenario-binary.hex", R"([
            {"waitFor": {"serial": "ok"}, "timeout": "1ms"},
            {"wait": "100us"}
        ])");
        WHEN("it runs") {
            const auto report = runOf(document);
            THEN("the bytes are kept as they are") {
                CHECK(report.verdict == scenario::Verdict::Pass);
                CHECK(report.serial.at(0) == std::string{"\xFFok\xC3\xA9"});
            }
            THEN("the JSON report holds them in base64 and as text, U+FFFD for what is not UTF-8") {
                const auto json = scenario::toJson(report, "trace.vcd");
                CHECK_NOTHROW(json.dump(2));  // Throws on invalid UTF-8
                const auto& port = json["serial"][0];
                CHECK(port["port"] == 0);
                CHECK(port["bytes"] == 5);
                CHECK(port["base64"] == "/29rw6k=");
                CHECK(port["text"] == "\xEF\xBF\xBDok\xC3\xA9");
            }
        }
    }
}

// Refusals before the run ----------------------------------------------------------------------------------------------

SCENARIO("A scenario that cannot run on the bench is refused before the run, with its cause", "[scenario]") {
    using enum scenario::Problem::Cause;
    GIVEN("the committed example") {
        auto document = example();
        WHEN("its format version is unknown") {
            document["formatVersion"] = 2;
            THEN("the cause is the version") {
                expectProblem(document, Version, "/formatVersion", "Unsupported scenario formatVersion 2");
            }
        }
        WHEN("its text is not JSON") {
            const auto problem = scenario::Scenario::prepare("{\"formatVersion\": 1,", scenarioDir, "broken.json");
            THEN("it is unreadable") {
                REQUIRE_FALSE(problem);
                CHECK(problem.error().cause == Unreadable);
            }
        }
        WHEN("a step has two actions") {
            document["steps"][0] = Json::parse(R"({"wait": "1ms", "set": {"probe": "A0", "volts": 1}})");
            THEN("it breaks the schema") {
                expectProblem(document, Schema, "/steps/0", "");
            }
        }
        WHEN("a duration has no unit") {
            document["steps"][0]["wait"] = "10";
            THEN("it breaks the schema") {
                expectProblem(document, Schema, "/steps/0", "");
            }
        }
        WHEN("its circuit has a wire that does not fit the board") {
            const auto file     = circuitWith("siliconforge-scenario-wiring.json", [](Json& circuit) {
                circuit["wires"][0]["to"] = "B+99";
            });
            document["circuit"] = file.generic_string();
            THEN("the wiring is at fault, in the circuit file") {
                expectProblem(document, Wiring, "/wires/0/to", "", file.generic_string());
            }
        }
        WHEN("its firmware is missing") {
            document["firmware"] = "missing.hex";
            THEN("it is unreadable") {
                expectProblem(document, Unreadable, "/firmware", "Cannot read the firmware missing.hex");
            }
        }
        WHEN("its firmware has a byte at 0x10000, beyond the 32 KiB of flash") {
            const auto file = std::filesystem::temp_directory_path() / "siliconforge-scenario-too-large.hex";
            std::ofstream{file, std::ios::binary} << ":020000040001F9\n:01000000AA55\n:00000001FF\n";
            document["firmware"] = file.generic_string();
            THEN("it is beyond the capacity of the MCU") {
                expectProblem(document, Capacity, "", "ATmega328P cannot load this firmware (it ends at 0x10001)", file.generic_string());
            }
        }
        WHEN("its circuit names an unknown microcontroller") {
            const auto file     = circuitWith("siliconforge-scenario-mcu.json", [](Json& circuit) {
                circuit["chip"]["mcu"] = "atmega9999";
            });
            document["circuit"] = file.generic_string();
            THEN("the resource is missing") {
                expectProblem(document, Resource, "/chip/mcu", "Unknown microcontroller atmega9999", file.generic_string());
            }
        }
        WHEN("a step names a component the circuit does not have") {
            document["steps"][3]["set"]["component"] = "button-d7";
            THEN("the resource is missing") {
                expectProblem(document, Resource, "/steps/3/set", "has no component button-d7");
            }
            THEN("the refusal keeps the identity of every input read before it") {
                const auto problem = problemOf(document);
                REQUIRE(problem.inputs.size() == 3);
                CHECK(problem.inputs[0].role == "scenario");
                CHECK(problem.inputs[1].role == "circuit");
                CHECK(problem.inputs[2].role == "firmware");
                CHECK(problem.inputs[2].sha256.size() == 64);
            }
        }
        WHEN("the initial state changes a state that is not interactive") {
            document["initial"]["components"]["led-d6"] = Json{
                {"brightness", 1}
            };
            THEN("the resource is missing") {
                expectProblem(document, Resource, "/initial/components/led-d6/brightness", "cannot be changed from outside");
            }
        }
        WHEN("a wait watches the brightness of an LED, a time average its present value does not show") {
            document["steps"][4] = Json::parse(R"({"waitFor": {"component": "led-d6", "state": "brightness", "above": 0.9}, "timeout": "1ms"})");
            THEN("it is beyond capacity, and the refusal points to the duty cycle of the pin") {
                expectProblem(document, Capacity, "/steps/4/waitFor", "Measure the duty cycle of the pin that drives it");
            }
        }
        WHEN("a measurement names a pin the MCU does not have") {
            document["steps"][1]["measure"]["pin"] = "D42";
            THEN("the resource is missing") {
                expectProblem(document, Resource, "/steps/1/measure/pin", "ATmega328P has no pin D42");
            }
        }
        WHEN("a wait names a serial port the MCU does not have") {
            document["steps"][4] = Json::parse(R"({"waitFor": {"serial": "ready", "port": 1}, "timeout": "1ms"})");
            THEN("the resource is missing") {
                expectProblem(document, Resource, "/steps/4/waitFor/port", "has no serial port 1");
            }
        }
        WHEN("a stimulus gives a boolean state the value 2") {
            document["steps"][3]["set"]["value"] = 2;
            THEN("it is beyond capacity") {
                expectProblem(document, Capacity, "/steps/3/set", "is a boolean");
            }
        }
        WHEN("a probe applies 7.5 V to a 5 V circuit") {
            document["initial"]["probes"] = Json{
                {"A0", 7.5}
            };
            THEN("it is beyond capacity") {
                expectProblem(document, Capacity, "/initial/probes/A0", "A probe applies 0 to 5 V");
            }
        }
        WHEN("a duty cycle tolerance is finer than its window resolves: 0.1 % over 100 cycles") {
            document["steps"][1]["measure"]["window"]              = "100 cycles";
            document["steps"][1]["measure"]["expect"]["tolerance"] = 0.1;
            THEN("it is beyond capacity") {
                expectProblem(document, Capacity, "/steps/1/measure/expect", "resolved to 1 %");
            }
        }
        WHEN("a frequency window is shorter than the shortest expected period: 255 cycles for 62 600 Hz at most") {
            document["steps"][2]["measure"]["window"] = "255 cycles";
            THEN("it is beyond capacity") {
                expectProblem(document, Capacity, "/steps/2/measure/expect", "is shorter than one period of a 62600 Hz signal");
            }
        }
        WHEN("the expected frequency is above half the clock") {
            document["steps"][2]["measure"]["expect"] = Json{
                {    "value", 10e6},
                {"tolerance",    1}
            };
            THEN("it is beyond capacity") {
                expectProblem(document, Capacity, "/steps/2/measure/expect", "cannot toggle above 8000000 Hz");
            }
        }
        WHEN("a duration has an unknown unit") {
            document["steps"][0]["wait"] = "2h";
            THEN("it breaks the schema") {
                expectProblem(document, Schema, "/steps/0", "");
            }
        }
        WHEN("a duration exceeds one hour") {
            document["steps"][0]["wait"] = "3601s";
            THEN("it is beyond the bench limit") {
                expectProblem(document, Capacity, "/steps/0/wait", "beyond the bench limit");
            }
        }
        WHEN("a duration does not fit a number: 400 digits") {
            for (const auto& [unit, name] : {
                     std::pair{      "s", "seconds"},
                     std::pair{" cycles",  "cycles"}
            }) {
                document["steps"][0]["wait"] = std::string(400, '9') + unit;
                THEN(std::string{"it is beyond the bench limit, in "} + name) {
                    expectProblem(document, Capacity, "/steps/0/wait", "beyond the bench limit");
                }
            }
        }
        WHEN("a step is scheduled at 2 ms after steps that last at least 3 ms") {
            document["steps"][3]["at"] = "2ms";
            THEN("the schedule cannot be kept") {
                expectProblem(document, Schedule, "/steps/3/at", "the steps before it last at least 48000 cycles");
            }
        }
        WHEN("two measurements share an id") {
            document["steps"][2]["measure"]["id"] = "duty-released";
            THEN("it breaks the schema") {
                expectProblem(document, Schema, "/steps/2/measure/id", "already defined");
            }
        }
    }
}

SCENARIO("An incompatible scenario file gives a report that names the problem", "[scenario]") {
    GIVEN("a scenario file that does not exist") {
        WHEN("it runs") {
            const auto report = scenario::run(scenarioDir / "missing.json");
            THEN("the verdict is incompatible, exit code 2, with an unreadable cause") {
                CHECK(report.verdict == scenario::Verdict::Incompatible);
                CHECK(scenario::exitCode(report.verdict) == 2);
                REQUIRE(report.problem);
                CHECK(report.problem->cause == scenario::Problem::Cause::Unreadable);
            }
            THEN("the JSON report names the problem and has no step") {
                const auto json = scenario::toJson(report);
                CHECK(json["verdict"] == "incompatible");
                CHECK(json["problem"]["cause"] == "unreadable");
                CHECK_FALSE(json.contains("steps"));
            }
        }
    }
}

// The timeline -----------------------------------------------------------------------------------------------------------

SCENARIO("A stimulus scheduled inside an instruction takes effect at its end, without delaying the timeline", "[scenario]") {
    GIVEN("a button press scheduled on each of twelve consecutive cycles, then a 1000-cycle measurement") {
        WHEN("each scenario runs") {
            THEN("the press starts on its cycle, takes effect within one instruction, and the next steps keep their cycles") {
                bool overshot = false;
                for (Cycles target = 16000; target < 16012; ++target) {
                    auto document     = exampleWith("pwm_setpoint.hex");
                    document["steps"] = Json::array({
                        Json{{"wait", std::format("{} cycles", target)}},
                        Json{{"at", std::format("{} cycles", target)}, {"set", {{"component", "button-d2"}, {"state", "pressed"}, {"value", true}}}},
                        Json{{"measure", {{"id", "window"}, {"pin", "D6"}, {"quantity", "frequency"}, {"window", "1000 cycles"}}}},
                        Json{{"at", std::format("{} cycles", target + 1000)}, {"wait", "0 cycles"}},
                    });
                    const auto report = prepared(document)->run();
                    INFO("target " << target << "\n" << scenario::toJson(report).dump(2));
                    REQUIRE(report.verdict == scenario::Verdict::Pass);
                    const auto& set = report.steps.at(1);
                    CHECK(set.start == target);
                    REQUIRE(set.applied);
                    CHECK(*set.applied >= target);
                    CHECK(*set.applied - target < 5);
                    overshot           = overshot || *set.applied != target;
                    const auto& window = measurement(report, "window");
                    CHECK(window.from == target);
                    CHECK(window.to == target + 1000);
                    CHECK(report.steps.at(3).start == target + 1000);
                }
                CHECK(overshot);  // At least one target fell inside an instruction
            }
        }
    }
}

SCENARIO("A bounded wait sees every change, at its cycle", "[scenario]") {
    GIVEN("a firmware raising D13 once, for the two cycles of a CBI") {
        AvrAssembler program;
        program.sbi(0x04, 5);  // DDRB: D13 output
        for (int i = 0; i < 100; ++i)
            program.nop();
        program.sbi(0x05, 5).cbi(0x05, 5).rjmp(-1);
        const auto document = programWith(program, "siliconforge-scenario-pulse.hex", R"([
            {"waitFor": {"pin": "D13", "state": "high"}, "timeout": "1000 cycles"},
            {"waitFor": {"pin": "D13", "state": "low"}, "timeout": "1000 cycles"},
            {"measure": {"id": "after", "pin": "D13", "quantity": "dutyCycle", "window": "100 cycles"}}
        ])");
        WHEN("the scenario waits for D13 high, then low") {
            const auto report = runOf(document);
            THEN("both waits are met at the exact cycles of the two edges") {
                REQUIRE(report.verdict == scenario::Verdict::Pass);
                const auto& high = report.steps.at(0);
                const auto& low  = report.steps.at(1);
                CHECK(high.end >= 100);
                CHECK(high.end < 110);
                CHECK(low.end - high.end == 2);
                CHECK(measurement(report, "after").value == 0.0);
            }
        }
    }
    GIVEN("the uart_echo firmware, which prints \"ready\" once") {
        const auto document = exampleWith("uart_echo.hex", R"([
            {"waitFor": {"serial": "ready"}, "timeout": "100ms"},
            {"waitFor": {"serial": "ready"}, "timeout": "10ms"}
        ])");
        WHEN("the scenario waits for \"ready\" twice") {
            const auto report = runOf(document);
            THEN("only the text emitted after a step begins counts: the second wait times out") {
                CHECK(report.steps.at(0).outcome == Outcome::Done);
                CHECK(report.steps.at(1).outcome == Outcome::Failed);
                CHECK(report.steps.at(1).detail.contains("Serial text \"ready\" not reached"));
            }
        }
    }
    GIVEN("a 1-cycle wait, which the core overshoots, then a 1-cycle wait for the released button") {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"wait": "1 cycles"},
            {"waitFor": {"component": "button-d2", "state": "pressed", "below": 0.5}, "timeout": "1 cycles"},
            {"set": {"component": "button-d2", "state": "pressed", "value": true}},
            {"waitFor": {"component": "button-d2", "state": "pressed", "above": 0.5}, "timeout": "10 cycles"}
        ])");
        WHEN("it runs") {
            const auto report = runOf(document);
            THEN("a condition met since reset is met at the start of its step") {
                REQUIRE(report.verdict == scenario::Verdict::Pass);
                CHECK(report.steps.at(1).start == 1);
                CHECK(report.steps.at(1).end == 1);
            }
            THEN("a state set by a stimulus counts from the boundary where the stimulus took effect") {
                CHECK(report.steps.at(3).end == report.steps.at(2).applied.value_or(0));
            }
        }
    }
}

// Measurements -----------------------------------------------------------------------------------------------------------

SCENARIO("A measurement reports a constant level and explains what it cannot measure", "[scenario]") {
    GIVEN("D13 low, then A1 held high by a probe, then released to float") {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"wait": "1ms"},
            {"measure": {"id": "led", "pin": "D13", "quantity": "dutyCycle", "window": "1ms"}},
            {"measure": {"id": "led-frequency", "pin": "D13", "quantity": "frequency", "window": "1ms"}},
            {"set": {"probe": "A1", "volts": 5}},
            {"measure": {"id": "probed", "pin": "A1", "quantity": "dutyCycle", "window": "10us"}},
            {"set": {"probe": "A1", "volts": null}},
            {"measure": {"id": "floating", "pin": "A1", "quantity": "dutyCycle", "window": "10us", "expect": {"min": 0, "max": 100}}}
        ])");
        WHEN("it runs") {
            const auto report = runOf(document);
            THEN("a constant level has a duty cycle of 0 % or 100 %") {
                CHECK(measurement(report, "led").value == 0.0);
                CHECK(measurement(report, "probed").value == 100.0);
            }
            THEN("a frequency without two rising edges has no value, and without expectation does not fail") {
                CHECK_FALSE(measurement(report, "led-frequency").value);
                CHECK(measurement(report, "led-frequency").passed);
                CHECK(measurement(report, "led-frequency").detail == "0 rising edges in the window: at least two are needed");
            }
            THEN("a floating pin cannot be measured, and its expectation fails") {
                CHECK_FALSE(measurement(report, "floating").passed);
                CHECK(measurement(report, "floating").detail == "The pin floats during the whole window");
                CHECK(report.verdict == scenario::Verdict::Fail);
            }
        }
    }
}

SCENARIO("A pin that floats during part of the window cannot be measured", "[scenario]") {
    GIVEN("D12, not wired, switched between output high and floating input, through its pull-up each way: never low") {
        AvrAssembler program;
        program.sbi(0x05, 4);  // PORTB4: pull-up, then high once an output
        program.sbi(0x04, 4);  // DDRB4: output high
        for (int i = 0; i < 20; ++i)
            program.nop();
        program.cbi(0x04, 4);  // Input with its pull-up: still high
        program.cbi(0x05, 4);  // No pull-up: floating
        for (int i = 0; i < 20; ++i)
            program.nop();
        program.rjmp(-45);  // Back to the first SBI
        const auto document = programWith(program, "siliconforge-scenario-floating.hex", R"([
            {"wait": "10us"},
            {"measure": {"id": "duty", "pin": "D12", "quantity": "dutyCycle", "window": "50us", "expect": {"min": 0, "max": 100}}},
            {"measure": {"id": "frequency", "pin": "D12", "quantity": "frequency", "window": "50us"}}
        ])");
        WHEN("its duty cycle and frequency are measured") {
            const auto report = runOf(document);
            THEN("neither has a value: from floating to high is not a rising edge") {
                for (const auto* id : {"duty", "frequency"}) {
                    const auto& entry = measurement(report, id);
                    CHECK_FALSE(entry.value);
                    CHECK(entry.risingEdges == 0);
                    CHECK(entry.detail == "The pin floats during part of the window: its level is not defined there");
                }
            }
            THEN("the expected duty cycle fails the run") {
                CHECK_FALSE(measurement(report, "duty").passed);
                CHECK(report.verdict == scenario::Verdict::Fail);
            }
        }
    }
}

SCENARIO("A frequency window of one period or more is accepted: the measurement tells whether it caught two edges", "[scenario]") {
    GIVEN("two 300-cycle windows on the 256-cycle period of D6, expecting 62 500 ± 500 Hz") {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"wait": "16112 cycles"},
            {"measure": {"id": "two-edges", "pin": "D6", "quantity": "frequency", "window": "300 cycles", "expect": {"value": 62500, "tolerance": 500}}},
            {"wait": "100 cycles"},
            {"measure": {"id": "one-edge", "pin": "D6", "quantity": "frequency", "window": "300 cycles", "expect": {"value": 62500, "tolerance": 500}}}
        ])");
        WHEN("it runs") {
            const auto report = runOf(document);
            THEN("the window whose phase holds two rising edges measures the frequency") {
                const auto& caught = measurement(report, "two-edges");
                CHECK(caught.risingEdges == 2);
                CHECK(caught.passed);
                CHECK(caught.value.value_or(0) == Approx(62500).margin(500));
            }
            THEN("the window that holds one says so, and fails") {
                const auto& missed = measurement(report, "one-edge");
                CHECK(missed.risingEdges == 1);
                CHECK_FALSE(missed.value);
                CHECK_FALSE(missed.passed);
                CHECK(missed.detail == "1 rising edge in the window: at least two are needed");
            }
        }
    }
}

SCENARIO("The edges at both ends of a measurement window belong to it", "[scenario]") {
    GIVEN("D6 rising at 15 896 and 16 152 cycles, and a closed window of exactly one period from the first edge") {
        for (const auto* quantity : {"frequency", "dutyCycle"}) {
            const bool frequency = std::string_view{quantity} == "frequency";
            auto       document  = exampleWith("pwm_setpoint.hex", R"([{"wait": "15896 cycles"}])");
            const auto expect    = frequency ? Json{{"value", 62500}, {"tolerance", 100}} : Json{{"value", 25}, {"tolerance", 0.5}};
            document["steps"].push_back(Json{
                {"measure", {{"id", "window"}, {"pin", "D6"}, {"quantity", quantity}, {"window", "256 cycles"}, {"expect", expect}}}
            });
            WHEN(std::string{"its "} + quantity + " is measured") {
                const auto report = runOf(document);
                THEN("both edges count and the expectation is met") {
                    CHECK(measurement(report, "window").risingEdges == 2);
                    CHECK(report.verdict == scenario::Verdict::Pass);
                }
            }
        }
    }
}

SCENARIO("Rising edges that span no time cannot be measured", "[scenario]") {
    GIVEN("three probe stimuli at cycle 0 giving A0 two rising edges on the same cycle") {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"set": {"probe": "A0", "volts": 5}},
            {"set": {"probe": "A0", "volts": 0}},
            {"set": {"probe": "A0", "volts": 5}},
            {"measure": {"id": "informative", "pin": "A0", "quantity": "frequency", "window": "100 cycles"}},
            {"measure": {"id": "expected", "pin": "A0", "quantity": "period", "window": "100 cycles", "expect": {"min": 0, "max": 1}}}
        ])");
        WHEN("their frequency is measured") {
            const auto report = runOf(document);
            const auto json   = scenario::toJson(report);
            THEN("there is no value, and the report says why, in its detail and its JSON") {
                const auto& informative = measurement(report, "informative");
                REQUIRE(informative.risingEdges >= 2);
                CHECK_FALSE(informative.value);
                CHECK(informative.detail.contains("rising edges in the window all fall on cycle"));
                CHECK(json["measurements"][0]["detail"] == informative.detail);
                CHECK_FALSE(report.steps.at(3).detail.contains("inf Hz"));
            }
            THEN("a measurement with an expectation fails") {
                CHECK_FALSE(measurement(report, "expected").passed);
            }
        }
    }
}

// The trace ----------------------------------------------------------------------------------------------------------------

SCENARIO("A VCD timescale is 1, 10 or 100 of a unit, and the times count in it", "[scenario]") {
    using scenario::detail::VcdClock;
    const auto check = [](u64 clockHz, std::string_view timescale, Cycles cycles, u64 time) {
        const VcdClock clock{clockHz};
        INFO(clockHz << " Hz");
        CHECK(clock.timescale() == timescale);
        CHECK(clock.time(cycles) == time);
    };
    GIVEN("a clock whose period is a whole number of femtoseconds") {
        THEN("the timescale is the largest valid one that divides the period, and cycles convert exactly") {
            check(16'000'000, "100 ps", 1, 625);
            check(16'000'000, "100 ps", 16'000'000, 10'000'000'000);
            check(1'000'000, "1 us", 7, 7);
            check(20'000'000, "10 ns", 3, 15);
            check(8'000'000, "1 ns", 2, 250);
            check(100'000'000, "10 ns", 1, 1);
            check(1, "1 s", 3, 3);
            check(32'768, "1 fs", 1, 30'517'578'125);
        }
    }
    GIVEN("a 3 MHz clock: 333 333 333.3 fs per cycle") {
        THEN("the timescale is 1 fs, times are rounded, and every whole second is exact") {
            check(3'000'000, "1 fs", 1, 333'333'333);
            check(3'000'000, "1 fs", 3'000'000 * Cycles{3600}, 3'600'000'000'000'000'000);
            CHECK(VcdClock{3'000'000}.time(2) == 666'666'667);
            CHECK_FALSE(VcdClock{3'000'000}.exact());
        }
    }
}

SCENARIO("The steps of a run end where the MCU stopped", "[scenario]") {
    GIVEN("a firmware whose first instruction is BREAK, and a 1 ms wait followed by a measurement") {
        AvrAssembler program;
        program.raw(0x9598).rjmp(-1);  // BREAK
        const auto document = programWith(program, "siliconforge-scenario-break.hex", R"([
            {"wait": "1ms"},
            {"measure": {"id": "led", "pin": "D13", "quantity": "dutyCycle", "window": "1ms"}}
        ])");
        WHEN("it runs") {
            const auto report = runOf(document);
            THEN("the wait fails where the MCU stopped, not at its planned end") {
                REQUIRE(report.verdict == scenario::Verdict::Fail);
                CHECK(report.duration < 16000);
                const auto& wait = report.steps.at(0);
                CHECK(wait.outcome == Outcome::Failed);
                CHECK(wait.detail.contains("BREAK instruction"));
                CHECK(wait.start == 0);
                CHECK(wait.end == report.duration);
            }
            THEN("the skipped steps start and end there too") {
                const auto& skipped = report.steps.at(1);
                CHECK(skipped.outcome == Outcome::Skipped);
                CHECK(skipped.start == report.duration);
                CHECK(skipped.end == report.duration);
                CHECK(report.measurements.empty());
            }
        }
    }
}
