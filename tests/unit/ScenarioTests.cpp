// Bench scenarios (ADR 0010, #57, #58, #59): a versioned file checked before it runs, executed on the emulated
// ATmega328P with stimuli on the scheduled timeline and bounded waits, measured on the captured edges, and reported the
// same way every time it runs on the same inputs.

#include "../support/AvrAssembler.hpp"
#include "scenario/Sha256.hpp"
#include "scenario/VcdClock.hpp"
#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"
#include <siliconforge/scenario/Scenario.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>

using namespace webfront::bdd;

using namespace siliconforge;
using siliconforge::test::Approx;
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
        skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
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
        fail(scenario::toString(result.error().cause), " ", result.error().file, result.error().path, ": ", result.error().message);
    return std::move(*result);
}

scenario::Report runOf(const Json& document) {
    auto report = prepared(document)->run();
    const auto note1 = info(scenario::toJson(report).dump(2));
    return report;
}

scenario::Problem problemOf(const Json& document) {
    auto result = scenario::Scenario::prepare(document.dump(), scenarioDir, "test.json");
    if (result)
        fail("The scenario was accepted: ", document.dump());
    return result.error();
}

/// Checks the cause, the place and the message of the refusal of a scenario.
void expectProblem(const Json& document, scenario::Problem::Cause cause, std::string_view path, std::string_view message, std::string_view file = "") {
    const auto problem = problemOf(document);
    const auto note2 = info(problem.file, problem.path, ": ", problem.message);
    check(problem.cause == cause);
    check(problem.path == path);
    check(problem.file == file);
    check(problem.message.contains(message));
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
    const auto note3 = info("Measurement ", id);
    require(found != report.measurements.end());
    return *found;
}
}  // namespace

// Inputs and reports -------------------------------------------------------------------------------------------------

const Scenario sha256IdentifiesTheInputsOfA{"SHA-256 identifies the inputs of a run", {"scenario"}, [] {
    const auto digest = [](std::string_view text) {
        return scenario::detail::sha256({reinterpret_cast<const unsigned char*>(text.data()), text.size()});
    };
    given("the test vectors of FIPS 180-2", [&] {
        then("the digests are the published ones, across one and two padding blocks", [&] {
            check(digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
            check(digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
            check(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
            check(digest(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        });
    });
}};

const Scenario theCommittedScenarioExampleRunsOnThe{"The committed scenario example runs on the committed firmware", {"scenario"}, [] {
    given("scenarios/pwm-setpoint.json", [&] {
        when("it runs from its file", [&] {
            const auto report = scenario::run(scenarioDir / "pwm-setpoint.json");
            const auto note4 = info(scenario::toJson(report).dump(2));
            then("it passes on the ATmega328P of its circuit", [&] {
                check(report.verdict == scenario::Verdict::Pass);
                check(report.mcu == "ATmega328P");
            });
            then("the report identifies the scenario, the circuit and the firmware by their SHA-256", [&] {
                require(report.inputs.size() == 3);
                check(report.inputs[0].role == "scenario");
                check(report.inputs[1].path == "../circuits/arduino-demo.json");
                check(report.inputs[2].path == "../firmware/prebuilt/pwm_setpoint.hex");
                for (const auto& input : report.inputs)
                    check(input.sha256.size() == 64);
            });
        });
    });
}};

const Scenario aScenarioSwitchesAnInputOnIts{"A scenario switches an input on its cycle and measures the PWM on the captured edges", {"scenario"}, [] {
    given("the example on the pwm_setpoint firmware", [&] {
        auto scenario = prepared(exampleWith("pwm_setpoint.hex"));
        when("it runs", [&] {
            const auto report = scenario->run();
            const auto note5 = info(scenario::toJson(report).dump(2));
            require(report.verdict == scenario::Verdict::Pass);
            then("the button is pressed at 5 ms and the firmware acknowledges it well before the timeout", [&] {
                const auto& press = report.steps.at(3);
                check(press.kind == "set");
                check(press.start == 80000);
                const auto& acknowledged = report.steps.at(4);
                check(acknowledged.outcome == Outcome::Done);
                check(acknowledged.end > press.start);
                check(acknowledged.end - press.start < 16000);
            });
            then("the duty cycle follows the setpoint, at 62.5 kHz, with its unit", [&] {
                check(measurement(report, "duty-released").value == Approx(25.0));
                check(measurement(report, "duty-released").unit == "%");
                check(measurement(report, "frequency").value == Approx(62500.0));
                check(measurement(report, "frequency").unit == "Hz");
                check(measurement(report, "duty-held").value.value_or(0) == Approx(75.0).margin(0.1));
                check(measurement(report, "duty-held").risingEdges >= 60);
            });
            then("the trace holds every pin from reset on a valid VCD timescale: 100 ps, 625 units per cycle", [&] {
                check(report.trace.starts_with("$comment SiliconForge bench trace"));
                check(report.trace.contains("$timescale 100 ps $end"));
                check(report.trace.contains(" D6_PD6 $end"));
                std::istringstream lines{report.trace};
                std::size_t        times = 0;
                for (std::string line; std::getline(lines, line);)
                    if (line.starts_with('#')) {
                        ++times;
                        check(std::stoull(line.substr(1)) % 625 == 0);
                    }
                check(times > 100);
            });
        });
    });
}};

const Scenario twoRunsOnTheSameInputsGive{"Two runs on the same inputs give the same measurements, verdict and trace", {"scenario"}, [] {
    given("a scenario that passes", [&] {
        auto scenario = prepared(exampleWith("pwm_setpoint.hex"));
        when("it runs twice", [&] {
            then("both reports are identical", [&] {
                check(scenario::toJson(scenario->run()) == scenario::toJson(scenario->run()));
            });
        });
    });
    given("a firmware that never acknowledges: pwm.hex keeps 25 % and never lights D13", [&] {
        auto scenario = prepared(exampleWith("pwm.hex"));
        when("it runs twice", [&] {
            const auto first  = scenario->run();
            const auto second = scenario->run();
            then("both reports and traces are identical", [&] {
                check(scenario::toJson(first) == scenario::toJson(second));
                check(first.trace == second.trace);
            });
            then("the acknowledgement reaches its timeout and the later steps are skipped", [&] {
                require(first.verdict == scenario::Verdict::Fail);
                check(first.steps.at(4).outcome == Outcome::Failed);
                check(first.steps.at(4).detail.contains("D13 high not reached within 16000 cycles"));
                check(first.steps.at(4).end - first.steps.at(4).start == 16000);
                check(first.steps.at(5).outcome == Outcome::Skipped);
            });
            then("the measurements made before the timeout and the trace are kept", [&] {
                check(first.measurements.size() == 2);
                check(first.duration > 0);
                check(!first.trace.empty());
            });
        });
    });
}};

const Scenario aMissedExpectationFailsTheRunBut{"A missed expectation fails the run but the later steps still run and measure", {"scenario"}, [] {
    given("the example expecting 60 % while the button is held", [&] {
        auto document                                      = exampleWith("pwm_setpoint.hex");
        document["steps"][5]["measure"]["expect"]["value"] = 60;
        when("it runs", [&] {
            const auto report = runOf(document);
            then("the run fails on that measurement, which keeps its value and says why", [&] {
                check(report.verdict == scenario::Verdict::Fail);
                const auto& held = measurement(report, "duty-held");
                check(!held.passed);
                check(held.value.value_or(0) == Approx(75.0).margin(0.1));
                check(held.detail.contains("is outside [59.5, 60.5] %"));
                check(report.steps.at(5).outcome == Outcome::Failed);
            });
            then("the steps after it still run", [&] {
                check(report.steps.at(8).outcome == Outcome::Done);
                check(measurement(report, "duty-released-again").passed);
            });
            then("the JSON report carries the verdict, the engine, the trace and the measurement", [&] {
                const auto json = scenario::toJson(report, "trace.vcd");
                check(json["verdict"] == "fail");
                check(json["engine"]["version"] == std::string{scenario::engineVersion()});
                check(json["trace"]["file"] == "trace.vcd");
                check(json["trace"]["timescale"] == "100 ps");
                const auto& entry = json["measurements"][2];
                check(entry["id"] == "duty-held");
                check(entry["verdict"] == "fail");
                check(entry["unit"] == "%");
                check(entry["expected"]["min"] == 59.5);
            });
        });
    });
}};

const Scenario binarySerialOutputIsKeptInThe{"Binary serial output is kept in the report, reversibly", {"scenario"}, [] {
    given("a firmware transmitting 0xFF, 'o', 'k', then 0xC3 0xA9 (\"é\" in UTF-8) at 1 Mbaud", [&] {
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
        when("it runs", [&] {
            const auto report = runOf(document);
            then("the bytes are kept as they are", [&] {
                check(report.verdict == scenario::Verdict::Pass);
                check(report.serial.at(0) == std::string{"\xFFok\xC3\xA9"});
            });
            then("the JSON report holds them in base64 and as text, U+FFFD for what is not UTF-8", [&] {
                const auto json = scenario::toJson(report, "trace.vcd");
                checkNothrow([&] { json.dump(2); });  // Throws on invalid UTF-8
                const auto& port = json["serial"][0];
                check(port["port"] == 0);
                check(port["bytes"] == 5);
                check(port["base64"] == "/29rw6k=");
                check(port["text"] == "\xEF\xBF\xBDok\xC3\xA9");
            });
        });
    });
}};

// Refusals before the run ----------------------------------------------------------------------------------------------

const Scenario aScenarioThatCannotRunOnThe{"A scenario that cannot run on the bench is refused before the run, with its cause", {"scenario"}, [] {
    using enum scenario::Problem::Cause;
    given("the committed example", [&] {
        auto document = example();
        when("its format version is unknown", [&] {
            document["formatVersion"] = 2;
            then("the cause is the version", [&] {
                expectProblem(document, Version, "/formatVersion", "Unsupported scenario formatVersion 2");
            });
        });
        when("its text is not JSON", [&] {
            const auto problem = scenario::Scenario::prepare("{\"formatVersion\": 1,", scenarioDir, "broken.json");
            then("it is unreadable", [&] {
                require(!problem);
                check(problem.error().cause == Unreadable);
            });
        });
        when("a step has two actions", [&] {
            document["steps"][0] = Json::parse(R"({"wait": "1ms", "set": {"probe": "A0", "volts": 1}})");
            then("it breaks the schema", [&] {
                expectProblem(document, Schema, "/steps/0", "");
            });
        });
        when("a duration has no unit", [&] {
            document["steps"][0]["wait"] = "10";
            then("it breaks the schema", [&] {
                expectProblem(document, Schema, "/steps/0", "");
            });
        });
        when("its circuit has a wire that does not fit the board", [&] {
            const auto file     = circuitWith("siliconforge-scenario-wiring.json", [](Json& circuit) {
                circuit["wires"][0]["to"] = "B+99";
            });
            document["circuit"] = file.generic_string();
            then("the wiring is at fault, in the circuit file", [&] {
                expectProblem(document, Wiring, "/wires/0/to", "", file.generic_string());
            });
        });
        when("its firmware is missing", [&] {
            document["firmware"] = "missing.hex";
            then("it is unreadable", [&] {
                expectProblem(document, Unreadable, "/firmware", "Cannot read the firmware missing.hex");
            });
        });
        when("its firmware has a byte at 0x10000, beyond the 32 KiB of flash", [&] {
            const auto file = std::filesystem::temp_directory_path() / "siliconforge-scenario-too-large.hex";
            std::ofstream{file, std::ios::binary} << ":020000040001F9\n:01000000AA55\n:00000001FF\n";
            document["firmware"] = file.generic_string();
            then("it is beyond the capacity of the MCU", [&] {
                expectProblem(document, Capacity, "", "ATmega328P cannot load this firmware (it ends at 0x10001)", file.generic_string());
            });
        });
        when("its circuit names an unknown microcontroller", [&] {
            const auto file     = circuitWith("siliconforge-scenario-mcu.json", [](Json& circuit) {
                circuit["chip"]["mcu"] = "atmega9999";
            });
            document["circuit"] = file.generic_string();
            then("the resource is missing", [&] {
                expectProblem(document, Resource, "/chip/mcu", "Unknown microcontroller atmega9999", file.generic_string());
            });
        });
        when("a step names a component the circuit does not have", [&] {
            document["steps"][3]["set"]["component"] = "button-d7";
            then("the resource is missing", [&] {
                expectProblem(document, Resource, "/steps/3/set", "has no component button-d7");
            });
            then("the refusal keeps the identity of every input read before it", [&] {
                const auto problem = problemOf(document);
                require(problem.inputs.size() == 3);
                check(problem.inputs[0].role == "scenario");
                check(problem.inputs[1].role == "circuit");
                check(problem.inputs[2].role == "firmware");
                check(problem.inputs[2].sha256.size() == 64);
            });
        });
        when("the initial state changes a state that is not interactive", [&] {
            document["initial"]["components"]["led-d6"] = Json{
                {"brightness", 1}
            };
            then("the resource is missing", [&] {
                expectProblem(document, Resource, "/initial/components/led-d6/brightness", "cannot be changed from outside");
            });
        });
        when("a wait watches the brightness of an LED, a time average its present value does not show", [&] {
            document["steps"][4] = Json::parse(R"({"waitFor": {"component": "led-d6", "state": "brightness", "above": 0.9}, "timeout": "1ms"})");
            then("it is beyond capacity, and the refusal points to the duty cycle of the pin", [&] {
                expectProblem(document, Capacity, "/steps/4/waitFor", "Measure the duty cycle of the pin that drives it");
            });
        });
        when("a measurement names a pin the MCU does not have", [&] {
            document["steps"][1]["measure"]["pin"] = "D42";
            then("the resource is missing", [&] {
                expectProblem(document, Resource, "/steps/1/measure/pin", "ATmega328P has no pin D42");
            });
        });
        when("a wait names a serial port the MCU does not have", [&] {
            document["steps"][4] = Json::parse(R"({"waitFor": {"serial": "ready", "port": 1}, "timeout": "1ms"})");
            then("the resource is missing", [&] {
                expectProblem(document, Resource, "/steps/4/waitFor/port", "has no serial port 1");
            });
        });
        when("a stimulus gives a boolean state the value 2", [&] {
            document["steps"][3]["set"]["value"] = 2;
            then("it is beyond capacity", [&] {
                expectProblem(document, Capacity, "/steps/3/set", "is a boolean");
            });
        });
        when("a probe applies 7.5 V to a 5 V circuit", [&] {
            document["initial"]["probes"] = Json{
                {"A0", 7.5}
            };
            then("it is beyond capacity", [&] {
                expectProblem(document, Capacity, "/initial/probes/A0", "A probe applies 0 to 5 V");
            });
        });
        when("a duty cycle tolerance is finer than its window resolves: 0.1 % over 100 cycles", [&] {
            document["steps"][1]["measure"]["window"]              = "100 cycles";
            document["steps"][1]["measure"]["expect"]["tolerance"] = 0.1;
            then("it is beyond capacity", [&] {
                expectProblem(document, Capacity, "/steps/1/measure/expect", "resolved to 1 %");
            });
        });
        when("a frequency window is shorter than the shortest expected period: 255 cycles for 62 600 Hz at most", [&] {
            document["steps"][2]["measure"]["window"] = "255 cycles";
            then("it is beyond capacity", [&] {
                expectProblem(document, Capacity, "/steps/2/measure/expect", "is shorter than one period of a 62600 Hz signal");
            });
        });
        when("the expected frequency is above half the clock", [&] {
            document["steps"][2]["measure"]["expect"] = Json{
                {    "value", 10e6},
                {"tolerance",    1}
            };
            then("it is beyond capacity", [&] {
                expectProblem(document, Capacity, "/steps/2/measure/expect", "cannot toggle above 8000000 Hz");
            });
        });
        when("a duration has an unknown unit", [&] {
            document["steps"][0]["wait"] = "2h";
            then("it breaks the schema", [&] {
                expectProblem(document, Schema, "/steps/0", "");
            });
        });
        when("a duration exceeds one hour", [&] {
            document["steps"][0]["wait"] = "3601s";
            then("it is beyond the bench limit", [&] {
                expectProblem(document, Capacity, "/steps/0/wait", "beyond the bench limit");
            });
        });
        when("a duration does not fit a number: 400 digits", [&] {
            for (const auto& [unit, name] : {
                     std::pair{      "s", "seconds"},
                     std::pair{" cycles",  "cycles"}
            }) {
                document["steps"][0]["wait"] = std::string(400, '9') + unit;
                then(std::string{"it is beyond the bench limit, in "} + name, [&] {
                    expectProblem(document, Capacity, "/steps/0/wait", "beyond the bench limit");
                });
            }
        });
        when("a step is scheduled at 2 ms after steps that last at least 3 ms", [&] {
            document["steps"][3]["at"] = "2ms";
            then("the schedule cannot be kept", [&] {
                expectProblem(document, Schedule, "/steps/3/at", "the steps before it last at least 48000 cycles");
            });
        });
        when("two measurements share an id", [&] {
            document["steps"][2]["measure"]["id"] = "duty-released";
            then("it breaks the schema", [&] {
                expectProblem(document, Schema, "/steps/2/measure/id", "already defined");
            });
        });
    });
}};

const Scenario anIncompatibleScenarioFileGivesAReport{"An incompatible scenario file gives a report that names the problem", {"scenario"}, [] {
    given("a scenario file that does not exist", [&] {
        when("it runs", [&] {
            const auto report = scenario::run(scenarioDir / "missing.json");
            then("the verdict is incompatible, exit code 2, with an unreadable cause", [&] {
                check(report.verdict == scenario::Verdict::Incompatible);
                check(scenario::exitCode(report.verdict) == 2);
                require(report.problem);
                check(report.problem->cause == scenario::Problem::Cause::Unreadable);
            });
            then("the JSON report names the problem and has no step", [&] {
                const auto json = scenario::toJson(report);
                check(json["verdict"] == "incompatible");
                check(json["problem"]["cause"] == "unreadable");
                check(!json.contains("steps"));
            });
        });
    });
}};

// The timeline -----------------------------------------------------------------------------------------------------------

const Scenario aStimulusScheduledInsideAnInstructionTakes{"A stimulus scheduled inside an instruction takes effect at its end, without delaying the timeline",
                                                          {"scenario"}, [] {
    given("a button press scheduled on each of twelve consecutive cycles, then a 1000-cycle measurement", [&] {
        when("each scenario runs", [&] {
            then("the press starts on its cycle, takes effect within one instruction, and the next steps keep their cycles", [&] {
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
                    const auto note6 = info("target ", target, "\n", scenario::toJson(report).dump(2));
                    require(report.verdict == scenario::Verdict::Pass);
                    const auto& set = report.steps.at(1);
                    check(set.start == target);
                    require(set.applied);
                    check(*set.applied >= target);
                    check(*set.applied - target < 5);
                    overshot           = overshot || *set.applied != target;
                    const auto& window = measurement(report, "window");
                    check(window.from == target);
                    check(window.to == target + 1000);
                    check(report.steps.at(3).start == target + 1000);
                }
                check(overshot);  // At least one target fell inside an instruction
            });
        });
    });
}};

const Scenario aBoundedWaitSeesEveryChangeAt{"A bounded wait sees every change, at its cycle", {"scenario"}, [] {
    given("a firmware raising D13 once, for the two cycles of a CBI", [&] {
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
        when("the scenario waits for D13 high, then low", [&] {
            const auto report = runOf(document);
            then("both waits are met at the exact cycles of the two edges", [&] {
                require(report.verdict == scenario::Verdict::Pass);
                const auto& high = report.steps.at(0);
                const auto& low  = report.steps.at(1);
                check(high.end >= 100);
                check(high.end < 110);
                check(low.end - high.end == 2);
                check(measurement(report, "after").value == 0.0);
            });
        });
    });
    given("the uart_echo firmware, which prints \"ready\" once", [&] {
        const auto document = exampleWith("uart_echo.hex", R"([
            {"waitFor": {"serial": "ready"}, "timeout": "100ms"},
            {"waitFor": {"serial": "ready"}, "timeout": "10ms"}
        ])");
        when("the scenario waits for \"ready\" twice", [&] {
            const auto report = runOf(document);
            then("only the text emitted after a step begins counts: the second wait times out", [&] {
                check(report.steps.at(0).outcome == Outcome::Done);
                check(report.steps.at(1).outcome == Outcome::Failed);
                check(report.steps.at(1).detail.contains("Serial text \"ready\" not reached"));
            });
        });
    });
    given("a 1-cycle wait, which the core overshoots, then a 1-cycle wait for the released button", [&] {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"wait": "1 cycles"},
            {"waitFor": {"component": "button-d2", "state": "pressed", "below": 0.5}, "timeout": "1 cycles"},
            {"set": {"component": "button-d2", "state": "pressed", "value": true}},
            {"waitFor": {"component": "button-d2", "state": "pressed", "above": 0.5}, "timeout": "10 cycles"}
        ])");
        when("it runs", [&] {
            const auto report = runOf(document);
            then("a condition met since reset is met at the start of its step", [&] {
                require(report.verdict == scenario::Verdict::Pass);
                check(report.steps.at(1).start == 1);
                check(report.steps.at(1).end == 1);
            });
            then("a state set by a stimulus counts from the boundary where the stimulus took effect", [&] {
                check(report.steps.at(3).end == report.steps.at(2).applied.value_or(0));
            });
        });
    });
}};

// Measurements -----------------------------------------------------------------------------------------------------------

const Scenario aMeasurementReportsAConstantLevelAnd{"A measurement reports a constant level and explains what it cannot measure", {"scenario"}, [] {
    given("D13 low, then A1 held high by a probe, then released to float", [&] {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"wait": "1ms"},
            {"measure": {"id": "led", "pin": "D13", "quantity": "dutyCycle", "window": "1ms"}},
            {"measure": {"id": "led-frequency", "pin": "D13", "quantity": "frequency", "window": "1ms"}},
            {"set": {"probe": "A1", "volts": 5}},
            {"measure": {"id": "probed", "pin": "A1", "quantity": "dutyCycle", "window": "10us"}},
            {"set": {"probe": "A1", "volts": null}},
            {"measure": {"id": "floating", "pin": "A1", "quantity": "dutyCycle", "window": "10us", "expect": {"min": 0, "max": 100}}}
        ])");
        when("it runs", [&] {
            const auto report = runOf(document);
            then("a constant level has a duty cycle of 0 % or 100 %", [&] {
                check(measurement(report, "led").value == 0.0);
                check(measurement(report, "probed").value == 100.0);
            });
            then("a frequency without two rising edges has no value, and without expectation does not fail", [&] {
                check(!measurement(report, "led-frequency").value);
                check(measurement(report, "led-frequency").passed);
                check(measurement(report, "led-frequency").detail == "0 rising edges in the window: at least two are needed");
            });
            then("a floating pin cannot be measured, and its expectation fails", [&] {
                check(!measurement(report, "floating").passed);
                check(measurement(report, "floating").detail == "The pin floats during the whole window");
                check(report.verdict == scenario::Verdict::Fail);
            });
        });
    });
}};

const Scenario aPinThatFloatsDuringPartOf{"A pin that floats during part of the window cannot be measured", {"scenario"}, [] {
    given("D12, not wired, switched between output high and floating input, through its pull-up each way: never low", [&] {
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
        when("its duty cycle and frequency are measured", [&] {
            const auto report = runOf(document);
            then("neither has a value: from floating to high is not a rising edge", [&] {
                for (const auto* id : {"duty", "frequency"}) {
                    const auto& entry = measurement(report, id);
                    check(!entry.value);
                    check(entry.risingEdges == 0);
                    check(entry.detail == "The pin floats during part of the window: its level is not defined there");
                }
            });
            then("the expected duty cycle fails the run", [&] {
                check(!measurement(report, "duty").passed);
                check(report.verdict == scenario::Verdict::Fail);
            });
        });
    });
}};

const Scenario aFrequencyWindowOfOnePeriodOr{"A frequency window of one period or more is accepted: the measurement tells whether it caught two edges",
                                             {"scenario"}, [] {
    given("two 300-cycle windows on the 256-cycle period of D6, expecting 62 500 ± 500 Hz", [&] {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"wait": "16112 cycles"},
            {"measure": {"id": "two-edges", "pin": "D6", "quantity": "frequency", "window": "300 cycles", "expect": {"value": 62500, "tolerance": 500}}},
            {"wait": "100 cycles"},
            {"measure": {"id": "one-edge", "pin": "D6", "quantity": "frequency", "window": "300 cycles", "expect": {"value": 62500, "tolerance": 500}}}
        ])");
        when("it runs", [&] {
            const auto report = runOf(document);
            then("the window whose phase holds two rising edges measures the frequency", [&] {
                const auto& caught = measurement(report, "two-edges");
                check(caught.risingEdges == 2);
                check(caught.passed);
                check(caught.value.value_or(0) == Approx(62500).margin(500));
            });
            then("the window that holds one says so, and fails", [&] {
                const auto& missed = measurement(report, "one-edge");
                check(missed.risingEdges == 1);
                check(!missed.value);
                check(!missed.passed);
                check(missed.detail == "1 rising edge in the window: at least two are needed");
            });
        });
    });
}};

const Scenario theEdgesAtBothEndsOfA{"The edges at both ends of a measurement window belong to it", {"scenario"}, [] {
    given("D6 rising at 15 896 and 16 152 cycles, and a closed window of exactly one period from the first edge", [&] {
        for (const auto* quantity : {"frequency", "dutyCycle"}) {
            const bool frequency = std::string_view{quantity} == "frequency";
            auto       document  = exampleWith("pwm_setpoint.hex", R"([{"wait": "15896 cycles"}])");
            const auto expect    = frequency ? Json{{"value", 62500}, {"tolerance", 100}} : Json{{"value", 25}, {"tolerance", 0.5}};
            document["steps"].push_back(Json{
                {"measure", {{"id", "window"}, {"pin", "D6"}, {"quantity", quantity}, {"window", "256 cycles"}, {"expect", expect}}}
            });
            when(std::string{"its "} + quantity + " is measured", [&] {
                const auto report = runOf(document);
                then("both edges count and the expectation is met", [&] {
                    check(measurement(report, "window").risingEdges == 2);
                    check(report.verdict == scenario::Verdict::Pass);
                });
            });
        }
    });
}};

const Scenario risingEdgesThatSpanNoTimeCannot{"Rising edges that span no time cannot be measured", {"scenario"}, [] {
    given("three probe stimuli at cycle 0 giving A0 two rising edges on the same cycle", [&] {
        const auto document = exampleWith("pwm_setpoint.hex", R"([
            {"set": {"probe": "A0", "volts": 5}},
            {"set": {"probe": "A0", "volts": 0}},
            {"set": {"probe": "A0", "volts": 5}},
            {"measure": {"id": "informative", "pin": "A0", "quantity": "frequency", "window": "100 cycles"}},
            {"measure": {"id": "expected", "pin": "A0", "quantity": "period", "window": "100 cycles", "expect": {"min": 0, "max": 1}}}
        ])");
        when("their frequency is measured", [&] {
            const auto report = runOf(document);
            const auto json   = scenario::toJson(report);
            then("there is no value, and the report says why, in its detail and its JSON", [&] {
                const auto& informative = measurement(report, "informative");
                require(informative.risingEdges >= 2);
                check(!informative.value);
                check(informative.detail.contains("rising edges in the window all fall on cycle"));
                check(json["measurements"][0]["detail"] == informative.detail);
                check(!report.steps.at(3).detail.contains("inf Hz"));
            });
            then("a measurement with an expectation fails", [&] {
                check(!measurement(report, "expected").passed);
            });
        });
    });
}};

// The trace ----------------------------------------------------------------------------------------------------------------

const Scenario aVcdTimescaleIs110Or{"A VCD timescale is 1, 10 or 100 of a unit, and the times count in it", {"scenario"}, [] {
    using scenario::detail::VcdClock;
    const auto expectTimescale = [](u64 clockHz, std::string_view timescale, Cycles cycles, u64 time) {
        const VcdClock clock{clockHz};
        const auto note7 = info(clockHz, " Hz");
        check(clock.timescale() == timescale);
        check(clock.time(cycles) == time);
    };
    given("a clock whose period is a whole number of femtoseconds", [&] {
        then("the timescale is the largest valid one that divides the period, and cycles convert exactly", [&] {
            expectTimescale(16'000'000, "100 ps", 1, 625);
            expectTimescale(16'000'000, "100 ps", 16'000'000, 10'000'000'000);
            expectTimescale(1'000'000, "1 us", 7, 7);
            expectTimescale(20'000'000, "10 ns", 3, 15);
            expectTimescale(8'000'000, "1 ns", 2, 250);
            expectTimescale(100'000'000, "10 ns", 1, 1);
            expectTimescale(1, "1 s", 3, 3);
            expectTimescale(32'768, "1 fs", 1, 30'517'578'125);
        });
    });
    given("a 3 MHz clock: 333 333 333.3 fs per cycle", [&] {
        then("the timescale is 1 fs, times are rounded, and every whole second is exact", [&] {
            expectTimescale(3'000'000, "1 fs", 1, 333'333'333);
            expectTimescale(3'000'000, "1 fs", 3'000'000 * Cycles{3600}, 3'600'000'000'000'000'000);
            check(VcdClock{3'000'000}.time(2) == 666'666'667);
            check(!VcdClock{3'000'000}.exact());
        });
    });
}};

const Scenario theStepsOfARunEndWhere{"The steps of a run end where the MCU stopped", {"scenario"}, [] {
    given("a firmware whose first instruction is BREAK, and a 1 ms wait followed by a measurement", [&] {
        AvrAssembler program;
        program.raw(0x9598).rjmp(-1);  // BREAK
        const auto document = programWith(program, "siliconforge-scenario-break.hex", R"([
            {"wait": "1ms"},
            {"measure": {"id": "led", "pin": "D13", "quantity": "dutyCycle", "window": "1ms"}}
        ])");
        when("it runs", [&] {
            const auto report = runOf(document);
            then("the wait fails where the MCU stopped, not at its planned end", [&] {
                require(report.verdict == scenario::Verdict::Fail);
                check(report.duration < 16000);
                const auto& wait = report.steps.at(0);
                check(wait.outcome == Outcome::Failed);
                check(wait.detail.contains("BREAK instruction"));
                check(wait.start == 0);
                check(wait.end == report.duration);
            });
            then("the skipped steps start and end there too", [&] {
                const auto& skipped = report.steps.at(1);
                check(skipped.outcome == Outcome::Skipped);
                check(skipped.start == report.duration);
                check(skipped.end == report.duration);
                check(report.measurements.empty());
            });
        });
    });
}};

// Signal generator (#63) -----------------------------------------------------------------------------------------------

namespace {
/// A signal generator wired to PC5 (the chip at column 3 of the breadboard) and to the ground rail, in a temporary file.
std::filesystem::path generatorCircuitFile() {
    const Json circuit = {
        {"formatVersion", 1},
        {"name", "Generator"},
        {"board", {{"type", "breadboard"}, {"columns", 30}}},
        {"chip", {{"mcu", "atmega328p"}, {"column", 3}}},
        {"components", {{{"id", "gbf"}, {"type", "signal-generator"}, {"at", {{"output", "i20"}, {"common", "i22"}}}}}},
        {"wires", {{{"from", "h22"}, {"to", "T-22"}, {"color", "black"}}, {{"from", "h20"}, {"to", "h3"}, {"color", "yellow"}}}}
    };
    const auto file = std::filesystem::temp_directory_path() / "siliconforge-generator-circuit.json";
    std::ofstream{file} << circuit.dump();
    return file;
}

Json generatorScenario(std::string_view steps) {
    const auto firmware = firmwareDir / "blink.hex";
    if (!std::filesystem::exists(firmware))
        skip(firmware.string(), " is missing: build the firmware (see firmware/README.md)");
    return {{"formatVersion", 1},
            {"name", "Generator"},
            {"circuit", generatorCircuitFile().generic_string()},
            {"firmware", firmware.generic_string()},
            {"steps", Json::parse(steps)}};
}
}  // namespace

const Scenario aScenarioProgramsAGeneratorAndMeasures{"A scenario programs a generator and measures its wave on a pin", {"scenario", "generator"}, [] {
    given("a 1 kHz square wave of 5 V peak around 2.5 V, 25 % high, on PC5", [&] {
        const auto document = generatorScenario(R"([
            {"set": {"component": "gbf", "state": "waveform", "value": 1}},
            {"set": {"component": "gbf", "state": "frequency", "value": 1000}},
            {"set": {"component": "gbf", "state": "amplitude", "value": 2.5}},
            {"set": {"component": "gbf", "state": "offset", "value": 2.5}},
            {"set": {"component": "gbf", "state": "duty", "value": 25}},
            {"set": {"component": "gbf", "state": "enabled", "value": true}},
            {"measure": {"id": "frequency", "pin": "PC5", "quantity": "frequency", "window": "5ms", "expect": {"value": 1000, "tolerance": 0.5}}},
            {"measure": {"id": "duty", "pin": "PC5", "quantity": "dutyCycle", "window": "5ms", "expect": {"value": 25, "tolerance": 0.5}}}
        ])");
        then("the captured edges give the frequency and duty cycle programmed", [&] {
            const auto report = runOf(document);
            check(report.verdict == scenario::Verdict::Pass);
            check(measurement(report, "frequency").value.value() == Approx(1000.0).margin(0.5));
            check(measurement(report, "duty").value.value() == Approx(25.0).margin(0.5));
        });
    });
    given("an arbitrary wave of four samples over 4 ms, two of them above half the supply", [&] {
        const auto document = generatorScenario(R"([
            {"set": {"component": "gbf", "samples": [0, 5, 5, 0], "period": "4ms"}},
            {"set": {"component": "gbf", "state": "enabled", "value": true}},
            {"measure": {"id": "frequency", "pin": "PC5", "quantity": "frequency", "window": "12ms", "expect": {"value": 250, "tolerance": 0.5}}},
            {"measure": {"id": "duty", "pin": "PC5", "quantity": "dutyCycle", "window": "12ms", "expect": {"value": 50, "tolerance": 1}}}
        ])");
        then("it repeats at 250 Hz with the share of time the samples are high", [&] {
            const auto report = runOf(document);
            check(report.verdict == scenario::Verdict::Pass);
            check(measurement(report, "frequency").value.value() == Approx(250.0).margin(0.5));
        });
    });
}};

const Scenario aScenarioThatAsksAGeneratorFor{"A scenario that asks a generator for more than it can do is refused before it runs",
                                              {"scenario", "generator"}, [] {
    given("a 20 kHz sine on a 16 MHz MCU, which steps 64 times per period", [&] {
        const auto document = generatorScenario(R"([{"set": {"component": "gbf", "state": "frequency", "value": 20000}}])");
        then("it is a capacity problem that names the highest frequency", [&] {
            expectProblem(document, scenario::Problem::Cause::Capacity, "/steps/0/set", "15625");
        });
    });
    given("the same frequency after the wave was changed to a square wave", [&] {
        const auto document = generatorScenario(R"([
            {"set": {"component": "gbf", "state": "waveform", "value": 1}},
            {"set": {"component": "gbf", "state": "frequency", "value": 20000}}
        ])");
        then("it is accepted: the steps are rehearsed in order", [&] {
            check(prepared(document));
        });
    });
    given("a request beyond the electrical bounds", [&] {
        then("an amplitude above 10 V is refused", [&] {
            expectProblem(generatorScenario(R"([{"set": {"component": "gbf", "state": "amplitude", "value": 12}}])"), scenario::Problem::Cause::Capacity,
                          "/steps/0/set", "amplitude");
        });
        then("a sample above 20 V is refused", [&] {
            expectProblem(generatorScenario(R"([{"set": {"component": "gbf", "samples": [0, 25], "period": "1ms"}}])"), scenario::Problem::Cause::Capacity,
                          "/steps/0/set", "25 V");
        });
        then("samples too close in time are refused", [&] {
            Json many = Json::array();  // 200 samples over 0.1 ms: 8 cycles each on a 16 MHz clock.
            for (int i = 0; i < 200; ++i)
                many.push_back(i % 2);
            auto document = generatorScenario(R"([{"set": {"component": "gbf", "samples": [0, 1], "period": "0.1ms"}}])");
            document["steps"][0]["set"]["samples"] = many;
            expectProblem(document, scenario::Problem::Cause::Capacity, "/steps/0/set", "below the minimum of 16 cycles");
        });
        then("the arbitrary wave cannot be selected before samples were programmed", [&] {
            expectProblem(generatorScenario(R"([{"set": {"component": "gbf", "state": "waveform", "value": 3}}])"), scenario::Problem::Cause::Capacity,
                          "/steps/0/set", "program its samples first");
        });
    });
}};

const Scenario theInitialStateOfAGeneratorIs{"The initial state of a generator is one state, whatever the order of its keys", {"scenario", "generator"}, [] {
    given("a square wave of 20 kHz set in the initial state, where frequency sorts before waveform", [&] {
        auto document       = generatorScenario(R"([
            {"measure": {"id": "frequency", "pin": "PC5", "quantity": "frequency", "window": "1ms", "expect": {"value": 20000, "tolerance": 20}}}
        ])");
        document["initial"] = Json::parse(R"({"components": {"gbf": {"frequency": 20000, "waveform": 1, "amplitude": 2.5, "offset": 2.5, "enabled": true}}})");
        then("it is accepted, and the run applies it: the pin toggles at 20 kHz", [&] {
            const auto report = runOf(document);
            check(report.verdict == scenario::Verdict::Pass);
            check(measurement(report, "frequency").value.value() == Approx(20000.0).margin(20.0));
        });
    });
    given("the same frequency with the default sine", [&] {
        auto document       = generatorScenario(R"([{"wait": "1ms"}])");
        document["initial"] = Json::parse(R"({"components": {"gbf": {"frequency": 20000}}})");
        then("it is still refused before the run", [&] {
            expectProblem(document, scenario::Problem::Cause::Capacity, "/initial/components/gbf/frequency", "15625");
        });
    });
}};

const Scenario aBoundedWaitOnAGeneratorState{"A bounded wait on a generator state sees the step that crosses its threshold", {"scenario", "generator"}, [] {
    given("a 1 kHz sine of 2 V around 0 V, which never makes a digital edge on the pin: nothing but the generator changes", [&] {
        const auto document = generatorScenario(R"([
            {"set": {"component": "gbf", "state": "amplitude", "value": 2}},
            {"set": {"component": "gbf", "state": "offset", "value": 0}},
            {"set": {"component": "gbf", "state": "enabled", "value": true}},
            {"label": "above 1.5 V", "waitFor": {"component": "gbf", "state": "outputVoltage", "above": 1.5}, "timeout": "2ms"}
        ])");
        then("the wait is met at the cycle of the first step above 1.5 V: step 9 of 64, 2250 cycles after the output was enabled", [&] {
            const auto report = runOf(document);
            require(report.verdict == scenario::Verdict::Pass);
            require(report.steps.size() == 4);
            check(report.steps[3].outcome == Outcome::Done);
            check(report.steps[3].end - report.steps[2].applied.value() == 2250);
        });
    });
    given("the same wave and a threshold it never reaches", [&] {
        const auto document = generatorScenario(R"([
            {"set": {"component": "gbf", "state": "amplitude", "value": 2}},
            {"set": {"component": "gbf", "state": "offset", "value": 0}},
            {"set": {"component": "gbf", "state": "enabled", "value": true}},
            {"waitFor": {"component": "gbf", "state": "outputVoltage", "above": 2.5}, "timeout": "2ms"}
        ])");
        then("the wait reaches its timeout", [&] {
            check(runOf(document).verdict == scenario::Verdict::Fail);
        });
    });
}};
