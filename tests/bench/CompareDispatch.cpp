// Linux dispatch benchmark: balanced randomized blocks, all samples retained, paired bootstrap intervals.
// Three builds of siliconforge (baseline, large, compact) run the same firmware headless at full speed, in every order
// within each group of six blocks; the emulated MHz they report are compared (docs/MCU-DESCRIPTIONS.md).
#include <nlohmann/json.hpp>

#include <sched.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <print>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern char** environ;

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr std::array names{"baseline", "large", "compact"};
constexpr const char* events = "cycles:u,instructions:u,branches:u,branch-misses:u,L1-dcache-loads:u,L1-dcache-load-misses:u";
constexpr std::string_view usage =
    "usage: siliconforge_compare_dispatch --baseline BASELINE --large LARGE --compact COMPACT --firmware FIRMWARE --output "
    "OUTPUT [--cpu CPU] [--blocks BLOCKS] [--seconds SECONDS] [--warmup-seconds WARMUP_SECONDS] [--seed SEED] "
    "[--resamples RESAMPLES] [--perf] [--perf-repeats PERF_REPEATS]";

[[noreturn]] void usageError(const std::string& message) {
    std::println(stderr, "{}\nsiliconforge_compare_dispatch: error: {}", usage, message);
    std::exit(2);
}

// --- Statistics, as Python's statistics module computes them. ---

double median(std::vector<double> values) {
    std::ranges::sort(values);
    const auto n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}

double mean(const std::vector<double>& values) { return std::reduce(values.begin(), values.end()) / static_cast<double>(values.size()); }

double stdev(const std::vector<double>& values) {
    const double m = mean(values);
    double sum = 0;
    for (double v : values)
        sum += (v - m) * (v - m);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

double percentile(std::vector<double> values, double fraction) {
    std::ranges::sort(values);
    const double position = static_cast<double>(values.size() - 1) * fraction;
    const auto lo = static_cast<std::size_t>(position);
    const auto hi = std::min(lo + 1, values.size() - 1);
    return values[lo] + (values[hi] - values[lo]) * (position - static_cast<double>(lo));
}

/// A uniform index below `n`, independent of the standard library's distributions, so a seed gives the same schedule
/// everywhere.
std::size_t below(std::mt19937_64& random, std::size_t n) {
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() - std::numeric_limits<std::uint64_t>::max() % n;
    std::uint64_t value;
    do
        value = random();
    while (value >= limit);
    return static_cast<std::size_t>(value % n);
}

// --- SHA-256 of the binaries, recorded with the measures (FIPS 180-4). ---

std::string sha256(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    if (!file.eof() && !file)
        throw std::runtime_error("cannot read " + path.string());
    static constexpr std::array<std::uint32_t, 64> k{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56)
        data.push_back(0);
    for (int i = 7; i >= 0; --i)
        data.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    for (std::size_t chunk = 0; chunk < data.size(); chunk += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i)
            w[i] = static_cast<std::uint32_t>(data[chunk + 4 * i]) << 24 | static_cast<std::uint32_t>(data[chunk + 4 * i + 1]) << 16 |
                   static_cast<std::uint32_t>(data[chunk + 4 * i + 2]) << 8 | data[chunk + 4 * i + 3];
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, hh] = h;
        for (std::size_t i = 0; i < 64; ++i) {
            const auto t1 = hh + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const auto t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        const std::array<std::uint32_t, 8> add{a, b, c, d, e, f, g, hh};
        for (std::size_t i = 0; i < 8; ++i)
            h[i] += add[i];
    }
    std::string hex;
    for (auto word : h)
        hex += std::format("{:08x}", word);
    return hex;
}

// --- Processes. ---

struct Run {
    int returncode;
    std::string out, err;
};

/// Runs `command` without a shell, its standard input closed, and collects both streams to the end.
Run run(const std::vector<std::string>& command) {
    std::array<int, 2> out{}, err{};
    if (pipe(out.data()) || pipe(err.data()))
        throw std::runtime_error("pipe failed");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err[1], 2);
    for (int fd : {out[0], out[1], err[0], err[1]})
        posix_spawn_file_actions_addclose(&actions, fd);
    std::vector<char*> argv;
    for (const auto& a : command)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out[1]);
    close(err[1]);
    if (spawned) {
        close(out[0]);
        close(err[0]);
        throw std::runtime_error(command[0] + ": " + std::strerror(spawned));
    }
    Run result{0, {}, {}};
    std::array<pollfd, 2> fds{pollfd{out[0], POLLIN, 0}, pollfd{err[0], POLLIN, 0}};
    std::array<std::string*, 2> targets{&result.out, &result.err};
    int open = 2;
    while (open) {
        if (poll(fds.data(), fds.size(), -1) < 0 && errno != EINTR)
            throw std::runtime_error("poll failed");
        for (std::size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            char buffer[65536];
            const auto n = read(fds[i].fd, buffer, sizeof buffer);
            if (n > 0) {
                targets[i]->append(buffer, static_cast<std::size_t>(n));
            } else if (n == 0 || errno != EINTR) {
                close(fds[i].fd);
                fds[i].fd = -1;
                --open;
            }
        }
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    result.returncode = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
    return result;
}

Run runChecked(const std::vector<std::string>& command) {
    auto result = run(command);
    if (result.returncode)
        throw std::runtime_error(std::format("{} exited with {}:\n{}", command[0], result.returncode, result.err));
    return result;
}

/// The "(12.3 MHz effective" the application reports on stderr at the end of a headless run.
std::optional<double> throughput(const std::string& err) {
    const auto end = err.find(" MHz effective");
    const auto start = end == std::string::npos ? std::string::npos : err.rfind('(', end);
    if (start == std::string::npos)
        return std::nullopt;
    const auto number = err.substr(start + 1, end - start - 1);
    if (number.empty() || number.find_first_not_of("0123456789.") != std::string::npos)
        return std::nullopt;
    return std::stod(number);
}

double cpuSeconds(const rusage& u) {
    const auto seconds = [](const timeval& t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1e6; };
    return seconds(u.ru_utime) + seconds(u.ru_stime);
}

/// A number as Python's str() writes it in a command line: 30.0 -> "30.0", 0.5 -> "0.5".
std::string pythonFloat(double value) { return Json(value).dump(); }

Json comparison(const Json& blocks, const std::string& candidate, const std::string& reference, std::uint64_t seed, int resamples) {
    std::vector<double> candidates, references, deltas;
    for (const auto& b : blocks) {
        candidates.push_back(b["runs"][candidate]["MHz"].get<double>());
        references.push_back(b["runs"][reference]["MHz"].get<double>());
        deltas.push_back(100 * (candidates.back() / references.back() - 1));
    }
    std::mt19937_64 random(seed);
    std::vector<double> pairedDraws, ratioDraws;
    const auto n = candidates.size();
    for (int r = 0; r < resamples; ++r) {
        std::vector<double> d, c, f;
        for (std::size_t i = 0; i < n; ++i) {
            const auto index = below(random, n);
            d.push_back(deltas[index]);
            c.push_back(candidates[index]);
            f.push_back(references[index]);
        }
        pairedDraws.push_back(median(d));
        ratioDraws.push_back(100 * (median(c) / median(f) - 1));
    }
    return Json{{"ratio_of_medians_delta_percent", 100 * (median(candidates) / median(references) - 1)},
                {"ratio_bootstrap_95_ci_percent", {percentile(ratioDraws, .025), percentile(ratioDraws, .975)}},
                {"paired_median_delta_percent", median(deltas)},
                {"paired_bootstrap_95_ci_percent", {percentile(pairedDraws, .025), percentile(pairedDraws, .975)}},
                {"paired_deltas_percent", deltas}};
}

}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> paths;
    int cpu = 0, blocks = 24, resamples = 10000, perfRepeats = 5;
    double seconds = 30, warmupSeconds = 5;
    std::uint64_t seed = 1729;
    bool perf = false;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        std::string value;
        if (const auto equal = argument.find('='); equal != std::string::npos) {
            value = argument.substr(equal + 1);
            argument.resize(equal);
        } else if (argument != "--perf" && argument != "-h" && argument != "--help") {
            if (i + 1 >= argc)
                usageError("argument " + argument + ": expected one argument");
            value = argv[++i];
        }
        try {
            if (argument == "-h" || argument == "--help") {
                std::println("{}", usage);
                return 0;
            } else if (argument == "--perf")
                perf = true;
            else if (argument == "--baseline" || argument == "--large" || argument == "--compact" || argument == "--firmware" ||
                     argument == "--output")
                paths[argument.substr(2)] = value;
            else if (argument == "--cpu")
                cpu = std::stoi(value);
            else if (argument == "--blocks")
                blocks = std::stoi(value);
            else if (argument == "--seconds")
                seconds = std::stod(value);
            else if (argument == "--warmup-seconds")
                warmupSeconds = std::stod(value);
            else if (argument == "--seed")
                seed = std::stoull(value);
            else if (argument == "--resamples")
                resamples = std::stoi(value);
            else if (argument == "--perf-repeats")
                perfRepeats = std::stoi(value);
            else
                usageError("unrecognized arguments: " + argument);
        } catch (const std::logic_error&) {
            usageError("argument " + argument + ": invalid value: '" + value + "'");
        }
    }
    for (const char* required : {"baseline", "large", "compact", "firmware", "output"})
        if (!paths.contains(required))
            usageError(std::string("the following arguments are required: --") + required);
    if (blocks < 6 || blocks % 6 || !(seconds > 0) || warmupSeconds < 0 || resamples < 100)
        usageError("Use a positive multiple of six blocks, seconds > 0, warmup >= 0 and resamples >= 100");
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed) || cpu < 0 || cpu >= CPU_SETSIZE || !CPU_ISSET(cpu, &allowed))
        usageError("Selected CPU is outside the allowed affinity mask");
    cpu_set_t selected;
    CPU_ZERO(&selected);
    CPU_SET(cpu, &selected);
    sched_setaffinity(0, sizeof selected, &selected);

    try {
        std::map<std::string, fs::path> programs;
        for (const char* name : names)
            programs[name] = fs::canonical(paths[name]);
        const fs::path firmware = fs::canonical(paths["firmware"]);
        const fs::path output = paths["output"];
        const auto command = [&](const std::string& name, double duration) {
            return std::vector<std::string>{programs[name].string(), "--headless", "--speed", "max", "--duration",
                                            pythonFloat(duration), firmware.string()};
        };
        const auto write = [&](const Json& result) {
            std::ofstream file(output);
            if (!(file << result.dump(2) << '\n'))
                throw std::runtime_error("cannot write " + output.string());
        };
        std::string paranoid;
        std::getline(std::ifstream("/proc/sys/kernel/perf_event_paranoid"), paranoid);

        Json binaries = Json::object();
        for (const char* name : names)
            binaries[name] = {{"path", programs[name].string()}, {"sha256", sha256(programs[name])}};
        Json result{
            {"created_utc", std::format("{:%FT%T}+00:00", std::chrono::floor<std::chrono::microseconds>(std::chrono::system_clock::now()))},
            {"configuration", {{"cpu", cpu}, {"blocks", blocks}, {"simulated_seconds", seconds}, {"warmup_seconds", warmupSeconds},
                               {"seed", seed}, {"bootstrap_resamples", resamples}, {"firmware", paths["firmware"]},
                               {"perf_event_paranoid", paranoid}}},
            {"binaries", binaries},
            {"perf_stat", Json::object()},
            {"blocks", Json::array()},
            {"interval_scope", "Exploratory paired-block bootstrap on this session only; drift/autocorrelation may remain. No sample exclusion."}};
        if (perf)
            for (const char* name : names) {
                std::vector<std::string> cmd{"perf", "stat", "-x", ";", "-r", std::to_string(perfRepeats), "-e", events, "--"};
                for (auto& part : command(name, seconds))
                    cmd.push_back(part);
                Json entry{{"command", cmd}};
                try {
                    const auto p = run(cmd);
                    entry["returncode"] = p.returncode;
                    entry["stdout"] = p.out;
                    entry["stderr"] = p.err;
                } catch (const std::exception& error) {  // perf is not installed.
                    entry["returncode"] = nullptr;
                    entry["stdout"] = "";
                    entry["stderr"] = error.what();
                }
                result["perf_stat"][name] = entry;
            }
        if (warmupSeconds > 0)
            for (const char* name : names)
                runChecked(command(name, warmupSeconds));

        std::mt19937_64 random(seed);
        std::vector<std::array<std::string, 3>> schedule;
        for (int group = 0; group < blocks / 6; ++group) {
            std::array<int, 3> order{0, 1, 2};
            std::vector<std::array<std::string, 3>> orders;
            do
                orders.push_back({names[static_cast<std::size_t>(order[0])], names[static_cast<std::size_t>(order[1])],
                                  names[static_cast<std::size_t>(order[2])]});
            while (std::ranges::next_permutation(order).found);
            for (std::size_t i = orders.size() - 1; i > 0; --i)  // Fisher-Yates.
                std::swap(orders[i], orders[below(random, i + 1)]);
            schedule.insert(schedule.end(), orders.begin(), orders.end());
        }
        for (std::size_t index = 0; index < schedule.size(); ++index) {
            Json block{{"index", index}, {"order", schedule[index]}, {"runs", Json::object()}};
            for (const auto& name : schedule[index]) {
                rusage before{}, after{};
                getrusage(RUSAGE_CHILDREN, &before);
                const auto start = std::chrono::steady_clock::now();
                const auto p = runChecked(command(name, seconds));
                const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - start;
                getrusage(RUSAGE_CHILDREN, &after);
                const auto mhz = throughput(p.err);
                if (!mhz)
                    throw std::runtime_error("No emulation throughput in " + p.err);
                block["runs"][name] = {{"MHz", *mhz}, {"process_wall_seconds", wall.count()},
                                       {"process_cpu_seconds", cpuSeconds(after) - cpuSeconds(before)},
                                       {"voluntary_switches", after.ru_nvcsw - before.ru_nvcsw},
                                       {"involuntary_switches", after.ru_nivcsw - before.ru_nivcsw}, {"stderr", p.err}};
            }
            result["blocks"].push_back(block);
            write(result);
            std::string progress;
            for (const char* name : names)
                progress += std::format("{}{}={:.1f}", progress.empty() ? "" : ", ", name, block["runs"][name]["MHz"].get<double>());
            std::println(stderr, "block {}/{}: {}", index + 1, blocks, progress);
        }
        result["summary"] = Json::object();
        for (const char* name : names) {
            std::vector<double> values;
            for (const auto& b : result["blocks"])
                values.push_back(b["runs"][name]["MHz"].get<double>());
            result["summary"][name] = {{"n", values.size()}, {"median_MHz", median(values)}, {"mean_MHz", mean(values)},
                                       {"stdev_MHz", stdev(values)}, {"min_MHz", std::ranges::min(values)},
                                       {"max_MHz", std::ranges::max(values)}, {"q1_MHz", percentile(values, .25)},
                                       {"q3_MHz", percentile(values, .75)}};
        }
        result["comparisons"] = Json::object();
        const std::array<std::pair<const char*, const char*>, 3> pairs{{{"compact", "baseline"}, {"large", "baseline"}, {"compact", "large"}}};
        for (std::size_t i = 0; i < pairs.size(); ++i)
            result["comparisons"][std::format("{}_vs_{}", pairs[i].first, pairs[i].second)] =
                comparison(result["blocks"], pairs[i].first, pairs[i].second, seed + i, resamples);
        write(result);
        std::println("{}", Json{{"summary", result["summary"]}, {"comparisons", result["comparisons"]}}.dump(2));
    } catch (const std::exception& error) {
        std::println(stderr, "siliconforge_compare_dispatch: {}", error.what());
        return 1;
    }
    return 0;
}
