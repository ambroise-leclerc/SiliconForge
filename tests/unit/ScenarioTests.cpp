// Bench scenarios (ADR 0010, #57, #58, #59): a versioned file checked before it runs, executed on the emulated
// ATmega328P with stimuli at their cycle and bounded waits, measured on the captured edges, and reported the same way
// every time it runs on the same inputs.

#include "scenario/Sha256.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/scenario/Scenario.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    CHECK(report.trace.contains("$timescale 62500 ps $end"));
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
        document["steps"][2]["measure"]["window"] = "20us";
        expectProblem(document, Capacity, "/steps/2/measure/expect", "cannot hold the two rising edges");
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
