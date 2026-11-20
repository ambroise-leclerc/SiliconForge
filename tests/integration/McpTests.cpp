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

SCENARIO("The server agrees on a protocol version and describes itself", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    GIVEN("a client that asks for a version the server speaks") {
        const auto answer = tools.handle(request(1, "initialize", {{"protocolVersion", "2025-03-26"}, {"capabilities", json::object()}}));
        THEN("the server answers in that version, with its tools capability, its name and its instructions") {
            REQUIRE(answer);
            const auto& result = answer->at("result");
            CHECK(answer->at("id") == 1);
            CHECK(result.at("protocolVersion") == "2025-03-26");
            CHECK(result.at("capabilities").at("tools").at("listChanged") == false);
            CHECK(result.at("serverInfo").at("name") == "siliconforge");
            CHECK(result.at("serverInfo").at("version") == "7");
            CHECK(result.at("instructions") == "Instructions for the agent");
        }
    }
    GIVEN("a client that asks for a version the server does not know") {
        const auto answer = tools.handle(request(2, "initialize", {{"protocolVersion", "1999-01-01"}}));
        THEN("the server answers with its latest version") { CHECK(answer->at("result").at("protocolVersion") == mcp::latestVersion); }
    }
}

SCENARIO("The tools are listed with their schemas and whether they change the emulation", "[app][mcp]") {
    Fixture fixture;
    WHEN("a client lists the tools") {
        const auto listed = fixture.tools.handle(request(1, "tools/list"))->at("result").at("tools");
        THEN("each comes with its input schema, and only those that change the emulation are not read-only") {
            REQUIRE(listed.size() == 3);
            CHECK(listed[0].at("name") == "echo");
            CHECK(listed[0].at("inputSchema").at("properties").contains("text"));
            CHECK(listed[0].at("annotations").at("readOnlyHint") == true);
            CHECK(listed[1].at("annotations").at("readOnlyHint") == false);
        }
    }
}

SCENARIO("A call returns its result as text and as structured content, an error as a result the agent reads", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    WHEN("a tool answers") {
        const auto echoed = call(tools, "echo", {{"text", "hello"}});
        THEN("the result is the text of its JSON and the JSON itself") {
            CHECK(echoed.at("isError") == false);
            CHECK(echoed.at("structuredContent") == json{{"text", "hello"}});
            CHECK(echoed.at("content").at(0).at("type") == "text");
            CHECK(json::parse(echoed.at("content").at(0).at("text").get<std::string>()) == json{{"text", "hello"}});
        }
    }
    WHEN("an argument is missing") {
        const auto missing = call(tools, "echo");
        THEN("the agent reads what to fix") {
            CHECK(missing.at("isError") == true);
            CHECK(missing.at("content").at(0).at("text") == "The argument text is missing or is not a string");
        }
    }
    WHEN("the tool fails") {
        const auto failed = call(tools, "broken");
        THEN("the agent reads why") {
            CHECK(failed.at("isError") == true);
            CHECK(failed.at("content").at(0).at("text") == "the MCU stopped");
        }
    }
}

SCENARIO("Protocol errors follow JSON-RPC", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    GIVEN("requests the server cannot satisfy") {
        THEN("each gets the JSON-RPC error that says why") {
            CHECK(tools.handle(request(1, "resources/list"))->at("error").at("code") == mcp::rpc::methodNotFound);
            CHECK(tools.handle(request(2, "tools/call", {{"name", "nothing"}}))->at("error").at("code") == mcp::rpc::invalidParams);
            CHECK(tools.handle(request(3, "tools/call", {{"name", "echo"}, {"arguments", "text"}}))->at("error").at("code") == mcp::rpc::invalidParams);
            CHECK(tools.handle(json{{"id", 4}, {"method", "ping"}})->at("error").at("code") == mcp::rpc::invalidRequest);  // no "jsonrpc"
            CHECK(json::parse(*tools.handleText("{not json")).at("error").at("code") == mcp::rpc::parseError);
        }
    }
    GIVEN("a ping") {
        THEN("it gets an empty result") { CHECK(tools.handle(request(5, "ping"))->at("result") == json::object()); }
    }
}

SCENARIO("A request whose members have the wrong types gets a JSON-RPC error, even inside a batch", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    GIVEN("a request whose jsonrpc is a number") {
        const auto answer = tools.handle(json{{"jsonrpc", 2}, {"method", "ping"}, {"id", 1}});
        THEN("it is an invalid request, answered with its id") {
            CHECK(answer->at("error").at("code") == mcp::rpc::invalidRequest);
            CHECK(answer->at("id") == 1);
        }
    }
    GIVEN("a request whose id is an object") {
        const auto answer = tools.handle(json{{"jsonrpc", "2.0"}, {"method", "ping"}, {"id", json::object()}});
        THEN("it is an invalid request, answered with a null id") {
            CHECK(answer->at("error").at("code") == mcp::rpc::invalidRequest);
            CHECK(answer->at("id").is_null());
        }
    }
    GIVEN("a method, params or a tool name of the wrong type") {
        THEN("each gets its error") {
            CHECK(tools.handle(json{{"jsonrpc", "2.0"}, {"method", 7}, {"id", 2}})->at("error").at("code") == mcp::rpc::invalidRequest);
            CHECK(tools.handle(request(3, "ping", json::array({1, 2})))->at("error").at("code") == mcp::rpc::invalidParams);
            CHECK(tools.handle(request(4, "tools/call", {{"name", 5}}))->at("error").at("code") == mcp::rpc::invalidParams);
        }
    }
    GIVEN("a protocol version that is not text") {
        const auto answer = tools.handle(request(5, "initialize", {{"protocolVersion", 2025}}));
        THEN("it is no version the server knows: it answers with its own") {
            CHECK(answer->at("result").at("protocolVersion") == mcp::latestVersion);
        }
    }
    GIVEN("a batch with two malformed elements and a request") {
        const auto batch = tools.handle(json::array({json{{"jsonrpc", 2}, {"method", "ping"}, {"id", 6}}, 42, request(7, "ping")}));
        THEN("each element is answered: the malformed ones do not stop the others") {
            REQUIRE(batch);
            REQUIRE(batch->size() == 3);
            CHECK(batch->at(0).at("error").at("code") == mcp::rpc::invalidRequest);
            CHECK(batch->at(1).at("error").at("code") == mcp::rpc::invalidRequest);
            CHECK(batch->at(2).at("result") == json::object());
        }
    }
    GIVEN("the text of a malformed request") {
        THEN("its answer carries its id") { CHECK(json::parse(*tools.handleText(R"({"jsonrpc":2,"method":"ping","id":8})")).at("id") == 8); }
    }
}

SCENARIO("Notifications and responses get no answer, a batch gets one per request", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    GIVEN("a notification and a response") {
        THEN("nothing is answered") {
            CHECK_FALSE(tools.handle(json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
            CHECK_FALSE(tools.handle(json{{"jsonrpc", "2.0"}, {"id", 9}, {"result", json::object()}}));
        }
    }
    GIVEN("a batch of two requests and a notification") {
        const auto batch = tools.handle(json::array({request(1, "ping"), json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, request(2, "ping")}));
        THEN("the two requests are answered") {
            REQUIRE(batch);
            CHECK(batch->size() == 2);
        }
    }
    GIVEN("an empty batch") {
        THEN("it is an invalid request") { CHECK(tools.handle(json::array())->at("error").at("code") == mcp::rpc::invalidRequest); }
    }
}

SCENARIO("A result holding bytes that are not UTF-8 is returned, with the bytes replaced", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    GIVEN("a tool that returns the output of a firmware: 0xff, then half of \"é\"") {
        tools.add({"serial", "Serial", "Bytes of a firmware", {{"type", "object"}}, false,
                   [](const json&) { return json{{"text", std::string{"ok\xff" "\xc3"}}, {"next", 4}}; }});
        WHEN("it is called") {
            const auto result = call(tools, "serial");
            THEN("the result comes back, with U+FFFD in place of the bytes") {
                REQUIRE(result.at("isError") == false);
                const auto text = result.at("content").at(0).at("text").get<std::string>();
                CHECK(text.find("\"next\": 4") != std::string::npos);
                CHECK(text.find("\xef\xbf\xbd") != std::string::npos);
            }
        }
        WHEN("it is called through the text of an HTTP body") {
            const auto answered = tools.handleText(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"serial"}})");
            THEN("the answer is JSON, with the offset where the next read starts") {
                REQUIRE(answered);
                CHECK(json::parse(*answered).at("result").at("structuredContent").at("next") == 4);
            }
        }
    }
}

SCENARIO("Optional arguments are checked as the required ones are", "[app][mcp]") {
    GIVEN("arguments of the right and of the wrong types") {
        const json arguments{{"run", "false"}, {"from", -1}, {"page", 3}, {"go", true}};
        THEN("a flag is a boolean, given or absent") {
            CHECK(mcp::flag(arguments, "go", false));
            CHECK(mcp::flag(arguments, "absent", true));
            CHECK_THROWS_AS(mcp::flag(arguments, "run", true), mcp::ToolError);
        }
        THEN("a count is an integer of 0 or more, given or absent") {
            CHECK(mcp::count(arguments, "page", 0) == 3);
            CHECK(mcp::count(arguments, "absent", 7) == 7);
            CHECK_THROWS_AS(mcp::count(arguments, "from", 0), mcp::ToolError);
            CHECK_THROWS_AS(mcp::count(arguments, "run", 0), mcp::ToolError);
        }
    }
}

SCENARIO("While the person keeps the controls, the tools that change the emulation are refused, reading goes on", "[app][mcp]") {
    Fixture fixture;
    auto&   tools  = fixture.tools;
    bool    drives = false;
    tools.allowsChanges = [&] { return drives; };
    GIVEN("the person keeps the controls") {
        THEN("a tool that changes the emulation is refused with the reason") {
            const auto refused = call(tools, "press");
            CHECK(refused.at("isError") == true);
            CHECK(refused.at("content").at(0).at("text").get<std::string>().find("keeps the controls") != std::string::npos);
        }
        THEN("a tool that reads still answers") { CHECK(call(tools, "echo", {{"text", "still"}}).at("isError") == false); }
    }
    GIVEN("the person lets the agent drive") {
        drives = true;
        THEN("the tool acts") { CHECK(call(tools, "press").at("isError") == false); }
    }
}

SCENARIO("Each call goes to the journal of the window, with its outcome", "[app][mcp]") {
    Fixture                    fixture;
    auto&                      tools = fixture.tools;
    std::vector<mcp::Activity> journal;
    tools.onActivity = [&](const mcp::Activity& activity) { journal.push_back(activity); };
    WHEN("a tool answers and another fails") {
        (void)call(tools, "echo", {{"text", "hello"}});
        (void)call(tools, "broken");
        THEN("the journal has both, with their arguments, result and outcome") {
            REQUIRE(journal.size() == 2);
            CHECK(journal[0].tool == "echo");
            CHECK(journal[0].arguments == json{{"text", "hello"}});
            CHECK(journal[0].ok);
            CHECK(journal[0].result == json{{"text", "hello"}});
            CHECK_FALSE(journal[1].ok);
            CHECK(journal[1].summary == "the MCU stopped");
        }
    }
}

SCENARIO("The route answers POST with the token, and refuses anything else", "[app][mcp]") {
    Fixture fixture;
    auto&   tools = fixture.tools;
    using webfront::http::Response;
    const auto ping = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    GIVEN("a request with the token") {
        THEN("a request is answered, a notification accepted") {
            const auto answered = exchange(tools, "POST", bearer, ping);
            CHECK(answered.status == Response::ok);
            CHECK(json::parse(answered.body).at("result") == json::object());
            CHECK(exchange(tools, "POST", bearer, R"({"jsonrpc":"2.0","method":"notifications/initialized"})").status == Response::accepted);
        }
        THEN("the scheme of the token ignores case") {
            CHECK(exchange(tools, "POST", {{"Authorization", "bearer 0123456789abcdef"}}, ping).status == Response::ok);
        }
        THEN("GET is refused: there is no event stream") { CHECK(exchange(tools, "GET", bearer, "").status == Response::methodNotAllowed); }
    }
    GIVEN("a request without the right token") {
        THEN("it is refused with 401") {
            CHECK(exchange(tools, "POST", {{"Authorization", "Bearer 0123456789abcdeX"}}, ping).status == Response::unauthorized);
            CHECK(exchange(tools, "POST", {{"Authorization", "Bearer 0123456789abcde"}}, ping).status == Response::unauthorized);
            CHECK(exchange(tools, "POST", {}, ping).status == Response::unauthorized);
        }
    }
    GIVEN("a request from a page") {
        THEN("a foreign origin is refused with 403, a loopback one served") {
            auto foreign = bearer;
            foreign.emplace_back("Origin", "http://evil.example");
            CHECK(exchange(tools, "POST", foreign, ping).status == Response::forbidden);
            auto own = bearer;
            own.emplace_back("Origin", "http://127.0.0.1:9002");
            CHECK(exchange(tools, "POST", own, ping).status == Response::ok);
        }
    }
}

SCENARIO("Only an absent or loopback origin is trusted", "[app][mcp]") {
    GIVEN("no origin, or a loopback one") {
        THEN("it is trusted") {
            CHECK(mcp::trustedOrigin(std::nullopt));
            for (const auto* origin : {"http://127.0.0.1:9002", "http://127.1.2.3", "http://localhost:80", "https://LOCALHOST", "http://[::1]:9002"})
                CHECK(mcp::trustedOrigin(std::string{origin}));
        }
    }
    GIVEN("an origin that only looks like one") {
        THEN("it is not trusted") {
            for (const auto* origin : {"null", "http://localhost.evil.example", "http://127.0.0.1.evil.example", "http://evil.example",
                                       "http://[::1].evil:1", "file://", "http://127.0.0.1:", "http://localhost:80x"})
                CHECK_FALSE(mcp::trustedOrigin(std::string{origin}));
        }
    }
}

SCENARIO("A token is 128 random bits in hexadecimal, compared whole", "[app][mcp]") {
    GIVEN("a new token") {
        const auto token = mcp::newToken();
        THEN("it has 32 hexadecimal digits and differs from the next one") {
            CHECK(token.size() == 32);
            CHECK(token.find_first_not_of("0123456789abcdef") == std::string::npos);
            CHECK(token != mcp::newToken());
        }
        THEN("only the whole token matches") {
            CHECK(mcp::sameToken(token, token));
            CHECK_FALSE(mcp::sameToken(token, token.substr(1)));
            CHECK_FALSE(mcp::sameToken("", token));
        }
    }
}
