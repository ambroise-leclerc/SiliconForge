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
