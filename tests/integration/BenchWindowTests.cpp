// The bench of the window (ADR 0010, #60): the engine of siliconforge-bench on the scenarios and runs of the project,
// reading and writing the evidence the command line keeps.

#include "../../apps/siliconforge/Bench.hpp"
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace siliconforge;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const fs::path sourceDir{SILICONFORGE_SOURCE_DIR};

std::string text(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

/// A project directory with the example scenario, a variant that cannot pass, and what they refer to; the working
/// directory while it lives, because the bench looks for scenarios and runs there.
class Project {
public:
    Project() : root{fs::temp_directory_path() / "siliconforge-bench-window"}, previous{fs::current_path()} {
        fs::remove_all(root);
        fs::create_directories(root / "scenarios");
        fs::create_directories(root / "circuits");
        fs::create_directories(root / "firmware" / "prebuilt");
        fs::copy_file(sourceDir / "circuits" / "arduino-demo.json", root / "circuits" / "arduino-demo.json");
        fs::copy_file(sourceDir / "firmware" / "prebuilt" / "pwm_setpoint.hex", root / "firmware" / "prebuilt" / "pwm_setpoint.hex");
        fs::copy_file(sourceDir / "scenarios" / "pwm-setpoint.json", root / "scenarios" / "pwm-setpoint.json");
        auto document = Json::parse(text(sourceDir / "scenarios" / "pwm-setpoint.json"));
        document["name"] = "PWM setpoint that cannot hold";
        document["steps"][1]["measure"]["expect"] = {{"value", 50}, {"tolerance", 0.5}};
        std::ofstream{root / "scenarios" / "off-target.json"} << document.dump(2);
        fs::current_path(root);
    }
    ~Project() {
        std::error_code error;
        fs::current_path(previous, error);
        fs::remove_all(root, error);
    }
    Project(const Project&)            = delete;
    Project& operator=(const Project&) = delete;

    fs::path root;

private:
    fs::path previous;
};

}  // namespace

SCENARIO("The window lists the scenarios and the runs of the project", "[app][bench]") {
    GIVEN("a project with two scenarios and no run") {
        Project project;
        THEN("the scenarios are named and no run is offered") {
            const auto listing = app::bench::list();
            CHECK(listing.at("scenarios") == Json::array({"off-target.json", "pwm-setpoint.json"}));
            CHECK(listing.at("runs").empty());
        }
        WHEN("the command line has run one of them") {
            const auto report = scenario::run(fs::path{"scenarios"} / "pwm-setpoint.json");
            REQUIRE(scenario::keepEvidence(report, scenario::defaultEvidenceDirectory("scenarios/pwm-setpoint.json")));
            THEN("the run is offered with its verdict") {
                const auto runs = app::bench::list().at("runs");
                REQUIRE(runs.size() == 1);
                CHECK(runs[0].at("name") == "pwm-setpoint");
                CHECK(runs[0].at("scenario") == "PWM setpoint on D6");
                CHECK(runs[0].at("verdict") == "pass");
            }
        }
    }
}

SCENARIO("The window reruns a scenario and compares it with the run of the command line", "[app][bench]") {
    GIVEN("a scenario the command line has run") {
        Project    project;
        const auto file      = fs::path{"scenarios"} / "pwm-setpoint.json";
        const auto directory = scenario::defaultEvidenceDirectory(file);
        const auto cli       = scenario::keepEvidence(scenario::run(file), directory);
        REQUIRE(cli);
        WHEN("the window runs it on the same inputs") {
            const auto result = app::bench::run("pwm-setpoint.json");
            REQUIRE(result);
            THEN("it passes and reproduces the report of the command line byte for byte") {
                CHECK(result->at("report").at("verdict") == "pass");
                CHECK(result->at("exitCode") == 0);
                CHECK(result->at("identical") == true);
                CHECK(result->at("previous") == result->at("report"));
                CHECK(text(directory / "report.json") == *cli);
            }
            THEN("it keeps its evidence where the command line keeps it") {
                CHECK(result->at("directory") == ".siliconforge/runs/pwm-setpoint");
                CHECK(result->at("trace").get<std::string>() == text(directory / "trace.vcd"));
            }
        }
        WHEN("a firmware change alters the run before the window repeats it") {
            auto stale = Json::parse(*cli);
            stale["inputs"][2]["sha256"] = "0";
            std::ofstream{directory / "report.json"} << stale.dump(2);
            const auto result = app::bench::run("pwm-setpoint.json");
            REQUIRE(result);
            THEN("the earlier report is returned with the difference flagged") {
                CHECK(result->at("identical") == false);
                CHECK(result->at("previous").at("inputs")[2].at("sha256") == "0");
                CHECK(result->at("report").at("inputs")[2].at("sha256") != "0");
            }
        }
    }
    GIVEN("a scenario that never ran") {
        Project project;
        WHEN("the window runs it") {
            const auto result = app::bench::run("pwm-setpoint.json");
            REQUIRE(result);
            THEN("there is nothing to compare with") {
                CHECK(result->at("previous").is_null());
                CHECK(result->at("identical") == false);
            }
        }
    }
}

SCENARIO("The window opens the evidence of a failed run", "[app][bench]") {
    GIVEN("a scenario whose measurement misses its expectation, run by the command line") {
        Project    project;
        const auto file = fs::path{"scenarios"} / "off-target.json";
        REQUIRE(scenario::keepEvidence(scenario::run(file), scenario::defaultEvidenceDirectory(file)));
        WHEN("the window opens it") {
            const auto opened = app::bench::open("off-target");
            REQUIRE(opened);
            const auto& report = opened->at("report");
            THEN("the verdict, the stimulus, the measurement with its expectation and the trace are there") {
                CHECK(report.at("verdict") == "fail");
                CHECK(report.at("steps")[3].at("detail") == "button-d2.pressed = 1");
                const auto& measurement = report.at("measurements")[0];
                CHECK(measurement.at("id") == "duty-released");
                CHECK(measurement.at("verdict") == "fail");
                CHECK(measurement.at("value").get<double>() > 24.0);
                CHECK(measurement.at("expected").at("min").get<double>() == 49.5);
                CHECK(opened->at("trace").get<std::string>().starts_with("$"));
            }
        }
        WHEN("the window runs it again") {
            const auto result = app::bench::run("off-target.json");
            REQUIRE(result);
            THEN("it fails the same way, with the exit code of the command line") {
                CHECK(result->at("exitCode") == 1);
                CHECK(result->at("identical") == true);
            }
        }
    }
}

SCENARIO("The window refuses what the project does not hold", "[app][bench]") {
    GIVEN("a project and a run") {
        Project project;
        WHEN("the page names something that is not a scenario or a run of the project") {
            THEN("each call refuses with its code, and names with a path are never followed") {
                for (const auto* name : {"missing.json", "../pwm-setpoint.json", "scenarios/pwm-setpoint.json", ".."}) {
                    const auto run = app::bench::run(name);
                    REQUIRE_FALSE(run);
                    CHECK(run.error().code == "siliconforge.unknownScenario");
                }
                for (const auto* name : {"missing", "../scenarios", "a/b", ""}) {
                    const auto opened = app::bench::open(name);
                    REQUIRE_FALSE(opened);
                    CHECK(opened.error().code == "siliconforge.unknownRun");
                }
            }
        }
        WHEN("a scenario cannot run on this bench") {
            std::ofstream{project.root / "scenarios" / "broken.json"} << "{\"formatVersion\": 99}";
            const auto result = app::bench::run("broken.json");
            REQUIRE(result);
            THEN("the evidence says incompatible, with the exit code of the command line") {
                CHECK(result->at("report").at("verdict") == "incompatible");
                CHECK(result->at("exitCode") == 2);
                CHECK(result->at("trace").is_null());
            }
        }
    }
}
