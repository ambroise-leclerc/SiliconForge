#pragma once

#include "LocalHttp.hpp"

#include <nlohmann/json.hpp>
#include <siliconforge/scenario/Scenario.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>

/// `siliconforge bench`: runs a bench scenario (ADR 0010) without a window and keeps the evidence of the run. Started in
/// the terminal of a window (#80), it hands the run to that window, which shows it in its Bench panel and loads the
/// circuit and the firmware of the scenario in the emulation the person sees; the output and the exit code are those of
/// a run made here.
namespace siliconforge::app::benchcommand {

/// Exit code of a usage or output error; 0, 1 and 2 are the verdicts (scenario::exitCode).
inline constexpr int usageError = 3;
/// Exit code of an error of the bench itself: an exception the engine did not turn into a verdict.
inline constexpr int internalError = 4;

inline void printUsage() {
    std::println(R"(Usage: siliconforge bench run [--out <directory>] [--json] <scenario.json>
       siliconforge bench check <scenario.json>

Runs a bench scenario on the emulated microcontroller of its circuit: the firmware is loaded, the stimuli applied at
their simulated time, the waits bounded and the measurements compared to their expected values.

Commands:
  run      Runs the scenario, prints its verdict and keeps report.json and trace.vcd
  check    Checks the scenario against the bench (circuit, firmware, references, capacity) without running it

Options:
  --out <directory>  Where run keeps its evidence (default .siliconforge/runs/<scenario name>)
  --json             Prints the report on stdout instead of the summary
  --help             This help

In the terminal of a SiliconForge window, run hands a scenario of the project (scenarios/*.json, without --out) to the
window: the run opens in its Bench panel, and the circuit and the firmware of the scenario are loaded in the emulation
it shows, unless the person keeps the controls. Elsewhere, or when the window does not answer, the scenario runs here.

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

/// The arguments after `bench`.
inline std::optional<Options> parse(int argc, char** argv) {
    Options options;
    for (int i = 0; i < argc; ++i) {
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

inline void printProblem(const std::filesystem::path& file, const scenario::Problem& problem) {
    const auto where = problem.file.empty() ? file.generic_string() : problem.file;
    std::println(stderr, "{}{}{}: incompatible ({}): {}", where, problem.path.empty() ? "" : ":", problem.path, scenario::toString(problem.cause),
                 problem.message);
}

/// The same, from the problem of a report.
inline void printProblem(const std::filesystem::path& file, const nlohmann::json& problem) {
    const auto stated = problem.value("file", std::string{});
    const auto path   = problem.value("path", std::string{});
    std::println(stderr, "{}{}{}: incompatible ({}): {}", stated.empty() ? file.generic_string() : stated, path.empty() ? "" : ":", path,
                 problem.value("cause", std::string{}), problem.value("message", std::string{}));
}

/// The summary of a run, read from its report: the same whether the run was made here or by the window.
inline void printSummary(const nlohmann::json& report, const std::filesystem::path& out) {
    std::println("{}: {}", report.value("scenario", std::string{}), report.value("verdict", std::string{}));
    for (const auto& step : report.value("steps", nlohmann::json::array())) {
        const auto label  = step.value("label", std::string{});
        const auto detail = step.value("detail", std::string{});
        std::print("  {:<8} {:>3} {:<8} {}", std::format("[{}]", step.value("outcome", std::string{})), step.value("index", 0u), step.value("kind", std::string{}),
                   label);
        if (!detail.empty())
            std::print("{}{}", label.empty() ? "" : " — ", detail);
        std::println("");
    }
    for (const auto& measurement : report.value("measurements", nlohmann::json::array())) {
        const auto  unit  = measurement.value("unit", std::string{});
        const auto& value = measurement.at("value");
        std::string expected;
        if (measurement.contains("expected"))
            expected = std::format(" (expected {} to {} {})", measurement.at("expected").at("min").get<double>(),
                                   measurement.at("expected").at("max").get<double>(), unit);
        std::println("  {} {} {} on {}: {}{}", measurement.value("verdict", std::string{}) == "pass" ? "ok  " : "FAIL", measurement.value("id", std::string{}),
                     measurement.value("quantity", std::string{}), measurement.value("pin", std::string{}),
                     value.is_number() ? std::format("{:.6g} {}", value.get<double>(), unit) : std::string{"not measurable"}, expected);
    }
    const auto& mcu     = report.at("mcu");
    const auto  clockHz = mcu.at("clockHz").get<u64>();
    std::println("Simulated {} s on {} at {} Hz", static_cast<double>(report.at("duration").at("cycles").get<Cycles>()) / static_cast<double>(clockHz),
                 mcu.value("name", std::string{}), clockHz);
    std::println("Report: {}", (out / "report.json").generic_string());
    std::println("Trace:  {}", (out / "trace.vcd").generic_string());
}

/// Prints the run whose report is `text` as the options ask, and returns its exit code.
inline int report(const Options& options, const std::string& text, const std::filesystem::path& out) {
    const auto parsed = nlohmann::json::parse(text);
    if (options.json)
        std::print("{}", text);
    else if (parsed.contains("problem"))
        printProblem(*options.scenario, parsed.at("problem"));
    else
        printSummary(parsed, out);
    const auto verdict = parsed.value("verdict", std::string{});
    return verdict == "pass" ? 0 : verdict == "fail" ? 1 : 2;
}

/// The window of the terminal the command runs in, when it is one and the scenario is one of its project's.
struct Window {
    localhttp::Url        url;
    std::string           token;
    std::filesystem::path project;
    std::string           scenario;  ///< The file name in scenarios/.
};

inline std::optional<Window> window(const Options& options) {
    const auto* url     = std::getenv("SILICONFORGE_MCP_URL");
    const auto* token   = std::getenv("SILICONFORGE_MCP_TOKEN");
    const auto* project = std::getenv("SILICONFORGE_PROJECT");
    if (options.out || !url || !token || !project || !*url || !*token || !*project)
        return std::nullopt;  // --out asks for evidence elsewhere than where the window keeps it.
    auto parsed = localhttp::parse(url);
    if (!parsed)
        return std::nullopt;
    std::error_code error;
    const auto      file      = std::filesystem::weakly_canonical(*options.scenario, error);
    const auto      scenarios = std::filesystem::weakly_canonical(std::filesystem::path{project} / "scenarios", error);
    if (error || file.parent_path() != scenarios || file.extension() != ".json")
        return std::nullopt;
    return Window{*parsed, token, std::filesystem::path{project}, file.filename().string()};
}

/// Hands the run to the window. Returns nothing when the window could not take it: the scenario then runs here.
inline std::optional<int> runInWindow(const Options& options, const Window& target) {
    const nlohmann::json call{{"jsonrpc", "2.0"},
                              {"id", 1},
                              {"method", "tools/call"},
                              {"params", {{"name", "bench_run"}, {"arguments", {{"scenario", target.scenario}, {"load", true}}}}}};
    const auto answer = localhttp::post(target.url, target.token, call.dump());
    if (!answer || answer->status != 200) {
        std::println(stderr, "The window does not answer ({}): the scenario runs here.", answer ? std::format("HTTP {}", answer->status) : answer.error());
        return std::nullopt;
    }
    const auto parsed = nlohmann::json::parse(answer->body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("result")) {
        std::println(stderr, "The window gave no result: the scenario runs here.");
        return std::nullopt;
    }
    const auto& result = parsed.at("result");
    if (result.value("isError", false)) {
        std::println(stderr, "The window refused the run: {}", result.at("content").at(0).value("text", std::string{}));
        return usageError;
    }
    const auto& content   = result.at("structuredContent");
    const auto  directory = std::filesystem::relative(target.project / ".siliconforge" / "runs" / content.at("run").get<std::string>());
    const auto  text      = scenario::readReport(directory);
    if (!text) {
        std::println(stderr, "{}", text.error());
        return usageError;
    }
    const auto code = report(options, *text, directory);
    if (!options.json) {
        if (const auto& loaded = content.value("loaded", nlohmann::json{}); loaded.is_object())
            std::println("Window: {} loaded and running, run {} opened in the Bench panel", loaded.value("firmware", std::string{}), content.at("run").get<std::string>());
        else
            std::println("Window: run {} opened in the Bench panel; {}", content.at("run").get<std::string>(), content.value("notLoaded", std::string{"nothing loaded"}));
    }
    return code;
}

inline int execute(const Options& options) {
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
    if (const auto target = window(options))
        if (const auto code = runInWindow(options, *target))
            return *code;

    const auto run  = scenario::run(file);
    const auto out  = options.out.value_or(scenario::defaultEvidenceDirectory(file));
    const auto kept = scenario::keepEvidence(run, out);
    if (!kept) {
        std::println(stderr, "{}", kept.error());
        return usageError;
    }
    return report(options, *kept, out);
}

/// `siliconforge bench <arguments>`: argv[0] is the first argument after `bench`.
inline int main(int argc, char** argv) {
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
        std::println(stderr, "siliconforge bench: internal error: {}", error.what());
        return internalError;
    }
}

}  // namespace siliconforge::app::benchcommand
