// The programs of the agent terminal (ADR 0009, #55): which clients are offered, where they are looked for, and that
// nothing is registered when WebFront does not allow PTY pages. The sessions themselves are WebFront's, and the window
// test (tests/ui/terminal.spec.js) runs one in the native window.

#include "../../apps/siliconforge/Terminal.hpp"
#include <webfront/testing/Bdd.hpp>

#include <chrono>
#include <coroutine>
#include <filesystem>
#include <format>
#include <functional>
#include <vector>
#include <fstream>
#include <random>

using namespace webfront::bdd;

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
        fail("a session was registered although PTY pages are refused");
    }
};
struct AppWithoutPty {};

}  // namespace

const Scenario theTerminalOffersTheFourAgentClients{"The terminal offers the four agent clients and names their sessions", {"app", "terminal"}, [] {
    std::vector<std::string_view> ids;
    for (const auto& client : terminal::clients)
        ids.push_back(client.id);
    check(ids == std::vector<std::string_view>{"claude", "codex", "opencode", "vibe"});
    check(terminal::callbackName("codex") == "sf.terminal.codex");
}};

const Scenario aClientIsLookedForInPath{"A client is looked for in PATH, in order", {"app", "terminal"}, [] {
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

    check(!terminal::find(program, both));
    make(second, true);
    check(terminal::find(program, both) == second.path / name);
#if !defined(_WIN32)
    make(first, false);  // A file that cannot be run is not the client.
    check(terminal::find(program, both) == second.path / name);
#endif
    make(first, true);
    check(terminal::find(program, both) == first.path / name);
    check(!terminal::find(program, ""));
}};

const Scenario theClientReceivesTheEnvironmentOfThe{"The client receives the environment of the application", {"app", "terminal"}, [] {
    const auto environment = terminal::currentEnvironment();
    check(environment.contains("PATH"));
    check(!terminal::shellProgram(environment).empty());
}};

const Scenario nothingIsRegisteredUnlessWebfrontAllowsPty{"Nothing is registered unless WebFront allows PTY pages", {"app", "terminal"}, [] {
    RefusingApp refusing;
    auto        refused = terminal::registerClients(refusing, fs::current_path());
    check(refused["enabled"] == false);
    check(refused["clients"].empty());
    AppWithoutPty plain;
    check(terminal::registerClients(plain, fs::current_path())["enabled"] == false);
}};

const Scenario aClientDoesNotInheritTheSession{"A client does not inherit the session of the terminal or the agent the application was started from",
                                               {"app", "terminal"}, [] {
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
    check(client == std::map<std::string, std::string>{{"PATH", "/usr/bin"}, {"HOME", "/home/user"}, {"LANG", "fr_FR.UTF-8"},
                                                       {"ANTHROPIC_BASE_URL", "https://proxy.example"},
                                                       {"CLAUDE_CODE_USE_BEDROCK", "1"}, {"CLAUDE_CODE_OAUTH_TOKEN", "token"},
                                                       {"OPENCODE_CONFIG", "/home/user/opencode.json"}, {"MISTRAL_API_KEY", "key"},
                                                       {"VIBE_HOME", "/home/user/.vibe"}, {"CODEX_HOME", "/home/user/.codex"}});
    // A GIT_ASKPASS the user set themselves is theirs to keep.
    check(terminal::clientEnvironment({{"GIT_ASKPASS", "/opt/mine.sh"}}).contains("GIT_ASKPASS"));
    // The UI tests give the browser a throwaway XDG profile; the client gets the user's own values instead, or the defaults.
    check(terminal::clientEnvironment({{"HOME", "/home/user"}, {"XDG_DATA_HOME", "/build/profile/data"}, {"XDG_CACHE_HOME", "/build/profile/cache"},
                                       {"SILICONFORGE_CLIENT_ENV_XDG_DATA_HOME", "/data/mine"}, {"SILICONFORGE_CLIENT_ENV_XDG_CACHE_HOME", ""}})
          == std::map<std::string, std::string>{{"HOME", "/home/user"}, {"XDG_DATA_HOME", "/data/mine"}});
}};

const Scenario eachClientIsToldOfTheMcp{"Each client is told of the MCP server of the window in its own terms, the token by the environment",
                                        {"app", "terminal", "mcp"}, [] {
    given("the server of the window", [&] {
        const terminal::AgentServer server{"http://127.0.0.1:9002/mcp/", "0123456789abcdef0123456789abcdef", "/home/someone/project"};
        then("every session gets the URL, the token and the project as variables, and no argument holds the token", [&] {
            for (const auto& client : terminal::clients) {
                const auto launch = terminal::launchWith(client.id, server);
                check(launch.environment.at("SILICONFORGE_MCP_URL") == server.url);
                check(launch.environment.at("SILICONFORGE_MCP_TOKEN") == server.token);
                check(launch.environment.at("SILICONFORGE_PROJECT") == server.project);  // siliconforge bench hands its runs to the window
                // The arguments of a process are readable by every user of the machine.
                check(std::ranges::none_of(launch.arguments, [&](const std::string& argument) { return argument.find(server.token) != std::string::npos; }));
            }
        });
        when("Claude Code starts", [&] {
            const auto claude = terminal::launchWith("claude", server);
            then("it gets --mcp-config, whose header names the variable of the token", [&] {
                require(claude.arguments.size() == 2);
                check(claude.arguments[0] == "--mcp-config");
                const auto config = nlohmann::json::parse(claude.arguments[1]).at("mcpServers").at("siliconforge");
                check(config.at("type") == "http");
                check(config.at("url") == server.url);
                check(config.at("headers").at("Authorization") == "Bearer ${SILICONFORGE_MCP_TOKEN}");
            });
        });
        when("Codex starts", [&] {
            const auto codex = terminal::launchWith("codex", server);
            then("it gets the URL and the variable of the token as overrides of its configuration", [&] {
                check(codex.arguments == std::vector<std::string>{"-c", R"(mcp_servers.siliconforge.url="http://127.0.0.1:9002/mcp/")", "-c",
                                                                  R"(mcp_servers.siliconforge.bearer_token_env_var="SILICONFORGE_MCP_TOKEN")"});
            });
        });
        when("OpenCode starts", [&] {
            const auto opencode = terminal::launchWith("opencode", server);
            then("it gets the server in OPENCODE_CONFIG_CONTENT", [&] {
                check(opencode.arguments.empty());
                const auto content = nlohmann::json::parse(opencode.environment.at("OPENCODE_CONFIG_CONTENT")).at("mcp").at("siliconforge");
                check(content.at("type") == "remote");
                check(content.at("headers").at("Authorization") == "Bearer {env:SILICONFORGE_MCP_TOKEN}");
            });
        });
        when("Mistral Vibe starts", [&] {
            const auto vibe = terminal::launchWith("vibe", server);
            then("it gets the variables only: its servers come from its configuration file", [&] {
                check(vibe.arguments.empty());
                check(vibe.environment.size() == 3);
            });
        });
    });
}};
