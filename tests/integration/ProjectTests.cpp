// The sources of the editor (ADR 0009, #56): which files are reachable, what a save may overwrite, and the Git changes
// shown beside them. The editor in the window is tested by tests/ui/editor.spec.js.

#include "../../apps/siliconforge/Project.hpp"
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <vector>

#if defined(__APPLE__)
#include <sys/xattr.h>
#endif

using namespace siliconforge::app::project;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct Directory {
    Directory() : path{fs::weakly_canonical(fs::temp_directory_path()) / std::format("siliconforge-project-{:x}", std::random_device{}())} {
        fs::create_directories(path);
    }
    ~Directory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    Directory(const Directory&)            = delete;
    Directory& operator=(const Directory&) = delete;
    fs::path path;
};

void put(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream{file, std::ios::binary | std::ios::trunc}.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string get(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, {}};
}

bool hasGit() {
    return detail::capture("git --version", 1024).has_value();
}

/// Runs a Git command in `directory`; the commits need an identity.
void git(const fs::path& directory, const std::string& arguments) {
    const auto where = detail::quoted(detail::toUtf8(directory));
    REQUIRE(where);
    REQUIRE(detail::capture("git -C " + *where + " -c user.name=test -c user.email=test@example.com -c commit.gpgsign=false " + arguments, 1 << 20));
}

/// The version in an answer of the project, as the page hands it back to write().
std::string versionIn(const json& answer) {
    return answer["version"].get<std::string>();
}

std::string statusOf(const json& list, std::string_view path) {
    for (const auto& file : list["files"])
        if (file["path"].get<std::string>() == path)
            return file["status"].get<std::string>();
    return "absent";
}

}  // namespace

TEST_CASE("Text is UTF-8 without NUL", "[app][project]") {
    CHECK(isText(""));
    CHECK(isText("int main() { return 0; }\n"));
    CHECK(isText("déjà vu — 日本語 🙂"));
    CHECK_FALSE(isText(std::string_view{"a\0b", 3}));
    CHECK_FALSE(isText("\xff\xfe"));
    CHECK_FALSE(isText("\xc3"));          // A cut sequence.
    CHECK_FALSE(isText("\xc0\x80"));      // Overlong NUL.
    CHECK_FALSE(isText("\xed\xa0\x80"));  // A surrogate.
}

TEST_CASE("Line endings are told apart, and a version follows the content", "[app][project]") {
    CHECK(lineEndings("") == "lf");
    CHECK(lineEndings("a\nb\n") == "lf");
    CHECK(lineEndings("a\r\nb\r\n") == "crlf");
    CHECK(lineEndings("a\r\nb\n") == "mixed");
    CHECK(lineEndings("a\rb") == "mixed");
    CHECK(versionOf("abc") == versionOf("abc"));
    CHECK(versionOf("abc") != versionOf("abd"));
    CHECK(versionOf("abc") != versionOf("abc "));
}

TEST_CASE("The files of a directory are listed, opened and saved", "[app][project]") {
    Directory directory;
    put(directory.path / "main.cpp", "int main() {}\n");
    put(directory.path / "src" / "pwm.cpp", "// pwm\n");
    put(directory.path / ".git" / "config", "[core]\n");  // Not a repository (no HEAD), but the folder is never offered.
    const Project project{directory.path};

    const auto list = project.list();
    CHECK(list["files"].size() == 2);
    CHECK(statusOf(list, "main.cpp") == "");
    CHECK(statusOf(list, "src/pwm.cpp") == "");
    CHECK(statusOf(list, ".git/config") == "absent");

    const auto opened = project.read("src/pwm.cpp");
    REQUIRE(opened);
    CHECK((*opened)["content"] == "// pwm\n");
    CHECK((*opened)["lineEndings"] == "lf");

    const auto saved = project.write("src/pwm.cpp", "// pwm at 62.5 kHz\n", versionIn(*opened));
    REQUIRE(saved);
    CHECK(get(directory.path / "src" / "pwm.cpp") == "// pwm at 62.5 kHz\n");
    CHECK((*saved)["version"] == (*project.read("src/pwm.cpp"))["version"]);
    CHECK_FALSE(fs::exists(directory.path / "src" / "pwm.cpp.sf-save"));
}

TEST_CASE("Only the files of the project can be named", "[app][project]") {
    Directory directory, outside;
    put(directory.path / "main.cpp", "x\n");
    put(directory.path / ".git" / "config", "x\n");
    put(outside.path / "secret.txt", "secret\n");
    const Project project{directory.path};

    for (const std::string& name : std::vector<std::string>{"", "../secret.txt", "src/../../secret.txt", "..", ".git/config", ".GIT/config", "nothing.txt", "src",
                                   (outside.path / "secret.txt").string()}) {
        const auto result = project.read(name);
        INFO(name);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == code::unknownFile);
        CHECK_FALSE(project.write(name, "x", "0-0"));
        CHECK_FALSE(project.diff(name));
    }
    CHECK(get(outside.path / "secret.txt") == "secret\n");
#if !defined(_WIN32)
    fs::create_symlink(outside.path / "secret.txt", directory.path / "link.txt");
    fs::create_symlink(directory.path / ".git", directory.path / "gitlink");
    CHECK_FALSE(project.read("link.txt"));
    CHECK_FALSE(project.read("gitlink/config"));
    CHECK_FALSE(project.write("link.txt", "overwritten", "0-0"));
    CHECK(get(outside.path / "secret.txt") == "secret\n");
#endif
}

TEST_CASE("A save refuses to overwrite a change made behind the editor's back", "[app][project]") {
    Directory directory;
    put(directory.path / "main.cpp", "one\n");
    const Project project{directory.path};
    const auto    opened = project.read("main.cpp");
    REQUIRE(opened);

    put(directory.path / "main.cpp", "two, written by the agent\n");
    const auto refused = project.write("main.cpp", "one, edited\n", versionIn(*opened));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == code::fileChanged);
    CHECK(get(directory.path / "main.cpp") == "two, written by the agent\n");

    // The page then reads the file again, and saves on top of that version if the person says so.
    const auto current = project.read("main.cpp");
    REQUIRE(current);
    CHECK((*current)["content"] == "two, written by the agent\n");
    REQUIRE(project.write("main.cpp", "one, edited\n", versionIn(*current)));
    CHECK(get(directory.path / "main.cpp") == "one, edited\n");

    // A file that is gone or turned binary is not a version to save over either.
    const auto stale = versionIn(*project.read("main.cpp"));
    put(directory.path / "main.cpp", std::string_view{"\0\1\2", 3});
    CHECK((*project.version("main.cpp"))["version"].is_null());
    CHECK_FALSE(project.write("main.cpp", "text", stale));
    fs::remove(directory.path / "main.cpp");
    CHECK((*project.version("main.cpp"))["version"].is_null());
    CHECK_FALSE(project.write("main.cpp", "text", stale));
    CHECK_FALSE(fs::exists(directory.path / "main.cpp"));
}

TEST_CASE("A save keeps the line endings and the permissions of the file", "[app][project]") {
    Directory directory;
    put(directory.path / "dos.txt", "a\r\nb\r\n");
    put(directory.path / "mixed.txt", "a\r\nb\n");
    put(directory.path / "script.sh", "#!/bin/sh\n");
    const Project project{directory.path};

    const auto dos = project.read("dos.txt");
    REQUIRE(dos);
    CHECK((*dos)["lineEndings"] == "crlf");
    REQUIRE(project.write("dos.txt", "a\nb\nc\n", versionIn(*dos)));  // The textarea hands over "\n" only.
    CHECK(get(directory.path / "dos.txt") == "a\r\nb\r\nc\r\n");

    const auto mixed = project.write("mixed.txt", "a\nb\n", versionIn(*project.read("mixed.txt")));
    REQUIRE_FALSE(mixed);
    CHECK(mixed.error().code == code::notEditable);
    CHECK(get(directory.path / "mixed.txt") == "a\r\nb\n");

#if !defined(_WIN32)
    fs::permissions(directory.path / "script.sh", fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
    REQUIRE(project.write("script.sh", "#!/bin/sh\necho hi\n", versionIn(*project.read("script.sh"))));
    CHECK((fs::status(directory.path / "script.sh").permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
}

TEST_CASE("Binary and large files are not opened", "[app][project]") {
    Directory directory;
    put(directory.path / "image.bin", std::string_view{"\x89PNG\0\0", 6});
    put(directory.path / "latin1.txt", "caf\xe9\n");
    put(directory.path / "big.txt", std::string(maxFileBytes + 1, 'x'));
    const Project project{directory.path};

    CHECK(project.read("image.bin").error().code == code::notText);
    CHECK(project.read("latin1.txt").error().code == code::notText);
    CHECK(project.read("big.txt").error().code == code::tooLarge);

    put(directory.path / "small.txt", "x\n");
    const auto version = versionIn(*project.read("small.txt"));
    CHECK(project.write("small.txt", std::string(maxFileBytes + 1, 'y'), version).error().code == code::tooLarge);
    CHECK(project.write("small.txt", std::string_view{"a\0b", 3}, version).error().code == code::notText);
    CHECK(get(directory.path / "small.txt") == "x\n");
}

TEST_CASE("The Git changes of the project are listed and shown as a diff", "[app][project][git]") {
    if (!hasGit())
        SKIP("git is not installed");
    Directory directory;
    git(directory.path, "init -q");
    put(directory.path / "main.cpp", "int main() {\n  return 0;\n}\n");
    put(directory.path / "keep.txt", "unchanged\n");
    put(directory.path / "gone.txt", "to delete\n");
    put(directory.path / "app" / "pwm.cpp", "// v1\n");
    const Project project{directory.path};

    SECTION("before the first commit every file is new") {
        const auto list = project.list();
        CHECK(list["git"] == true);
        CHECK(statusOf(list, "main.cpp") == "?");
        const auto diff = project.diff("main.cpp");
        REQUIRE(diff);
        const std::string text = (*diff)["diff"];
        CHECK(text.contains("+++ b/main.cpp"));
        CHECK(text.contains("+  return 0;"));
        CHECK(text.contains("@@ -0,0 +1,3 @@"));
    }

    git(directory.path, "add -A");
    git(directory.path, "commit -q -m initial");
    put(directory.path / "main.cpp", "int main() {\n  return 1;\n}\n");
    fs::remove(directory.path / "gone.txt");
    put(directory.path / "new.cpp", "// new\n");
    put(directory.path / "no newline.txt", "last line");

    const auto list = project.list();
    CHECK(statusOf(list, "main.cpp") == "M");
    CHECK(statusOf(list, "gone.txt") == "D");
    CHECK(statusOf(list, "new.cpp") == "?");
    CHECK(statusOf(list, "keep.txt") == "");
    CHECK(statusOf(list, "app/pwm.cpp") == "");
    CHECK(list["files"][0]["status"] != "");  // The changes come first.

    const auto modified = project.diff("main.cpp");
    REQUIRE(modified);
    const std::string text = (*modified)["diff"];
    CHECK(text.contains("-  return 0;"));
    CHECK(text.contains("+  return 1;"));

    CHECK((*project.diff("keep.txt"))["diff"] == "");
    CHECK(std::string{(*project.diff("gone.txt"))["diff"]}.contains("-to delete"));
    const std::string added = (*project.diff("new.cpp"))["diff"];
    CHECK(added.contains("+// new"));
    const std::string open = (*project.diff("no newline.txt"))["diff"];  // A name with a space: quoted for the shell.
    CHECK(open.contains("\\ No newline at end of file"));

    SECTION("in a folder of the repository only its files are listed, by their name in the folder") {
        put(directory.path / "app" / "pwm.cpp", "// v2\n");
        const Project below{directory.path / "app"};
        const auto    inside = below.list();
        CHECK(inside["files"].size() == 1);
        CHECK(statusOf(inside, "pwm.cpp") == "M");
        CHECK(std::string{(*below.diff("pwm.cpp"))["diff"]}.contains("+// v2"));
    }
}

TEST_CASE("Reading the Git changes does not take the lock of the index", "[app][project][git]") {
    if (!hasGit())
        SKIP("git is not installed");
    Directory directory;
    git(directory.path, "init -q");
    put(directory.path / "a.txt", "a\n");
    git(directory.path, "add -A");
    git(directory.path, "commit -q -m initial");
    put(directory.path / "a.txt", "b\n");
    const Project project{directory.path};

    // The agent may run `git commit` at any moment: it must not meet an index.lock left by the editor.
    put(directory.path / ".git" / "index.lock", "");
    CHECK(statusOf(project.list(), "a.txt") == "M");
    CHECK(std::string{(*project.diff("a.txt"))["diff"]}.contains("+b"));
}

#if !defined(_WIN32)
TEST_CASE("A hook of the repository configuration is not run to read the changes", "[app][project][git]") {
    if (!hasGit())
        SKIP("git is not installed");
    Directory directory;
    git(directory.path, "init -q");
    put(directory.path / "a.txt", "a\n");
    git(directory.path, "add -A");
    git(directory.path, "commit -q -m initial");
    put(directory.path / "a.txt", "b\n");
    // core.fsmonitor names a program Git would run at each status: a repository from elsewhere could make it anything.
    const auto marker = directory.path.parent_path() / (directory.path.filename().string() + ".fsmonitor-ran");
    put(directory.path / "hook.sh", "#!/bin/sh\ntouch '" + marker.string() + "'\n");
    fs::permissions(directory.path / "hook.sh", fs::perms::owner_all);
    git(directory.path, "config core.fsmonitor '" + (directory.path / "hook.sh").string() + "'");
    fs::remove(marker);

    const Project project{directory.path};
    CHECK(statusOf(project.list(), "a.txt") == "M");
    CHECK(std::string{(*project.diff("a.txt"))["diff"]}.contains("+b"));
    CHECK_FALSE(fs::exists(marker));
    fs::remove(marker);
}
#endif

#if defined(__APPLE__)
TEST_CASE("A save keeps the extended attributes of the file", "[app][project]") {
    Directory directory;
    put(directory.path / "tagged.txt", "a\n");
    REQUIRE(::setxattr((directory.path / "tagged.txt").c_str(), "com.siliconforge.test", "kept", 4, 0, 0) == 0);
    const Project project{directory.path};
    REQUIRE(project.write("tagged.txt", "b\n", versionIn(*project.read("tagged.txt"))));
    CHECK(get(directory.path / "tagged.txt") == "b\n");
    char value[8]{};
    const auto size = ::getxattr((directory.path / "tagged.txt").c_str(), "com.siliconforge.test", value, sizeof value, 0, 0);
    REQUIRE(size == 4);
    CHECK(std::string(value, 4) == "kept");
}
#endif
