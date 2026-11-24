#pragma once

#include <nlohmann/json.hpp>

#include <webfront/PtyPage.hpp>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#else
#include <unistd.h>
extern char** environ;
#endif

/// The agent terminal of the window (ADR 0009, #55): the client the user installed runs in a native pseudoterminal
/// (WebFront's PTY, ConPTY on Windows) in the project directory, and keeps its own sign-in. The page never gives a
/// command line: it opens one of the programs named here, one session each.
namespace siliconforge::app::terminal {

/// A program the terminal can run. `program` is searched in PATH.
struct Client {
    std::string_view id;
    std::string_view title;
    std::string_view program;
};

inline constexpr Client clients[] = {
    {"claude", "Claude Code", "claude"},
    {"codex", "Codex", "codex"},
    {"opencode", "OpenCode", "opencode"},
    {"vibe", "Mistral Vibe", "vibe"},
};

/// The MCP server of the window (Mcp.hpp, ADR 0011), as the clients are told of it when they start.
struct AgentServer {
    std::string url;    ///< http://127.0.0.1:<port>/mcp/
    std::string token;  ///< Presented as `Authorization: Bearer <token>`.
    std::string project;  ///< The project directory of the window: `siliconforge bench run` hands it the runs of its scenarios.
};

/// What a session is started with to reach the MCP server: arguments after the program, and variables.
struct Launch {
    std::vector<std::string>           arguments;
    std::map<std::string, std::string> environment;
};

/// Every session gets `SILICONFORGE_MCP_URL`, `SILICONFORGE_MCP_TOKEN` and `SILICONFORGE_PROJECT`; the clients that take a server when they start
/// get it in their own terms, the token through the environment (readable by its owner only, where the arguments of a
/// process are readable by every user of the machine). Mistral Vibe reads its servers from its configuration file only.
[[nodiscard]] inline Launch launchWith(std::string_view id, const AgentServer& server) {
    Launch launch;
    launch.environment = {{"SILICONFORGE_MCP_URL", server.url}, {"SILICONFORGE_MCP_TOKEN", server.token}};
    if (!server.project.empty())
        launch.environment["SILICONFORGE_PROJECT"] = server.project;
    const auto quoted  = nlohmann::json(server.url).dump();
    if (id == "claude")
        launch.arguments = {"--mcp-config", R"({"mcpServers":{"siliconforge":{"type":"http","url":)" + quoted +
                                                R"(,"headers":{"Authorization":"Bearer ${SILICONFORGE_MCP_TOKEN}"}}}})"};
    else if (id == "codex")
        launch.arguments = {"-c", "mcp_servers.siliconforge.url=" + quoted, "-c", R"(mcp_servers.siliconforge.bearer_token_env_var="SILICONFORGE_MCP_TOKEN")"};
    else if (id == "opencode")
        launch.environment["OPENCODE_CONFIG_CONTENT"] = R"({"mcp":{"siliconforge":{"type":"remote","url":)" + quoted +
                                                        R"(,"headers":{"Authorization":"Bearer {env:SILICONFORGE_MCP_TOKEN}"}}}})";
    return launch;
}

/// Name of the callbacks of a client's session (`<name>.open`, `.read`...), the `name` of the page adapter.
[[nodiscard]] inline std::string callbackName(std::string_view id) {
    return "sf.terminal." + std::string{id};
}

/// The environment of this process: the client needs its HOME, its configuration and its PATH to keep its sign-in.
[[nodiscard]] inline std::map<std::string, std::string> currentEnvironment() {
    std::map<std::string, std::string> variables;
#if defined(_WIN32)
    for (char** entry = _environ; entry && *entry; ++entry)
#else
    for (char** entry = environ; entry && *entry; ++entry)
#endif
    {
        const std::string_view text{*entry};
        const auto             equals = text.find('=', 1);  // A leading '=' names a Windows per-drive variable.
        if (equals != std::string_view::npos)
            variables.emplace(std::string{text.substr(0, equals)}, std::string{text.substr(equals + 1)});
    }
    return variables;
}

/// What the environment of the application says about the terminal or the agent it was started from, and that must not
/// reach a client of its own: the client would join that session. Started from the terminal of VSCodium, the
/// application carried the port of the IDE of the outer Claude Code (`CLAUDE_CODE_SSE_PORT`) and its session; the
/// embedded client connected to the same IDE and took it from the first one ("switched to another Claude Code session").
/// The settings of the user (provider, token, model, proxy) are kept.
[[nodiscard]] inline bool belongsToHostSession(std::string_view name) {
    constexpr std::string_view exact[] = {
        "CLAUDECODE", "CLAUDE_PID", "ENABLE_IDE_INTEGRATION", "CLAUDE_CODE_SSE_PORT", "CLAUDE_CODE_ENTRYPOINT", "CLAUDE_CODE_EXECPATH",
        "CLAUDE_CODE_SESSION_ID", "CLAUDE_CODE_CHILD_SESSION", "CLAUDE_CODE_SESSION_ATTENDED", "CLAUDE_CODE_BRIDGE_SESSION_ID",
        "CLAUDE_CODE_MESSAGING_SOCKET", "CLAUDE_CODE_MESSAGING_TOKEN",
        // OpenCode joins the editor named by CLAUDE_CODE_SSE_PORT or OPENCODE_EDITOR_SSE_PORT, and detects Zed by ZED_TERM
        // or TERM_PROGRAM; it marks its own process and the ones it starts with OPENCODE and OPENCODE_PID. Vibe marks the
        // shells it starts with VIBE_SESSION.
        "OPENCODE", "OPENCODE_PID", "OPENCODE_CLIENT", "OPENCODE_EDITOR_SSE_PORT", "ZED_TERM", "VIBE_SESSION",
        "TERM_PROGRAM", "TERM_PROGRAM_VERSION", "TERMINAL_EMULATOR",
    };
    for (const auto candidate : exact)
        if (name == candidate)
            return true;
    return name.starts_with("VSCODE_") || name.starts_with("CURSOR_TRACE") || name.starts_with("JETBRAINS_");
}

/// The environment a client starts with: that of the application, without the session of the terminal or the agent
/// that started it. A `GIT_ASKPASS` that the IDE provided goes with it, since its channel (`VSCODE_GIT_IPC_HANDLE`) goes.
[[nodiscard]] inline std::map<std::string, std::string> clientEnvironment(std::map<std::string, std::string> variables) {
    const bool ideAskpass = variables.contains("VSCODE_GIT_ASKPASS_MAIN") || variables.contains("VSCODE_GIT_IPC_HANDLE");
    std::erase_if(variables, [&](const auto& entry) { return belongsToHostSession(entry.first) || (ideAskpass && entry.first == "GIT_ASKPASS"); });
    // `SILICONFORGE_CLIENT_ENV_<NAME>=<value>` sets <NAME> for the client alone, and removes it when the value is empty.
    // The UI tests give the browser a throwaway XDG profile; a client that inherited it installed itself there and
    // repointed ~/.local/bin/claude into build/. The test hands the real values over through these variables.
    constexpr std::string_view prefix = "SILICONFORGE_CLIENT_ENV_";
    std::map<std::string, std::string> overrides;
    for (const auto& [name, value] : variables)
        if (name.starts_with(prefix))
            overrides.emplace(name.substr(prefix.size()), value);
    std::erase_if(variables, [&](const auto& entry) { return entry.first.starts_with(prefix); });
    for (const auto& [name, value] : overrides) {
        if (value.empty())
            variables.erase(name);
        else
            variables[name] = value;
    }
    return variables;
}

/// The executable `program` names in `path`, or nothing.
[[nodiscard]] inline std::optional<std::filesystem::path> find(std::string_view program, const std::string& path) {
#if defined(_WIN32)
    constexpr char separator = ';';
    constexpr std::string_view extensions[] = {".exe", ".cmd", ".bat"};
#else
    constexpr char separator = ':';
    constexpr std::string_view extensions[] = {""};
#endif
    std::string_view rest{path};
    while (!rest.empty()) {
        const auto cut       = rest.find(separator);
        const auto directory = rest.substr(0, cut);
        rest                 = cut == std::string_view::npos ? std::string_view{} : rest.substr(cut + 1);
        if (directory.empty())
            continue;
        for (const auto extension : extensions) {
            const auto      candidate = std::filesystem::path{std::string{directory}} / (std::string{program} + std::string{extension});
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)
#if !defined(_WIN32)
                && ::access(candidate.c_str(), X_OK) == 0
#endif
            )
                return candidate;
        }
    }
    return std::nullopt;
}

/// The shell of the user, for the commands of the bench (`siliconforge bench`) and for what no client does.
[[nodiscard]] inline std::string shellProgram(const std::map<std::string, std::string>& environment) {
#if defined(_WIN32)
    const auto found = environment.find("COMSPEC");
    return found != environment.end() ? found->second : "cmd.exe";
#else
    const auto found = environment.find("SHELL");
    return found != environment.end() && !found->second.empty() ? found->second : "/bin/sh";
#endif
}

/// Registers the session of each client installed on this machine and of the shell, in `directory`, and describes them
/// for the page: `{enabled, clients: [{id, title, available}]}`. Without PTY pages (the application listens
/// elsewhere than on the loopback interface, or is a test double) nothing is registered and `enabled` is false.
/// With `server`, each session is told of the MCP server of the window (launchWith).
template <typename App>
nlohmann::json registerClients(App& app, const std::filesystem::path& directory, const std::optional<AgentServer>& server = std::nullopt) {
    auto list = nlohmann::json::array();
    if constexpr (requires { app.allowsPtyPages(); }) {
        if (!app.allowsPtyPages())
            return {{"enabled", false}, {"clients", list}};
        const auto environment = clientEnvironment(currentEnvironment());
        const auto path        = environment.contains("PATH") ? environment.at("PATH") : std::string{};
        const auto add         = [&](std::string_view id, std::string_view title, std::string argv0, bool available) {
            list.push_back({{"id", id}, {"title", title}, {"available", available}});
            if (!available)
                return;
            std::vector<std::string> argv{std::move(argv0)};
            auto                     variables = environment;
            if (server) {
                auto launch = launchWith(id, *server);
                argv.insert(argv.end(), launch.arguments.begin(), launch.arguments.end());
                for (auto& [name, value] : launch.environment)
                    variables[name] = std::move(value);
            }
            webfront::registerPtyPage(app, {.argv = std::move(argv), .directory = directory, .environment = std::move(variables), .size = {}}, callbackName(id));
        };
        for (const auto& client : clients) {
            auto program = find(client.program, path);
            add(client.id, client.title, program ? program->string() : std::string{}, program.has_value());
        }
        add("shell", "Shell", shellProgram(environment), true);
        return {{"enabled", true}, {"clients", std::move(list)}};
    } else {
        return {{"enabled", false}, {"clients", list}};
    }
}

}  // namespace siliconforge::app::terminal
