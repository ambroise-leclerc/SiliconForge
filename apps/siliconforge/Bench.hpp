#pragma once

#include <nlohmann/json.hpp>
#include <siliconforge/scenario/Scenario.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

/// The bench of the window (ADR 0010, #60): the same engine and the same evidence as `siliconforge-bench`, on the
/// scenarios and runs of the project directory. The page names a scenario or a run from the lists below and never
/// gives a path, so that an interface served to other machines cannot read or write elsewhere.
namespace siliconforge::app::bench {

/// Where the scenarios of the project are looked for, relative to the working directory.
inline const std::filesystem::path scenarioDirectory{"scenarios"};
/// Where the runs of the project are kept, relative to the working directory: what the command line writes by default.
inline const std::filesystem::path runDirectory{std::filesystem::path{".siliconforge"} / "runs"};

/// Why a bench call was refused; `code` is one of the codes of WebBridge's error_code.
struct Refusal {
    std::string_view code;
    std::string      message;
};

/// A file name as the page may give it: one path component, no directory.
[[nodiscard]] inline bool plainName(std::string_view name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\") == std::string_view::npos;
}

/// Names the scenarios (`scenarios/*.json`) and the runs that hold a report, sorted.
[[nodiscard]] inline nlohmann::json list() {
    auto           scenarios = nlohmann::json::array();
    auto           runs      = nlohmann::json::array();
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it{scenarioDirectory, error}, end; !error && it != end; it.increment(error))
        if (it->path().extension() == ".json" && it->is_regular_file(error))
            files.push_back(it->path());
    std::ranges::sort(files);
    for (const auto& file : files)
        scenarios.push_back(file.filename().string());
    std::vector<std::filesystem::path> directories;
    error.clear();
    for (std::filesystem::directory_iterator it{runDirectory, error}, end; !error && it != end; it.increment(error))
        if (it->is_directory(error) && std::filesystem::exists(it->path() / "report.json", error))
            directories.push_back(it->path());
    std::ranges::sort(directories);
    for (const auto& directory : directories) {
        const auto evidence = scenario::readEvidence(directory);
        if (!evidence)
            continue;
        const auto report = nlohmann::json::parse(evidence->report, nullptr, false);
        runs.push_back({{"name", directory.filename().string()}, {"scenario", report.value("scenario", "")}, {"verdict", report.value("verdict", "")}});
    }
    return {{"scenarios", std::move(scenarios)}, {"runs", std::move(runs)}};
}

/// The evidence of a run as the page reads it: the report as the file holds it, and the trace.
[[nodiscard]] inline nlohmann::json describe(std::string_view name, const scenario::Evidence& evidence) {
    return {{"run", name},
            {"directory", (runDirectory / name).generic_string()},
            {"report", nlohmann::json::parse(evidence.report)},
            {"trace", evidence.trace ? nlohmann::json(*evidence.trace) : nlohmann::json(nullptr)}};
}

/// Opens the evidence kept for a run, whoever ran it.
[[nodiscard]] inline std::expected<nlohmann::json, Refusal> open(std::string_view name) {
    if (!plainName(name))
        return std::unexpected(Refusal{"siliconforge.unknownRun", std::string{"Unknown run: "} + std::string{name}});
    auto evidence = scenario::readEvidence(runDirectory / name);
    if (!evidence)
        return std::unexpected(Refusal{"siliconforge.unknownRun", evidence.error()});
    return describe(name, *evidence);
}

/// Runs a scenario of the project and keeps its evidence where the command line does, replacing the run of the same
/// scenario. `previous` is the report that was there before, `identical` whether this run reproduced it byte for byte:
/// the same inputs on the same engine give the same report, so a difference is a change of inputs or of behavior.
[[nodiscard]] inline std::expected<nlohmann::json, Refusal> run(std::string_view file) {
    const std::filesystem::path path = scenarioDirectory / std::string{file};
    if (!plainName(file) || !std::filesystem::is_regular_file(path))
        return std::unexpected(Refusal{"siliconforge.unknownScenario", std::string{"Unknown scenario: "} + std::string{file}});
    const auto directory = scenario::defaultEvidenceDirectory(path);
    const auto before    = scenario::readEvidence(directory);
    const auto report    = scenario::run(path);
    const auto kept      = scenario::keepEvidence(report, directory);
    if (!kept)
        return std::unexpected(Refusal{"siliconforge.evidenceNotKept", kept.error()});
    auto result = describe(directory.filename().string(), *scenario::readEvidence(directory));
    result["exitCode"]  = scenario::exitCode(report.verdict);
    result["previous"]  = before ? nlohmann::json::parse(before->report) : nlohmann::json(nullptr);
    result["identical"] = before && before->report == *kept;
    return result;
}

}  // namespace siliconforge::app::bench
