#pragma once

#include <nlohmann/json.hpp>
#include <siliconforge/core/Types.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Bench scenarios (ADR 0010): a versioned file references a circuit and a firmware, fixes the initial state, then
/// orders stimuli, bounded waits and measurements. The command line (`siliconforge-bench`) and the window run them
/// through this engine, so both produce the same report from the same inputs.
namespace siliconforge::scenario {

/// The scenario format this engine reads (`formatVersion`).
inline constexpr int formatVersion = 1;

/// Version of the engine, recorded in every report.
[[nodiscard]] std::string_view engineVersion() noexcept;

/// One input of a run, identified by the SHA-256 of its bytes.
struct Input {
    std::string role;  ///< "scenario", "circuit" or "firmware".
    std::string path;  ///< As given: on the command line for the scenario, in the scenario for the others.
    std::string sha256;
};

/// Why a scenario cannot run on this bench. Found before the run starts, except a step scheduled before the time
/// the previous steps need at least.
struct Problem {
    enum class Cause {
        Unreadable,  ///< A file cannot be read, or is not JSON.
        Version,     ///< Unknown `formatVersion`.
        Schema,      ///< The scenario breaks schemas/scenario.schema.json or one of its rules.
        Wiring,      ///< The circuit is invalid or does not fit the chip.
        Resource,    ///< Something the scenario refers to is missing: MCU, firmware, component, state, pin, port.
        Capacity,    ///< A request is beyond what the bench can do or measure.
        Schedule,    ///< A step is scheduled before the previous steps can have finished.
    };
    Cause       cause;
    std::string file;  ///< The file at fault, as the scenario names it; empty for the scenario itself.
    std::string path;  ///< JSON Pointer in that file, empty for a whole-file problem.
    std::string message;
    /// The inputs read before the problem was found, so that a refusal identifies what produced it.
    std::vector<Input> inputs;
};
[[nodiscard]] std::string_view toString(Problem::Cause cause) noexcept;

/// What a run concluded. The command line exits with its exitCode().
enum class Verdict {
    Pass,          ///< Every wait was satisfied in time and every measurement met its expectation.
    Fail,          ///< A measurement missed its expectation, a wait reached its timeout, or the MCU stopped.
    Incompatible,  ///< The scenario cannot run on this bench: see Report::problem.
};
[[nodiscard]] std::string_view toString(Verdict verdict) noexcept;
/// 0 pass, 1 fail, 2 incompatible bench.
[[nodiscard]] int exitCode(Verdict verdict) noexcept;

/// A measurement on the edges captured during its window, with the unit of its quantity.
struct Measurement {
    std::string           id;
    std::string           label;
    std::string           pin;
    std::string           quantity;  ///< "dutyCycle" (%), "frequency" (Hz) or "period" (s).
    std::string           unit;
    std::optional<double> value;     ///< nullopt when the window does not allow the measurement; see `detail`.
    Cycles                from{0};
    Cycles                to{0};
    unsigned              risingEdges{0};
    std::optional<double> minimum;  ///< Expected bounds, when the scenario gives them.
    std::optional<double> maximum;
    bool                  passed{true};
    std::string           detail;
};

/// What one step did.
struct StepRecord {
    enum class Outcome { Done, Failed, Skipped };
    std::size_t index{0};
    std::string kind;  ///< "wait", "set", "waitFor" or "measure".
    std::string label;
    Outcome     outcome{Outcome::Skipped};
    /// On the scheduled timeline: `at`, or the end of the previous step. A run is not delayed by the cycles the core
    /// spends finishing an instruction past a target.
    Cycles start{0};
    /// Also on the timeline: start + duration for wait and measure, the cycle a waitFor condition was met or its
    /// deadline, start for set.
    Cycles end{0};
    /// set: the instruction boundary where the stimulus took effect, the first at or after `start`.
    std::optional<Cycles> applied;
    std::string           detail;
};
[[nodiscard]] std::string_view toString(StepRecord::Outcome outcome) noexcept;

/// Everything a run leaves: verdict, inputs, measurements, steps and the trace, even when an assertion failed.
struct Report {
    Verdict                  verdict{Verdict::Incompatible};
    std::optional<Problem>   problem;
    std::string              name;
    std::vector<Input>       inputs;
    std::string              mcu;
    u64                      clockHz{0};
    Cycles                   duration{0};
    std::vector<StepRecord>  steps;
    std::vector<Measurement> measurements;
    std::vector<std::string> serial;  ///< What the MCU transmitted on each serial port.
    std::string              trace;   ///< Value Change Dump of every pin, one time unit per clock cycle; empty when nothing ran.
};

/// The report as JSON (`formatVersion` 1). `traceFile` is recorded as the place of the trace, when it was saved.
[[nodiscard]] nlohmann::json toJson(const Report& report, std::string_view traceFile = {});

/// A scenario checked against the bench: circuit, MCU and firmware loaded, every reference resolved, every request
/// within capacity. run() executes it once; prepare it again for another run.
class Scenario {
public:
    /// Reads and checks a scenario file. Its circuit and firmware paths are relative to its directory.
    [[nodiscard]] static std::expected<std::unique_ptr<Scenario>, Problem> prepare(const std::filesystem::path& file);
    /// The same from scenario text, with `directory` for the relative paths and `name` for the report.
    [[nodiscard]] static std::expected<std::unique_ptr<Scenario>, Problem>
    prepare(std::string_view text, const std::filesystem::path& directory, std::string name);

    ~Scenario();
    Scenario(const Scenario&)            = delete;
    Scenario& operator=(const Scenario&) = delete;

    [[nodiscard]] const std::string& name() const noexcept;

    /// A fresh MCU in a fresh circuit with the initial state, then the steps in order: each run starts from the same
    /// state, so two runs give the same report.
    [[nodiscard]] Report run();

    struct State;  ///< Implementation.

private:
    Scenario();
    std::unique_ptr<State> state;
};

/// prepare() then run(); a problem gives an Incompatible report that names it.
[[nodiscard]] Report run(const std::filesystem::path& file);

}  // namespace siliconforge::scenario
