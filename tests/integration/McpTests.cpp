// The MCP server of the window (ADR 0011, #76): the protocol (JSON-RPC 2.0, MCP lifecycle and tools), the switch with
// which the person keeps the controls, the journal of the calls, and the HTTP route with its token and origin checks.
// The tools themselves act on the emulation: tests/ui/agent.spec.js calls them from the terminal of the window.

#include "../../apps/siliconforge/Mcp.hpp"
#include <webfront/testing/Bdd.hpp>

#include <string>
#include <vector>

using namespace webfront::bdd;

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

/// The reply of the HTTP route to a request built from its parts.
struct Exchange {
    webfront::http::Response::StatusCode             status;
    std::string                                      body;
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

const Scenario theServerAgreesOnAProtocolVersion{"The server agrees on a protocol version and describes itself", {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools = fixture.tools;
    given("a client that asks for a version the server speaks", [&] {
        const auto answer = tools.handle(request(1, "initialize", {{"protocolVersion", "2025-03-26"}, {"capabilities", json::object()}}));
        then("the server answers in that version, with its tools capability, its name and its instructions", [&] {
            require(answer);
            const auto& result = answer->at("result");
            check(answer->at("id") == 1);
            check(result.at("protocolVersion") == "2025-03-26");
            check(result.at("capabilities").at("tools").at("listChanged") == false);
            check(result.at("serverInfo").at("name") == "siliconforge");
            check(result.at("serverInfo").at("version") == "7");
            check(result.at("instructions") == "Instructions for the agent");
        });
    });
    given("a client that asks for a version the server does not know", [&] {
        const auto answer = tools.handle(request(2, "initialize", {{"protocolVersion", "1999-01-01"}}));
        then("the server answers with its latest version", [&] { check(answer->at("result").at("protocolVersion").get<std::string>() == mcp::latestVersion); });
    });
}};

const Scenario theToolsAreListedWithTheirSchemas{"The tools are listed with their schemas and whether they change the emulation", {"app", "mcp"}, [] {
    Fixture fixture;
    when("a client lists the tools", [&] {
        const auto listed = fixture.tools.handle(request(1, "tools/list"))->at("result").at("tools");
        then("each comes with its input schema, and only those that change the emulation are not read-only", [&] {
            require(listed.size() == 3);
            check(listed[0].at("name") == "echo");
            check(listed[0].at("inputSchema").at("properties").contains("text"));
            check(listed[0].at("annotations").at("readOnlyHint") == true);
            check(listed[1].at("annotations").at("readOnlyHint") == false);
        });
    });
}};

const Scenario aCallReturnsItsResultAsText{"A call returns its result as text and as structured content, an error as a result the agent reads",
                                           {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools = fixture.tools;
    when("a tool answers", [&] {
        const auto echoed = call(tools, "echo", {{"text", "hello"}});
        then("the result is the text of its JSON and the JSON itself", [&] {
            check(echoed.at("isError") == false);
            check(echoed.at("structuredContent") == json{{"text", "hello"}});
            check(echoed.at("content").at(0).at("type") == "text");
            check(json::parse(echoed.at("content").at(0).at("text").get<std::string>()) == json{{"text", "hello"}});
        });
    });
    when("an argument is missing", [&] {
        const auto missing = call(tools, "echo");
        then("the agent reads what to fix", [&] {
            check(missing.at("isError") == true);
            check(missing.at("content").at(0).at("text") == "The argument text is missing or is not a string");
        });
    });
    when("the tool fails", [&] {
        const auto failed = call(tools, "broken");
        then("the agent reads why", [&] {
            check(failed.at("isError") == true);
            check(failed.at("content").at(0).at("text") == "the MCU stopped");
        });
    });
}};

const Scenario protocolErrorsFollowJsonRpc{"Protocol errors follow JSON-RPC", {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools = fixture.tools;
    given("requests the server cannot satisfy", [&] {
        then("each gets the JSON-RPC error that says why", [&] {
            check(tools.handle(request(1, "resources/list"))->at("error").at("code") == mcp::rpc::methodNotFound);
            check(tools.handle(request(2, "tools/call", {{"name", "nothing"}}))->at("error").at("code") == mcp::rpc::invalidParams);
            check(tools.handle(request(3, "tools/call", {{"name", "echo"}, {"arguments", "text"}}))->at("error").at("code") == mcp::rpc::invalidParams);
            check(tools.handle(json{{"id", 4}, {"method", "ping"}})->at("error").at("code") == mcp::rpc::invalidRequest);  // no "jsonrpc"
            check(json::parse(*tools.handleText("{not json")).at("error").at("code") == mcp::rpc::parseError);
        });
    });
    given("a ping", [&] {
        then("it gets an empty result", [&] { check(tools.handle(request(5, "ping"))->at("result") == json::object()); });
    });
}};

const Scenario aRequestWhoseMembersHaveTheWrong{"A request whose members have the wrong types gets a JSON-RPC error, even inside a batch", {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools = fixture.tools;
    given("a request whose jsonrpc is a number", [&] {
        const auto answer = tools.handle(json{{"jsonrpc", 2}, {"method", "ping"}, {"id", 1}});
        then("it is an invalid request, answered with its id", [&] {
            check(answer->at("error").at("code") == mcp::rpc::invalidRequest);
            check(answer->at("id") == 1);
        });
    });
    given("a request whose id is an object", [&] {
        const auto answer = tools.handle(json{{"jsonrpc", "2.0"}, {"method", "ping"}, {"id", json::object()}});
        then("it is an invalid request, answered with a null id", [&] {
            check(answer->at("error").at("code") == mcp::rpc::invalidRequest);
            check(answer->at("id").is_null());
        });
    });
    given("a method, params or a tool name of the wrong type", [&] {
        then("each gets its error", [&] {
            check(tools.handle(json{{"jsonrpc", "2.0"}, {"method", 7}, {"id", 2}})->at("error").at("code") == mcp::rpc::invalidRequest);
            check(tools.handle(request(3, "ping", json::array({1, 2})))->at("error").at("code") == mcp::rpc::invalidParams);
            check(tools.handle(request(4, "tools/call", {{"name", 5}}))->at("error").at("code") == mcp::rpc::invalidParams);
        });
    });
    given("a protocol version that is not text", [&] {
        const auto answer = tools.handle(request(5, "initialize", {{"protocolVersion", 2025}}));
        then("it is no version the server knows: it answers with its own", [&] {
            check(answer->at("result").at("protocolVersion").get<std::string>() == mcp::latestVersion);
        });
    });
    given("a batch with two malformed elements and a request", [&] {
        const auto batch = tools.handle(json::array({json{{"jsonrpc", 2}, {"method", "ping"}, {"id", 6}}, 42, request(7, "ping")}));
        then("each element is answered: the malformed ones do not stop the others", [&] {
            require(batch);
            require(batch->size() == 3);
            check(batch->at(0).at("error").at("code") == mcp::rpc::invalidRequest);
            check(batch->at(1).at("error").at("code") == mcp::rpc::invalidRequest);
            check(batch->at(2).at("result") == json::object());
        });
    });
    given("the text of a malformed request", [&] {
        then("its answer carries its id", [&] { check(json::parse(*tools.handleText(R"({"jsonrpc":2,"method":"ping","id":8})")).at("id") == 8); });
    });
}};

const Scenario notificationsAndResponsesGetNoAnswerA{"Notifications and responses get no answer, a batch gets one per request", {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools = fixture.tools;
    given("a notification and a response", [&] {
        then("nothing is answered", [&] {
            check(!tools.handle(json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
            check(!tools.handle(json{{"jsonrpc", "2.0"}, {"id", 9}, {"result", json::object()}}));
        });
    });
    given("a batch of two requests and a notification", [&] {
        const auto batch = tools.handle(json::array({request(1, "ping"), json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, request(2, "ping")}));
        then("the two requests are answered", [&] {
            require(batch);
            check(batch->size() == 2);
        });
    });
    given("an empty batch", [&] {
        then("it is an invalid request", [&] { check(tools.handle(json::array())->at("error").at("code") == mcp::rpc::invalidRequest); });
    });
}};

const Scenario aResultHoldingBytesThatAreNot{"A result holding bytes that are not UTF-8 is returned, with the bytes replaced", {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools = fixture.tools;
    given("a tool that returns the output of a firmware: 0xff, then half of \"é\"", [&] {
        tools.add({"serial", "Serial", "Bytes of a firmware", {{"type", "object"}}, false,
                   [](const json&) { return json{{"text", std::string{"ok\xff" "\xc3"}}, {"next", 4}}; }});
        when("it is called", [&] {
            const auto result = call(tools, "serial");
            then("the result comes back, with U+FFFD in place of the bytes", [&] {
                require(result.at("isError") == false);
                const auto text = result.at("content").at(0).at("text").get<std::string>();
                check(text.find("\"next\": 4") != std::string::npos);
                check(text.find("\xef\xbf\xbd") != std::string::npos);
            });
        });
        when("it is called through the text of an HTTP body", [&] {
            const auto answered = tools.handleText(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"serial"}})");
            then("the answer is JSON, with the offset where the next read starts", [&] {
                require(answered);
                check(json::parse(*answered).at("result").at("structuredContent").at("next") == 4);
            });
        });
    });
}};

const Scenario optionalArgumentsAreCheckedAsTheRequired{"Optional arguments are checked as the required ones are", {"app", "mcp"}, [] {
    given("arguments of the right and of the wrong types", [&] {
        const json arguments{{"run", "false"}, {"from", -1}, {"page", 3}, {"go", true}};
        then("a flag is a boolean, given or absent", [&] {
            check(mcp::flag(arguments, "go", false));
            check(mcp::flag(arguments, "absent", true));
            checkThrows<mcp::ToolError>([&] { mcp::flag(arguments, "run", true); });
        });
        then("a count is an integer of 0 or more, given or absent", [&] {
            check(mcp::count(arguments, "page", 0) == 3);
            check(mcp::count(arguments, "absent", 7) == 7);
            checkThrows<mcp::ToolError>([&] { mcp::count(arguments, "from", 0); });
            checkThrows<mcp::ToolError>([&] { mcp::count(arguments, "run", 0); });
        });
    });
}};

const Scenario whileThePersonKeepsTheControlsThe{"While the person keeps the controls, the tools that change the emulation are refused, reading goes on",
                                                 {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools  = fixture.tools;
    bool    drives = false;
    tools.allowsChanges = [&] { return drives; };
    given("the person keeps the controls", [&] {
        then("a tool that changes the emulation is refused with the reason", [&] {
            const auto refused = call(tools, "press");
            check(refused.at("isError") == true);
            check(refused.at("content").at(0).at("text").get<std::string>().find("keeps the controls") != std::string::npos);
        });
        then("a tool that reads still answers", [&] { check(call(tools, "echo", {{"text", "still"}}).at("isError") == false); });
    });
    given("the person lets the agent drive", [&] {
        drives = true;
        then("the tool acts", [&] { check(call(tools, "press").at("isError") == false); });
    });
}};

const Scenario eachCallGoesToTheJournalOf{"Each call goes to the journal of the window, with its outcome", {"app", "mcp"}, [] {
    Fixture                    fixture;
    auto&                      tools = fixture.tools;
    std::vector<mcp::Activity> journal;
    tools.onActivity = [&](const mcp::Activity& activity) { journal.push_back(activity); };
    when("a tool answers and another fails", [&] {
        (void)call(tools, "echo", {{"text", "hello"}});
        (void)call(tools, "broken");
        then("the journal has both, with their arguments, result and outcome", [&] {
            require(journal.size() == 2);
            check(journal[0].tool == "echo");
            check(journal[0].arguments == json{{"text", "hello"}});
            check(journal[0].ok);
            check(journal[0].result == json{{"text", "hello"}});
            check(!journal[1].ok);
            check(journal[1].summary == "the MCU stopped");
        });
    });
}};

const Scenario theRouteAnswersPostWithTheToken{"The route answers POST with the token, and refuses anything else", {"app", "mcp"}, [] {
    Fixture fixture;
    auto&   tools = fixture.tools;
    using webfront::http::Response;
    const auto ping = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    given("a request with the token", [&] {
        then("a request is answered, a notification accepted", [&] {
            const auto answered = exchange(tools, "POST", bearer, ping);
            check(answered.status == Response::ok);
            check(json::parse(answered.body).at("result") == json::object());
            check(exchange(tools, "POST", bearer, R"({"jsonrpc":"2.0","method":"notifications/initialized"})").status == Response::accepted);
        });
        then("the scheme of the token ignores case", [&] {
            check(exchange(tools, "POST", {{"Authorization", "bearer 0123456789abcdef"}}, ping).status == Response::ok);
        });
        then("GET is refused: there is no event stream", [&] { check(exchange(tools, "GET", bearer, "").status == Response::methodNotAllowed); });
    });
    given("a request without the right token", [&] {
        then("it is refused with 401", [&] {
            check(exchange(tools, "POST", {{"Authorization", "Bearer 0123456789abcdeX"}}, ping).status == Response::unauthorized);
            check(exchange(tools, "POST", {{"Authorization", "Bearer 0123456789abcde"}}, ping).status == Response::unauthorized);
            check(exchange(tools, "POST", {}, ping).status == Response::unauthorized);
        });
    });
    given("a request from a page", [&] {
        then("a foreign origin is refused with 403, a loopback one served", [&] {
            auto foreign = bearer;
            foreign.emplace_back("Origin", "http://evil.example");
            check(exchange(tools, "POST", foreign, ping).status == Response::forbidden);
            auto own = bearer;
            own.emplace_back("Origin", "http://127.0.0.1:9002");
            check(exchange(tools, "POST", own, ping).status == Response::ok);
        });
    });
}};

const Scenario onlyAnAbsentOrLoopbackOriginIs{"Only an absent or loopback origin is trusted", {"app", "mcp"}, [] {
    given("no origin, or a loopback one", [&] {
        then("it is trusted", [&] {
            check(mcp::trustedOrigin(std::nullopt));
            for (const auto* origin : {"http://127.0.0.1:9002", "http://127.1.2.3", "http://localhost:80", "https://LOCALHOST", "http://[::1]:9002"})
                check(mcp::trustedOrigin(std::string{origin}));
        });
    });
    given("an origin that only looks like one", [&] {
        then("it is not trusted", [&] {
            for (const auto* origin : {"null", "http://localhost.evil.example", "http://127.0.0.1.evil.example", "http://evil.example",
                                       "http://[::1].evil:1", "file://", "http://127.0.0.1:", "http://localhost:80x"})
                check(!mcp::trustedOrigin(std::string{origin}));
        });
    });
}};

const Scenario aTokenIs128RandomBitsIn{"A token is 128 random bits in hexadecimal, compared whole", {"app", "mcp"}, [] {
    given("a new token", [&] {
        const auto token = mcp::newToken();
        then("it has 32 hexadecimal digits and differs from the next one", [&] {
            check(token.size() == 32);
            check(token.find_first_not_of("0123456789abcdef") == std::string::npos);
            check(token != mcp::newToken());
        });
        then("only the whole token matches", [&] {
            check(mcp::sameToken(token, token));
            check(!mcp::sameToken(token, token.substr(1)));
            check(!mcp::sameToken("", token));
        });
    });
}};

const Scenario serialChunksPreserveUtf8CharactersAt{"Serial chunks preserve UTF-8 characters at the boundary", {"app", "mcp"}, [] {
    const std::string output = std::string(65535, 'x') + "\xe2\x82\xac" + "!";
    const auto first = mcp::serialChunkSize(output, 0, 65536);
    check(first == 65535);
    check(output.substr(first, mcp::serialChunkSize(output, first, 65536)) == "\xe2\x82\xac!");
    check(mcp::serialChunkSize("abc", 0, 2) == 2);
}};
