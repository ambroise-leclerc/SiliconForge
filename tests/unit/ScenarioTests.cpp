// Bench scenarios (ADR 0010, #57, #58, #59): a versioned file checked before it runs, executed on the emulated
// ATmega328P with stimuli at their cycle and bounded waits, measured on the captured edges, and reported the same way
// every time it runs on the same inputs.

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
#include <iterator>
#include <sstream>
#include <string>

using namespace siliconforge;
using Catch::Approx;
using Json = nlohmann::json;

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

std::unique_ptr<scenario::Scenario> prepared(const Json& document) {
    auto result = scenario::Scenario::prepare(document.dump(), scenarioDir, "test.json");
    if (!result)
        FAIL(scenario::toString(result.error().cause) << " " << result.error().file << result.error().path << ": " << result.error().message);
    return std::move(*result);
}

scenario::Problem problemOf(const Json& document) {
    auto result = scenario::Scenario::prepare(document.dump(), scenarioDir, "test.json");
    if (result)
        FAIL("The scenario was accepted: " << document.dump());
    return result.error();
}

/// Writes an assembled program as an Intel HEX file and returns its path.
std::filesystem::path hexFile(const siliconforge::test::AvrAssembler& program, std::string_view name) {
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

const scenario::Measurement& measurement(const scenario::Report& report, std::string_view id) {
    const auto found = std::ranges::find(report.measurements, id, &scenario::Measurement::id);
    INFO("Measurement " << id);
    REQUIRE(found != report.measurements.end());
    return *found;
}
}  // namespace

TEST_CASE("SHA-256 identifies the inputs of a run", "[scenario]") {
    const auto digest = [](std::string_view text) {
        return scenario::detail::sha256({reinterpret_cast<const unsigned char*>(text.data()), text.size()});
    };
    CHECK(digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(digest(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("The committed scenario example is valid and runs on the committed firmware", "[scenario]") {
    const auto report = scenario::run(scenarioDir / "pwm-setpoint.json");
    INFO(scenario::toJson(report).dump(2));
    REQUIRE(report.verdict == scenario::Verdict::Pass);
    CHECK(report.mcu == "ATmega328P");
    REQUIRE(report.inputs.size() == 3);
    CHECK(report.inputs[0].role == "scenario");
    CHECK(report.inputs[1].path == "../circuits/arduino-demo.json");
    CHECK(report.inputs[2].path == "../firmware/prebuilt/pwm_setpoint.hex");
    for (const auto& input : report.inputs)
        CHECK(input.sha256.size() == 64);
}

TEST_CASE("A scenario switches an input at its cycle and measures the PWM on the captured edges", "[scenario]") {
    auto       scenario = prepared(exampleWith("pwm_setpoint.hex"));
    const auto report   = scenario->run();
    INFO(scenario::toJson(report).dump(2));
    REQUIRE(report.verdict == scenario::Verdict::Pass);

    // The press is applied at 5 ms, give or take the instruction in progress.
    const auto& press = report.steps.at(3);
    CHECK(press.kind == "set");
    CHECK(press.start >= 80000);
    CHECK(press.start < 80004);
    // The acknowledgement comes after the press, well before its timeout.
    const auto& acknowledged = report.steps.at(4);
    CHECK(acknowledged.outcome == scenario::StepRecord::Outcome::Done);
    CHECK(acknowledged.end > press.start);
    CHECK(acknowledged.end - press.start < 16000);

    CHECK(measurement(report, "duty-released").value == Approx(25.0));
    CHECK(measurement(report, "duty-released").unit == "%");
    CHECK(measurement(report, "frequency").value == Approx(62500.0));
    CHECK(measurement(report, "frequency").unit == "Hz");
    CHECK(measurement(report, "duty-held").value.value_or(0) == Approx(75.0).margin(0.1));
    CHECK(measurement(report, "duty-held").risingEdges >= 60);

    // The trace holds every pin from reset.
    CHECK(report.trace.starts_with("$comment SiliconForge bench trace"));
    // 62.5 ns per cycle: a valid timescale of 100 ps, 625 units per cycle.
    CHECK(report.trace.contains("$timescale 100 ps $end"));
    std::istringstream lines{report.trace};
    std::size_t        times = 0;
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with('#')) {
            ++times;
            CHECK(std::stoull(line.substr(1)) % 625 == 0);
        }
    CHECK(times > 100);
    CHECK(report.trace.contains(" D6_PD6 $end"));
}

TEST_CASE("Two runs on the same inputs give the same measurements, verdict and trace", "[scenario]") {
    SECTION("A passing run") {
        auto scenario = prepared(exampleWith("pwm_setpoint.hex"));
        CHECK(scenario::toJson(scenario->run()) == scenario::toJson(scenario->run()));
    }
    SECTION("A run whose bounded wait reaches its timeout") {
        // pwm.hex keeps 25 % and never lights D13: the acknowledgement times out and the later steps are skipped.
        auto       scenario = prepared(exampleWith("pwm.hex"));
        const auto first    = scenario->run();
        const auto second   = scenario->run();
        CHECK(scenario::toJson(first) == scenario::toJson(second));
        CHECK(first.trace == second.trace);
        REQUIRE(first.verdict == scenario::Verdict::Fail);
        CHECK(first.steps.at(4).outcome == scenario::StepRecord::Outcome::Failed);
        CHECK(first.steps.at(4).detail.contains("D13 high not reached within 16000 cycles"));
        CHECK(first.steps.at(4).end - first.steps.at(4).start == 16000);
        CHECK(first.steps.at(5).outcome == scenario::StepRecord::Outcome::Skipped);
        CHECK(first.measurements.size() == 2);  // The measurements made before the timeout are kept
        CHECK(first.duration > 0);
        CHECK_FALSE(first.trace.empty());
    }
}

TEST_CASE("A missed expectation fails the run but the later steps still run and measure", "[scenario]") {
    auto document                                      = exampleWith("pwm_setpoint.hex");
    document["steps"][5]["measure"]["expect"]["value"] = 60;
    const auto report                                  = prepared(document)->run();
    CHECK(report.verdict == scenario::Verdict::Fail);
    const auto& held = measurement(report, "duty-held");
    CHECK_FALSE(held.passed);
    CHECK(held.value.value_or(0) == Approx(75.0).margin(0.1));
    CHECK(held.detail.contains("is outside [59.5, 60.5] %"));
    CHECK(report.steps.at(5).outcome == scenario::StepRecord::Outcome::Failed);
    CHECK(report.steps.at(8).outcome == scenario::StepRecord::Outcome::Done);
    CHECK(measurement(report, "duty-released-again").passed);

    const auto json = scenario::toJson(report, "trace.vcd");
    CHECK(json["verdict"] == "fail");
    CHECK(json["engine"]["version"] == std::string{scenario::engineVersion()});
    CHECK(json["trace"]["file"] == "trace.vcd");
    const auto& entry = json["measurements"][2];
    CHECK(entry["id"] == "duty-held");
    CHECK(entry["verdict"] == "fail");
    CHECK(entry["unit"] == "%");
    CHECK(entry["expected"]["min"] == 59.5);
}

TEST_CASE("A measurement reports a constant level, and refuses what it cannot measure", "[scenario]") {
    auto document     = exampleWith("pwm_setpoint.hex");
    document["steps"] = Json::parse(R"([
        {"wait": "1ms"},
        {"measure": {"id": "led", "pin": "D13", "quantity": "dutyCycle", "window": "1ms"}},
        {"measure": {"id": "led-frequency", "pin": "D13", "quantity": "frequency", "window": "1ms"}},
        {"set": {"probe": "A1", "volts": 5}},
        {"measure": {"id": "probed", "pin": "A1", "quantity": "dutyCycle", "window": "10us"}},
        {"set": {"probe": "A1", "volts": null}},
        {"measure": {"id": "floating", "pin": "A1", "quantity": "dutyCycle", "window": "10us", "expect": {"min": 0, "max": 100}}}
    ])");
    const auto report = prepared(document)->run();
    INFO(scenario::toJson(report).dump(2));
    CHECK(measurement(report, "led").value == 0.0);
    CHECK_FALSE(measurement(report, "led-frequency").value);
    CHECK(measurement(report, "led-frequency").passed);  // Without an expectation a measurement is informative
    CHECK(measurement(report, "led-frequency").detail == "0 rising edges in the window: at least two are needed");
    CHECK(measurement(report, "probed").value == 100.0);
    CHECK_FALSE(measurement(report, "floating").passed);
    CHECK(measurement(report, "floating").detail == "The pin floats during the whole window");
    CHECK(report.verdict == scenario::Verdict::Fail);
}

TEST_CASE("A scenario that cannot run on the bench is refused before the run, with its cause", "[scenario]") {
    const auto expectProblem =
        [](const Json& document, scenario::Problem::Cause cause, std::string_view path, std::string_view message, std::string_view file = "") {
            const auto problem = problemOf(document);
            INFO(problem.file << problem.path << ": " << problem.message);
            CHECK(problem.cause == cause);
            CHECK(problem.path == path);
            CHECK(problem.file == file);
            CHECK(problem.message.contains(message));
        };
    using enum scenario::Problem::Cause;
    auto document = example();

    SECTION("An unknown format version") {
        document["formatVersion"] = 2;
        expectProblem(document, Version, "/formatVersion", "Unsupported scenario formatVersion 2");
    }
    SECTION("Text that is not JSON") {
        const auto problem = scenario::Scenario::prepare("{\"formatVersion\": 1,", scenarioDir, "broken.json");
        REQUIRE_FALSE(problem);
        CHECK(problem.error().cause == Unreadable);
    }
    SECTION("A step that breaks the schema") {
        document["steps"][0] = Json::parse(R"({"wait": "1ms", "set": {"probe": "A0", "volts": 1}})");
        expectProblem(document, Schema, "/steps/0", "");
    }
    SECTION("A duration without unit") {
        document["steps"][0]["wait"] = "10";
        expectProblem(document, Schema, "/steps/0", "");
    }
    SECTION("A circuit with a wire that does not fit the board") {
        auto circuit              = Json::parse(std::ifstream{sourceDir / "circuits" / "arduino-demo.json"});
        circuit["wires"][0]["to"] = "B+99";
        const auto file           = std::filesystem::temp_directory_path() / "siliconforge-scenario-wiring.json";
        std::ofstream{file} << circuit.dump();
        document["circuit"] = file.generic_string();
        expectProblem(document, Wiring, "/wires/0/to", "", file.generic_string());
    }
    SECTION("A missing firmware") {
        document["firmware"] = "missing.hex";
        expectProblem(document, Unreadable, "/firmware", "Cannot read the firmware missing.hex");
    }
    SECTION("A firmware larger than the flash of the MCU") {
        // One byte at 0x10000 (extended linear address 0x0001), beyond the 32 KiB of the ATmega328P.
        const auto file = std::filesystem::temp_directory_path() / "siliconforge-scenario-too-large.hex";
        std::ofstream{file, std::ios::binary} << ":020000040001F9\n:01000000AA55\n:00000001FF\n";
        document["firmware"] = file.generic_string();
        expectProblem(document, Capacity, "", "ATmega328P cannot load this firmware (it ends at 0x10001)", file.generic_string());
    }
    SECTION("An unknown microcontroller") {
        auto circuit           = Json::parse(std::ifstream{sourceDir / "circuits" / "arduino-demo.json"});
        circuit["chip"]["mcu"] = "atmega9999";
        const auto file        = std::filesystem::temp_directory_path() / "siliconforge-scenario-mcu.json";
        std::ofstream{file} << circuit.dump();
        document["circuit"] = file.generic_string();
        expectProblem(document, Resource, "/chip/mcu", "Unknown microcontroller atmega9999", file.generic_string());
    }
    SECTION("A component the circuit does not have") {
        document["steps"][3]["set"]["component"] = "button-d7";
        expectProblem(document, Resource, "/steps/3/set", "has no component button-d7");
        // The refusal keeps the identity of every input read before it.
        const auto problem = problemOf(document);
        REQUIRE(problem.inputs.size() == 3);
        CHECK(problem.inputs[0].role == "scenario");
        CHECK(problem.inputs[1].role == "circuit");
        CHECK(problem.inputs[2].role == "firmware");
        CHECK(problem.inputs[2].sha256.size() == 64);
    }
    SECTION("A state that cannot be changed from outside") {
        document["initial"]["components"]["led-d6"] = Json{
            {"brightness", 1}
        };
        expectProblem(document, Resource, "/initial/components/led-d6/brightness", "cannot be changed from outside");
    }
    SECTION("A pin the MCU does not have") {
        document["steps"][1]["measure"]["pin"] = "D42";
        expectProblem(document, Resource, "/steps/1/measure/pin", "ATmega328P has no pin D42");
    }
    SECTION("A serial port the MCU does not have") {
        document["steps"][4] = Json::parse(R"({"waitFor": {"serial": "ready", "port": 1}, "timeout": "1ms"})");
        expectProblem(document, Resource, "/steps/4/waitFor/port", "has no serial port 1");
    }
    SECTION("A value outside what a component accepts") {
        document["steps"][3]["set"]["value"] = 2;
        expectProblem(document, Capacity, "/steps/3/set", "is a boolean");
    }
    SECTION("A probe above the supply") {
        document["initial"]["probes"] = Json{
            {"A0", 7.5}
        };
        expectProblem(document, Capacity, "/initial/probes/A0", "A probe applies 0 to 5 V");
    }
    SECTION("A tolerance finer than the instrument resolves") {
        document["steps"][1]["measure"]["window"]              = "100 cycles";
        document["steps"][1]["measure"]["expect"]["tolerance"] = 0.1;
        expectProblem(document, Capacity, "/steps/1/measure/expect", "resolved to 1 %");
    }
    SECTION("A frequency the window is too short to measure") {
        // 62 600 Hz at most: no expected period is shorter than 255.6 cycles.
        document["steps"][2]["measure"]["window"] = "255 cycles";
        expectProblem(document, Capacity, "/steps/2/measure/expect", "is shorter than one period of a 62600 Hz signal");
    }
    SECTION("A frequency no pin can reach") {
        document["steps"][2]["measure"]["expect"] = Json{
            {    "value", 10e6},
            {"tolerance",    1}
        };
        expectProblem(document, Capacity, "/steps/2/measure/expect", "cannot toggle above 8000000 Hz");
    }
    SECTION("A duration beyond the bench limit") {
        document["steps"][0]["wait"] = "2h";
        expectProblem(document, Schema, "/steps/0", "");
        document["steps"][0]["wait"] = "3601s";
        expectProblem(document, Capacity, "/steps/0/wait", "beyond the bench limit");
    }
    SECTION("A duration too large for a number") {
        document["steps"][0]["wait"] = std::string(400, '9') + "s";
        expectProblem(document, Capacity, "/steps/0/wait", "beyond the bench limit");
        document["steps"][0]["wait"] = std::string(400, '9') + " cycles";
        expectProblem(document, Capacity, "/steps/0/wait", "beyond the bench limit");
    }
    SECTION("A step scheduled before the previous ones can end") {
        document["steps"][3]["at"] = "2ms";  // After a 1 ms wait and two 1 ms measurements
        expectProblem(document, Schedule, "/steps/3/at", "the steps before it last at least 48000 cycles");
    }
    SECTION("Two measurements with the same id") {
        document["steps"][2]["measure"]["id"] = "duty-released";
        expectProblem(document, Schema, "/steps/2/measure/id", "already defined");
    }
}

TEST_CASE("An incompatible scenario file gives a report that names the problem", "[scenario]") {
    const auto report = scenario::run(scenarioDir / "missing.json");
    CHECK(report.verdict == scenario::Verdict::Incompatible);
    CHECK(scenario::exitCode(report.verdict) == 2);
    REQUIRE(report.problem);
    CHECK(report.problem->cause == scenario::Problem::Cause::Unreadable);
    const auto json = scenario::toJson(report);
    CHECK(json["verdict"] == "incompatible");
    CHECK(json["problem"]["cause"] == "unreadable");
    CHECK_FALSE(json.contains("steps"));
}

TEST_CASE("A stimulus scheduled inside an instruction takes effect at its end, without delaying the timeline", "[scenario]") {
    // The core acts at instruction boundaries: a target inside a multi-cycle instruction is reached a few cycles late.
    // The next step still starts on the scheduled cycle, and the measurements use the exact cycles of the edges.
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
        CHECK(*set.applied - target < 5);  // At most the rest of one instruction
        overshot           = overshot || *set.applied != target;
        const auto& window = measurement(report, "window");
        CHECK(window.from == target);
        CHECK(window.to == target + 1000);
        CHECK(report.steps.at(3).start == target + 1000);
    }
    CHECK(overshot);  // The loop did land inside an instruction at least once
}

TEST_CASE("A bounded wait sees a pulse shorter than any polling interval", "[scenario]") {
    // D13 goes high for the two cycles of a CBI, once, then stays low.
    siliconforge::test::AvrAssembler program;
    program.sbi(0x04, 5);  // DDRB: D13 output
    for (int i = 0; i < 100; ++i)
        program.nop();
    program.sbi(0x05, 5).cbi(0x05, 5).rjmp(-1);
    auto document        = example();
    document["firmware"] = hexFile(program, "siliconforge-scenario-pulse.hex").generic_string();
    document["initial"]  = Json::object();
    document["steps"]    = Json::parse(R"([
        {"waitFor": {"pin": "D13", "state": "high"}, "timeout": "1000 cycles"},
        {"waitFor": {"pin": "D13", "state": "low"}, "timeout": "1000 cycles"},
        {"measure": {"id": "after", "pin": "D13", "quantity": "dutyCycle", "window": "100 cycles"}}
    ])");
    const auto report    = prepared(document)->run();
    INFO(scenario::toJson(report).dump(2));
    REQUIRE(report.verdict == scenario::Verdict::Pass);
    const auto& high = report.steps.at(0);
    const auto& low  = report.steps.at(1);
    CHECK(high.end >= 100);
    CHECK(high.end < 110);
    CHECK(low.end - high.end == 2);  // The exact cycles of both edges, not the boundary of a polling interval
    CHECK(measurement(report, "after").value == 0.0);
}

TEST_CASE("A bounded wait for serial text only counts what is emitted after the step begins", "[scenario]") {
    auto document     = exampleWith("uart_echo.hex");
    document["steps"] = Json::parse(R"([
        {"waitFor": {"serial": "ready"}, "timeout": "100ms"},
        {"waitFor": {"serial": "ready"}, "timeout": "10ms"}
    ])");
    const auto report = prepared(document)->run();
    INFO(scenario::toJson(report).dump(2));
    CHECK(report.steps.at(0).outcome == scenario::StepRecord::Outcome::Done);
    CHECK(report.steps.at(1).outcome == scenario::StepRecord::Outcome::Failed);
    CHECK(report.steps.at(1).detail.contains("Serial text \"ready\" not reached"));
}

TEST_CASE("A VCD timescale is 1, 10 or 100 of a unit, and the times count in it", "[scenario]") {
    using scenario::detail::VcdClock;
    const auto check = [](u64 clockHz, std::string_view timescale, Cycles cycles, u64 time) {
        const VcdClock clock{clockHz};
        INFO(clockHz << " Hz");
        CHECK(clock.timescale() == timescale);
        CHECK(clock.time(cycles) == time);
    };
    check(16'000'000, "100 ps", 1, 625);
    check(16'000'000, "100 ps", 16'000'000, 10'000'000'000);
    check(1'000'000, "1 us", 7, 7);
    check(20'000'000, "10 ns", 3, 15);
    check(8'000'000, "1 ns", 2, 250);
    check(100'000'000, "10 ns", 1, 1);
    check(1, "1 s", 3, 3);
    check(32'768, "1 fs", 1, 30'517'578'125);
    // 3 MHz: 333 333 333.3 fs per cycle, rounded, but every whole second exact.
    check(3'000'000, "1 fs", 1, 333'333'333);
    check(3'000'000, "1 fs", 3'000'000 * Cycles{3600}, 3'600'000'000'000'000'000);
    CHECK(VcdClock{3'000'000}.time(2) == 666'666'667);
    CHECK_FALSE(VcdClock{3'000'000}.exact());
}

TEST_CASE("A pin that floats during the window cannot be measured", "[scenario]") {
    // D12 (PB4) is not wired in the circuit: as an input without pull-up it floats. The program switches it between
    // an output driven high and that floating input, through the pull-up each way, so it is never low.
    siliconforge::test::AvrAssembler program;
    program.sbi(0x05, 4);  // PORTB4: pull-up, then high once an output
    program.sbi(0x04, 4);  // DDRB4: output high
    for (int i = 0; i < 20; ++i)
        program.nop();
    program.cbi(0x04, 4);  // Input with its pull-up: still high
    program.cbi(0x05, 4);  // No pull-up: floating
    for (int i = 0; i < 20; ++i)
        program.nop();
    program.rjmp(-45);  // Back to the first SBI
    auto document        = example();
    document["firmware"] = hexFile(program, "siliconforge-scenario-floating.hex").generic_string();
    document["initial"]  = Json::object();
    document["steps"]    = Json::parse(R"([
        {"wait": "10us"},
        {"measure": {"id": "duty", "pin": "D12", "quantity": "dutyCycle", "window": "50us", "expect": {"min": 0, "max": 100}}},
        {"measure": {"id": "frequency", "pin": "D12", "quantity": "frequency", "window": "50us"}}
    ])");
    const auto report    = prepared(document)->run();
    INFO(scenario::toJson(report).dump(2));
    for (const auto* id : {"duty", "frequency"}) {
        const auto& entry = measurement(report, id);
        CHECK_FALSE(entry.value);
        CHECK(entry.risingEdges == 0);  // From floating to high is not a rising edge
        CHECK(entry.detail == "The pin floats during part of the window: its level is not defined there");
    }
    CHECK_FALSE(measurement(report, "duty").passed);
    CHECK(report.verdict == scenario::Verdict::Fail);
}

TEST_CASE("A window of one period or more is accepted: the measurement tells whether it caught two edges", "[scenario]") {
    // 62.5 kHz is 256 cycles per period. A 300-cycle window holds two rising edges or one, depending on its phase.
    auto document     = exampleWith("pwm_setpoint.hex");
    document["steps"] = Json::parse(R"([
        {"wait": "16112 cycles"},
        {"measure": {"id": "two-edges", "pin": "D6", "quantity": "frequency", "window": "300 cycles", "expect": {"value": 62500, "tolerance": 500}}},
        {"wait": "100 cycles"},
        {"measure": {"id": "one-edge", "pin": "D6", "quantity": "frequency", "window": "300 cycles", "expect": {"value": 62500, "tolerance": 500}}}
    ])");
    const auto report = prepared(document)->run();
    INFO(scenario::toJson(report).dump(2));
    const auto& caught = measurement(report, "two-edges");
    CHECK(caught.risingEdges == 2);
    CHECK(caught.passed);
    CHECK(caught.value.value_or(0) == Approx(62500).margin(500));
    const auto& missed = measurement(report, "one-edge");
    CHECK(missed.risingEdges == 1);
    CHECK_FALSE(missed.value);
    CHECK_FALSE(missed.passed);
    CHECK(missed.detail == "1 rising edge in the window: at least two are needed");
}

TEST_CASE("A component condition already met when the step begins is met at its start", "[scenario]") {
    // The 1-cycle wait ends inside an instruction: the core stands past the 1-cycle deadline of the next step, yet the
    // button has been released since reset.
    auto document     = exampleWith("pwm_setpoint.hex");
    document["steps"] = Json::parse(R"([
        {"wait": "1 cycles"},
        {"waitFor": {"component": "button-d2", "state": "pressed", "below": 0.5}, "timeout": "1 cycles"},
        {"set": {"component": "button-d2", "state": "pressed", "value": true}},
        {"waitFor": {"component": "button-d2", "state": "pressed", "above": 0.5}, "timeout": "10 cycles"}
    ])");
    const auto report = prepared(document)->run();
    INFO(scenario::toJson(report).dump(2));
    REQUIRE(report.verdict == scenario::Verdict::Pass);
    CHECK(report.steps.at(1).start == 1);
    CHECK(report.steps.at(1).end == 1);
    // A state set by a stimulus counts from the boundary where the stimulus took effect.
    CHECK(report.steps.at(3).end == report.steps.at(2).applied.value_or(0));
}

TEST_CASE("The edges at both ends of a measurement window belong to it", "[scenario]") {
    // D6 rises at 15 896 and 16 152 cycles: a closed window of exactly one period from the first edge holds both.
    for (const auto* quantity : {"frequency", "dutyCycle"}) {
        auto document     = exampleWith("pwm_setpoint.hex");
        document["steps"] = Json::parse(R"([{"wait": "15896 cycles"}])");
        const auto expect =
            std::string_view{
                quantity
        }
                    == "frequency"
                ? Json{{"value", 62500}, {"tolerance", 100}}
                : Json{{"value", 25}, {"tolerance", 0.5}};
        document["steps"].push_back(Json{
            {"measure", {{"id", "window"}, {"pin", "D6"}, {"quantity", quantity}, {"window", "256 cycles"}, {"expect", expect}}}
        });
        const auto report = prepared(document)->run();
        INFO(quantity << "\n" << scenario::toJson(report).dump(2));
        CHECK(measurement(report, "window").risingEdges == 2);
        CHECK(report.verdict == scenario::Verdict::Pass);
    }
}

TEST_CASE("Binary serial output is kept in the report, reversibly", "[scenario]") {
    // USART0 at 1 Mbaud (UBRR0 = 0) transmits 0xFF, 'o', 'k', then 0xC3 0xA9 ("é" in UTF-8).
    siliconforge::test::AvrAssembler program;
    program.ldi(16, 0x08).sts(0xC1, 16);             // UCSR0B: TXEN0
    for (const unsigned byte : {0xFFu, unsigned{'o'}, unsigned{'k'}, 0xC3u, 0xA9u}) {
        program.lds(17, 0xC0).sbrs(17, 5).rjmp(-4);  // Wait for UDRE0
        program.ldi(18, byte).sts(0xC6, 18);         // UDR0
    }
    program.rjmp(-1);
    auto document        = example();
    document["firmware"] = hexFile(program, "siliconforge-scenario-binary.hex").generic_string();
    document["initial"]  = Json::object();
    document["steps"]    = Json::parse(R"([{"waitFor": {"serial": "ok"}, "timeout": "1ms"}, {"wait": "100us"}])");
    const auto report    = prepared(document)->run();
    REQUIRE(report.verdict == scenario::Verdict::Pass);
    REQUIRE(report.serial.at(0) == std::string{"\xFFok\xC3\xA9"});

    const auto json = scenario::toJson(report, "trace.vcd");
    const auto text = json.dump(2);  // Throws on invalid UTF-8
    INFO(text);
    const auto& port = json["serial"][0];
    CHECK(port["port"] == 0);
    CHECK(port["bytes"] == 5);
    CHECK(port["base64"] == "/29rw6k=");
    CHECK(port["text"] == "\xEF\xBF\xBDok\xC3\xA9");  // U+FFFD for 0xFF, the valid "é" kept
}

TEST_CASE("Rising edges that span no time cannot be measured", "[scenario]") {
    // Three stimuli at cycle 0 give A0 two rising edges on the same cycle.
    auto document       = exampleWith("pwm_setpoint.hex");
    document["steps"]   = Json::parse(R"([
        {"set": {"probe": "A0", "volts": 5}},
        {"set": {"probe": "A0", "volts": 0}},
        {"set": {"probe": "A0", "volts": 5}},
        {"measure": {"id": "informative", "pin": "A0", "quantity": "frequency", "window": "100 cycles"}},
        {"measure": {"id": "expected", "pin": "A0", "quantity": "period", "window": "100 cycles", "expect": {"min": 0, "max": 1}}}
    ])");
    auto       scenario = prepared(document);
    const auto report   = scenario->run();
    const auto json     = scenario::toJson(report);
    INFO(json.dump(2));
    const auto& informative = measurement(report, "informative");
    REQUIRE(informative.risingEdges >= 2);
    CHECK_FALSE(informative.value);
    CHECK(informative.detail.contains("rising edges in the window all fall on cycle"));
    CHECK(json["measurements"][0]["detail"] == informative.detail);
    CHECK_FALSE(report.steps.at(3).detail.contains("inf Hz"));
    CHECK_FALSE(measurement(report, "expected").passed);
}
