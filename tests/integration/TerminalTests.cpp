// The programs of the agent terminal (ADR 0009, #55): which clients are offered, where they are looked for, and that
// nothing is registered when WebFront does not allow PTY pages. The sessions themselves are WebFront's, and the window
// test (tests/ui/terminal.spec.js) runs one in the native window.

#include "../../apps/siliconforge/Terminal.hpp"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <coroutine>
#include <filesystem>
#include <format>
#include <functional>
#include <vector>
#include <fstream>
#include <random>

using namespace siliconforge::app;
namespace fs = std::filesystem;

namespace {

struct Directory {
    Directory() : path{fs::temp_directory_path() / std::format("siliconforge-terminal-{:x}", std::random_device{}())} { fs::create_directories(path); }
    ~Directory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    Directory(const Directory&)            = delete;
    Directory& operator=(const Directory&) = delete;
    fs::path path;
};

/// An application that refuses PTY pages, as WebFront's does unless the configuration allows them.
struct RefusingApp {
    struct UI {
        auto resumeAfter(std::chrono::milliseconds) { return std::suspend_never{}; }
    };
    std::vector<std::function<void()>> ptyPageCleanups;  // Where registerPtyPage keeps its cleanups.
    bool allowsPtyPages() const { return false; }
    bool hasCppFunction(std::string_view) const { return false; }
    template <typename F>
    void cppFunction(std::string_view, F&&) {
        FAIL("a session was registered although PTY pages are refused");
    }
};
struct AppWithoutPty {};

}  // namespace

TEST_CASE("The terminal offers the four agent clients and names their sessions", "[app][terminal]") {
    std::vector<std::string_view> ids;
    for (const auto& client : terminal::clients)
        ids.push_back(client.id);
    CHECK(ids == std::vector<std::string_view>{"claude", "codex", "opencode", "vibe"});
    CHECK(terminal::callbackName("codex") == "sf.terminal.codex");
}

TEST_CASE("A client is looked for in PATH, in order", "[app][terminal]") {
    Directory first, second;
#if defined(_WIN32)
    const std::string name = "agent.exe", program = "agent", separator = ";";
#else
    const std::string name = "agent", program = "agent", separator = ":";
#endif
    const auto make = [&](const Directory& directory, bool executable) {
        const auto file = directory.path / name;
        std::ofstream{file} << "x";
#if !defined(_WIN32)
        fs::permissions(file, executable ? fs::perms::owner_all : fs::perms::owner_read | fs::perms::owner_write);
#else
        (void)executable;
#endif
    };
    const auto both = first.path.string() + separator + second.path.string();

    CHECK_FALSE(terminal::find(program, both));
    make(second, true);
    CHECK(terminal::find(program, both) == second.path / name);
#if !defined(_WIN32)
    make(first, false);  // A file that cannot be run is not the client.
    CHECK(terminal::find(program, both) == second.path / name);
#endif
    make(first, true);
    CHECK(terminal::find(program, both) == first.path / name);
    CHECK_FALSE(terminal::find(program, ""));
}

TEST_CASE("The client receives the environment of the application", "[app][terminal]") {
    const auto environment = terminal::currentEnvironment();
    CHECK(environment.contains("PATH"));
    CHECK_FALSE(terminal::shellProgram(environment).empty());
}

TEST_CASE("Nothing is registered unless WebFront allows PTY pages", "[app][terminal]") {
    RefusingApp refusing;
    auto        refused = terminal::registerClients(refusing, fs::current_path());
    CHECK(refused["enabled"] == false);
    CHECK(refused["clients"].empty());
    AppWithoutPty plain;
    CHECK(terminal::registerClients(plain, fs::current_path())["enabled"] == false);
}

TEST_CASE("A client does not inherit the session of the terminal or the agent the application was started from", "[app][terminal]") {
    const std::map<std::string, std::string> inherited{
        {"PATH", "/usr/bin"}, {"HOME", "/home/user"}, {"LANG", "fr_FR.UTF-8"},
        {"ANTHROPIC_BASE_URL", "https://proxy.example"}, {"CLAUDE_CODE_USE_BEDROCK", "1"}, {"CLAUDE_CODE_OAUTH_TOKEN", "token"},
        // Started from the terminal of VSCodium, itself running Claude Code.
        {"CLAUDECODE", "1"}, {"CLAUDE_CODE_SSE_PORT", "51234"}, {"CLAUDE_CODE_SESSION_ID", "abc"}, {"CLAUDE_CODE_ENTRYPOINT", "cli"},
        {"CLAUDE_CODE_MESSAGING_SOCKET", "/tmp/s"}, {"CLAUDE_CODE_MESSAGING_TOKEN", "secret"}, {"ENABLE_IDE_INTEGRATION", "true"},
        {"TERM_PROGRAM", "vscode"}, {"TERM_PROGRAM_VERSION", "1.0"}, {"VSCODE_GIT_IPC_HANDLE", "/tmp/git"},
        {"VSCODE_GIT_ASKPASS_MAIN", "/opt/askpass.js"}, {"GIT_ASKPASS", "/opt/askpass.sh"},
        // The same for the other clients: OpenCode's editor channel and process marks, Zed, Vibe's shell mark.
        {"OPENCODE_EDITOR_SSE_PORT", "51235"}, {"OPENCODE", "1"}, {"OPENCODE_PID", "42"}, {"OPENCODE_CLIENT", "acp"},
        {"ZED_TERM", "true"}, {"VIBE_SESSION", "1"},
        // Their settings stay.
        {"OPENCODE_CONFIG", "/home/user/opencode.json"}, {"MISTRAL_API_KEY", "key"}, {"VIBE_HOME", "/home/user/.vibe"}, {"CODEX_HOME", "/home/user/.codex"},
    };
    const auto client = terminal::clientEnvironment(inherited);
    CHECK(client == std::map<std::string, std::string>{{"PATH", "/usr/bin"}, {"HOME", "/home/user"}, {"LANG", "fr_FR.UTF-8"},
                                                       {"ANTHROPIC_BASE_URL", "https://proxy.example"},
                                                       {"CLAUDE_CODE_USE_BEDROCK", "1"}, {"CLAUDE_CODE_OAUTH_TOKEN", "token"},
                                                       {"OPENCODE_CONFIG", "/home/user/opencode.json"}, {"MISTRAL_API_KEY", "key"},
                                                       {"VIBE_HOME", "/home/user/.vibe"}, {"CODEX_HOME", "/home/user/.codex"}});
    // A GIT_ASKPASS the user set themselves is theirs to keep.
    CHECK(terminal::clientEnvironment({{"GIT_ASKPASS", "/opt/mine.sh"}}).contains("GIT_ASKPASS"));
}
