// siliconforge-bench: runs a bench scenario (ADR 0010) without a window and keeps the evidence of the run.

#include <siliconforge/scenario/Scenario.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>

using namespace siliconforge;

namespace {

/// Exit code of a usage or output error; 0, 1 and 2 are the verdicts (scenario::exitCode).
constexpr int usageError = 3;
/// Exit code of an error of the bench itself: an exception the engine did not turn into a verdict.
constexpr int internalError = 4;

void printUsage() {
    std::println(R"(Usage: siliconforge-bench run [--out <directory>] [--json] <scenario.json>
       siliconforge-bench check <scenario.json>

Runs a bench scenario on the emulated microcontroller of its circuit: the firmware is loaded, the stimuli applied at
their simulated time, the waits bounded and the measurements compared to their expected values.

Commands:
  run      Runs the scenario, prints its verdict and keeps report.json and trace.vcd
  check    Checks the scenario against the bench (circuit, firmware, references, capacity) without running it

Options:
  --out <directory>  Where run keeps its evidence (default .siliconforge/runs/<scenario name>)
  --json             Prints the report on stdout instead of the summary
  --help             This help

Exit codes:
  0  pass: every wait was satisfied in time and every measurement met its expectation
  1  fail: a measurement missed its expectation, a wait reached its timeout or the MCU stopped
  2  incompatible: the scenario cannot run on this bench (version, schema, wiring, resource, capacity, schedule)
  3  usage or output error
  4  internal error of the bench

Scenario format: schemas/scenario.schema.json, engine {} (scenario format {}).)",
                 scenario::engineVersion(),
                 scenario::formatVersion);
}

struct Options {
    std::string                          command;
    std::optional<std::filesystem::path> scenario;
    std::optional<std::filesystem::path> out;
    bool                                 json{false};
    bool                                 help{false};
};

std::optional<Options> parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--help" || argument == "-h")
            options.help = true;
        else if (argument == "--json")
            options.json = true;
        else if (argument == "--out") {
            if (++i >= argc)
                return std::nullopt;
            options.out = std::filesystem::path{argv[i]};
        } else if (argument.starts_with("-")) {
            std::println(stderr, "Unknown option: {}", argument);
            return std::nullopt;
        } else if (options.command.empty())
            options.command = argument;
        else if (!options.scenario)
            options.scenario = std::filesystem::path{argument};
        else {
            std::println(stderr, "Unexpected argument: {}", argument);
            return std::nullopt;
        }
    }
    if (options.help)
        return options;
    if ((options.command != "run" && options.command != "check") || !options.scenario)
        return std::nullopt;
    return options;
}

void printProblem(const std::filesystem::path& file, const scenario::Problem& problem) {
    const auto where = problem.file.empty() ? file.generic_string() : problem.file;
    std::println(stderr,
                 "{}{}{}: incompatible ({}): {}",
                 where,
                 problem.path.empty() ? "" : ":",
                 problem.path,
                 scenario::toString(problem.cause),
                 problem.message);
}

std::string expected(const scenario::Measurement& measurement) {
    if (!measurement.minimum)
        return {};
    return std::format(" (expected {} to {} {})", *measurement.minimum, *measurement.maximum, measurement.unit);
}

void printSummary(const scenario::Report& report, const std::filesystem::path& out) {
    std::println("{}: {}", report.name, scenario::toString(report.verdict));
    for (const auto& step : report.steps) {
        std::print("  {:<8} {:>3} {:<8} {}", std::format("[{}]", scenario::toString(step.outcome)), step.index, step.kind, step.label);
        if (!step.detail.empty())
            std::print("{}{}", step.label.empty() ? "" : " — ", step.detail);
        std::println("");
    }
    for (const auto& measurement : report.measurements) {
        const auto value = measurement.value ? std::format("{:.6g} {}", *measurement.value, measurement.unit) : std::string{"not measurable"};
        std::println("  {} {} {} on {}: {}{}",
                     measurement.passed ? "ok  " : "FAIL",
                     measurement.id,
                     measurement.quantity,
                     measurement.pin,
                     value,
                     expected(measurement));
    }
    std::println("Simulated {} s on {} at {} Hz", static_cast<double>(report.duration) / static_cast<double>(report.clockHz), report.mcu, report.clockHz);
    std::println("Report: {}", (out / "report.json").generic_string());
    std::println("Trace:  {}", (out / "trace.vcd").generic_string());
}

bool write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream{path, std::ios::binary};
    stream << text;
    return static_cast<bool>(stream.flush());
}


int execute(const Options& options) {
    const auto& file = *options.scenario;

    if (options.command == "check") {
        const auto prepared = scenario::Scenario::prepare(file);
        if (!prepared) {
            printProblem(file, prepared.error());
            return scenario::exitCode(scenario::Verdict::Incompatible);
        }
        std::println("{}: ready to run", (*prepared)->name());
        return 0;
    }

    const auto report = scenario::run(file);
    if (report.problem && !options.json)
        printProblem(file, *report.problem);

    const auto      out = options.out.value_or(std::filesystem::path{".siliconforge"} / "runs" / file.stem());
    std::error_code error;
    std::filesystem::create_directories(out, error);
    if (error) {
        std::println(stderr, "Cannot create {}: {}", out.generic_string(), error.message());
        return usageError;
    }
    // A trace left by an earlier run would not match this report, whatever this run produces.
    std::filesystem::remove(out / "trace.vcd", error);
    if (error) {
        std::println(stderr, "Cannot remove the trace of an earlier run, {}: {}", (out / "trace.vcd").generic_string(), error.message());
        return usageError;
    }
    const auto hasTrace = !report.trace.empty();
    // The report is valid UTF-8; replacing what would not be keeps the evidence even if a string slipped through.
    const auto json = scenario::toJson(report, hasTrace ? "trace.vcd" : "").dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    if (!write(out / "report.json", json) || (hasTrace && !write(out / "trace.vcd", report.trace))) {
        std::println(stderr, "Cannot write the evidence of the run in {}", out.generic_string());
        return usageError;
    }
    if (options.json)
        std::print("{}", json);
    else if (!report.problem)
        printSummary(report, out);
    return scenario::exitCode(report.verdict);
}

}  // namespace

int main(int argc, char** argv) {
    const auto options = parse(argc, argv);
    if (!options) {
        printUsage();
        return usageError;
    }
    if (options->help) {
        printUsage();
        return 0;
    }
    try {
        return execute(*options);
    } catch (const std::exception& error) {
        std::println(stderr, "siliconforge-bench: internal error: {}", error.what());
        return internalError;
    }
}
