#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

/// The sources of the project directory, for the editor of the window (ADR 0009, #56): the files the agent and the
/// person work on together, their Git changes, and a save that never overwrites a change made behind the editor's back.
///
/// The page names files by a path relative to the project directory, with `/` separators. Nothing outside the directory
/// is reachable (no `..`, no absolute path, no link that leaves it) and neither is `.git`. Git is the `git` program of
/// the machine, run read-only and without locking the index, so that it never gets in the way of the agent's own Git
/// commands; without it, or outside a repository, the editor still lists, opens and saves files and shows no changes.
namespace siliconforge::app::project {

/// Codes of the errors of this class: the `error.code` the sf.files.* functions reject with.
namespace code {
inline constexpr std::string_view unknownFile = "siliconforge.unknownFile";  ///< Not a file of the project directory.
inline constexpr std::string_view notText     = "siliconforge.notText";      ///< Binary content, or not UTF-8.
inline constexpr std::string_view tooLarge    = "siliconforge.tooLarge";     ///< Larger than maxFileBytes.
inline constexpr std::string_view fileChanged = "siliconforge.fileChanged";  ///< The file is no longer the version the editor read.
inline constexpr std::string_view notEditable = "siliconforge.notEditable";  ///< Mixed line endings: saving would alter them.
inline constexpr std::string_view writeFailed = "siliconforge.writeFailed";  ///< The file could not be written.
}  // namespace code

/// Largest file the editor opens, in bytes.
inline constexpr std::size_t maxFileBytes = 1024 * 1024;
/// Files listed at most; the answer says when there were more.
inline constexpr std::size_t maxListed = 5000;
/// Bytes of a diff returned at most; the answer says when it was cut.
inline constexpr std::size_t maxDiffBytes = 1024 * 1024;

struct Failure {
    std::string code;
    std::string message;
};

/// Whether `text` is UTF-8 without NUL: what the editor accepts as text.
[[nodiscard]] inline bool isText(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0)
            return false;
        if (c < 0x80) {
            ++i;
            continue;
        }
        unsigned extra, minimum, point;
        if ((c & 0xE0) == 0xC0) {
            extra = 1; minimum = 0x80; point = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2; minimum = 0x800; point = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3; minimum = 0x10000; point = c & 0x07;
        } else {
            return false;
        }
        if (i + extra >= text.size())
            return false;
        for (unsigned k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80)
                return false;
            point = (point << 6) | (next & 0x3F);
        }
        if (point < minimum || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF))
            return false;
        i += extra + 1;
    }
    return true;
}

/// The version of a content: its size and a 64-bit FNV-1a hash. Two different texts of one size get different versions
/// except by a 1 in 2^64 chance, and a rewrite with the same text keeps its version, whatever the modification time.
[[nodiscard]] inline std::string versionOf(std::string_view bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return std::format("{:x}-{:016x}", bytes.size(), hash);
}

/// How a text ends its lines: "lf", "crlf", or "mixed" when the editor could not write it back as it read it.
[[nodiscard]] inline std::string_view lineEndings(std::string_view text) {
    std::size_t crlf = 0, lf = 0, cr = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
        } else if (text[i] == '\n') {
            ++lf;
        }
    }
    if (cr > 0 || (crlf > 0 && lf > 0))
        return "mixed";
    return crlf > 0 ? "crlf" : "lf";
}

namespace detail {

[[nodiscard]] inline std::filesystem::path fromUtf8(std::string_view text) {
    return std::filesystem::path{std::u8string{text.begin(), text.end()}};
}

[[nodiscard]] inline std::string toUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string{text.begin(), text.end()};
}

[[nodiscard]] inline std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// `text` as one argument of the command line of a shell, or nothing when it cannot be quoted safely.
[[nodiscard]] inline std::optional<std::string> quoted(std::string_view text) {
    if (text.find_first_of("\r\n") != std::string_view::npos)
        return std::nullopt;
#if defined(_WIN32)
    if (text.find_first_of("\"%^!&|<>") != std::string_view::npos)
        return std::nullopt;
    return "\"" + std::string{text} + "\"";
#else
    std::string result{"'"};
    for (const char c : text) {
        if (c == '\'')
            result += "'\\''";
        else
            result += c;
    }
    return result + "'";
#endif
}

/// Output of `command` (standard output only), or nothing when it fails. An output longer than `limit` is cut and
/// `truncated` set: that is no failure.
[[nodiscard]] inline std::optional<std::string> capture(const std::string& command, std::size_t limit, bool* truncated = nullptr) {
#if defined(_WIN32)
    std::FILE* pipe = ::_popen((command + " 2>nul").c_str(), "rb");
#else
    std::FILE* pipe = ::popen((command + " 2>/dev/null").c_str(), "r");
#endif
    if (!pipe)
        return std::nullopt;
    std::string output;
    char        buffer[8192];
    bool        cut = false;
    while (const auto count = std::fread(buffer, 1, sizeof buffer, pipe)) {
        if (output.size() + count > limit) {
            output.append(buffer, limit - output.size());
            cut = true;
            break;
        }
        output.append(buffer, count);
    }
#if defined(_WIN32)
    const int status = ::_pclose(pipe);
    const bool ok    = status == 0;
#else
    const int status = ::pclose(pipe);
    const bool ok    = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    if (truncated)
        *truncated = cut;
    if (!ok && !cut)
        return std::nullopt;
    return output;
}

/// The unified diff of a file that Git does not know: every line added.
[[nodiscard]] inline std::string additionDiff(const std::string& path, std::string_view text) {
    std::string diff = std::format("diff --git a/{0} b/{0}\nnew file mode 100644\n--- /dev/null\n+++ b/{0}\n", path);
    if (text.empty())
        return diff;
    std::size_t lines = static_cast<std::size_t>(std::ranges::count(text, '\n'));
    const bool  open  = text.back() != '\n';
    if (open)
        ++lines;
    diff += std::format("@@ -0,0 +1,{} @@\n", lines);
    std::size_t begin = 0;
    while (begin < text.size()) {
        auto end = text.find('\n', begin);
        if (end == std::string_view::npos)
            end = text.size();
        diff += '+';
        diff.append(text.substr(begin, end - begin));
        diff += '\n';
        begin = end + 1;
    }
    if (open)
        diff += "\\ No newline at end of file\n";
    return diff;
}

}  // namespace detail

class Project {
public:
    explicit Project(const std::filesystem::path& directory) {
        std::error_code error;
        root = std::filesystem::weakly_canonical(directory, error);
        if (error)
            root = directory;
    }

    [[nodiscard]] const std::filesystem::path& directory() const { return root; }

    /// The files of the directory, those with a change first: `{name, git, truncated, files: [{path, status}]}`. The
    /// status is "" (unchanged), "M" (modified), "A" (added), "D" (deleted), "R" (renamed), "U" (conflict) or "?" (not
    /// tracked).
    [[nodiscard]] nlohmann::json list() const {
        std::map<std::string, std::string> entries;
        const auto                         prefix = repositoryPrefix();
        if (prefix) {
            if (const auto tracked = git("ls-files -z --cached --others --exclude-standard", maxFileBytes * 8))
                for (const auto& path : split(*tracked))
                    entries.emplace(path, "");
            if (const auto status = git("status --porcelain=v1 -z -uall", maxFileBytes * 8))
                applyStatus(*status, *prefix, entries);
        } else {
            walk(entries);
        }
        std::vector<std::pair<std::string, std::string>> sorted(entries.begin(), entries.end());
        std::ranges::stable_sort(sorted, [](const auto& a, const auto& b) { return a.second.empty() < b.second.empty(); });
        const bool truncated = sorted.size() > maxListed;
        if (truncated)
            sorted.resize(maxListed);
        nlohmann::json files = nlohmann::json::array();
        for (const auto& [path, status] : sorted)
            files.push_back({{"path", path}, {"status", status}});
        return {{"name", detail::toUtf8(root.filename())}, {"git", prefix.has_value()}, {"truncated", truncated}, {"files", std::move(files)}};
    }

    /// `{path, content, version, lineEndings}` of a text file. `content` keeps the line endings of the file.
    [[nodiscard]] std::expected<nlohmann::json, Failure> read(std::string_view name) const {
        const auto target = resolve(name);
        if (!target)
            return std::unexpected{target.error()};
        const auto bytes = load(*target);
        if (!bytes)
            return std::unexpected{bytes.error()};
        return nlohmann::json{{"path", std::string{name}}, {"content", *bytes}, {"version", versionOf(*bytes)}, {"lineEndings", lineEndings(*bytes)}};
    }

    /// `{version}` of the file now on disk, null when it is gone or no longer text: the page compares it with the one
    /// it read, and reloads or warns.
    [[nodiscard]] std::expected<nlohmann::json, Failure> version(std::string_view name) const {
        const auto target = resolve(name);
        if (!target)
            return std::unexpected{target.error()};
        const auto bytes = load(*target);
        return nlohmann::json{{"version", bytes ? nlohmann::json(versionOf(*bytes)) : nlohmann::json(nullptr)}};
    }

    /// Replaces a file by `content` if it is still the version `expected`; otherwise refuses with code::fileChanged and
    /// touches nothing, so that the page can show both versions. `content` has "\n" line ends: a file that used
    /// "\r\n" gets them back. Returns `{version}` of what is written.
    [[nodiscard]] std::expected<nlohmann::json, Failure> write(std::string_view name, std::string_view content, std::string_view expected) const {
        const auto target = resolve(name);
        if (!target)
            return std::unexpected{target.error()};
        const auto current = load(*target);
        if (!current)
            return std::unexpected{current.error()};
        if (versionOf(*current) != expected)
            return std::unexpected{Failure{std::string{code::fileChanged}, "The file changed since it was read: " + std::string{name}}};
        const auto endings = lineEndings(*current);
        if (endings == "mixed")
            return std::unexpected{Failure{std::string{code::notEditable}, "Mixed line endings: " + std::string{name}}};
        std::string bytes;
        bytes.reserve(content.size());
        for (std::size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\r') {
                if (i + 1 < content.size() && content[i + 1] == '\n')
                    ++i;
                bytes += endings == "crlf" ? "\r\n" : "\n";
            } else if (content[i] == '\n') {
                bytes += endings == "crlf" ? "\r\n" : "\n";
            } else {
                bytes += content[i];
            }
        }
        if (bytes.size() > maxFileBytes)
            return std::unexpected{Failure{std::string{code::tooLarge}, "The file would be larger than 1 MiB: " + std::string{name}}};
        if (!isText(bytes))
            return std::unexpected{Failure{std::string{code::notText}, "Not UTF-8 text: " + std::string{name}}};
        // Written next to the file and renamed over it: the agent never reads half a file, and the permissions stay.
        std::filesystem::path temporary = *target;
        temporary += ".sf-save";
        std::error_code error;
        {
            std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out) {
                out.close();
                std::filesystem::remove(temporary, error);
                return std::unexpected{Failure{std::string{code::writeFailed}, "Cannot write " + std::string{name}}};
            }
        }
        std::filesystem::permissions(temporary, std::filesystem::status(*target, error).permissions(), error);
        std::filesystem::rename(temporary, *target, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return std::unexpected{Failure{std::string{code::writeFailed}, "Cannot replace " + std::string{name}}};
        }
        return nlohmann::json{{"version", versionOf(bytes)}};
    }

    /// `{git, diff, truncated}`: the unified diff of the file against the last commit, empty when it has no change.
    [[nodiscard]] std::expected<nlohmann::json, Failure> diff(std::string_view name) const {
        const auto target = resolve(name);
        if (!target)
            return std::unexpected{target.error()};
        std::error_code error;
        const bool      exists = std::filesystem::is_regular_file(*target, error);
        const auto      path   = detail::toUtf8(std::filesystem::relative(*target, root));
        const auto      arg    = detail::quoted(path);
        const auto      unknown = [&] { return std::unexpected{Failure{std::string{code::unknownFile}, "Not a file: " + std::string{name}}}; };
        if (std::filesystem::is_directory(*target, error))
            return unknown();
        if (!repositoryPrefix()) {
            if (!exists)
                return unknown();
            return nlohmann::json{{"git", false}, {"diff", ""}, {"truncated", false}};
        }
        if (!arg)
            return exists ? std::expected<nlohmann::json, Failure>{nlohmann::json{{"git", true}, {"diff", ""}, {"truncated", false}}} : unknown();
        if (!exists) {  // A deleted file is still one Git knows.
            const auto known = git("ls-files --cached -- " + *arg, 4096);
            if (!known || known->empty())
                return unknown();
        }
        bool       truncated = false;
        const auto text      = git("diff HEAD --no-color --no-ext-diff --no-textconv -- " + *arg, maxDiffBytes, &truncated);
        if (text && !text->empty())
            return nlohmann::json{{"git", true}, {"diff", *text}, {"truncated", truncated}};
        // Git has nothing to compare a new file with, and nothing at all before the first commit.
        const auto untracked = git("ls-files --others --exclude-standard -- " + *arg, 4096);
        const auto known     = git("ls-files --cached -- " + *arg, 4096);
        if ((untracked && !untracked->empty()) || (!text && known && !known->empty())) {
            if (const auto bytes = load(*target))
                return nlohmann::json{{"git", true}, {"diff", detail::additionDiff(path, *bytes)}, {"truncated", false}};
        }
        return nlohmann::json{{"git", true}, {"diff", ""}, {"truncated", false}};
    }

private:
    std::filesystem::path root;

    /// The file `name` names, inside the directory, or why it is refused.
    [[nodiscard]] std::expected<std::filesystem::path, Failure> resolve(std::string_view name) const {
        const auto refuse = [&](std::string_view why) {
            return std::unexpected{Failure{std::string{code::unknownFile}, std::string{why} + ": " + std::string{name}}};
        };
        if (name.empty() || name.size() > 4096 || name.find('\0') != std::string_view::npos)
            return refuse("Not a file of the project");
#if defined(_WIN32)
        if (name.find(':') != std::string_view::npos)
            return refuse("Not a file of the project");
#endif
        const auto relative = detail::fromUtf8(name);
        if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
            return refuse("Not a path of the project");
        const auto inside = [](const std::filesystem::path& path) {
            return std::ranges::none_of(path, [](const std::filesystem::path& part) {
                const auto text = detail::lower(part.string());
                return text == ".." || text == ".git";
            });
        };
        if (!inside(relative))
            return refuse("Not a file of the project");
        std::error_code error;
        const auto      target = std::filesystem::weakly_canonical(root / relative, error);
        if (error)
            return refuse("Not a file of the project");
        const auto [mismatch, unused] = std::ranges::mismatch(root, target);
        if (mismatch != root.end() || !inside(target.lexically_relative(root)))  // A link that leaves the project, or reaches .git
            return refuse("Outside the project");
        return target;
    }

    [[nodiscard]] static std::expected<std::string, Failure> load(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            return std::unexpected{Failure{std::string{code::unknownFile}, "Not a file: " + detail::toUtf8(path.filename())}};
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::unexpected{Failure{std::string{code::unknownFile}, "Cannot read: " + detail::toUtf8(path.filename())}};
        if (size > maxFileBytes)
            return std::unexpected{Failure{std::string{code::tooLarge}, "Larger than 1 MiB: " + detail::toUtf8(path.filename())}};
        std::ifstream in{path, std::ios::binary};
        std::string   bytes(static_cast<std::size_t>(size), '\0');
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(in.gcount()));
        if (!isText(bytes))
            return std::unexpected{Failure{std::string{code::notText}, "Not UTF-8 text: " + detail::toUtf8(path.filename())}};
        return bytes;
    }

    /// Runs `git <arguments>` in the directory: no color, no pager, no optional lock.
    [[nodiscard]] std::optional<std::string> git(const std::string& arguments, std::size_t limit, bool* truncated = nullptr) const {
        const auto directory = detail::quoted(detail::toUtf8(root));
        if (!directory)
            return std::nullopt;
        return detail::capture("git --no-optional-locks --no-pager --literal-pathspecs -c core.quotepath=off -C " + *directory + " " + arguments, limit,
                               truncated);
    }

    /// Where the directory is in its repository ("" at the top, "apps/x/" below it), or nothing outside a repository.
    [[nodiscard]] std::optional<std::string> repositoryPrefix() const {
        const auto answer = git("rev-parse --is-inside-work-tree --show-prefix", 8192);
        if (!answer)
            return std::nullopt;
        const auto newline = answer->find('\n');
        if (newline == std::string::npos || answer->substr(0, newline) != "true")
            return std::nullopt;
        auto prefix = answer->substr(newline + 1);
        while (!prefix.empty() && (prefix.back() == '\n' || prefix.back() == '\r'))
            prefix.pop_back();
        return prefix;
    }

    [[nodiscard]] static std::vector<std::string> split(const std::string& text) {
        std::vector<std::string> parts;
        std::size_t              begin = 0;
        while (begin < text.size()) {
            auto end = text.find('\0', begin);
            if (end == std::string::npos)
                end = text.size();
            if (end > begin)
                parts.emplace_back(text, begin, end - begin);
            begin = end + 1;
        }
        return parts;
    }

    /// Reads `git status --porcelain=v1 -z`: "XY path", and for a rename or a copy the path it came from after it.
    static void applyStatus(const std::string& text, const std::string& prefix, std::map<std::string, std::string>& entries) {
        const auto records = split(text);
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto& record = records[i];
            if (record.size() < 4)
                continue;
            const char x = record[0], y = record[1];
            if (x == 'R' || x == 'C' || y == 'R' || y == 'C')
                ++i;
            if (record.compare(3, prefix.size(), prefix) != 0)
                continue;  // Changes elsewhere in the repository.
            const auto path = record.substr(3 + prefix.size());
            std::string status;
            if (x == '?' || y == '?')
                status = "?";
            else if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D'))
                status = "U";
            else if (x == 'A' || y == 'A')
                status = "A";
            else if (x == 'D' || y == 'D')
                status = "D";
            else if (x == 'R' || y == 'R' || x == 'C' || y == 'C')
                status = "R";
            else
                status = "M";
            entries[path] = status;
        }
    }

    /// The files of a directory that is not in a repository, but for the usual build and tool folders.
    void walk(std::map<std::string, std::string>& entries) const {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it{root, std::filesystem::directory_options::skip_permission_denied, error}, end; !error && it != end;
             it.increment(error)) {
            const auto name = detail::lower(it->path().filename().string());
            if (it->is_directory(error)) {
                if (name == ".git" || name == "node_modules" || name == "build" || name == ".cache")
                    it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(error)) {
                entries.emplace(detail::toUtf8(std::filesystem::relative(it->path(), root)), "");
                if (entries.size() > maxListed * 2)
                    break;
            }
        }
    }
};

}  // namespace siliconforge::app::project
