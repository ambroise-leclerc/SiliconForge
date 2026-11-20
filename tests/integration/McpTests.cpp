// The MCP server of the window (ADR 0011, #76): the protocol (JSON-RPC 2.0, MCP lifecycle and tools), the switch with
// which the person keeps the controls, the journal of the calls, and the HTTP route with its token and origin checks.
// The tools themselves act on the emulation: tests/ui/agent.spec.js calls them from the terminal of the window.

#include "../../apps/siliconforge/Mcp.hpp"
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace siliconforge::app;
using nlohmann::json;

namespace {

/// A server with three tools: one that reads, one that changes the emulation, one that fails.
struct Fixture {
    mcp::Server tools{"Instructions for the agent", "7"};
    Fixture() {
    tools.add({"echo", "Echo", "Returns its text", {{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}}, false,
               [](const json& arguments) { return json{{"text", mcp::text(arguments, "text")}}; }});
    tools.add({"press", "Press", "Changes the emulation", {{"type", "object"}}, true, [](const json&) { return json{{"pressed", true}}; }});
    tools.add({"broken", "Broken", "Fails", {{"type", "object"}}, false, [](const json&) -> json { throw std::runtime_error("the MCU stopped"); }});
    }
};

json request(int id, std::string method, json params = json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", std::move(method)}, {"params", std::move(params)}};
}

json call(mcp::Server& tools, std::string name, json arguments = json::object()) {
    return tools.handle(request(1, "tools/call", {{"name", std::move(name)}, {"arguments", std::move(arguments)}}))->at("result");
}

/// The reply of the HTTP route to a request built from its text.
struct Exchange {
    webfront::http::Response::StatusCode status;
    std::string                          body;
    std::vector<std::pair<std::string, std::string>> headers;
};

Exchange exchange(mcp::Server& tools, std::string_view method, std::vector<std::pair<std::string, std::string>> headers, std::string body) {
    webfront::http::Request request;
    request.setMethod(method);
    request.uri = "/mcp/";
    for (auto& [name, value] : headers)
        request.headers.push_back({name, value});
    const auto reply = mcp::answer(tools, "0123456789abcdef", webfront::http::RouteRequest{request, "/mcp/", "", body});
    std::string text;
    if (reply.body) {
        std::vector<char> chunk(256);
        while (const auto count = reply.body(chunk))
            text.append(chunk.data(), count);
    }
    return {reply.status, text, reply.headers};
}

const std::vector<std::pair<std::string, std::string>> bearer{{"Authorization", "Bearer 0123456789abcdef"}, {"Content-Type", "application/json"}};

}  // namespace

TEST_CASE("The server agrees on a protocol version and describes itself", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    const auto answer = tools.handle(request(1, "initialize", {{"protocolVersion", "2025-03-26"}, {"capabilities", json::object()}}));
    REQUIRE(answer);
    const auto& result = answer->at("result");
    CHECK(answer->at("id") == 1);
    CHECK(result.at("protocolVersion") == "2025-03-26");  // A version it speaks: the client's.
    CHECK(result.at("capabilities").at("tools").at("listChanged") == false);
    CHECK(result.at("serverInfo").at("name") == "siliconforge");
    CHECK(result.at("serverInfo").at("version") == "7");
    CHECK(result.at("instructions") == "Instructions for the agent");
    const auto unknown = tools.handle(request(2, "initialize", {{"protocolVersion", "1999-01-01"}}));
    CHECK(unknown->at("result").at("protocolVersion") == mcp::latestVersion);
}

TEST_CASE("The tools are listed with their schemas and whether they change the emulation", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    const auto listed = tools.handle(request(1, "tools/list"))->at("result").at("tools");
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].at("name") == "echo");
    CHECK(listed[0].at("inputSchema").at("properties").contains("text"));
    CHECK(listed[0].at("annotations").at("readOnlyHint") == true);
    CHECK(listed[1].at("annotations").at("readOnlyHint") == false);
}

TEST_CASE("A call returns its result as text and as structured content, an error as a result the agent reads", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    const auto echoed = call(tools, "echo", {{"text", "hello"}});
    CHECK(echoed.at("isError") == false);
    CHECK(echoed.at("structuredContent") == json{{"text", "hello"}});
    CHECK(echoed.at("content").at(0).at("type") == "text");
    CHECK(json::parse(echoed.at("content").at(0).at("text").get<std::string>()) == json{{"text", "hello"}});

    const auto missing = call(tools, "echo");
    CHECK(missing.at("isError") == true);
    CHECK(missing.at("content").at(0).at("text") == "The argument text is missing or is not a string");
    const auto failed = call(tools, "broken");
    CHECK(failed.at("isError") == true);
    CHECK(failed.at("content").at(0).at("text") == "the MCU stopped");
}

TEST_CASE("Protocol errors follow JSON-RPC", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    CHECK(tools.handle(request(1, "resources/list"))->at("error").at("code") == mcp::rpc::methodNotFound);
    CHECK(tools.handle(request(2, "tools/call", {{"name", "nothing"}}))->at("error").at("code") == mcp::rpc::invalidParams);
    CHECK(tools.handle(request(3, "tools/call", {{"name", "echo"}, {"arguments", "text"}}))->at("error").at("code") == mcp::rpc::invalidParams);
    CHECK(tools.handle(json{{"id", 4}, {"method", "ping"}})->at("error").at("code") == mcp::rpc::invalidRequest);  // no "jsonrpc"
    CHECK(json::parse(*tools.handleText("{not json")).at("error").at("code") == mcp::rpc::parseError);
    CHECK(tools.handle(request(5, "ping"))->at("result") == json::object());
}

TEST_CASE("Notifications and responses get no answer, a batch gets one per request", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    CHECK_FALSE(tools.handle(json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
    CHECK_FALSE(tools.handle(json{{"jsonrpc", "2.0"}, {"id", 9}, {"result", json::object()}}));
    const auto batch = tools.handle(json::array({request(1, "ping"), json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, request(2, "ping")}));
    REQUIRE(batch);
    CHECK(batch->size() == 2);
    CHECK(tools.handle(json::array())->at("error").at("code") == mcp::rpc::invalidRequest);
}

TEST_CASE("While the person keeps the controls, the tools that change the emulation are refused, reading goes on", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    bool drives         = false;
    tools.allowsChanges = [&] { return drives; };
    const auto refused  = call(tools, "press");
    CHECK(refused.at("isError") == true);
    CHECK(refused.at("content").at(0).at("text").get<std::string>().find("keeps the controls") != std::string::npos);
    CHECK(call(tools, "echo", {{"text", "still"}}).at("isError") == false);
    drives = true;
    CHECK(call(tools, "press").at("isError") == false);
}

TEST_CASE("Each call goes to the journal of the window, with its outcome", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    std::vector<mcp::Activity> journal;
    tools.onActivity = [&](const mcp::Activity& activity) { journal.push_back(activity); };
    (void)call(tools, "echo", {{"text", "hello"}});
    (void)call(tools, "broken");
    REQUIRE(journal.size() == 2);
    CHECK(journal[0].tool == "echo");
    CHECK(journal[0].arguments == json{{"text", "hello"}});
    CHECK(journal[0].ok);
    CHECK(journal[0].result == json{{"text", "hello"}});
    CHECK_FALSE(journal[1].ok);
    CHECK(journal[1].summary == "the MCU stopped");
}

TEST_CASE("The route answers POST with the token, and refuses anything else", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    using webfront::http::Response;
    const auto ping = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";

    const auto answered = exchange(tools, "POST", bearer, ping);
    CHECK(answered.status == Response::ok);
    CHECK(json::parse(answered.body).at("result") == json::object());

    CHECK(exchange(tools, "POST", bearer, R"({"jsonrpc":"2.0","method":"notifications/initialized"})").status == Response::accepted);
    CHECK(exchange(tools, "POST", {{"Authorization", "Bearer 0123456789abcdeX"}}, ping).status == Response::unauthorized);
    CHECK(exchange(tools, "POST", {{"Authorization", "Bearer 0123456789abcde"}}, ping).status == Response::unauthorized);
    CHECK(exchange(tools, "POST", {}, ping).status == Response::unauthorized);
    CHECK(exchange(tools, "POST", {{"Authorization", "bearer 0123456789abcdef"}}, ping).status == Response::ok);  // The scheme ignores case.
    CHECK(exchange(tools, "GET", bearer, "").status == Response::methodNotAllowed);

    auto foreign = bearer;
    foreign.emplace_back("Origin", "http://evil.example");
    CHECK(exchange(tools, "POST", foreign, ping).status == Response::forbidden);
    auto own = bearer;
    own.emplace_back("Origin", "http://127.0.0.1:9002");
    CHECK(exchange(tools, "POST", own, ping).status == Response::ok);
}

TEST_CASE("Only an absent or loopback origin is trusted", "[app][mcp]") {
    CHECK(mcp::trustedOrigin(std::nullopt));
    for (const auto* origin : {"http://127.0.0.1:9002", "http://127.1.2.3", "http://localhost:80", "https://LOCALHOST", "http://[::1]:9002"})
        CHECK(mcp::trustedOrigin(std::string{origin}));
    for (const auto* origin : {"null", "http://localhost.evil.example", "http://127.0.0.1.evil.example", "http://evil.example", "http://[::1].evil:1",
                               "file://", "http://127.0.0.1:", "http://localhost:80x"})
        CHECK_FALSE(mcp::trustedOrigin(std::string{origin}));
}

TEST_CASE("A token is 128 random bits in hexadecimal, compared whole", "[app][mcp]") {
    const auto a = mcp::newToken();
    CHECK(a.size() == 32);
    CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(a != mcp::newToken());
    CHECK(mcp::sameToken(a, a));
    CHECK_FALSE(mcp::sameToken(a, a.substr(1)));
    CHECK_FALSE(mcp::sameToken("", a));
}

TEST_CASE("A request whose members have the wrong types gets a JSON-RPC error, even inside a batch", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    const auto version = tools.handle(json{{"jsonrpc", 2}, {"method", "ping"}, {"id", 1}});
    CHECK(version->at("error").at("code") == mcp::rpc::invalidRequest);
    CHECK(version->at("id") == 1);
    CHECK(tools.handle(json{{"jsonrpc", "2.0"}, {"method", 7}, {"id", 2}})->at("error").at("code") == mcp::rpc::invalidRequest);
    const auto id = tools.handle(json{{"jsonrpc", "2.0"}, {"method", "ping"}, {"id", json::object()}});
    CHECK(id->at("error").at("code") == mcp::rpc::invalidRequest);
    CHECK(id->at("id").is_null());
    CHECK(tools.handle(request(3, "ping", json::array({1, 2})))->at("error").at("code") == mcp::rpc::invalidParams);
    CHECK(tools.handle(request(4, "tools/call", {{"name", 5}}))->at("error").at("code") == mcp::rpc::invalidParams);
    // A protocol version that is not text is no version this server knows: it answers with its own.
    CHECK(tools.handle(request(5, "initialize", {{"protocolVersion", 2025}}))->at("result").at("protocolVersion") == mcp::latestVersion);

    const auto batch = tools.handle(json::array({json{{"jsonrpc", 2}, {"method", "ping"}, {"id", 6}}, 42, request(7, "ping")}));
    REQUIRE(batch);
    REQUIRE(batch->size() == 3);  // Each element answered: the malformed ones do not stop the others.
    CHECK(batch->at(0).at("error").at("code") == mcp::rpc::invalidRequest);
    CHECK(batch->at(1).at("error").at("code") == mcp::rpc::invalidRequest);
    CHECK(batch->at(2).at("result") == json::object());
    CHECK(json::parse(*tools.handleText(R"({"jsonrpc":2,"method":"ping","id":8})")).at("id") == 8);
}

TEST_CASE("A result holding bytes that are not UTF-8 is returned, with the bytes replaced", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    tools.add({"serial", "Serial", "Bytes of a firmware", {{"type", "object"}}, false,
               [](const json&) { return json{{"text", std::string{"ok\xff" "\xc3"}}, {"next", 4}}; }});  // 0xff, then half of "é"
    const auto result = call(tools, "serial");
    REQUIRE(result.at("isError") == false);
    const auto text = result.at("content").at(0).at("text").get<std::string>();
    CHECK(text.find("\"next\": 4") != std::string::npos);
    CHECK(text.find("\xef\xbf\xbd") != std::string::npos);  // U+FFFD
    const auto answered = tools.handleText(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"serial"}})");
    REQUIRE(answered);
    CHECK(json::parse(*answered).at("result").at("structuredContent").at("next") == 4);
}

TEST_CASE("Optional arguments are checked as the required ones are", "[app][mcp]") {
    const json arguments{{"run", "false"}, {"from", -1}, {"page", 3}, {"go", true}};
    CHECK(mcp::flag(arguments, "go", false));
    CHECK(mcp::flag(arguments, "absent", true));
    CHECK_THROWS_AS(mcp::flag(arguments, "run", true), mcp::ToolError);
    CHECK(mcp::count(arguments, "page", 0) == 3);
    CHECK(mcp::count(arguments, "absent", 7) == 7);
    CHECK_THROWS_AS(mcp::count(arguments, "from", 0), mcp::ToolError);
    CHECK_THROWS_AS(mcp::count(arguments, "run", 0), mcp::ToolError);
}
