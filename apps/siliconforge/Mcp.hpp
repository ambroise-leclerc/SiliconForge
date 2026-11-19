#pragma once

#include <nlohmann/json.hpp>

#include <webfront/http/HTTPServer.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// The MCP server of the window (ADR 0011, #76): the agent client launched in the terminal acts on the emulation the
/// person sees, through tools, and each call shows in the window. The protocol is JSON-RPC 2.0 over the Streamable HTTP
/// transport of MCP, reduced to what a tool server needs: POST answered with application/json, no event stream.
namespace siliconforge::app::mcp {

/// The newest revision of the protocol this server speaks; an older one a client asks for is answered in its own terms.
inline constexpr std::string_view latestVersion = "2025-06-18";
inline constexpr std::array<std::string_view, 3> versions{latestVersion, "2025-03-26", "2024-11-05"};

/// A tool that could not act on its arguments: the agent reads the message as the result of its call (isError), and
/// the protocol goes on.
struct ToolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Tool {
    std::string    name;
    std::string    title;
    std::string    description;
    nlohmann::json inputSchema;
    /// The tool changes the emulation shown in the window: refused while the person keeps the controls.
    bool           changesEmulation{false};
    std::function<nlohmann::json(const nlohmann::json& arguments)> call;
};

/// One call of a tool, as the window's journal shows it.
struct Activity {
    std::string    tool;
    nlohmann::json arguments;
    bool           ok{true};
    std::string    summary;  ///< The result or the error, shortened.
    nlohmann::json result;   ///< What the tool returned (null on an error).
};

/// JSON-RPC error codes.
namespace rpc {
inline constexpr int parseError     = -32700;
inline constexpr int invalidRequest = -32600;
inline constexpr int methodNotFound = -32601;
inline constexpr int invalidParams  = -32602;
}  // namespace rpc

/// The arguments of a tool, read with the message the agent needs to fix its call.
inline const nlohmann::json& argument(const nlohmann::json& arguments, std::string_view name, nlohmann::json::value_t type) {
    const auto found = arguments.find(name);
    const auto fits  = found != arguments.end() &&
                      (found->type() == type || (type == nlohmann::json::value_t::number_float && found->is_number()));
    if (!fits)
        throw ToolError{std::format("The argument {} is missing or is not a {}", name,
                                    type == nlohmann::json::value_t::string ? "string" : type == nlohmann::json::value_t::boolean ? "boolean" : "number")};
    return *found;
}
inline std::string text(const nlohmann::json& arguments, std::string_view name) {
    return argument(arguments, name, nlohmann::json::value_t::string).get<std::string>();
}
inline double number(const nlohmann::json& arguments, std::string_view name) {
    return argument(arguments, name, nlohmann::json::value_t::number_float).get<double>();
}

/// The tools and the protocol, independent of the transport.
class Server {
public:
    explicit Server(std::string serverInstructions, std::string serverVersion = "1")
        : instructions(std::move(serverInstructions)), version(std::move(serverVersion)) {}

    void add(Tool tool) {
        std::scoped_lock lock{mutex};
        tools.push_back(std::move(tool));
    }

    /// While it returns false, the tools that change the emulation are refused: the person keeps the controls.
    std::function<bool()> allowsChanges = [] { return true; };
    /// Called after each tools/call, from the thread of the call.
    std::function<void(const Activity&)> onActivity;

    /// Answers one JSON-RPC message, or a batch of them: a request gets its response, a notification nothing.
    [[nodiscard]] std::optional<nlohmann::json> handle(const nlohmann::json& message) {
        if (message.is_array()) {
            if (message.empty())
                return failure(nullptr, rpc::invalidRequest, "Empty batch");
            auto answers = nlohmann::json::array();
            for (const auto& entry : message)
                if (auto answer = handleOne(entry))
                    answers.push_back(std::move(*answer));
            return answers.empty() ? std::nullopt : std::optional{std::move(answers)};
        }
        return handleOne(message);
    }

    /// The same for the text of an HTTP body; text that is not JSON gets a parse error.
    [[nodiscard]] std::optional<std::string> handleText(std::string_view body) {
        nlohmann::json message;
        try {
            message = nlohmann::json::parse(body);
        } catch (const nlohmann::json::exception&) {
            return dump(failure(nullptr, rpc::parseError, "The body is not JSON"));
        }
        auto answer = handle(message);
        return answer ? std::optional{dump(*answer)} : std::nullopt;
    }

    [[nodiscard]] static std::string dump(const nlohmann::json& value) {
        return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

private:
    static nlohmann::json failure(const nlohmann::json& id, int code, std::string message) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
    }
    static nlohmann::json success(const nlohmann::json& id, nlohmann::json result) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
    }

    std::optional<nlohmann::json> handleOne(const nlohmann::json& message) {
        if (!message.is_object() || message.value("jsonrpc", "") != "2.0" || !message.contains("method") || !message.at("method").is_string())
            return message.is_object() && (message.contains("result") || message.contains("error"))
                       ? std::nullopt  // A response to a request of ours: there are none, nothing to answer.
                       : std::optional{failure(message.is_object() ? message.value("id", nlohmann::json{}) : nlohmann::json{}, rpc::invalidRequest,
                                               "Not a JSON-RPC 2.0 request")};
        const auto method = message.at("method").get<std::string>();
        if (!message.contains("id"))
            return std::nullopt;  // A notification (notifications/initialized, notifications/cancelled...): no answer.
        const auto& id     = message.at("id");
        const auto  params = message.value("params", nlohmann::json::object());
        if (method == "initialize")
            return success(id, initialize(params));
        if (method == "ping")
            return success(id, nlohmann::json::object());
        if (method == "tools/list")
            return success(id, {{"tools", list()}});
        if (method == "tools/call") {
            if (!params.is_object() || !params.contains("name") || !params.at("name").is_string())
                return failure(id, rpc::invalidParams, "tools/call needs the name of a tool");
            const auto arguments = params.value("arguments", nlohmann::json::object());
            if (!arguments.is_object())
                return failure(id, rpc::invalidParams, "The arguments of a tool are an object");
            auto tool = find(params.at("name").get<std::string>());
            if (!tool)
                return failure(id, rpc::invalidParams, "Unknown tool: " + params.at("name").get<std::string>());
            return success(id, call(*tool, arguments));
        }
        return failure(id, rpc::methodNotFound, "Unknown method: " + method);
    }

    nlohmann::json initialize(const nlohmann::json& params) const {
        const auto asked  = params.is_object() ? params.value("protocolVersion", std::string{}) : std::string{};
        const auto agreed = std::ranges::find(versions, asked) != versions.end() ? asked : std::string{latestVersion};
        return {{"protocolVersion", agreed},
                {"capabilities", {{"tools", {{"listChanged", false}}}}},
                {"serverInfo", {{"name", "siliconforge"}, {"title", "SiliconForge"}, {"version", version}}},
                {"instructions", instructions}};
    }

    nlohmann::json list() {
        std::scoped_lock lock{mutex};
        auto             listed = nlohmann::json::array();
        for (const auto& tool : tools)
            listed.push_back({{"name", tool.name},
                              {"title", tool.title},
                              {"description", tool.description},
                              {"inputSchema", tool.inputSchema},
                              {"annotations", {{"readOnlyHint", !tool.changesEmulation}, {"openWorldHint", false}}}});
        return listed;
    }

    std::optional<Tool> find(const std::string& name) {
        std::scoped_lock lock{mutex};
        const auto found = std::ranges::find(tools, name, &Tool::name);
        return found == tools.end() ? std::nullopt : std::optional{*found};
    }

    nlohmann::json call(const Tool& tool, const nlohmann::json& arguments) {
        Activity activity{.tool = tool.name, .arguments = arguments, .ok = true, .summary = {}, .result = nullptr};
        nlohmann::json result;
        try {
            if (tool.changesEmulation && !allowsChanges())
                throw ToolError{"The person keeps the controls of the window: the tools that change the emulation are off "
                                "(\"Let the agent drive\" in the window). Read-only tools still answer."};
            auto value      = tool.call(arguments);
            activity.result = value;
            activity.summary = shorten(value.is_string() ? value.get<std::string>() : dump(value));
            result = {{"content", nlohmann::json::array({{{"type", "text"}, {"text", value.is_string() ? value.get<std::string>() : value.dump(2)}}})},
                      {"isError", false}};
            if (value.is_object())
                result["structuredContent"] = value;
        } catch (const std::exception& error) {  // A ToolError, or a failure of the emulation the agent should know of.
            activity.ok      = false;
            activity.summary = shorten(error.what());
            result = {{"content", nlohmann::json::array({{{"type", "text"}, {"text", error.what()}}})}, {"isError", true}};
        }
        if (onActivity)
            onActivity(activity);
        return result;
    }

    static std::string shorten(std::string text) {
        constexpr std::size_t limit = 300;
        if (text.size() > limit) {
            text.resize(limit);
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
                text.pop_back();  // Not in the middle of a UTF-8 sequence.
            text += "…";
        }
        return text;
    }

    std::string       instructions;
    std::string       version;
    std::mutex        mutex;
    std::vector<Tool> tools;
};

/// A token of 128 random bits, in hexadecimal: what a client presents as `Authorization: Bearer <token>`.
[[nodiscard]] inline std::string newToken() {
    std::random_device device;
    std::string        token;
    for (int i = 0; i < 4; ++i)
        token += std::format("{:08x}", static_cast<std::uint32_t>(device()));
    return token;
}

/// Compares in a time that does not depend on where the two differ.
[[nodiscard]] inline bool sameToken(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}

/// A request without `Origin` comes from a program (the client); a page may only be of a loopback origin. This stops a
/// web page the user opens elsewhere from reaching the server through a name rebound to 127.0.0.1 (DNS rebinding).
[[nodiscard]] inline bool trustedOrigin(const std::optional<std::string>& origin) {
    if (!origin)
        return true;
    std::string lowered;
    for (const char c : *origin)
        lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (std::string_view scheme : {"http://", "https://"}) {
        if (!lowered.starts_with(scheme))
            continue;
        auto host = std::string_view{lowered}.substr(scheme.size());
        if (host.starts_with("[::1]"))
            host.remove_prefix(5);
        else if (host.starts_with("localhost"))
            host.remove_prefix(9);
        else if (host.starts_with("127.")) {
            host.remove_prefix(4);
            while (!host.empty() && (std::isdigit(static_cast<unsigned char>(host.front())) || host.front() == '.'))
                host.remove_prefix(1);
        } else
            return false;
        return host.empty() || (host.front() == ':' && host.size() > 1 &&
                                std::ranges::all_of(host.substr(1), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }));
    }
    return false;
}

/// The HTTP route of the server: `POST <prefix>` with `Authorization: Bearer <token>`.
[[nodiscard]] inline webfront::http::RouteReply answer(Server& server, std::string_view token, const webfront::http::RouteRequest& request) {
    using webfront::http::Response;
    webfront::http::RouteReply reply;
    const auto                 respond = [&](Response::StatusCode status, std::string body, std::string type = "application/json") {
        reply.status  = status;
        reply.headers = {{"Content-Type", std::move(type)}, {"Cache-Control", "no-store"}};
        reply.contentLength = body.size();
        if (!body.empty())
            reply.body = [body = std::move(body), sent = std::size_t{0}](std::span<char> out) mutable {
                const auto count = std::min(out.size(), body.size() - sent);
                std::copy_n(body.data() + sent, count, out.data());
                sent += count;
                return count;
            };
        return reply;
    };
    const auto error = [&](Response::StatusCode status, std::string_view message) {
        return respond(status, Server::dump({{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", rpc::invalidRequest}, {"message", message}}}}));
    };
    if (!trustedOrigin(request.request.getHeaderValue("Origin")))
        return error(Response::forbidden, "This origin may not use the server");
    const auto authorization = request.request.getHeaderValue("Authorization").value_or("");
    constexpr std::string_view bearer = "Bearer ";
    const auto presented = authorization.size() > bearer.size() &&
                                   std::ranges::equal(std::string_view{authorization}.substr(0, bearer.size()), bearer,
                                                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })
                               ? std::string_view{authorization}.substr(bearer.size())
                               : std::string_view{};
    if (!sameToken(presented, token)) {
        auto refused = error(Response::unauthorized, "Missing or wrong token");
        refused.headers.emplace_back("WWW-Authenticate", "Bearer");
        return refused;
    }
    if (request.request.method != webfront::http::Request::Method::Post) {
        auto refused = error(Response::methodNotAllowed, "This server answers POST only: there is no event stream");
        refused.headers.emplace_back("Allow", "POST");
        return refused;
    }
    auto answered = server.handleText(request.body);
    if (!answered)
        return respond(Response::accepted, {});  // Notifications and responses: accepted, nothing to say.
    return respond(Response::ok, std::move(*answered));
}

}  // namespace siliconforge::app::mcp
