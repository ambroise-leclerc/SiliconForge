// The bench of the window (ADR 0010, #60): the engine of siliconforge bench on the scenarios and runs of the project,
// reading and writing the evidence the command line keeps.

#include "../../apps/siliconforge/Bench.hpp"
#include <webfront/testing/Bdd.hpp>

#include <filesystem>
#include <format>
#include <random>
#include <fstream>
#include <iterator>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

using namespace webfront::bdd;
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
    Project() : root{fs::temp_directory_path() / std::format("siliconforge-bench-window-{:x}", std::random_device{}())}, previous{fs::current_path()} {
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

const Scenario theWindowListsTheScenariosAndThe{"The window lists the scenarios and the runs of the project", {"app", "bench"}, [] {
    given("a project with two scenarios and no run", [&] {
        Project project;
        then("the scenarios are named and no run is offered", [&] {
            const auto listing = app::bench::list();
            check(listing.at("scenarios") == Json::array({"off-target.json", "pwm-setpoint.json"}));
            check(listing.at("runs").empty());
        });
        when("the command line has run one of them", [&] {
            const auto report = scenario::run(fs::path{"scenarios"} / "pwm-setpoint.json");
            require(scenario::keepEvidence(report, scenario::defaultEvidenceDirectory("scenarios/pwm-setpoint.json")));
            then("the run is offered with its verdict", [&] {
                const auto runs = app::bench::list().at("runs");
                require(runs.size() == 1);
                check(runs[0].at("name") == "pwm-setpoint");
                check(runs[0].at("scenario") == "PWM setpoint on D6");
                check(runs[0].at("verdict") == "pass");
            });
        });
    });
}};

const Scenario theWindowRerunsAScenarioAndCompares{"The window reruns a scenario and compares it with the run of the command line", {"app", "bench"}, [] {
    given("a scenario the command line has run", [&] {
        Project    project;
        const auto file      = fs::path{"scenarios"} / "pwm-setpoint.json";
        const auto directory = scenario::defaultEvidenceDirectory(file);
        const auto cli       = scenario::keepEvidence(scenario::run(file), directory);
        require(cli);
        when("the window runs it on the same inputs", [&] {
            const auto result = app::bench::run("pwm-setpoint.json");
            require(result);
            then("it passes and reproduces the report of the command line byte for byte", [&] {
                check(result->at("report").at("verdict") == "pass");
                check(result->at("exitCode") == 0);
                check(result->at("identical") == true);
                check(result->at("previous") == result->at("report"));
                check(text(directory / "report.json") == *cli);
            });
            then("it keeps its evidence where the command line keeps it", [&] {
                check(result->at("directory") == ".siliconforge/runs/pwm-setpoint");
                check(result->at("trace").get<std::string>() == text(directory / "trace.vcd"));
            });
        });
        when("a firmware change alters the run before the window repeats it", [&] {
            auto stale = Json::parse(*cli);
            stale["inputs"][2]["sha256"] = "0";
            std::ofstream{directory / "report.json"} << stale.dump(2);
            const auto result = app::bench::run("pwm-setpoint.json");
            require(result);
            then("the earlier report is returned with the difference flagged", [&] {
                check(result->at("identical") == false);
                check(result->at("previous").at("inputs")[2].at("sha256") == "0");
                check(result->at("report").at("inputs")[2].at("sha256") != "0");
            });
        });
    });
    given("a scenario that never ran", [&] {
        Project project;
        when("the window runs it", [&] {
            const auto result = app::bench::run("pwm-setpoint.json");
            require(result);
            then("there is nothing to compare with", [&] {
                check(result->at("previous").is_null());
                check(result->at("identical") == false);
            });
        });
    });
}};

const Scenario theWindowOpensTheEvidenceOfA{"The window opens the evidence of a failed run", {"app", "bench"}, [] {
    given("a scenario whose measurement misses its expectation, run by the command line", [&] {
        Project    project;
        const auto file = fs::path{"scenarios"} / "off-target.json";
        require(scenario::keepEvidence(scenario::run(file), scenario::defaultEvidenceDirectory(file)));
        when("the window opens it", [&] {
            const auto opened = app::bench::open("off-target");
            require(opened);
            const auto& report = opened->at("report");
            then("the verdict, the stimulus, the measurement with its expectation and the trace are there", [&] {
                check(report.at("verdict") == "fail");
                check(report.at("steps")[3].at("detail") == "button-d2.pressed = 1");
                const auto& measurement = report.at("measurements")[0];
                check(measurement.at("id") == "duty-released");
                check(measurement.at("verdict") == "fail");
                check(measurement.at("value").get<double>() > 24.0);
                check(measurement.at("expected").at("min").get<double>() == 49.5);
                check(opened->at("trace").get<std::string>().starts_with("$"));
            });
        });
        when("the window runs it again", [&] {
            const auto result = app::bench::run("off-target.json");
            require(result);
            then("it fails the same way, with the exit code of the command line", [&] {
                check(result->at("exitCode") == 1);
                check(result->at("identical") == true);
            });
        });
    });
}};

const Scenario theWindowRefusesWhatTheProjectDoes{"The window refuses what the project does not hold", {"app", "bench"}, [] {
    given("a project and a run", [&] {
        Project project;
        when("the page names something that is not a scenario or a run of the project", [&] {
            then("each call refuses with its code, and names with a path are never followed", [&] {
                for (const auto* name : {"missing.json", "../pwm-setpoint.json", "scenarios/pwm-setpoint.json", ".."}) {
                    const auto run = app::bench::run(name);
                    require(!run);
                    check(run.error().code == "siliconforge.unknownScenario");
                }
                for (const auto* name : {"missing", "../scenarios", "a/b", ""}) {
                    const auto opened = app::bench::open(name);
                    require(!opened);
                    check(opened.error().code == "siliconforge.unknownRun");
                }
            });
        });
        when("a scenario cannot run on this bench", [&] {
            std::ofstream{project.root / "scenarios" / "broken.json"} << "{\"formatVersion\": 99}";
            const auto result = app::bench::run("broken.json");
            require(result);
            then("the evidence says incompatible, with the exit code of the command line", [&] {
                check(result->at("report").at("verdict") == "incompatible");
                check(result->at("exitCode") == 2);
                check(result->at("trace").is_null());
            });
        });
    });
}};

namespace {

/// Makes a symbolic link, or skips the case where the platform does not allow it.
void link(const fs::path& target, const fs::path& at) {
    std::error_code error;
    fs::create_directories(at.parent_path(), error);
    fs::create_symlink(target, at, error);
    if (error)
        skip("Cannot create a symbolic link: ", error.message());
}

}  // namespace

const Scenario theWindowDoesNotFollowLinksOut{"The window does not follow links out of the project", {"app", "bench"}, [] {
    given("a project and a file outside it", [&] {
        Project       project;
        const auto    outside = project.root.parent_path() / (project.root.filename().string() + "-outside");
        fs::remove_all(outside);
        fs::create_directories(outside);
        std::ofstream{outside / "secret.txt"} << "secret";
        const auto file      = fs::path{"scenarios"} / "pwm-setpoint.json";
        const auto directory = scenario::defaultEvidenceDirectory(file);
        require(scenario::keepEvidence(scenario::run(file), directory));

        when("trace.vcd of a run is a link to that file", [&] {
            fs::remove(directory / "trace.vcd");
            link(outside / "secret.txt", directory / "trace.vcd");
            const auto opened = app::bench::open("pwm-setpoint");
            then("the run is not opened, and nothing of the file is returned", [&] {
                require(!opened);
                check(opened.error().message.find("secret") == std::string::npos);
            });
        });
        when("report.json of a run is a link to that file", [&] {
            fs::remove(directory / "report.json");
            link(outside / "secret.txt", directory / "report.json");
            then("the run is not opened", [&] {
                require(!app::bench::open("pwm-setpoint"));
            });
            andThen("running the scenario again replaces the link by a plain file and leaves the target alone", [&] {
                const auto result = app::bench::run("pwm-setpoint.json");
                require(result);
                check(text(outside / "secret.txt") == "secret");
                check(!fs::is_symlink(fs::symlink_status(directory / "report.json")));
                check(text(directory / "report.json").starts_with("{"));
            });
        });
        when("the directory of a run is a link to a directory outside the project", [&] {
            fs::remove_all(directory);
            link(outside, directory);
            then("the run is neither opened, listed nor written to", [&] {
                check(!app::bench::open("pwm-setpoint"));
                check(app::bench::list().at("runs").empty());
                const auto result = app::bench::run("pwm-setpoint.json");
                require(!result);
                check(result.error().code == "siliconforge.evidenceNotKept");
                check(!fs::exists(outside / "report.json"));
            });
        });
        when(".siliconforge/runs itself is a link outside the project", [&] {
            fs::remove_all(fs::path{".siliconforge"});
            link(outside, fs::path{".siliconforge"} / "runs");
            then("nothing is written through it", [&] {
                const auto result = app::bench::run("pwm-setpoint.json");
                require(!result);
                check(!fs::exists(outside / "pwm-setpoint"));
            });
        });
        when("a scenario is a link to a file outside the project", [&] {
            fs::copy_file(sourceDir / "scenarios" / "pwm-setpoint.json", outside / "elsewhere.json");
            link(outside / "elsewhere.json", fs::path{"scenarios"} / "elsewhere.json");
            then("it is neither listed nor run", [&] {
                const auto names = app::bench::list().at("scenarios");
                check(names == Json::array({"off-target.json", "pwm-setpoint.json"}));
                const auto result = app::bench::run("elsewhere.json");
                require(!result);
                check(result.error().code == "siliconforge.unknownScenario");
            });
        });
        when("report.json of a run is a hard link to a file outside the project", [&] {
            fs::remove(directory / "report.json");
            std::error_code error;
            fs::create_hard_link(outside / "secret.txt", directory / "report.json", error);
            if (error)
                skip("Cannot create a hard link: ", error.message());
            const auto result = app::bench::run("pwm-setpoint.json");
            then("the run keeps its evidence in a new file and the other file keeps its content", [&] {
                require(result);
                check(text(outside / "secret.txt") == "secret");
                check(text(directory / "report.json").starts_with("{"));
                check(fs::hard_link_count(directory / "report.json") == 1);
            });
        });
        fs::remove_all(outside);
    });
}};

#if defined(__unix__) || defined(__APPLE__)
const Scenario theWindowDoesNotWaitOnA{"The window does not wait on a special file in a run directory", {"app", "bench"}, [] {
    given("a run kept by the command line", [&] {
        Project    project;
        const auto file      = fs::path{"scenarios"} / "pwm-setpoint.json";
        const auto directory = scenario::defaultEvidenceDirectory(file);
        require(scenario::keepEvidence(scenario::run(file), directory));
        when("trace.vcd is a FIFO nobody writes to", [&] {
            fs::remove(directory / "trace.vcd");
            require(::mkfifo((directory / "trace.vcd").c_str(), 0600) == 0);
            const auto opened = app::bench::open("pwm-setpoint");
            then("the call returns, and the run is not opened since its trace is not the one its report names", [&] {
                check(!opened);
            });
        });
        when("report.json is a FIFO nobody writes to", [&] {
            fs::remove(directory / "report.json");
            require(::mkfifo((directory / "report.json").c_str(), 0600) == 0);
            then("the run is not opened and not listed, and the listing returns", [&] {
                check(!app::bench::open("pwm-setpoint"));
                check(app::bench::list().at("runs").empty());
            });
        });
    });
}};
#endif

const Scenario theEvidenceOfARunIsAlways{"The evidence of a run is always a report with its own trace", {"app", "bench"}, [] {
    given("a run kept by the command line", [&] {
        Project    project;
        const auto file      = fs::path{"scenarios"} / "pwm-setpoint.json";
        const auto directory = scenario::defaultEvidenceDirectory(file);
        const auto report    = scenario::run(file);
        const auto kept      = scenario::keepEvidence(report, directory);
        require(kept);
        const auto trace = text(directory / "trace.vcd");

        then("the report names the SHA-256 of its trace", [&] {
            const auto json = Json::parse(*kept);
            check(json.at("trace").at("sha256").get<std::string>().size() == 64);
        });
        when("the trace is not the one the report names, as between the two replacements of a run", [&] {
            std::ofstream{directory / "trace.vcd"} << "$comment another run $end\n";
            then("the run is not opened, rather than shown with another run's trace", [&] {
                const auto opened = app::bench::open("pwm-setpoint");
                require(!opened);
                check(opened.error().message.contains("does not match"));
            });
            andThen("the listing still shows it, since it reads the reports only", [&] {
                const auto runs = app::bench::list().at("runs");
                require(runs.size() == 1);
                check(runs[0].at("verdict") == "pass");
            });
        });
        when("the trace of the new run cannot be written", [&] {
            fs::create_directories(directory / "trace.vcd.new" / "in-the-way");
            auto other = report;
            other.name = "Another run";
            const auto result = scenario::keepEvidence(other, directory);
            then("the earlier report and trace are both left as they were", [&] {
                require(!result);
                check(text(directory / "report.json") == *kept);
                check(text(directory / "trace.vcd") == trace);
                check(app::bench::open("pwm-setpoint"));
            });
        });
        when("a run that keeps no trace replaces it", [&] {
            std::ofstream{project.root / "scenarios" / "broken.json"} << "{\"formatVersion\": 99}";
            const auto incompatible = scenario::run(fs::path{"scenarios"} / "broken.json");
            require(scenario::keepEvidence(incompatible, directory));
            then("the earlier trace is removed, and one put back is not taken for its own", [&] {
                check(!fs::exists(directory / "trace.vcd"));
                std::ofstream{directory / "trace.vcd"} << trace;
                const auto opened = app::bench::open("pwm-setpoint");
                require(opened);
                check(opened->at("trace").is_null());
            });
        });
    });
}};
