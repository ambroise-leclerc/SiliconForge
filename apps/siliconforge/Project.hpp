#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <copyfile.h>
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

/// Whether `name` is one of the temporary files of a save: `.<name>.sf-` and 16 hexadecimal digits. They live for the
/// instant of a save, and are not files of the project.
[[nodiscard]] inline bool isTemporary(std::string_view name) {
    constexpr std::string_view mark = ".sf-";
    if (name.size() < 1 + mark.size() + 16 + 1 || name.front() != '.')
        return false;
    const auto digits = name.substr(name.size() - 16);
    return name.substr(name.size() - 16 - mark.size(), mark.size()) == mark && std::ranges::all_of(digits, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

/// Creates a file next to `target` that did not exist, holding `bytes`: a name of its own, created exclusively and without
/// following a link, so that nothing already there (a file, a link to somewhere else) is ever written to.
[[nodiscard]] inline std::expected<std::filesystem::path, std::error_code> createTemporary(const std::filesystem::path& target, std::string_view bytes) {
    std::random_device random;
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::filesystem::path name{"."};
        name += target.filename();
        name += std::filesystem::path{std::format(".sf-{:08x}{:08x}", random(), random())};
        const auto candidate = target.parent_path() / name;
#if defined(_WIN32)
        int         descriptor = -1;
        const auto  opened     = ::_wsopen_s(&descriptor, candidate.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
        if (opened == EEXIST)
            continue;
        if (opened != 0)
            return std::unexpected{std::error_code{opened, std::generic_category()}};
        const auto close = [&] { ::_close(descriptor); };
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto chunk   = static_cast<unsigned>(std::min<std::size_t>(bytes.size() - done, 1u << 20));
            const int  written = ::_write(descriptor, bytes.data() + done, chunk);
            if (written <= 0) {
                const std::error_code failure{errno, std::generic_category()};
                close();
                std::error_code ignored;
                std::filesystem::remove(candidate, ignored);
                return std::unexpected{failure};
            }
            done += static_cast<std::size_t>(written);
        }
        close();
#else
        const int descriptor = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (descriptor < 0) {
            if (errno == EEXIST)
                continue;
            return std::unexpected{std::error_code{errno, std::generic_category()}};
        }
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto written = ::write(descriptor, bytes.data() + done, bytes.size() - done);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                const std::error_code failure{errno, std::generic_category()};
                ::close(descriptor);
                std::error_code ignored;
                std::filesystem::remove(candidate, ignored);
                return std::unexpected{failure};
            }
            done += static_cast<std::size_t>(written);
        }
        ::close(descriptor);
#endif
        return candidate;
    }
    return std::unexpected{std::make_error_code(std::errc::file_exists)};
}

/// Exchanges two files in one step (Linux, macOS). 0 on success; -1 with `errno` otherwise, ENOTSUP when the system or the
/// file system cannot.
[[nodiscard]] inline int exchange(const std::filesystem::path& a, const std::filesystem::path& b) {
#if defined(__linux__)
    const int result = ::renameat2(AT_FDCWD, a.c_str(), AT_FDCWD, b.c_str(), RENAME_EXCHANGE);
    if (result != 0 && (errno == EINVAL || errno == ENOSYS))
        errno = ENOTSUP;
    return result;
#elif defined(__APPLE__)
    const int result = ::renamex_np(a.c_str(), b.c_str(), RENAME_SWAP);
    if (result != 0 && errno == EINVAL)
        errno = ENOTSUP;
    return result;
#else
    (void)a;
    (void)b;
    errno = ENOTSUP;
    return -1;
#endif
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

    /// Records `files` (names relative to the directory) in a Git repository of the directory itself, made when it has none,
    /// so that the editor shows what the person and the agent change from then on: the folder of a session may lie in a
    /// tree that another repository ignores, which would show nothing. Does nothing when `git` is missing.
    bool record(const std::vector<std::string>& files, const std::string& message) const {
        std::error_code error;
        if (!std::filesystem::exists(root / ".git", error) && !git("init -q", 4096))
            return false;
        std::string names;
        for (const auto& file : files) {
            const auto quoted = detail::quoted(file);
            if (!quoted)
                return false;
            names += " " + *quoted;
        }
        const auto quotedMessage = detail::quoted(message);
        if (!quotedMessage || !git("add --" + names, 4096))
            return false;
        static constexpr std::string_view identity = "-c user.name=SiliconForge -c user.email=siliconforge@localhost -c commit.gpgsign=false ";
        return git(std::string{identity} + "commit -q -m " + *quotedMessage, 4096).has_value();
    }

    /// Called by write() once the new content is ready and the file was found at the expected version, just before the
    /// replacement: a seam for tests, which play the agent writing in that interval.
    std::function<void()> beforeReplace;

    /// The files of the directory, those with a change first: `{name, root, git, truncated, files: [{path, status}]}`
    /// (`root` is the full path of the directory, which tells two projects of the same name apart). The
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
        std::erase_if(entries, [](const auto& entry) { return detail::isTemporary(entry.first.substr(entry.first.rfind('/') + 1)); });
        std::vector<std::pair<std::string, std::string>> sorted(entries.begin(), entries.end());
        std::ranges::stable_sort(sorted, [](const auto& a, const auto& b) { return a.second.empty() < b.second.empty(); });
        const bool truncated = sorted.size() > maxListed;
        if (truncated)
            sorted.resize(maxListed);
        nlohmann::json files = nlohmann::json::array();
        for (const auto& [path, status] : sorted)
            files.push_back({{"path", path}, {"status", status}});
        return {{"name", detail::toUtf8(root.filename())}, {"root", detail::toUtf8(root)}, {"git", prefix.has_value()}, {"truncated", truncated}, {"files", std::move(files)}};
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

    /// The bytes of a file of the project, text or not (a firmware: Intel HEX or ELF), with the path checks of read() and
    /// at most `limit` bytes.
    [[nodiscard]] std::expected<std::string, Failure> readBytes(std::string_view name, std::uintmax_t limit) const {
        const auto target = resolve(name);
        if (!target)
            return std::unexpected{target.error()};
        return load(*target, limit, false);
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
        // Written to a file of its own next to the target, then put in its place: the agent never reads half a file.
        const auto created = detail::createTemporary(*target, bytes);
        if (!created)
            return std::unexpected{Failure{std::string{code::writeFailed}, "Cannot write " + std::string{name} + ": " + created.error().message()}};
        const auto& temporary = *created;
        std::error_code error;
#if defined(__APPLE__)
        // The extended attributes (Finder tags, quarantine...), the ACL, the owner and the permissions stay with the file.
        // copyfile may refuse some of them (a volume without extended attributes, an owner that is not ours): that is no
        // reason to refuse the save, so only its failure sends us to the plain permissions below.
        const bool kept = ::copyfile(target->c_str(), temporary.c_str(), nullptr, COPYFILE_METADATA) == 0;
#else
        const bool kept = false;
#endif
        if (!kept)
            std::filesystem::permissions(temporary, std::filesystem::status(*target, error).permissions(), error);
        if (beforeReplace)
            beforeReplace();
        const auto replaced = replace(*target, temporary, expected);
        if (!replaced) {
            return std::unexpected{replaced.error()};
        }
        if (!*replaced)
            return std::unexpected{Failure{std::string{code::fileChanged}, "The file changed while it was saved, and was left as it is: " + std::string{name}}};
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
            // In the index, or in the last commit: a file removed with `git rm` is in the commit only.
            const auto known    = git("ls-files --cached -- " + *arg, 4096);
            const auto inCommit = git("ls-tree --name-only HEAD -- " + *arg, 4096);
            if ((!known || known->empty()) && (!inCommit || inCommit->empty()))
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

    /// Puts `temporary` in the place of `target`, provided `target` still is the version `expected` at the moment of the
    /// replacement: true when done, false when the file had changed (nothing of it is lost), a failure otherwise.
    ///
    /// Checking the version and then renaming leaves a gap in which the agent may write. Where the system can exchange two
    /// files in one step (Linux, macOS), the file is exchanged first and the version of what came out is checked: if it is
    /// not the one that was read, the exchange is undone, so that what the agent wrote is back and no save has hidden it.
    /// The only writes that can still be lost are those of a program that keeps the old file open and writes into it after
    /// the check, within microseconds. Elsewhere (Windows) the version is checked again just before the rename, which narrows
    /// the gap without closing it.
    [[nodiscard]] static std::expected<bool, Failure> replace(const std::filesystem::path& target, const std::filesystem::path& temporary,
                                                              std::string_view expected) {
        const auto failure = [&](const std::string& what) {
            return std::unexpected{Failure{std::string{code::writeFailed}, what + ": " + detail::toUtf8(target.filename())}};
        };
        std::error_code ignored;
        if (detail::exchange(temporary, target) == 0) {
            const auto before = load(temporary);  // What the file was at the moment of the exchange.
            if (before && versionOf(*before) == expected) {
                std::filesystem::remove(temporary, ignored);
                return true;
            }
            if (detail::exchange(temporary, target) == 0) {
                std::filesystem::remove(temporary, ignored);
                return false;
            }
            return failure("The file changed while it was saved and could not be put back; the newer version is kept in " + detail::toUtf8(temporary));
        }
        if (errno == ENOENT) {  // The file was removed since it was read: that is a change too.
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        if (errno != ENOTSUP) {
            std::filesystem::remove(temporary, ignored);
            return failure("Cannot replace");
        }
        const auto now = load(target);
        if (!now || versionOf(*now) != expected) {
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        std::error_code error;
        std::filesystem::rename(temporary, target, error);
        if (error) {
            std::filesystem::remove(temporary, ignored);
            return failure("Cannot replace");
        }
        return true;
    }

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
        // The name `.git` is looked for twice: in the path as given, and in the path as the file system resolved it. The
        // second look is what catches the other names of the same folder (`.GIT` on a case-insensitive volume, `.git.` or
        // `.git ` and the short name `GIT~1` on Windows, a link): it relies on weakly_canonical returning the real name
        // of what exists, so a new way to name a file that it does not resolve needs a test in ProjectTests.cpp.
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

    [[nodiscard]] static std::expected<std::string, Failure> load(const std::filesystem::path& path, std::uintmax_t limit = maxFileBytes, bool text = true) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            return std::unexpected{Failure{std::string{code::unknownFile}, "Not a file: " + detail::toUtf8(path.filename())}};
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::unexpected{Failure{std::string{code::unknownFile}, "Cannot read: " + detail::toUtf8(path.filename())}};
        if (size > limit)
            return std::unexpected{Failure{std::string{code::tooLarge}, (limit % (1u << 20) == 0 ? std::format("Larger than {} MiB: ", limit >> 20) : std::format("Larger than {} bytes: ", limit)) + detail::toUtf8(path.filename())}};
        std::ifstream in{path, std::ios::binary};
        std::string   bytes(static_cast<std::size_t>(size), '\0');
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(in.gcount()));
        if (text && !isText(bytes))
            return std::unexpected{Failure{std::string{code::notText}, "Not UTF-8 text: " + detail::toUtf8(path.filename())}};
        return bytes;
    }

    /// Runs `git <arguments>` in the directory: no color, no pager, no optional lock, and no fsmonitor hook (a program the
    /// configuration of the repository would run: the directory may come from anywhere).
    [[nodiscard]] std::optional<std::string> git(const std::string& arguments, std::size_t limit, bool* truncated = nullptr) const {
        const auto directory = detail::quoted(detail::toUtf8(root));
        if (!directory)
            return std::nullopt;
        return detail::capture("git --no-optional-locks --no-pager --literal-pathspecs -c core.quotepath=off -c core.fsmonitor=false -C " + *directory + " " + arguments, limit,
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
